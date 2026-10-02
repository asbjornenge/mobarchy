"""Media: whatever plays (MPRIS through playerctl, via playerctld, which follows the player
that was active last, as the shell does), and Spotify through spotify_player's CLI."""

from __future__ import annotations

import json
import shutil
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


# --- Spotify ------------------------------------------------------------------------------

SPOTIFY = "spotify_player"
KINDS = ("track", "album", "artist", "playlist")


def _spotify_running() -> bool:
    return _run(["pgrep", "-x", SPOTIFY]).returncode == 0


def _ensure_spotify() -> None:
    """The player must run to play here: in the background (a Spotify Connect device), unless
    its app is open already."""
    if shutil.which(SPOTIFY) is None:
        raise RuntimeError("spotify_player is not installed")
    if _spotify_running():
        return
    subprocess.Popen(["systemd-run", "--user", "--quiet", "--collect", "--unit=spotify-player-daemon",
                      SPOTIFY, "--daemon"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        time.sleep(0.2)
        if _spotify_running():
            time.sleep(2)  # its client and Connect device need a moment
            return
    raise RuntimeError("spotify_player did not start")


def summarize_search(data: dict, limit: int = 5) -> dict:
    """spotify_player's search JSON -> short lists the agent can choose from."""
    out = {}
    for key, kind in (("tracks", "track"), ("albums", "album"), ("artists", "artist"), ("playlists", "playlist")):
        items = []
        for item in data.get(key, [])[:limit]:
            entry = {"id": item["id"], "name": item["name"]}
            if item.get("artists"):
                entry["artists"] = ", ".join(a["name"] for a in item["artists"])
            if kind == "track" and item.get("album"):
                entry["album"] = item["album"]["name"]
            items.append(entry)
        if items:
            out[key] = items
    return out


def spotify_search(query: str) -> dict:
    r = _run([SPOTIFY, "search", query], timeout=30)
    if r.returncode != 0 or not r.stdout.strip().startswith("{"):
        raise RuntimeError(r.stderr.strip() or "search failed (is Spotify logged in?)")
    return summarize_search(json.loads(r.stdout))


def spotify_play(kind: str, item_id: str = "", name: str = "", shuffle: bool = False) -> dict:
    if kind not in KINDS:
        raise ValueError(f"kind must be one of {', '.join(KINDS)}")
    if not item_id and not name:
        raise ValueError("give an id (from spotify_search) or a name")
    _ensure_spotify()
    which = ["--id", item_id] if item_id else ["--name", name]
    if kind == "track":
        cmd = [SPOTIFY, "playback", "start", "track", *which]
    else:
        cmd = [SPOTIFY, "playback", "start", "context", *which, kind] + (["--shuffle"] if shuffle else [])
    r = _run(cmd, timeout=30)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or r.stdout.strip() or "playback failed")
    time.sleep(1.5)
    return status()
