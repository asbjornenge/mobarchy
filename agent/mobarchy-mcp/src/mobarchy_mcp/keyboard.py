"""Hardware keyboards' layout (a Bluetooth or USB keyboard): the agent sets it, there is no
settings page for it. The on-screen keyboard is the shell's own and keeps its own layout
(ADR 0016).

  ~/.config/hypr/keyboard.conf   written here: Hyprland's input { kb_layout, kb_variant,
                                 kb_options }; hyprland.conf sources it (before the owner's
                                 local.conf, which wins), and Hyprland reloads it as it changes
  /usr/share/X11/xkb             the layouts there are (rules/evdev.xml: names, variants,
                                 options)
  ~/.config/xkb/symbols/<name>   the owner's own layouts, beside them

A layout is checked before it is written: it has to exist, and the keymap has to compile
(xkbcli compile-keymap), so a mistake can't leave the keyboard without one."""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

SYSTEM_XKB = Path("/usr/share/X11/xkb")
CONF_HEADER = ("# Hardware keyboards' layout, written by mobarchy-mcp (the agent's keyboard tools):\n"
               "# changes here are overwritten. Your own settings go in local.conf.\n")


def config_home() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")


def conf_path() -> Path:
    return config_home() / "hypr" / "keyboard.conf"


def own_xkb() -> Path:
    return config_home() / "xkb"


# --- what there is -------------------------------------------------------------------------
def _registry() -> ET.Element | None:
    try:
        return ET.parse(SYSTEM_XKB / "rules" / "evdev.xml").getroot()
    except (OSError, ET.ParseError):
        return None


def _text(e: ET.Element | None, path: str) -> str:
    found = e.find(path) if e is not None else None
    return (found.text or "").strip() if found is not None else ""


def system_layouts() -> list[dict]:
    root = _registry()
    if root is None:
        return []
    out = []
    for layout in root.iter("layout"):
        name = _text(layout, "configItem/name")
        out.append({
            "layout": name,
            "description": _text(layout, "configItem/description"),
            "variants": [{"variant": _text(v, "configItem/name"), "description": _text(v, "configItem/description")}
                         for v in layout.iter("variant")],
        })
    return out


_SYMBOLS = re.compile(r'xkb_symbols\s+"([^"]+)"\s*\{(.*?)\n\};', re.S)
_GROUP_NAME = re.compile(r'name\[Group1\]\s*=\s*"([^"]*)"')


def own_layouts(root: Path | None = None) -> list[dict]:
    """The owner's layouts (~/.config/xkb/symbols/<name>): each file a layout, its
    xkb_symbols sections the variants (the default one is the layout itself)."""
    symbols = (root or own_xkb()) / "symbols"
    out = []
    for f in sorted(symbols.glob("*")) if symbols.is_dir() else []:
        try:
            text = f.read_text(errors="replace")
        except OSError:
            continue
        sections = [(m.group(1), m.group(2)) for m in _SYMBOLS.finditer(text)]
        if not sections:
            continue
        default = next(((n, b) for n, b in sections
                        if re.search(r'default[^\n]*xkb_symbols\s+"' + re.escape(n) + '"', text)), sections[0])

        def described(body: str, fallback: str) -> str:
            m = _GROUP_NAME.search(body)
            return m.group(1) if m else fallback
        out.append({
            "layout": f.name,
            "description": described(default[1], f.name),
            "own": True,
            "variants": [{"variant": n, "description": described(b, n)} for n, b in sections if n != default[0]],
        })
    return out


def options() -> list[dict]:
    root = _registry()
    if root is None:
        return []
    out = []
    for group in root.iter("group"):
        gname = _text(group, "configItem/description")
        for opt in group.iter("option"):
            out.append({"option": _text(opt, "configItem/name"), "description": _text(opt, "configItem/description"),
                        "group": gname})
    return out


def search(query: str, limit: int = 40) -> dict:
    """Layouts and variants whose name or description has the query in it (any case), and
    the options that do; an empty query lists the owner's own layouts and the layouts'
    names only."""
    q = query.strip().lower()
    layouts = own_layouts() + system_layouts()
    if not q:
        return {"layouts": [{"layout": l["layout"], "description": l["description"], **({"own": True} if l.get("own") else {})}
                            for l in layouts][:limit * 4]}
    hits = []
    for l in layouts:
        whole = q in l["layout"].lower() or q in l["description"].lower()
        variants = [v for v in l["variants"]
                    if whole or q in v["variant"].lower() or q in v["description"].lower()]
        if whole or variants:
            hits.append({"layout": l["layout"], "description": l["description"], "variants": variants,
                         **({"own": True} if l.get("own") else {})})
    opts = [o for o in options() if q in o["option"].lower() or q in o["description"].lower()]
    return {"layouts": hits[:limit], "options": opts[:limit]}


# --- what is set -----------------------------------------------------------------------------
_SETTING = re.compile(r"^\s*(kb_layout|kb_variant|kb_options)\s*=\s*(.*?)\s*$", re.M)


def read_conf(path: Path | None = None) -> dict:
    try:
        text = (path or conf_path()).read_text()
    except OSError:
        text = ""
    found = dict(_SETTING.findall(text))
    return {"layout": found.get("kb_layout", ""), "variant": found.get("kb_variant", ""),
            "options": [o for o in found.get("kb_options", "").split(",") if o]}


# not keyboards to type on: touchscreens, the buttons (gpio), the shell's on-screen keyboard
_NOT_TYPED_ON = re.compile(r"touch|_ts\d*$|gpio|virtual", re.I)


def hardware_keyboards() -> list[dict]:
    try:
        r = subprocess.run(["hyprctl", "-j", "devices"], capture_output=True, text=True, timeout=5)
        devices = json.loads(r.stdout or "{}")
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return []
    return [{"name": k.get("name", ""), "keymap": k.get("active_keymap", ""), "layout": k.get("layout", ""),
             "variant": k.get("variant", ""), "options": k.get("options", "")}
            for k in devices.get("keyboards", []) if not _NOT_TYPED_ON.search(k.get("name", ""))]


def status() -> dict:
    set_here = read_conf()
    out = {
        "setting": set_here if set_here["layout"] else {"layout": "us", "variant": "", "options": [],
                                                         "note": "Mobarchy's default (none set)"},
        "connected": hardware_keyboards(),
    }
    # (the owner's local.conf is read last: a layout there wins over this one)
    local = read_conf(config_home() / "hypr" / "local.conf")
    if local["layout"] or local["variant"] or local["options"]:
        out["overridden_by"] = {"file": str(config_home() / "hypr" / "local.conf"), **local}
    return out


# --- setting it ------------------------------------------------------------------------------
def check(layout: str, variant: str = "", opts: list[str] | None = None) -> None:
    """Raises ValueError unless the layout (and variant, options) exist and compile."""
    opts = opts or []
    known = {l["layout"]: l for l in own_layouts() + system_layouts()}
    if layout not in known:
        raise ValueError(f"no keyboard layout '{layout}' (keyboard_layouts finds them)")
    if variant and variant not in {v["variant"] for v in known[layout]["variants"]}:
        names = ", ".join(v["variant"] for v in known[layout]["variants"]) or "none"
        raise ValueError(f"layout '{layout}' has no variant '{variant}' (it has: {names})")
    valid = {o["option"] for o in options()}
    bad = [o for o in opts if o not in valid]
    if bad:
        raise ValueError(f"unknown option(s): {', '.join(bad)} (keyboard_layouts finds them)")
    try:
        r = subprocess.run(["xkbcli", "compile-keymap", "--layout", layout, "--variant", variant,
                            "--options", ",".join(opts)], capture_output=True, text=True, timeout=10)
    except FileNotFoundError:
        return  # (no xkbcli: the checks above have to do)
    if r.returncode != 0 or not r.stdout.strip():
        raise ValueError(f"the keymap doesn't compile: {(r.stderr or 'no output').strip()[:300]}")


def write_conf(layout: str, variant: str, opts: list[str], path: Path | None = None) -> None:
    path = path or conf_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    body = CONF_HEADER
    if layout:
        body += f"\ninput {{\n    kb_layout = {layout}\n    kb_variant = {variant}\n    kb_options = {','.join(opts)}\n}}\n"
    tmp = path.with_suffix(".tmp")
    tmp.write_text(body)
    tmp.replace(path)


def set_layout(layout: str, variant: str = "", opts: list[str] | None = None) -> dict:
    """Check, write keyboard.conf (Hyprland reloads it), and wait a moment for the
    keyboards to have it."""
    opts = [o.strip() for o in (opts or []) if o.strip()]
    check(layout, variant, opts)
    write_conf(layout, variant, opts)
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        keyboards = hardware_keyboards()
        if all(k["layout"] == layout and k["variant"] == variant for k in keyboards):
            break
        time.sleep(0.2)
    return status()


def reset() -> dict:
    """Back to Mobarchy's default (us), nothing set here."""
    write_conf("", "", [])
    return status()
