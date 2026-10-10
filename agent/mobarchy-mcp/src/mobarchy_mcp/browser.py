"""Mobarchy's browser (ADR 0023), through org.mobarchy.Browser on the session bus: the
agent opens pages in the browser the owner uses (with the owner's sessions) and sees which
are open."""

from __future__ import annotations

import json
import secrets
import subprocess
from urllib.parse import urlparse

BUS = ["org.mobarchy.Browser", "/org/mobarchy/Browser", "org.mobarchy.Browser"]


class BrowserError(Exception):
    pass


def _call(method: str, signature: str = "", *args: str) -> list:
    r = subprocess.run(["busctl", "--user", "--json=short", "--timeout=5", "call",
                        *BUS, method, *([signature, *args] if signature else [])],
                       capture_output=True, text=True, timeout=10)
    if r.returncode != 0:
        raise BrowserError(r.stderr.strip() or f"{method} failed")
    return json.loads(r.stdout)["data"] if r.stdout.strip() else []


def check_url(url: str) -> str:
    """Web pages only: http(s), not file: or other schemes."""
    raw = urlparse(url)
    if raw.scheme in ("file", "javascript", "about", "data", "chrome", "view-source", "blob"):
        raise BrowserError(f"not a web address: {url!r}")
    u = urlparse(url if "://" in url else "https://" + url)
    if u.scheme not in ("http", "https") or not u.netloc:
        raise BrowserError(f"not a web address: {url!r}")
    return u.geturl()


def open_url(url: str) -> str:
    """Opens the page in a new tab of the running browser, or starts it with the page."""
    url = check_url(url)
    try:
        _call("Open", "s", url)
    except BrowserError:
        # (named as the shell names the apps it starts: app-mobarchy-*, which have a memory cap)
        unit = f"app-mobarchy-org.mobarchy.Browser-{secrets.token_hex(4)}.service"
        subprocess.Popen(["systemd-run", "--user", "--collect", "--quiet", f"--unit={unit}",
                          "mobarchy-browser", url],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return url


def parse_tabs(tabs: list[str], current: int) -> list[dict]:
    """Tabs()'s "title\\turl" strings -> [{title, url, current}]."""
    out = []
    for i, t in enumerate(tabs):
        title, _, url = t.partition("\t")
        out.append({"title": title, "url": url, "current": i == current})
    return out


def tabs() -> list[dict]:
    try:
        listed = _call("Tabs")[0]
        current = _call("Current")[0]
    except BrowserError:
        return []       # (not running: no tabs)
    return parse_tabs(listed, current)
