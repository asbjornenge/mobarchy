"""Themes (ADR 0025): the shell's and the browser's colours and font, as files they follow
live. The agent lists them, switches, and makes new ones ("a Pokémon theme").

  /usr/share/mobarchy/themes/<id>.json       the ones that come with Mobarchy
  ~/.local/share/mobarchy/themes/<id>.json   the owner's; they win over the system's
  ~/.config/mobarchy/theme                   the active one's id

A new theme has to stay readable: text, accents and the buttons' text on the accent are
checked for contrast (WCAG), and a font has to be installed."""

from __future__ import annotations

import json
import os
import re
import subprocess
from pathlib import Path

SYSTEM_DIR = Path("/usr/share/mobarchy/themes")
DEFAULT_ID = "mobarchy"
DEFAULT_FONT = "JetBrainsMono Nerd Font"

# What each colour is for, for the agent making a theme.
ROLES = {
    "bg": "the background of screens and sheets",
    "bgDark": "a darker background: the bars, the lock screen",
    "panel": "cards, tiles and buttons on bg",
    "line": "borders, dividers, pressed states",
    "fg": "text",
    "muted": "secondary text, hints, inactive icons",
    "accent": "the highlight: active tiles, sliders, primary buttons (their text is bg)",
    "green": "success, on, charging",
    "red": "errors, warnings, destructive actions",
    "magenta": "a second accent, used sparingly",
}

# (foreground, background, minimum contrast ratio, why)
CONTRAST = [
    ("fg", "bg", 4.5, "text on the background"),
    ("fg", "panel", 4.5, "text on cards and buttons"),
    ("muted", "bg", 2.5, "secondary text on the background"),
    ("accent", "bg", 3.0, "the accent on the background"),
    ("bg", "accent", 3.0, "button text (bg) on the accent"),
    ("red", "bg", 3.0, "errors on the background"),
    ("green", "bg", 3.0, "success on the background"),
]

ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,40}$")
HEX = re.compile(r"^#[0-9a-fA-F]{6}$")


class ThemeError(Exception):
    pass


def user_dir() -> Path:
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "mobarchy/themes"


def active_file() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "mobarchy/theme"


def _read(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def _themes() -> dict[str, tuple[dict, bool]]:
    """id -> (theme, the owner's); the owner's win."""
    found: dict[str, tuple[dict, bool]] = {}
    for d, own in ((SYSTEM_DIR, False), (user_dir(), True)):
        for f in sorted(d.glob("*.json")) if d.is_dir() else []:
            t = _read(f)
            if isinstance(t, dict):
                found[f.stem] = (t, own)
    return found


def current_id() -> str:
    try:
        return active_file().read_text().strip() or DEFAULT_ID
    except OSError:
        return DEFAULT_ID


def list_themes() -> dict:
    cur = current_id()
    return {"active": cur, "themes": [
        {"id": i, "name": t.get("name", i), "description": t.get("description", ""), "own": own,
         "active": i == cur} for i, (t, own) in sorted(_themes().items())]}


def current() -> dict:
    i = current_id()
    t, own = _themes().get(i, ({}, False))
    return {"id": i, "name": t.get("name", i), "description": t.get("description", ""), "own": own,
            "colors": t.get("colors", {}), "font": t.get("font", DEFAULT_FONT), "roles": ROLES}


def set_theme(theme_id: str) -> dict:
    if theme_id not in _themes():
        raise ThemeError(f"no theme {theme_id!r}; there are: {', '.join(sorted(_themes()))}")
    _write(active_file(), theme_id + "\n")
    return {"active": theme_id}


def _luminance(hex_color: str) -> float:
    def channel(c: int) -> float:
        s = c / 255
        return s / 12.92 if s <= 0.03928 else ((s + 0.055) / 1.055) ** 2.4
    r, g, b = (int(hex_color[i:i + 2], 16) for i in (1, 3, 5))
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def contrast(a: str, b: str) -> float:
    la, lb = sorted((_luminance(a), _luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def installed_fonts() -> set[str]:
    try:
        out = subprocess.run(["fc-list", ":", "family"], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return set()
    return {f.strip() for line in out.splitlines() for f in line.split(",") if f.strip()}


def check(colors: dict, font: str | None) -> list[str]:
    """What keeps a theme from being used, if anything."""
    problems = []
    missing = [r for r in ROLES if r not in colors]
    if missing:
        problems.append(f"missing colours: {', '.join(missing)}")
    for role, value in colors.items():
        if role not in ROLES:
            problems.append(f"unknown colour {role!r} (the roles: {', '.join(ROLES)})")
        elif not isinstance(value, str) or not HEX.match(value):
            problems.append(f"{role}: {value!r} is not #rrggbb")
    if problems:
        return problems
    for fg, bg, minimum, why in CONTRAST:
        ratio = contrast(colors[fg], colors[bg])
        if ratio < minimum:
            problems.append(f"{why}: {fg} {colors[fg]} on {bg} {colors[bg]} has contrast "
                            f"{ratio:.2f}, needs {minimum}")
    if font:
        fonts = installed_fonts()
        if fonts and font not in fonts:
            problems.append(f"font {font!r} is not installed")
    return problems


def create_theme(theme_id: str, name: str, colors: dict, font: str | None = None,
                 description: str = "", activate: bool = True) -> dict:
    if not ID.match(theme_id):
        raise ThemeError("id: lower-case letters, digits and dashes (e.g. pokemon-pikachu)")
    if theme_id in _themes() and not _themes()[theme_id][1]:
        raise ThemeError(f"{theme_id!r} comes with Mobarchy; choose another id")
    colors = {k: (v.lower() if isinstance(v, str) else v) for k, v in colors.items()}
    problems = check(colors, font)
    if problems:
        raise ThemeError("not readable enough or not complete: " + "; ".join(problems))
    theme = {"name": name, "description": description, "colors": colors}
    if font:
        theme["font"] = font
    _write(user_dir() / f"{theme_id}.json", json.dumps(theme, indent=2, ensure_ascii=False) + "\n")
    if activate:
        set_theme(theme_id)
    return {"id": theme_id, "active": activate}


def delete_theme(theme_id: str) -> dict:
    themes = _themes()
    if theme_id not in themes or not themes[theme_id][1]:
        raise ThemeError(f"{theme_id!r} is not one of the owner's themes")
    (user_dir() / f"{theme_id}.json").unlink()
    if current_id() == theme_id:
        set_theme(DEFAULT_ID)
    return {"deleted": theme_id, "active": current_id()}


def _write(path: Path, text: str) -> None:
    # (whole files, by rename: the shell watches them and reads them as they change)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.tmp")
    tmp.write_text(text)
    tmp.replace(path)
