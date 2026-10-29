"""The owner's own widgets on the home board, made by the agent (ADR 0046).

A widget is a description, not code: what it shows (a template the shell draws: a list, a
value, a text or an image) and where that comes from (a source this module fetches). The
shell runs nothing of it; it draws the content this module writes.

  ~/.config/mobarchy/home.json             "widgets": {id: spec}, and the board's entry for
                                           each ("board": [{kind: "widget:<id>", ...}])
  ~/.local/state/mobarchy/widgets/<id>.json   its content, as last fetched (and its images)

    mobarchy-widgets refresh [ID ...] [--force]   fetch the ones due (all of them: --force)
    mobarchy-widgets list

A spec:
  {"title": "NRK", "icon": "󰎕", "template": "list",
   "source": {"type": "feed", "url": "https://www.nrk.no/toppsaker.rss"},
   "refresh_minutes": 30,
   "board": {"cols": 4, "rows": 2, "screens": "wide"}}

Sources:
  feed    RSS or Atom: {"url", "limit"}                      → items
  json    any JSON: {"url", "items": "a.b", "fields": {"title": "x", ...}}   → items
          or {"url", "value": "a.b", "caption": "c"}          → value
          or {"url", "text": "a.b"}                            → text
  image   an image's URL: {"url", "caption"}                  → image
  static  the content in the spec itself: {"content": {...}}  (a note, a number)
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

UA = "Mozilla/5.0 (Linux; Android 15; Mobile) Mobarchy"
TEMPLATES = {
    # template -> the sizes its card comes in, [cols, rows], the first its default
    "list": [[4, 2], [4, 4], [2, 2]],
    "value": [[2, 1], [2, 2]],
    "text": [[4, 2], [2, 2], [4, 4]],
    "image": [[2, 2], [4, 2], [4, 4]],
}
SOURCES = {"feed", "json", "image", "static"}
# The board while the owner hasn't changed it: as HomeBoard.qml's `defaults` (keep the two
# alike), with the owner's widgets after it.
DEFAULT_BOARD = [
    {"kind": "frigg", "cols": 4, "rows": 2},
    {"kind": "weather", "cols": 2, "rows": 1}, {"kind": "alarm", "cols": 2, "rows": 1},
    {"kind": "calendar", "cols": 4, "rows": 2},
    {"kind": "media", "cols": 4, "rows": 2}, {"kind": "recent", "cols": 4, "rows": 1},
    {"kind": "messages", "cols": 4, "rows": 2}, {"kind": "battery", "cols": 2, "rows": 1}]
OWN_KINDS = {e["kind"] for e in DEFAULT_BOARD}
SCREENS = {"all", "wide", "narrow"}
MIN_REFRESH = 5             # minutes
MAX_BYTES = 4 * 1024 * 1024
MAX_IMAGE = 2 * 1024 * 1024
MAX_ITEMS = 20
TIMEOUT = 15


class WidgetError(Exception):
    pass


# --- where things are --------------------------------------------------------------------

def home_file() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "mobarchy/home.json"


def content_dir() -> Path:
    return Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "mobarchy/widgets"


def _write(path: Path, text: str | bytes) -> None:
    """Written whole, then renamed into place: the shell, watching, never reads half of it."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.")
    with os.fdopen(fd, "wb") as f:
        f.write(text.encode() if isinstance(text, str) else text)
    os.replace(tmp, path)


def load_home() -> dict:
    try:
        data = json.loads(home_file().read_text())
    except FileNotFoundError:
        return {}
    except json.JSONDecodeError as e:
        raise WidgetError(f"home.json is not valid JSON: {e}") from e
    return data if isinstance(data, dict) else {}


def save_home(home: dict) -> None:
    _write(home_file(), json.dumps(home, indent=4, ensure_ascii=False) + "\n")


def kind_of(widget_id: str) -> str:
    return f"widget:{widget_id}"


def slug(text: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:40] or "widget"


# --- the description ---------------------------------------------------------------------

def check(spec: dict) -> dict:
    """A spec as stored: known fields only, and each one sound."""
    title = str(spec.get("title", "")).strip()
    if not title:
        raise WidgetError("a widget needs a title")
    template = spec.get("template", "list")
    if template not in TEMPLATES:
        raise WidgetError(f"template: one of {', '.join(TEMPLATES)}")
    source = spec.get("source")
    if not isinstance(source, dict) or source.get("type") not in SOURCES:
        raise WidgetError(f"source: an object with a type, one of {', '.join(sorted(SOURCES))}")
    if source["type"] != "static":
        url = str(source.get("url", ""))
        if urllib.parse.urlsplit(url).scheme not in ("http", "https"):
            raise WidgetError("source.url: an http(s) URL")
    elif not isinstance(source.get("content"), dict):
        raise WidgetError("a static source has its content: {\"content\": {...}}")
    if source["type"] == "image" and template != "image":
        raise WidgetError("an image source goes with the image template")
    refresh = int(spec.get("refresh_minutes", 30))
    board = dict(spec.get("board") or {})
    sizes = TEMPLATES[template]
    cols, rows = board.get("cols", sizes[0][0]), board.get("rows", sizes[0][1])
    if [cols, rows] not in sizes:
        raise WidgetError(f"board size: the {template} template comes in "
                          + ", ".join(f"{c}x{r}" for c, r in sizes) + " (cols x rows)")
    screens = board.get("screens", "all")
    if screens not in SCREENS:
        raise WidgetError("board.screens: all, wide (a wide screen only: a fold's inner one, a "
                          "tablet) or narrow (a phone-sized screen only)")
    return {"title": title, "icon": str(spec.get("icon", "")), "template": template, "source": source,
            "refresh_minutes": max(MIN_REFRESH, refresh),
            "board": {"cols": cols, "rows": rows, "screens": screens}}


def _entry(widget_id: str, spec: dict) -> dict:
    b = spec["board"]
    entry = {"kind": kind_of(widget_id), "cols": b["cols"], "rows": b["rows"]}
    if b["screens"] != "all":
        entry["screens"] = b["screens"]
    return entry


def board_of(home: dict) -> list[dict]:
    """The board as the shell shows it."""
    if home.get("board") is not None:
        return list(home["board"])
    return DEFAULT_BOARD + [_entry(wid, check(spec)) for wid, spec in (home.get("widgets") or {}).items()]


def list_widgets() -> dict:
    home = load_home()
    board = board_of(home)
    out = []
    for wid, spec in (home.get("widgets") or {}).items():
        content = read_content(wid)
        out.append({"id": wid, **spec,
                    "on_board": any(e.get("kind") == kind_of(wid) for e in board),
                    "updated": content.get("updated"), "error": content.get("error")})
    return {"widgets": out, "board": board}


def set_board(entries: list[dict]) -> dict:
    home = load_home()
    if not entries:
        home.pop("board", None)
        save_home(home)
        return {"board": board_of(home)}
    widgets = home.get("widgets") or {}
    board, seen = [], set()
    for e in entries:
        kind = str(e.get("kind", ""))
        if kind in seen:
            raise WidgetError(f"{kind} is on the board twice")
        seen.add(kind)
        if kind.startswith("widget:"):
            wid = kind.removeprefix("widget:")
            if wid not in widgets:
                raise WidgetError(f"no widget {wid!r} (widget_list has them)")
            sizes = TEMPLATES[widgets[wid]["template"]]
        elif kind in OWN_KINDS:
            sizes = None    # (the shell's own cards: it knows their sizes, and fits a wrong one)
        else:
            raise WidgetError(f"no card {kind!r}: {', '.join(sorted(OWN_KINDS))} or widget:<id>")
        cols, rows = int(e.get("cols", 0)), int(e.get("rows", 0))
        if sizes and [cols, rows] not in sizes:
            raise WidgetError(f"{kind}: comes in " + ", ".join(f"{c}x{r}" for c, r in sizes))
        entry = {"kind": kind, "cols": cols, "rows": rows}
        screens = e.get("screens", "all")
        if screens not in SCREENS:
            raise WidgetError(f"{kind}: screens is all, wide or narrow")
        if screens != "all":
            entry["screens"] = screens
        board.append(entry)
    home["board"] = board
    save_home(home)
    return {"board": board}


def create(spec: dict, widget_id: str = "") -> dict:
    spec = check(spec)
    home = load_home()
    widgets = home.setdefault("widgets", {})
    wid = slug(widget_id or spec["title"])
    base, n = wid, 2
    while wid in widgets:
        wid, n = f"{base}-{n}", n + 1
    widgets[wid] = spec
    # (no board of the owner's yet: Mobarchy's, with every widget at the end)
    if home.get("board") is not None:
        home["board"].append(_entry(wid, spec))
    save_home(home)
    return {"id": wid, **refresh_one(wid, spec)}


def update(widget_id: str, changes: dict) -> dict:
    home = load_home()
    widgets = home.get("widgets") or {}
    if widget_id not in widgets:
        raise WidgetError(f"no widget {widget_id!r} (widget_list has them)")
    current = widgets[widget_id]
    merged = {**current, **{k: v for k, v in changes.items() if v is not None and k != "board"}}
    board = dict(current.get("board") or {})
    if changes.get("template") and changes["template"] != current.get("template"):
        # another template, other sizes: its default one
        board = {"screens": board.get("screens", "all")}
    board.update(changes.get("board") or {})
    merged["board"] = board
    spec = check(merged)
    widgets[widget_id] = spec
    if home.get("board") is not None:
        kind = kind_of(widget_id)
        home["board"] = [_entry(widget_id, spec) if e.get("kind") == kind else e for e in home["board"]]
        if not any(e.get("kind") == kind for e in home["board"]):
            home["board"].append(_entry(widget_id, spec))
    save_home(home)
    return {"id": widget_id, **refresh_one(widget_id, spec)}


def remove(widget_id: str) -> dict:
    home = load_home()
    widgets = home.get("widgets") or {}
    if widgets.pop(widget_id, None) is None:
        raise WidgetError(f"no widget {widget_id!r} (widget_list has them)")
    if home.get("board") is not None:
        home["board"] = [e for e in home["board"] if e.get("kind") != kind_of(widget_id)]
    save_home(home)
    d = content_dir()
    for f in [d / f"{widget_id}.json", *d.glob(f"{widget_id}--*")]:
        f.unlink(missing_ok=True)
    return {"removed": widget_id}


# --- fetching ----------------------------------------------------------------------------

def fetch(url: str, limit: int = MAX_BYTES) -> tuple[bytes, str]:
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        data = r.read(limit + 1)
        ctype = r.headers.get("Content-Type", "")
    if len(data) > limit:
        raise WidgetError(f"{url}: more than {limit // 1024} kB")
    return data, ctype


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _text(el: ET.Element | None) -> str:
    if el is None:
        return ""
    return _plain("".join(el.itertext()))


def _plain(s: str) -> str:
    """Text out of what may be HTML (a feed's description)."""
    s = re.sub(r"<[^>]+>", " ", s)
    return re.sub(r"\s+", " ", html.unescape(s)).strip()


def _http(url: str) -> str:
    return url if urllib.parse.urlsplit(url).scheme in ("http", "https") else ""


def parse_feed(data: bytes, limit: int = 10) -> list[dict]:
    """RSS 2.0, RSS 1.0 or Atom: title, a line of text, the link and an image."""
    try:
        root = ET.fromstring(data)
    except ET.ParseError as e:
        raise WidgetError(f"not an RSS or Atom feed: {e}") from e
    items = []
    for el in root.iter():
        if _local(el.tag) not in ("item", "entry"):
            continue
        item = {"title": "", "subtitle": "", "url": "", "image": ""}
        for child in el:
            name = _local(child.tag)
            if name == "title":
                item["title"] = _text(child)
            elif name in ("description", "summary") and not item["subtitle"]:
                item["subtitle"] = _text(child)[:200]
            elif name == "link":
                href = child.get("href")
                if href and child.get("rel", "alternate") == "alternate":
                    item["url"] = _http(href)
                elif not href and child.text:
                    item["url"] = _http(child.text.strip())
                elif href and child.get("rel") == "enclosure" and (child.get("type") or "").startswith("image/"):
                    item["image"] = item["image"] or _http(href)
            elif name in ("thumbnail", "content") and child.get("url"):
                medium = child.get("medium") or (child.get("type") or "").split("/")[0]
                if name == "thumbnail" or medium == "image":
                    item["image"] = item["image"] or _http(child.get("url"))
            elif name == "enclosure" and (child.get("type") or "").startswith("image/"):
                item["image"] = item["image"] or _http(child.get("url", ""))
            elif name == "group":
                for g in child:
                    if _local(g.tag) in ("thumbnail", "content") and g.get("url"):
                        item["image"] = item["image"] or _http(g.get("url"))
        if item["title"]:
            items.append(item)
        if len(items) >= limit:
            break
    return items


def pick(data, path: str):
    """A value at a dotted path: "list.0.main.temp"."""
    for part in [p for p in str(path).split(".") if p]:
        if isinstance(data, list):
            try:
                data = data[int(part)]
            except (ValueError, IndexError):
                return None
        elif isinstance(data, dict):
            data = data.get(part)
        else:
            return None
    return data


def _str(v) -> str:
    if v is None:
        return ""
    if isinstance(v, float):
        return f"{v:g}"
    return v if isinstance(v, str) else json.dumps(v, ensure_ascii=False)


def from_json(data: bytes, source: dict, template: str) -> dict:
    try:
        doc = json.loads(data)
    except json.JSONDecodeError as e:
        raise WidgetError(f"not JSON: {e}") from e
    if template == "list":
        rows = pick(doc, source.get("items", ""))
        if not isinstance(rows, list):
            raise WidgetError(f"source.items {source.get('items', '')!r}: not a list in the JSON")
        fields = source.get("fields") or {}
        items = []
        for row in rows[:int(source.get("limit", 10))]:
            item = {k: _plain(_str(pick(row, fields[k]))) if k in fields else ""
                    for k in ("title", "subtitle", "url", "image")}
            item["url"], item["image"] = _http(item["url"]), _http(item["image"])
            if item["title"]:
                items.append(item)
        return {"items": items}
    if template == "value":
        return {"value": _str(pick(doc, source.get("value", ""))),
                "caption": _str(pick(doc, source["caption"])) if source.get("caption") else ""}
    if template == "text":
        return {"text": _str(pick(doc, source.get("text", "")))}
    raise WidgetError(f"a json source doesn't fill the {template} template")


def _store_image(widget_id: str, url: str, again: bool = False) -> str:
    """An image, fetched and kept beside the content: the shell shows a file (and fetches
    nothing itself). A list's are fetched once; again: each time (a webcam's still)."""
    path = content_dir() / f"{widget_id}--{hashlib.sha1(url.encode()).hexdigest()[:16]}"
    if path.exists() and not again:
        return str(path)
    data, _ = fetch(url, MAX_IMAGE)
    if not (data[:3] == b"\xff\xd8\xff" or data[:8] == b"\x89PNG\r\n\x1a\n" or data[:4] == b"GIF8"
            or (data[:4] == b"RIFF" and data[8:12] == b"WEBP")):
        raise WidgetError(f"{url}: not a JPEG, PNG, GIF or WebP image")
    _write(path, data)
    return str(path)


def _with_images(widget_id: str, content: dict) -> dict:
    keep = set()
    for item in content.get("items", [])[:MAX_ITEMS]:
        if item.get("image"):
            try:
                item["image"] = _store_image(widget_id, item["image"])
                keep.add(item["image"])
            except (OSError, WidgetError):
                item["image"] = ""
    if content.get("image"):
        content["image"] = _store_image(widget_id, content["image"], again=True)
        keep.add(content["image"])
    # the images no longer shown
    for f in content_dir().glob(f"{widget_id}--*"):
        if str(f) not in keep:
            f.unlink(missing_ok=True)
    return content


def build(widget_id: str, spec: dict) -> dict:
    """The content of a widget, fetched now."""
    source, template = spec["source"], spec["template"]
    kind = source["type"]
    if kind == "static":
        content = dict(source["content"])
    elif kind == "image":
        content = {"image": source["url"], "caption": str(source.get("caption", ""))}
    else:
        data, _ = fetch(source["url"])
        if kind == "feed":
            if template != "list":
                raise WidgetError("a feed goes with the list template")
            content = {"items": parse_feed(data, int(source.get("limit", 10)))}
        else:
            content = from_json(data, source, template)
    if "items" in content:
        content["items"] = [{k: _str(i.get(k)) for k in ("title", "subtitle", "url", "image")}
                            for i in content["items"][:MAX_ITEMS] if isinstance(i, dict)]
    return _with_images(widget_id, content)


def read_content(widget_id: str) -> dict:
    try:
        return json.loads((content_dir() / f"{widget_id}.json").read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def refresh_one(widget_id: str, spec: dict) -> dict:
    """Fetched and written. A failure is written too (the card says so), and what was shown
    before stays."""
    now = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    try:
        content = {**build(widget_id, spec), "updated": now, "error": None}
    except (OSError, ValueError, WidgetError) as e:
        content = {**read_content(widget_id), "error": str(e) or type(e).__name__, "tried": now}
    _write(content_dir() / f"{widget_id}.json", json.dumps(content, indent=1, ensure_ascii=False))
    return {"updated": content.get("updated"), "error": content["error"],
            "shown": {k: (len(v) if isinstance(v, list) else v) for k, v in content.items()
                      if k in ("items", "value", "caption", "text") and v}}


def due(widget_id: str, spec: dict) -> bool:
    content = read_content(widget_id)
    last = content.get("tried") if content.get("error") else content.get("updated")
    if not last:
        return True
    try:
        then = time.mktime(time.strptime(last[:19], "%Y-%m-%dT%H:%M:%S"))
    except ValueError:
        return True
    return time.time() - then >= spec["refresh_minutes"] * 60 - 30


def refresh(ids: list[str] | None = None, force: bool = False) -> dict:
    widgets = load_home().get("widgets") or {}
    unknown = [i for i in ids or [] if i not in widgets]
    if unknown:
        raise WidgetError(f"no widget {', '.join(unknown)} (widget_list has them)")
    done = {}
    for wid, spec in widgets.items():
        if ids and wid not in ids:
            continue
        try:
            spec = check(spec)
        except WidgetError as e:
            done[wid] = {"error": f"its description: {e}"}
            continue
        if force or ids or due(wid, spec):
            done[wid] = refresh_one(wid, spec)
    return {"refreshed": done}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-widgets", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("refresh")
    r.add_argument("ids", nargs="*")
    r.add_argument("--force", action="store_true")
    sub.add_parser("list")
    args = p.parse_args(argv)
    try:
        out = refresh(args.ids, args.force) if args.cmd == "refresh" else list_widgets()
    except WidgetError as e:
        print(f"mobarchy-widgets: {e}", file=sys.stderr)
        return 1
    print(json.dumps(out, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
