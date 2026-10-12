"""The owner's pictures, ~/Pictures (the Photos app's: the camera's photos in Camera/,
screenshots in Screenshots/, anything else saved there): listed newest first, and shown in
the Photos app (org.mobarchy.Photos.Show, or the app started at the picture)."""

from __future__ import annotations

import datetime as dt
import os
import secrets
import subprocess
from pathlib import Path

IMAGES = {".jpg", ".jpeg", ".png", ".webp", ".gif"}
VIDEOS = {".mp4", ".webm", ".mkv"}
BUS = ["org.mobarchy.Photos", "/org/mobarchy/Photos", "org.mobarchy.Photos"]


class PhotosError(Exception):
    pass


def root() -> Path:
    return Path(os.environ.get("XDG_PICTURES_DIR", Path.home() / "Pictures"))


def list_pictures(album: str = "", limit: int = 30) -> dict:
    """Newest first: [{path, album, time, video}]; album: a folder in ~/Pictures, "" for all."""
    base = root()
    found = []
    for p in base.rglob("*"):
        rel = p.relative_to(base)
        if any(part.startswith(".") for part in rel.parts) or len(rel.parts) > 3 or not p.is_file():
            continue
        suffix = p.suffix.lower()
        if suffix not in IMAGES | VIDEOS:
            continue
        where = rel.parts[0] if len(rel.parts) > 1 else ""
        if album and where != album:
            continue
        found.append((p.stat().st_mtime, p, where, suffix in VIDEOS))
    found.sort(reverse=True)
    albums = sorted({str(p.relative_to(base).parts[0]) for p in base.iterdir() if p.is_dir()
                     and not p.name.startswith(".")}) if base.is_dir() else []
    return {"albums": albums, "count": len(found),
            "pictures": [{"path": str(p), "album": where,
                          "time": dt.datetime.fromtimestamp(t).isoformat(timespec="seconds"), "video": video}
                         for t, p, where, video in found[:max(1, min(limit, 200))]]}


def check_path(path: str) -> Path:
    p = Path(path).expanduser().resolve()
    if not p.is_relative_to(root().resolve()) or not p.is_file():
        raise PhotosError(f"not one of the owner's pictures (in ~/Pictures): {path}")
    return p


def show(path: str) -> str:
    """Shows the picture in the Photos app, on the screen."""
    p = check_path(path)
    r = subprocess.run(["busctl", "--user", "call", *BUS, "Show", "s", str(p)],
                       capture_output=True, text=True, timeout=10)
    if r.returncode != 0:
        # not running: start it at the picture (as the shell starts apps)
        unit = f"app-mobarchy-org.mobarchy.Photos-{secrets.token_hex(4)}.service"
        subprocess.Popen(["systemd-run", "--user", "--collect", "--quiet", f"--unit={unit}",
                          "mobarchy-shell", "--photos", str(p)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return str(p)
