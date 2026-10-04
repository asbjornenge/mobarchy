"""The cameras (ADR 0018): photos through org.mobarchy.Camera (mobarchy-camera-source), the
same service the camera app uses. The photos are the HAL's JPEGs, saved in
~/Pictures/Camera, where the camera app shows them."""

from __future__ import annotations

import json
import subprocess

BUS = ["org.mobarchy.Camera", "/org/mobarchy/Camera", "org.mobarchy.Camera"]


class CameraError(Exception):
    pass


def _call(method: str, signature: str = "", *args: str, timeout: float = 10) -> list:
    r = subprocess.run(["busctl", "--user", "--json=short", f"--timeout={int(timeout)}", "call",
                        *BUS, method, *([signature, *args] if signature else [])],
                       capture_output=True, text=True, timeout=timeout + 5)
    if r.returncode != 0:
        raise CameraError(r.stderr.strip() or f"{method} failed")
    return json.loads(r.stdout)["data"]


def parse_cameras(data: list) -> list[dict]:
    """ListCameras' a(ssuuu) -> [{id, facing, orientation, max_width, max_height}]."""
    return [{"id": c[0], "facing": c[1], "orientation": c[2], "max_width": c[3], "max_height": c[4]}
            for c in data[0]]


def cameras() -> list[dict]:
    try:
        return parse_cameras(_call("ListCameras"))
    except (OSError, subprocess.TimeoutExpired) as e:
        raise CameraError(str(e)) from e


def pick(cams: list[dict], facing: str) -> dict:
    """The first camera facing that way ("back" or "front")."""
    for c in cams:
        if c["facing"] == facing:
            return c
    have = ", ".join(c["facing"] for c in cams) or "none"
    raise CameraError(f"no {facing} camera (cameras: {have})")


def take_photo(facing: str = "back") -> str:
    """Take a photo with the back or front camera; returns the JPEG's path. (Exposure and
    focus settle first: a few seconds unless the camera is already streaming.)"""
    if facing not in ("back", "front"):
        raise CameraError(f"facing must be back or front, not {facing!r}")
    cam = pick(cameras(), facing)
    try:
        return _call("TakePhoto", "suuu", cam["id"], "0", "0", "0", timeout=30)[0]
    except subprocess.TimeoutExpired as e:
        raise CameraError("the camera took too long") from e
