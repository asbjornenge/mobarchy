"""Hyprland adapter, via hyprctl."""

from __future__ import annotations

import json
import subprocess

from . import Compositor, CompositorError, Output, Window


def hyprctl(*args: str, json_out: bool = False) -> object:
    cmd = ["hyprctl", *(["-j"] if json_out else []), *args]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    if r.returncode != 0:
        raise CompositorError(f"hyprctl {' '.join(args)}: {r.stderr.strip() or r.stdout.strip()}")
    if json_out:
        return json.loads(r.stdout)
    out = r.stdout.strip()
    if out and out != "ok":
        raise CompositorError(f"hyprctl {' '.join(args)}: {out}")
    return out


class Hyprland(Compositor):
    name = "hyprland"

    def outputs(self) -> list[Output]:
        return [
            Output(name=m["name"], width=m["width"], height=m["height"], scale=m["scale"], x=m["x"], y=m["y"])
            for m in hyprctl("monitors", json_out=True)
        ]

    def windows(self) -> list[Window]:
        active = hyprctl("activewindow", json_out=True) or {}
        monitors = {m["id"]: m["name"] for m in hyprctl("monitors", json_out=True)}
        return [
            Window(
                id=c["address"],
                app_id=c["class"],
                title=c["title"],
                focused=c["address"] == active.get("address"),
                x=c["at"][0], y=c["at"][1], width=c["size"][0], height=c["size"][1],
                output=monitors.get(c["monitor"], str(c["monitor"])),
            )
            for c in hyprctl("clients", json_out=True)
            if c.get("mapped", True)
        ]

    def focus(self, window_id: str) -> None:
        hyprctl("dispatch", "focuswindow", f"address:{window_id}")

    def close(self, window_id: str) -> None:
        hyprctl("dispatch", "closewindow", f"address:{window_id}")

    def launch(self, command: str) -> None:
        hyprctl("dispatch", "exec", command)
