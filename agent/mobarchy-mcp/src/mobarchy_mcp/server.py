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
from . import bluetooth, camera, clipboard, clock, media, network, power, store, webapps
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


def locked() -> bool:
    """The phone is locked with a PIN (the shell publishes it, ADR 0013). The agent may not
    use the screen then: unlocking is the owner's. MOBARCHY_MCP_IGNORE_LOCK=1 is for
    development only."""
    if os.environ.get("MOBARCHY_MCP_IGNORE_LOCK") == "1":
        return False
    runtime = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    return Path(runtime, "mobarchy", "locked").exists()


def tool(risk: Risk, title: str, screen: bool = False):
    """Register a tool with a risk level. The policy check runs before the tool body.
    screen: the tool sees or drives the screen, which a locked phone doesn't allow."""
    annotations = ToolAnnotations(
        title=title,
        read_only_hint=risk is Risk.READ,
        destructive_hint=risk is Risk.CONFIRM,
    )

    def decorator(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            if screen and locked():
                raise ToolError("the phone is locked: the owner has to unlock it (with the PIN) first")
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


@tool(Risk.READ, "Screenshot", screen=True)
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


@tool(Risk.READ, "List windows", screen=True)
def list_windows() -> dict:
    """List open app windows with id, app_id, title, focus state, and geometry in logical pixels."""
    return {"windows": [w.to_dict() for w in compositor().windows()]}


@tool(Risk.READ, "List apps")
def list_apps() -> dict:
    """List installed apps that can be launched with launch_app."""
    return {"apps": [{"id": a["id"], "name": a["name"]} for a in _desktop_entries().values()]}


# --- act ------------------------------------------------------------------------


@tool(Risk.ACT, "Tap", screen=True)
def tap(x: float, y: float, hold_ms: int = 50) -> str:
    """Tap the screen at (x, y) in logical pixels. Use hold_ms around 600 for a long press."""
    fx, fy = to_fraction(x, y)
    touchscreen().tap(fx, fy, hold_ms / 1000)
    return "ok"


@tool(Risk.ACT, "Swipe", screen=True)
def swipe(x1: float, y1: float, x2: float, y2: float, duration_ms: int = 300) -> str:
    """Swipe with one finger from (x1, y1) to (x2, y2) in logical pixels."""
    f1, f2 = to_fraction(x1, y1), to_fraction(x2, y2)
    touchscreen().swipe(*f1, *f2, duration_ms / 1000)
    return "ok"


@tool(Risk.ACT, "Type text", screen=True)
def type_text(text: str) -> str:
    """Type text into the focused window, as if typed on a keyboard."""
    _run(["wtype", "--", text])
    return "ok"


@tool(Risk.ACT, "Press key", screen=True)
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


@tool(Risk.ACT, "Launch app", screen=True)
def launch_app(app_id: str) -> str:
    """Launch an installed app by id (see list_apps)."""
    entries = _desktop_entries()
    if app_id not in entries:
        raise ToolError(f"unknown app '{app_id}'")
    compositor().launch(entries[app_id]["exec"])
    time.sleep(0.5)
    return "launched"


@tool(Risk.ACT, "Focus window", screen=True)
def focus_window(window_id: str) -> str:
    """Bring a window (id from list_windows) to the front."""
    compositor().focus(window_id)
    return "ok"


@tool(Risk.ACT, "Close window", screen=True)
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


def _bluetooth(fn, *args):
    try:
        return fn(*args)
    except ValueError as e:
        raise ToolError(str(e)) from e
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired, subprocess.CalledProcessError) as e:
        raise ToolError(f"bluetooth: {e}") from e


@tool(Risk.READ, "Bluetooth status")
def bluetooth_status() -> dict:
    """Whether Bluetooth is on, and the paired devices (headphones, speakers, ...) with
    whether they are connected and, if they report it, their battery."""
    return _bluetooth(bluetooth.status)


@tool(Risk.READ, "Scan for Bluetooth devices")
def bluetooth_scan(seconds: int = 10) -> dict:
    """Look for Bluetooth devices in range for a few seconds. A device to pair must be in
    pairing mode (e.g. AirPods: lid open, double-tap the case or hold its button)."""
    return {"devices": _bluetooth(bluetooth.scan, max(3, min(seconds, 30)))}


@tool(Risk.ACT, "Connect a Bluetooth device")
def bluetooth_connect(address: str) -> dict:
    """Connect a device by address (from bluetooth_status or bluetooth_scan). A new device
    is paired and trusted first. Audio devices become the sound output."""
    return _bluetooth(bluetooth.connect, address)


@tool(Risk.ACT, "Disconnect a Bluetooth device")
def bluetooth_disconnect(address: str) -> dict:
    """Disconnect a device; it stays paired."""
    return _bluetooth(bluetooth.disconnect, address)


@tool(Risk.ACT, "Bluetooth on/off")
def set_bluetooth(enabled: bool) -> dict:
    """Switch Bluetooth on or off (the radio's power, as the quick settings tile does)."""
    return _bluetooth(bluetooth.set_enabled, enabled)


@tool(Risk.CONFIRM, "Forget a Bluetooth device")
def bluetooth_forget(address: str) -> dict:
    """Unpair a device: it must be paired again (in pairing mode) to connect."""
    return {"result": _bluetooth(bluetooth.forget, address)}


def _media(fn, *args):
    try:
        return fn(*args)
    except ValueError as e:
        raise ToolError(str(e)) from e
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"media: {e}") from e


@tool(Risk.READ, "What's playing")
def media_status() -> dict:
    """The media player in charge (the one that played last: Spotify, a browser, ...): what
    it plays, whether it plays, position and length in seconds."""
    return _media(media.status)


@tool(Risk.ACT, "Control playback")
def media_control(action: str) -> dict:
    """Control the player in charge: play, pause, play-pause, next, previous or stop."""
    return _media(media.control, action)


@tool(Risk.ACT, "Seek")
def media_seek(seconds: float, relative: bool = False) -> dict:
    """Jump to a position in the current track (seconds), or by an offset (relative=true,
    negative goes back)."""
    return _media(media.seek, seconds, relative)


# The clipboard often holds passwords and codes: reading it is not a plain read, and not
# while the phone is locked.
@tool(Risk.ACT, "Read the clipboard", screen=True)
def clipboard_get() -> dict:
    """The text the owner copied last (empty if nothing, or not text)."""
    try:
        return clipboard.get()
    except FileNotFoundError as e:
        raise ToolError(f"clipboard: {e}") from e


@tool(Risk.ACT, "Copy to the clipboard", screen=True)
def clipboard_set(text: str) -> dict:
    """Put text on the clipboard, for the owner to paste anywhere (the keyboard's paste
    button shows it)."""
    try:
        return clipboard.set_text(text)
    except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"clipboard: {e}") from e


# A photo shows the owner's surroundings: an action, and not while the phone is locked
# (as the screen; ADR 0018).
@tool(Risk.ACT, "Take a photo", screen=True)
def take_photo(facing: str = "back") -> list:
    """Take a photo with the back or front camera and look at it. It is saved in the
    owner's photos (~/Pictures/Camera, as from the camera app). On a foldable, the front
    camera is the outer one when folded and the inner one when open."""
    try:
        path = camera.take_photo(facing)
    except camera.CameraError as e:
        raise ToolError(f"camera: {e}") from e
    return [f"Saved as {path}", Image(path=path)]


@tool(Risk.READ, "Browse the app store")
def store_list(category: str = "", query: str = "") -> dict:
    """Apps known to work on this phone (Mobarchy's curated store): native apps and web
    apps, with what each does and whether it is installed. Filter by category (music,
    video, communication, internet, reading, photos, maps, productivity, utilities) or by
    words in the name and description."""
    try:
        apps = store.entries()
    except (OSError, ValueError, KeyError) as e:
        raise ToolError(f"store: {e}") from e
    words = query.lower().split()
    apps = [a for a in apps if (not category or a["category"] == category)
            and all(w in f"{a['name']} {a['summary']} {a['description']}".lower() for w in words)]
    return {"apps": [{k: a[k] for k in ("id", "kind", "name", "summary", "category", "installed", "tested", "note")}
                     for a in apps]}


@tool(Risk.ACT, "Install an app from the store")
def store_install(id: str) -> dict:
    """Install an app from the store by id (from store_list): a package, or a web app on
    the home screen."""
    try:
        return store.install(id)
    except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"could not install {id}: {e}") from e


@tool(Risk.ACT, "Remove an app installed from the store")
def store_remove(id: str) -> dict:
    """Uninstall an app that came from the store (id from store_list)."""
    try:
        return store.remove(id)
    except (ValueError, RuntimeError, OSError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"could not remove {id}: {e}") from e


@tool(Risk.ACT, "Add a web app to the home screen")
def add_web_app(url: str, name: str = "") -> dict:
    """Put a website on the home screen as its own app (own window, no browser bar), with
    the name and icon from its web app manifest. name overrides the site's name."""
    try:
        return webapps.add(url, name)
    except (OSError, ValueError) as e:
        raise ToolError(f"could not add {url}: {e}") from e


@tool(Risk.READ, "List web apps")
def list_web_apps() -> dict:
    """Web apps on the home screen: added here, or installed from Chromium ("Install app")."""
    return {"web_apps": webapps.listed()}


@tool(Risk.ACT, "Remove a web app")
def remove_web_app(app_id: str) -> dict:
    """Take a web app (app_id from list_web_apps) off the home screen."""
    try:
        return {"result": webapps.remove(app_id)}
    except (OSError, ValueError) as e:
        raise ToolError(str(e)) from e


@tool(Risk.READ, "Date and time")
def time_status() -> dict:
    """Local date and time, the time zone, and whether the clock follows network time (NTP)."""
    try:
        return clock.status()
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"time settings unavailable: {e}") from e


@tool(Risk.READ, "Find time zones")
def find_timezones(query: str) -> dict:
    """Time zone names (IANA, e.g. Europe/Oslo) matching a city or region, e.g. "oslo",
    "new york", "america". Use the exact name with set_timezone."""
    try:
        return {"timezones": clock.match_timezones(query, clock.timezones())[:50]}
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"time settings unavailable: {e}") from e


@tool(Risk.ACT, "Set time zone")
def set_timezone(timezone: str) -> dict:
    """Set the system time zone by IANA name, e.g. Europe/Oslo (see find_timezones).
    Returns the new local time."""
    try:
        return clock.set_timezone(timezone)
    except ValueError as e:
        raise ToolError(str(e)) from e
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"could not set the time zone: {e}") from e


@tool(Risk.ACT, "Network time on/off")
def set_network_time(enabled: bool) -> dict:
    """Keep the clock in sync with network time (NTP), or stop doing so."""
    try:
        return clock.set_network_time(enabled)
    except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"could not change network time: {e}") from e


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
