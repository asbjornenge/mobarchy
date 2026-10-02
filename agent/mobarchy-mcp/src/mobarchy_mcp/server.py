"""mobarchy-mcp: the phone's OS exposed as MCP tools.

Runs on the device inside the user's Wayland session and serves Streamable
HTTP. Coordinates in every tool are logical pixels of the screen, which is
the coordinate space of the images that `screenshot` returns.
"""

from __future__ import annotations

import argparse
import configparser
import functools
import os
import shlex
import subprocess
import time
from pathlib import Path

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations

from . import compositor as comp
from . import network, power
from .device import load_device
from .policy import Decision, Policy, Risk

INSTRUCTIONS = """\
You are operating a Mobarchy phone. Take a `screenshot` to see the screen.
Coordinates for tap/swipe are logical pixels in the same space as the screenshot image.
After acting, take a new screenshot to verify the result.
Tools marked risk=confirm are irreversible or outward-facing and may be denied by the device's policy."""

mcp = MCPServer(name="mobarchy", title="Mobarchy phone", version="0.1.0", instructions=INSTRUCTIONS)
policy = Policy.load()
_compositor: comp.Compositor | None = None
_touch = None


def compositor() -> comp.Compositor:
    global _compositor
    if _compositor is None:
        try:
            _compositor = comp.detect()
        except comp.CompositorError as e:
            raise ToolError(str(e)) from e
    return _compositor


def touchscreen():
    global _touch
    if _touch is None:
        from .touch import Touchscreen
        try:
            _touch = Touchscreen()
        except OSError as e:
            raise ToolError(f"cannot open /dev/uinput for touch input: {e}") from e
    return _touch


def tool(risk: Risk, title: str):
    """Register a tool with a risk level. The policy check runs before the tool body."""
    annotations = ToolAnnotations(
        title=title,
        read_only_hint=risk is Risk.READ,
        destructive_hint=risk is Risk.CONFIRM,
    )

    def decorator(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            if policy.decide(fn.__name__, risk) is Decision.DENY:
                raise ToolError(
                    f"denied by device policy: '{fn.__name__}' has risk level '{risk}' "
                    f"(policy: {policy.source})"
                )
            try:
                return fn(*args, **kwargs)
            except comp.CompositorError as e:
                raise ToolError(str(e)) from e

        wrapper.__doc__ = f"{fn.__doc__.strip()}\n\nRisk level: {risk}."
        return mcp.tool(title=title, annotations=annotations, meta={"mobarchy/risk": str(risk)})(wrapper)

    return decorator


def primary_output() -> comp.Output:
    outputs = compositor().outputs()
    if not outputs:
        raise ToolError("no screens")
    return outputs[0]


def to_fraction(x: float, y: float) -> tuple[float, float]:
    out = primary_output()
    if not (0 <= x <= out.logical_width and 0 <= y <= out.logical_height):
        raise ToolError(f"({x}, {y}) is outside the screen ({out.logical_width}x{out.logical_height})")
    return x / out.logical_width, y / out.logical_height


# --- observe ------------------------------------------------------------------


@tool(Risk.READ, "Device info")
def device_info() -> dict:
    """Describe the device: model, screens (physical and logical size), compositor, and permission policy."""
    dev = load_device()
    try:
        outputs = [o.to_dict() for o in compositor().outputs()]
        comp_name = compositor().name
    except ToolError:
        outputs, comp_name = [], None
    return {
        "device": dev.get("device", {}),
        "screens": outputs,
        "compositor": comp_name,
        "policy": {
            "source": policy.source,
            "risk": {str(k): str(v) for k, v in policy.risk.items()},
            "tools": {k: str(v) for k, v in policy.tools.items()},
        },
    }


@tool(Risk.READ, "Screenshot")
def screenshot() -> Image:
    """Capture the screen as a PNG at logical resolution. Pixel (x, y) in the image is the coordinate to pass to tap/swipe."""
    out = primary_output()
    # grim's scale is relative to logical size: 1 gives logical resolution.
    try:
        r = subprocess.run(["grim", "-o", out.name, "-s", "1", "-t", "png", "-"],
                           capture_output=True, timeout=15)
    except subprocess.TimeoutExpired:
        # grim waits for a frame, and a screen that is off renders none.
        raise ToolError("screenshot timed out: the screen is probably off (the shell turns it off "
                        "after a minute without input)") from None
    if r.returncode != 0:
        raise ToolError(f"screenshot failed: {r.stderr.decode(errors='replace').strip()}")
    return Image(data=r.stdout, format="png")


@tool(Risk.READ, "List windows")
def list_windows() -> dict:
    """List open app windows with id, app_id, title, focus state, and geometry in logical pixels."""
    return {"windows": [w.to_dict() for w in compositor().windows()]}


@tool(Risk.READ, "List apps")
def list_apps() -> dict:
    """List installed apps that can be launched with launch_app."""
    return {"apps": [{"id": a["id"], "name": a["name"]} for a in _desktop_entries().values()]}


# --- act ------------------------------------------------------------------------


@tool(Risk.ACT, "Tap")
def tap(x: float, y: float, hold_ms: int = 50) -> str:
    """Tap the screen at (x, y) in logical pixels. Use hold_ms around 600 for a long press."""
    fx, fy = to_fraction(x, y)
    touchscreen().tap(fx, fy, hold_ms / 1000)
    return "ok"


@tool(Risk.ACT, "Swipe")
def swipe(x1: float, y1: float, x2: float, y2: float, duration_ms: int = 300) -> str:
    """Swipe with one finger from (x1, y1) to (x2, y2) in logical pixels."""
    f1, f2 = to_fraction(x1, y1), to_fraction(x2, y2)
    touchscreen().swipe(*f1, *f2, duration_ms / 1000)
    return "ok"


@tool(Risk.ACT, "Type text")
def type_text(text: str) -> str:
    """Type text into the focused window, as if typed on a keyboard."""
    _run(["wtype", "--", text])
    return "ok"


@tool(Risk.ACT, "Press key")
def press_key(key: str, modifiers: list[str] | None = None) -> str:
    """Press a key by XKB keysym name, e.g. Return, BackSpace, Escape, Tab, Up, a.
    modifiers: any of shift, ctrl, alt, logo."""
    mods = modifiers or []
    cmd = ["wtype"]
    for m in mods:
        cmd += ["-M", m]
    cmd += ["-k", key]
    for m in reversed(mods):
        cmd += ["-m", m]
    _run(cmd)
    return "ok"


@tool(Risk.ACT, "Launch app")
def launch_app(app_id: str) -> str:
    """Launch an installed app by id (see list_apps)."""
    entries = _desktop_entries()
    if app_id not in entries:
        raise ToolError(f"unknown app '{app_id}'")
    compositor().launch(entries[app_id]["exec"])
    time.sleep(0.5)
    return "launched"


@tool(Risk.ACT, "Focus window")
def focus_window(window_id: str) -> str:
    """Bring a window (id from list_windows) to the front."""
    compositor().focus(window_id)
    return "ok"


@tool(Risk.ACT, "Close window")
def close_window(window_id: str) -> str:
    """Ask a window (id from list_windows) to close. The app may prompt to save first."""
    compositor().close(window_id)
    return "ok"


# --- confirm ----------------------------------------------------------------------


@tool(Risk.READ, "Battery status")
def battery_status() -> dict:
    """Battery level (percent), charging state, health and temperature."""
    info = power.battery()
    if info is None:
        raise ToolError("this device reports no battery")
    return info


@tool(Risk.READ, "Wifi status")
def wifi_status() -> dict:
    """Wifi connection state: network (SSID), state, frequency and IP addresses."""
    try:
        return network.status()
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"wifi unavailable: {e}") from e


@tool(Risk.READ, "Scan for wifi networks")
def wifi_scan() -> dict:
    """Wifi networks in range, strongest first, with signal (dBm) and whether they are secured."""
    try:
        return {"networks": network.scan()}
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"wifi unavailable: {e}") from e


@tool(Risk.CONFIRM, "Connect to a wifi network")
def wifi_connect(ssid: str, passphrase: str = "") -> dict:
    """Connect to a wifi network and remember it. Leave passphrase empty for an open network.
    The passphrase is stored only on the device (wpa_supplicant)."""
    try:
        return network.connect(ssid, passphrase or None)
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"wifi connect failed: {e}") from e


@tool(Risk.CONFIRM, "Run shell command")
def run_shell(command: str, timeout_s: int = 30) -> dict:
    """Run a shell command on the device as the session user. Returns exit code, stdout and stderr (truncated)."""
    r = subprocess.run(["bash", "-lc", command], capture_output=True, text=True, timeout=timeout_s)
    return {"exit_code": r.returncode, "stdout": r.stdout[-8000:], "stderr": r.stderr[-4000:]}


# --- helpers ----------------------------------------------------------------------


def _run(cmd: list[str]) -> None:
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
    if r.returncode != 0:
        raise ToolError(f"{cmd[0]} failed: {r.stderr.strip()}")


def _desktop_entries() -> dict[str, dict]:
    data_dirs = [os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))]
    data_dirs += os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":")
    entries: dict[str, dict] = {}
    for d in data_dirs:
        for f in sorted(Path(d, "applications").glob("*.desktop")):
            app_id = f.stem
            if app_id in entries:
                continue  # earlier data dirs take precedence
            cp = configparser.ConfigParser(interpolation=None, strict=False)
            try:
                cp.read(f, encoding="utf-8")
                e = cp["Desktop Entry"]
            except (configparser.Error, KeyError, UnicodeDecodeError):
                continue
            if e.get("Type") != "Application" or e.get("NoDisplay") == "true" or e.get("Hidden") == "true":
                continue
            exec_ = " ".join(a for a in shlex.split(e.get("Exec", "")) if not a.startswith("%"))
            if exec_:
                entries[app_id] = {"id": app_id, "name": e.get("Name", app_id), "exec": exec_}
    return entries


def main() -> None:
    p = argparse.ArgumentParser(prog="mobarchy-mcp")
    p.add_argument("--transport", choices=["streamable-http", "stdio"], default="streamable-http")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8765)
    args = p.parse_args()
    if args.transport == "stdio":
        mcp.run("stdio")
    else:
        mcp.run("streamable-http", host=args.host, port=args.port)


if __name__ == "__main__":
    main()
