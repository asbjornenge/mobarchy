"""Media: whatever plays (MPRIS through playerctl, via playerctld, which follows the player
that was active last, as the shell does): Spotify's web player, a video, a podcast."""

from __future__ import annotations

import json
import subprocess
import time

PLAYERCTL = ["playerctl", "--player=playerctld"]
FIELDS = {"status": "status", "title": "title", "artist": "artist",
          "album": "album", "length_us": "mpris:length", "position_us": "position"}


def _run(cmd: list[str], timeout: float = 10) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def parse_status(text: str) -> dict:
    """playerctl's tab-separated FIELDS line -> a dict (empty values left out)."""
    values = text.rstrip("\n").split("\t")
    out: dict = {}
    for key, value in zip(FIELDS, values):
        if not value:
            continue
        if key.endswith("_us"):
            out[key.replace("_us", "_s")] = round(int(value) / 1_000_000)
        else:
            out[key] = value
    return out


def status() -> dict:
    fmt = "\t".join("{{%s}}" % f for f in FIELDS.values())
    r = _run([*PLAYERCTL, "metadata", "--format", fmt])
    if r.returncode != 0 or not r.stdout.strip():
        return {"playing": False, "player": None}
    info = parse_status(r.stdout)
    info["playing"] = info.get("status") == "Playing"
    info["player"] = active_player()
    return info


def active_player() -> str | None:
    """The player playerctld stands for (its list starts with the one active last)."""
    r = _run(["busctl", "--user", "--json=short", "get-property", "org.mpris.MediaPlayer2.playerctld",
              "/org/mpris/MediaPlayer2", "com.github.altdesktop.playerctld", "PlayerNames"])
    try:
        names = json.loads(r.stdout)["data"]
    except (ValueError, KeyError):
        return None
    if not names:
        return None
    # org.mpris.MediaPlayer2.firefox.instance_1_253 -> firefox
    return names[0].removeprefix("org.mpris.MediaPlayer2.").split(".")[0]


def control(action: str) -> dict:
    """play, pause, play-pause, next, previous, stop."""
    if action not in ("play", "pause", "play-pause", "next", "previous", "stop"):
        raise ValueError(f"unknown action {action!r}")
    r = _run([*PLAYERCTL, action])
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or "no player")
    time.sleep(0.3)
    return status()


def seek(seconds: float, relative: bool) -> dict:
    arg = f"{abs(seconds)}{'+' if seconds >= 0 else '-'}" if relative else str(max(0.0, seconds))
    r = _run([*PLAYERCTL, "position", arg])
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or "no player")
    return status()
