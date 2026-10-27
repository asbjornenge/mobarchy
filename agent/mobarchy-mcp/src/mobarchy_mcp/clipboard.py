"""The clipboard (text), through wl-clipboard: what the owner copied, and putting text there
for them to paste (the keyboard's paste button shows it). A copy a password manager marks as
secret (x-kde-passwordManagerHint: 1Password, KeePassXC) is not read: the agent is told there
is one, not what it is."""

from __future__ import annotations

import subprocess

MAX_CHARS = 20_000
SECRET_HINT = "x-kde-passwordManagerHint"


def get() -> dict:
    """The clipboard's text ("" when empty or not text; secret: a password manager's)."""
    try:
        types = subprocess.run(["wl-paste", "--list-types"], capture_output=True, timeout=5)
        if types.returncode == 0 and SECRET_HINT in types.stdout.decode("utf-8", "replace").split():
            return {"text": "", "secret": True,
                    "note": "a password manager's secret (a password): not read; the owner can paste it"}
        r = subprocess.run(["wl-paste", "--no-newline", "--type", "text"], capture_output=True,
                           timeout=5)
    except subprocess.TimeoutExpired:
        return {"text": "", "error": "the app that owns the clipboard didn't answer"}
    if r.returncode != 0:
        return {"text": ""}
    text = r.stdout.decode("utf-8", errors="replace")
    out = {"text": text[:MAX_CHARS]}
    if len(text) > MAX_CHARS:
        out["truncated"] = True
        out["length"] = len(text)
    return out


def set_text(text: str) -> dict:
    """Put text on the clipboard. wl-copy serves it from the background until something
    else is copied, so it must not hold our pipes."""
    subprocess.run(["wl-copy", "--type", "text/plain;charset=utf-8"], input=text.encode("utf-8"),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5,
                   start_new_session=True, check=True)
    return {"ok": True, "length": len(text)}
