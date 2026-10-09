"""Web apps on the home screen: a website opened as its own app (its own window, no browser
bar), with its name and icon from the site's web app manifest. Mobarchy's browser runs them
(mobarchy-browser --app: its own profile, the phone's user agent); Chromium's app mode is
there for a site that needs it (browser="chromium").

Each is a desktop entry in the user's applications directory, which the shell's app list
watches; its icon goes next to it. (Chromium's own "Install app" makes the same kind of
entry, chrome-<id>-Default.desktop.)
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

APPS = Path.home() / ".local/share/applications"
ICONS = Path.home() / ".local/share/mobarchy/webapp-icons"
PREFIX = "mobarchy-webapp-"
UA = "Mozilla/5.0 (Linux; Android 15; Mobile) Mobarchy"


class _Head(HTMLParser):
    """The bits of a page's <head> that describe it as an app."""

    def __init__(self) -> None:
        super().__init__()
        self.title = ""
        self.manifest = ""
        self.icons: list[tuple[int, str]] = []  # (size, href)
        self._in_title = False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "title":
            self._in_title = True
        elif tag == "link" and a.get("href"):
            rel = (a.get("rel") or "").lower().split()
            if "manifest" in rel:
                self.manifest = a["href"]
            elif "apple-touch-icon" in rel or "icon" in rel:
                self.icons.append((_size(a.get("sizes", "")) or (180 if "apple-touch-icon" in rel else 32), a["href"]))

    def handle_endtag(self, tag):
        if tag == "title":
            self._in_title = False

    def handle_data(self, data):
        if self._in_title:
            self.title += data


def _size(sizes: str) -> int:
    m = re.match(r"(\d+)x\d+", sizes.strip().split(" ")[-1]) if sizes else None
    return int(m.group(1)) if m else 0


def _get(url: str, limit: int = 2_000_000) -> tuple[bytes, str]:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read(limit), r.geturl()


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:40] or "app"


def best_icon(manifest_icons: list[dict], page_icons: list[tuple[int, str]], base: str) -> str | None:
    """The largest raster icon (manifest first), as an absolute URL."""
    candidates = []
    for icon in manifest_icons:
        if "svg" in icon.get("type", "") or icon.get("src", "").endswith(".svg"):
            continue
        candidates.append((max((_size(s) for s in icon.get("sizes", "").split()), default=0), icon["src"]))
    candidates += page_icons
    if not candidates:
        return urllib.parse.urljoin(base, "/favicon.ico")
    return urllib.parse.urljoin(base, max(candidates, key=lambda c: c[0])[1])


def describe(url: str) -> dict:
    """Name, start URL and icon URL of a site, from its manifest and page."""
    html, final = _get(url)
    head = _Head()
    head.feed(html.decode("utf-8", "replace"))
    name, start, manifest_icons, base = head.title.strip(), final, [], final
    if head.manifest:
        try:
            murl = urllib.parse.urljoin(final, head.manifest)
            data, _ = _get(murl)
            m = json.loads(data)
            name = m.get("short_name") or m.get("name") or name
            if m.get("start_url"):
                start = urllib.parse.urljoin(murl, m["start_url"])
            manifest_icons, base = m.get("icons", []), murl
        except (OSError, ValueError):
            pass
    icon = best_icon(manifest_icons, [(s, urllib.parse.urljoin(final, h)) for s, h in head.icons], base)
    return {"name": name or urllib.parse.urlparse(final).hostname, "url": start, "icon_url": icon}


def wm_class(url: str) -> str:
    """The Wayland app ID Chromium gives an --app window: "chrome-<host>_<path, / as _>-Default".
    The shell matches open windows to their entry by it (StartupWMClass)."""
    u = urllib.parse.urlparse(url)
    return "chrome-" + (f"{u.hostname}_{u.path or '/'}").replace("/", "_") + "-Default"


def desktop_entry(name: str, url: str, icon: str, key: str, browser: str = "mobarchy") -> str:
    """Mobarchy's browser names the app's window by its entry (StartupWMClass = the key);
    Chromium by the URL."""
    if browser == "chromium":
        exec_, wm = f"chromium --app={url}", wm_class(url)
    else:
        exec_, wm = f"mobarchy-browser --app={url} --app-id={key}", key
    return ("[Desktop Entry]\nType=Application\n"
            f"Name={name}\nComment=Web app: {url}\n"
            f"Exec={exec_}\n"
            f"Icon={icon}\nStartupWMClass={wm}\nCategories=Network;WebApp;\n")


def add(url: str, name: str = "", keep_url: bool = False, browser: str = "") -> dict:
    """keep_url: start at url, not where the site's manifest says (the store's curated
    URLs: some manifests start at a sign-in page that only works with a redirect's state).
    browser: "mobarchy" (default, where it is installed) or "chromium"."""
    browser = browser or ("mobarchy" if shutil.which("mobarchy-browser") else "chromium")
    if not urllib.parse.urlparse(url).scheme:
        url = "https://" + url
    info = describe(url)
    if keep_url:
        info["url"] = url
    name = name or info["name"]
    key = PREFIX + slug(name)
    icon = "web-browser"
    if info["icon_url"]:
        try:
            data, _ = _get(info["icon_url"])
            ext = Path(urllib.parse.urlparse(info["icon_url"]).path).suffix.lower()
            ext = ext if ext in (".png", ".ico", ".jpg", ".jpeg", ".webp") else ".png"
            ICONS.mkdir(parents=True, exist_ok=True)
            path = ICONS / f"{key}{ext}"
            path.write_bytes(data)
            icon = str(path)
        except OSError:
            pass
    APPS.mkdir(parents=True, exist_ok=True)
    (APPS / f"{key}.desktop").write_text(desktop_entry(name, info["url"], icon, key, browser))
    return {"app_id": key, "name": name, "url": info["url"], "icon": icon, "browser": browser}


def listed() -> list[dict]:
    out = []
    for f in sorted(APPS.glob(f"{PREFIX}*.desktop")) + sorted(APPS.glob("chrome-*-Default.desktop")):
        fields = dict(line.split("=", 1) for line in f.read_text().splitlines() if "=" in line)
        out.append({"app_id": f.stem, "name": fields.get("Name", f.stem),
                    "installed_by": "mobarchy" if f.name.startswith(PREFIX) else "chromium"})
    return out


def remove(app_id: str) -> str:
    f = APPS / f"{app_id}.desktop"
    if not f.exists() or not (app_id.startswith(PREFIX) or app_id.startswith("chrome-")):
        raise ValueError(f"no web app {app_id!r} (see list_web_apps)")
    f.unlink()
    for icon in ICONS.glob(f"{app_id}.*"):
        icon.unlink()
    return f"removed {app_id}"


def main() -> None:
    """mobarchy-webapp add URL [--name NAME] [--chromium] | list | remove APP_ID: for the
    browser's "Add to home screen"; the last line printed is for people (the name)."""
    p = argparse.ArgumentParser(prog="mobarchy-webapp")
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add")
    a.add_argument("url")
    a.add_argument("--name", default="")
    a.add_argument("--chromium", action="store_true")
    sub.add_parser("list")
    sub.add_parser("remove").add_argument("app_id")
    args = p.parse_args()
    try:
        if args.cmd == "add":
            r = add(args.url, args.name, browser="chromium" if args.chromium else "")
            print(json.dumps(r))
            print(r["name"])
        elif args.cmd == "list":
            print(json.dumps(listed()))
        else:
            print(remove(args.app_id))
    except (OSError, ValueError) as e:
        print(str(e))
        sys.exit(1)
