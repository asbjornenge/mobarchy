"""The clipboard (text), through wl-clipboard: what the owner copied, and putting text there
for them to paste (the keyboard's paste button shows it)."""

from __future__ import annotations

import subprocess

MAX_CHARS = 20_000


def get() -> dict:
    """The clipboard's text ("" when empty or not text)."""
    try:
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
