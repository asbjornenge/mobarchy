"""mobarchy-mcp: the phone's OS exposed as MCP tools.

Runs on the device inside the user's Wayland session and serves Streamable
HTTP. Coordinates in every tool are logical pixels of the screen, which is
the coordinate space of the images that `screenshot` returns.
"""

from __future__ import annotations

import argparse
import configparser
import functools
import inspect
import os
import shlex
import subprocess
import time
from pathlib import Path

from mcp.server.mcpserver import Context, Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.types import ToolAnnotations

from . import compositor as comp
from . import alarms, approval, bluetooth, browser, calendars, mail, themes, camera, clipboard, clock, contacts, keyboard, media, network, phone, photos, power, store, webapps, widgets
from .device import load_device
from .policy import Decision, Policy, Risk

INSTRUCTIONS = """\
You are operating a Mobarchy phone. Take a `screenshot` to see the screen.
Coordinates for tap/swipe are logical pixels in the same space as the screenshot image.
After acting, take a new screenshot to verify the result.
Tools marked risk=confirm are irreversible or outward-facing: the owner approves each call on the
screen (or the device's policy denies them). Say what you are about to do before such a call.
For root, use `pkexec COMMAND` in run_shell, never sudo: the phone shows the owner a sheet,
and pkexec waits until they authorize it with a finger or the PIN (that can take a minute).
Run it in the foreground with timeout_s of 180 or more; don't background it, poll it or kill
it, and don't wrap it in a shorter `timeout`. Its exit code tells: 0 done, 126 the owner
declined, 127 not authorized."""

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


# Tools the owner allowed for the rest of a task: task -> tool names. mobarchy-agent names
# each run of the agent in the server's URL, ?task=ID (MCP itself is stateless); the last
# few tasks are kept.
_granted: dict[str, set[str]] = {}


def _query(ctx: Context | None, key: str) -> str | None:
    try:
        request = ctx.request_context.request if ctx is not None else None
    except ValueError:
        return None
    query = getattr(request, "query_params", None)
    return (query.get(key) if query is not None else None) or None


def task_of(ctx: Context | None) -> str | None:
    return _query(ctx, "task")


# Someone Frigg didn't recognise as the owner, on a locked phone (ADR 0026: mobarchy-agent
# talk names them in the URL, ?voice=guest). They get these tools only: nothing personal,
# nothing that changes the phone beyond the music.
GUEST_TOOLS = {"battery_status", "wifi_status", "bluetooth_status", "media_status", "media_control",
               "media_seek", "time_status", "find_timezones", "theme_list", "theme_current", "store_list"}


def guest(ctx: Context | None) -> bool:
    return _query(ctx, "voice") == "guest"


def approve(name: str, title: str, arguments: dict, ctx: Context | None) -> None:
    """The owner allows this call on the screen, or it doesn't run (ADR 0024)."""
    if locked():
        raise ToolError("the phone is locked: the owner has to unlock it (fingerprint or PIN) and "
                        "approve this on the screen; ask them to, then try again")
    task = task_of(ctx)
    if task and name in _granted.get(task, ()):
        return
    try:
        answer = approval.ask(name, title, approval.detail(arguments))
    except (approval.ApprovalError, subprocess.TimeoutExpired) as e:
        raise ToolError(f"could not ask the owner, so not allowed: {e}") from e
    if answer is approval.Answer.DENY:
        raise ToolError("the owner declined this")
    if answer is approval.Answer.TASK and task:
        _granted.setdefault(task, set()).add(name)
        while len(_granted) > 20:
            del _granted[next(iter(_granted))]


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
        def wrapper(*args, mcp_context: Context | None = None, **kwargs):
            if guest(mcp_context) and fn.__name__ not in GUEST_TOOLS:
                raise ToolError("the phone is locked and this voice isn't the owner's: only the owner "
                                "can do this, by unlocking the phone (or saying it themselves)")
            if screen and locked():
                raise ToolError("the phone is locked: the owner has to unlock it (with the PIN) first")
            decision = policy.decide(fn.__name__, risk)
            if decision is Decision.DENY:
                raise ToolError(
                    f"denied by device policy: '{fn.__name__}' has risk level '{risk}' "
                    f"(policy: {policy.source})"
                )
            if decision is Decision.ASK:
                arguments = inspect.signature(fn).bind(*args, **kwargs)
                arguments.apply_defaults()
                approve(fn.__name__, title, dict(arguments.arguments), mcp_context)
            try:
                return fn(*args, **kwargs)
            except comp.CompositorError as e:
                raise ToolError(str(e)) from e

        # The request's context comes in as mcp_context (for the session an approval is for);
        # it isn't one of the tool's arguments.
        sig = inspect.signature(fn, eval_str=True)
        wrapper.__signature__ = sig.replace(parameters=[
            *sig.parameters.values(),
            inspect.Parameter("mcp_context", inspect.Parameter.KEYWORD_ONLY, default=None, annotation=Context)])
        wrapper.__annotations__ = {**fn.__annotations__, "mcp_context": Context}
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


@tool(Risk.READ, "Hardware keyboard status")
def keyboard_status() -> dict:
    """The layout set for hardware keyboards (Bluetooth, USB) and the ones connected, with
    the keymap each has. (The on-screen keyboard is the shell's and keeps its own.)"""
    return keyboard.status()


@tool(Risk.READ, "Find keyboard layouts")
def keyboard_layouts(query: str = "") -> dict:
    """Search the keyboard layouts, their variants and the options (e.g. "norw", "mac",
    "dvorak", "caps", "lv3" for which key is AltGr): names and descriptions with the query
    in them. The owner's own layouts (~/.config/xkb/symbols) are marked own. An empty query
    lists the layouts' names."""
    return keyboard.search(query)


@tool(Risk.ACT, "Set the hardware keyboard layout")
def set_keyboard_layout(layout: str, variant: str = "", options: list[str] | None = None) -> dict:
    """Set the layout of hardware keyboards (all of them): a layout, a variant of it ("" for
    the plain one) and xkb options (e.g. ["lv3:rwin_switch"] for the right Win/Command key
    as AltGr, ["caps:ctrl_modifier"]). It is checked first and applies at once. layout ""
    goes back to Mobarchy's default (us). Find names with keyboard_layouts."""
    try:
        return keyboard.reset() if not layout else keyboard.set_layout(layout, variant, options)
    except ValueError as e:
        raise ToolError(str(e)) from e


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


# The owner's pictures are theirs: not while the phone is locked (as the screen).
@tool(Risk.READ, "List the owner's pictures", screen=True)
def photos_list(album: str = "", limit: int = 30) -> dict:
    """The owner's pictures (~/Pictures: the camera's photos in "Camera", screenshots in
    "Screenshots", other folders), newest first: path, album, time, video. album: one
    folder, or "" for all."""
    return photos.list_pictures(album, limit)


@tool(Risk.READ, "Look at a picture", screen=True)
def photos_view(path: str) -> list:
    """Look at one of the owner's pictures (a path from photos_list)."""
    try:
        p = photos.check_path(path)
    except photos.PhotosError as e:
        raise ToolError(str(e)) from e
    if p.suffix.lower() in photos.VIDEOS:
        raise ToolError("that is a video: it can be shown (photos_show), not looked at")
    return [str(p), Image(path=str(p))]


@tool(Risk.ACT, "Show a picture", screen=True)
def photos_show(path: str) -> str:
    """Show one of the owner's pictures on the screen, in the Photos app (a path from
    photos_list, or a photo just taken)."""
    try:
        return f"Showing {photos.show(path)}"
    except photos.PhotosError as e:
        raise ToolError(str(e)) from e


# The browser shows on the screen, and its tabs are the owner's browsing: not while the
# phone is locked (ADR 0023).
@tool(Risk.ACT, "Open a web page", screen=True)
def browser_open(url: str) -> str:
    """Open a web page (http or https) in a new tab of the phone's browser, which the
    owner sees and uses (with their logins), and bring it to the front."""
    try:
        return f"Opened {browser.open_url(url)}"
    except browser.BrowserError as e:
        raise ToolError(f"browser: {e}") from e


@tool(Risk.READ, "List the browser's tabs", screen=True)
def browser_tabs() -> dict:
    """The browser's open tabs: title, URL, and which one is in front."""
    return {"tabs": browser.tabs()}


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
def add_web_app(url: str, name: str = "", chromium: bool = False) -> dict:
    """Put a website on the home screen as its own app (own window, no browser bar, the
    phone's site), with the name and icon from its web app manifest. name overrides the
    site's name. It runs in Mobarchy's browser; chromium=true runs it in Chromium instead
    (for a site Mobarchy's browser can't play or show)."""
    try:
        return webapps.add(url, name, browser="chromium" if chromium else "")
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


@tool(Risk.READ, "List themes")
def theme_list() -> dict:
    """The themes: Mobarchy's own and the owner's, and which is active."""
    return themes.list_themes()


@tool(Risk.READ, "Current theme")
def theme_current() -> dict:
    """The active theme: its colours and font, and what each colour role is for (start from
    these when making a new one)."""
    return themes.current()


@tool(Risk.ACT, "Switch theme")
def theme_set(theme_id: str) -> dict:
    """Switch to a theme by id. The shell and the browser change at once."""
    try:
        return themes.set_theme(theme_id)
    except themes.ThemeError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Make a theme")
def theme_create(theme_id: str, name: str, colors: dict[str, str], font: str = "",
                 description: str = "", activate: bool = True, wallpaper: str = "",
                 cards: str = "filled") -> dict:
    """Make a theme (or replace one of the owner's) and switch to it: any style the owner asks
    for, e.g. "Pokémon", "solarized", "pink and gold".

    colors: all ten roles as #rrggbb: bg, bgDark, panel, line, fg, muted, accent, green, red,
    magenta (theme_current tells what each is for; most of the screen is bg, bgDark and panel,
    so they set the mood, while accent is the style's signature colour). Dark backgrounds suit
    the phone's OLED screens. It has to stay readable: text, accent and the buttons' text (bg)
    on the accent are checked for contrast, and a failed check says which pair to fix.
    font: an installed font family (empty: Mobarchy's); icons come from the Nerd Font, so a
    Nerd Font keeps them. theme_id: lower-case, digits, dashes.
    wallpaper (optional; the lock screen and home, dimmed for the text): an image's own URL
    (JPEG, PNG, WebP; search the web for one that fits the style, portrait suits the phone),
    a file on the phone (e.g. a photo in ~/Pictures/Camera), or SVG text you write (a
    pattern in the theme's colours).
    cards: the home board's widgets, "filled" (the panel colour) or "outline" (no fill, a thin
    line: airy, best without a busy wallpaper)."""
    try:
        return themes.create_theme(theme_id, name, colors, font or None, description, activate, wallpaper, cards)
    except themes.ThemeError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Theme wallpaper")
def theme_wallpaper(theme_id: str, wallpaper: str) -> dict:
    """Set the wallpaper of one of the owner's themes ("" removes it): an image's own URL, a
    file on the phone, or SVG text, as for theme_create."""
    try:
        return themes.set_wallpaper(theme_id, wallpaper)
    except themes.ThemeError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Theme cards")
def theme_cards(theme_id: str, cards: str) -> dict:
    """How a theme (Mobarchy's own too) draws the home board's widgets: "filled" (the panel
    colour) or "outline" (no fill, a thin line). The owner can set it in the Themes app too."""
    try:
        return themes.set_cards(theme_id, cards)
    except themes.ThemeError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Delete a theme")
def theme_delete(theme_id: str) -> dict:
    """Delete one of the owner's themes (Mobarchy's own stay). If it was active, Mobarchy's own
    becomes active."""
    try:
        return themes.delete_theme(theme_id)
    except themes.ThemeError as e:
        raise ToolError(str(e)) from e


@tool(Risk.READ, "List the home widgets")
def widget_list() -> dict:
    """The owner's own widgets on the home board (made with widget_create): each one's
    description, when it was last fetched and any error, and the board as it is (the order,
    sizes and screens of every card, Mobarchy's own too)."""
    try:
        return widgets.list_widgets()
    except widgets.WidgetError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Make a home widget")
def widget_create(title: str, template: str, source: dict, icon: str = "", refresh_minutes: int = 30,
                  cols: int = 0, rows: int = 0, screens: str = "all", widget_id: str = "",
                  style: dict | None = None) -> dict:
    """Make a widget for the home board, e.g. "a news widget", "the price of bitcoin", "today's
    lunch menu". It is a description the shell draws, not code: a template filled from a
    source fetched now and every refresh_minutes (5 at the least). Returns its id and what was
    fetched (or the error: fix the source and widget_update).

    template: "list" (items: a title, a line under it, an image, a link a tap opens), "value"
    (one big value and a caption), "text" (markdown) or "image".
    source (an object, by "type"):
      {"type": "feed", "url": RSS/Atom URL, "limit": 10}      a list (find the site's feed)
      {"type": "json", "url": ..., "items": "path.to.list",
       "fields": {"title": "path", "subtitle": "path", "url": "path", "image": "path"}}   a list
      {"type": "json", "url": ..., "value": "path", "caption": "path"}    a value
      {"type": "json", "url": ..., "text": "path", "caption": "path"}   a text (and under it,
                                                               who said it: a quote's author)
      {"type": "image", "url": image URL, "caption": "..."}     an image (a webcam's still)
      {"type": "static", "content": {"text": "..."} or {"value": "..", "caption": ".."} or
       {"items": [{"title", "subtitle", "url"}]}}               what you give, as it is
    Paths are dotted ("data.children", "list.0.main.temp"); fetch the URL first to find them.
    icon: a Nerd Font glyph for its title (e.g. "󰎕" news, "󰖐" weather, "" bitcoin).
    cols x rows: its size on the board (0: the template's default): list 4x2, 4x4, 2x2; value
    2x1, 2x2; text 4x2, 2x2, 4x4; image 2x2, 4x2, 4x4.
    screens: "all", "wide" (only on a wide screen: a fold's inner one, a tablet) or "narrow"
    (only on a phone-sized screen). It goes at the end of the board; the owner moves it by
    holding it.
    style (optional): the widget's own look for its text, e.g. a quote: {"font": an installed
    family (widget_fonts), "size": 10-48 px (the theme's is 14), "italic": true, "align":
    "left" | "center" | "right"}."""
    board = {"screens": screens, **({"cols": cols, "rows": rows} if cols and rows else {})}
    try:
        return widgets.create({"title": title, "icon": icon, "template": template, "source": source,
                               "refresh_minutes": refresh_minutes, "board": board, "style": style}, widget_id)
    except widgets.WidgetError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Change a home widget")
def widget_update(widget_id: str, title: str | None = None, template: str | None = None,
                  source: dict | None = None, icon: str | None = None, refresh_minutes: int | None = None,
                  cols: int = 0, rows: int = 0, screens: str | None = None, style: dict | None = None) -> dict:
    """Change one of the owner's widgets (widget_list has them): only what is given changes,
    as in widget_create. It is fetched again at once. style: only the keys given change; an
    empty one ("font": "", "size": 0) goes back to the theme's."""
    board = {**({"screens": screens} if screens else {}), **({"cols": cols, "rows": rows} if cols and rows else {})}
    try:
        return widgets.update(widget_id, {"title": title, "template": template, "source": source, "icon": icon,
                                          "refresh_minutes": refresh_minutes, "board": board or None,
                                          "style": style})
    except widgets.WidgetError as e:
        raise ToolError(str(e)) from e


@tool(Risk.READ, "Fonts for a widget")
def widget_fonts(query: str = "") -> dict:
    """The font families installed on the phone, for a widget's style (e.g. "serif", "noto",
    "mono"; empty: all of them)."""
    q = query.strip().lower()
    return {"fonts": sorted(f for f in widgets.installed_fonts() if q in f.lower())[:200]}


@tool(Risk.ACT, "Remove a home widget")
def widget_remove(widget_id: str) -> dict:
    """Remove one of the owner's widgets, from the board and for good."""
    try:
        return widgets.remove(widget_id)
    except widgets.WidgetError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Refresh home widgets")
def widget_refresh(widget_id: str = "") -> dict:
    """Fetch a widget's content now (all of them: no id), as its refresh would."""
    try:
        return widgets.refresh([widget_id] if widget_id else None, force=True)
    except widgets.WidgetError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Arrange the home board")
def home_board_set(entries: list[dict]) -> dict:
    """Set the home board: every card in order, as widget_list's "board" has them,
    [{"kind", "cols", "rows", "screens"}] (screens "all", "wide" or "narrow"; a card left out
    is taken off the board, the owner can add it back). Use widget_list first and change only
    what the owner asked for. An empty list goes back to Mobarchy's own board."""
    try:
        return widgets.set_board(entries)
    except widgets.WidgetError as e:
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


# --- alarms and timers (ADR 0028): the shell rings them, over the lock screen ------------

@tool(Risk.READ, "Alarms and timers")
def alarm_list() -> dict:
    """The alarms (time, days, label, on/off, when each rings next) and the running timers
    (time left)."""
    try:
        return alarms.list_all()
    except alarms.AlarmError as e:
        raise ToolError(str(e)) from e


@tool(Risk.READ, "Calendar")
def calendar_events(start: str = "", days: int = 1) -> dict:
    """The owner's calendar: every event (each occurrence of repeating ones) from a day
    ("YYYY-MM-DD", default today) for some days, from the phone's calendar and the accounts'
    (iCloud, Google, ...: synced every half hour). Times are local, with their offset. Use it
    for "what do I have tomorrow", "am I free on Friday afternoon". Also lists the calendars
    (id, name, account, editable: whether events can be added, changed or deleted there)."""
    import datetime as dt
    try:
        day = dt.date.fromisoformat(start) if start else dt.date.today()
    except ValueError as e:
        raise ToolError("start: a date, YYYY-MM-DD") from e
    begin = dt.datetime.combine(day, dt.time(), calendars.local_tz())
    return {"from": day.isoformat(), "days": days,
            "calendars": [{k: c[k] for k in ("id", "name", "account", "editable")}
                          for c in calendars.calendars() if c["enabled"]],
            "events": calendars.events(begin, begin + dt.timedelta(days=max(1, min(days, 62))))}


@tool(Risk.ACT, "Add to the calendar")
def calendar_add(title: str, start: str, end: str = "", location: str = "", notes: str = "",
                 repeat: str = "", calendar: str = "phone") -> dict:
    """Put an event in a calendar: the phone's ("phone", the default) or an account's that is
    editable (its id, from calendar_events: e.g. the owner's Google calendar when they say
    so). start/end: "YYYY-MM-DD" for all day, or "YYYY-MM-DDTHH:MM" local time (end defaults
    to an hour later). repeat: "daily", "weekly", "monthly", "yearly" or an RRULE
    ("FREQ=WEEKLY;BYDAY=MO,WE"). Check the date with the owner when it is not clear (e.g.
    "Friday": which one)."""
    try:
        return calendars.add_event(title, start, end, location, notes, repeat, calendar)
    except calendars.CalendarError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Change a calendar event")
def calendar_update(uid: str, title: str | None = None, start: str | None = None, end: str | None = None,
                    location: str | None = None, notes: str | None = None) -> dict:
    """Change an event (uid from calendar_events), in the phone's calendar or an account's
    (written to the account at once). A new start keeps its length unless an end is given
    too. A repeating event changes in all its occurrences."""
    try:
        return calendars.update_event(uid, title, start, end, location, notes)
    except calendars.CalendarError as e:
        raise ToolError(str(e)) from e


@tool(Risk.CONFIRM, "Delete a calendar event")
def calendar_delete(uid: str) -> dict:
    """Delete an event (uid from calendar_events), from the phone's calendar or an account's
    (deleted there too: from Google Calendar, iCloud, ...); all its occurrences, if it
    repeats."""
    try:
        return calendars.delete_event(uid)
    except calendars.CalendarError as e:
        raise ToolError(str(e)) from e


@tool(Risk.READ, "Mail")
def mail_list(category: str = "", snoozed: bool = False, limit: int = 30) -> dict:
    """The owner's inbox (all their mail accounts, synced as mail comes): newest first, each
    with its id, sender, subject, snippet, read or not. category: "people" (mail from people),
    "notifications" or "newsletters", or "" for all. snoozed: the snoozed ones instead. Use it
    for "any new mail?", "what did Kari write?"."""
    return mail.listing("snoozed" if snoozed else "inbox", category, max(1, min(limit, 200)))


@tool(Risk.READ, "Read a mail")
def mail_read(id: str) -> dict:
    """A message in full (id from mail_list): its text, sender, recipients and attachments.
    It is marked as read."""
    try:
        out = mail.read(id)
    except mail.MailError as e:
        raise ToolError(str(e)) from e
    out.pop("html", None)
    return out


@tool(Risk.ACT, "Archive a mail")
def mail_archive(id: str) -> dict:
    """Out of the inbox, into the account's archive (id from mail_list)."""
    try:
        return mail.archive(id)
    except mail.MailError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Snooze a mail")
def mail_snooze(id: str, until: str) -> dict:
    """Out of the inbox until a time ("YYYY-MM-DDTHH:MM", local), then back at the top."""
    import datetime as dt
    try:
        when = dt.datetime.fromisoformat(until)
        if when.tzinfo is None:
            when = when.replace(tzinfo=calendars.local_tz())
        return mail.snooze(id, int(when.timestamp()))
    except ValueError as e:
        raise ToolError("until: YYYY-MM-DDTHH:MM") from e
    except mail.MailError as e:
        raise ToolError(str(e)) from e


@tool(Risk.CONFIRM, "Send a mail")
def mail_send(to: str, subject: str, body: str, account: str = "", cc: str = "", reply_to_id: str = "") -> dict:
    """Send an email as the owner (it asks them first). account: the mail account's id (from
    mail_list's accounts; the only one if just one). reply_to_id: the message answered (id from
    mail_list), to keep the thread; then account defaults to that message's."""
    try:
        if not account:
            account = reply_to_id.split("|", 1)[0] if reply_to_id else ""
        if not account:
            known = mail.mail_accounts()
            if len(known) != 1:
                raise ToolError("which account? (mail_list's accounts)")
            account = known[0]["id"]
        return mail.send(account, to, subject, body, cc, reply_to_id)
    except mail.MailError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Set an alarm")
def alarm_set(time: str, label: str = "", days: list[str] | None = None) -> dict:
    """Set an alarm: the phone rings at that time of day (24 h, "07:30"), on the screen even
    when locked, until the owner snoozes or stops it.

    days: when it repeats: day names (mon..sun), "weekdays", "weekend" or "daily"; none: once,
    at the next such time (then it turns itself off). label: what it is for, shown as it rings.
    Returns when it rings next: tell the owner."""
    try:
        return alarms.add_alarm(time, label, days)
    except alarms.AlarmError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Change an alarm")
def alarm_update(alarm_id: str, time: str | None = None, label: str | None = None,
                 days: list[str] | None = None, enabled: bool | None = None) -> dict:
    """Change an alarm (by id, from alarm_list): its time, label or days, or turn it off or
    on (enabled). What isn't given stays."""
    try:
        return alarms.update_alarm(alarm_id, time, label, days, enabled)
    except alarms.AlarmError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Delete an alarm")
def alarm_delete(alarm_id: str) -> dict:
    """Delete an alarm (by id, from alarm_list)."""
    try:
        return alarms.delete_alarm(alarm_id)
    except alarms.AlarmError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Start a timer")
def timer_start(minutes: float = 0, seconds: int = 0, label: str = "") -> dict:
    """Start a countdown ("10 minutes for the pasta"): the phone rings when it ends, as an
    alarm does. label: what it is for."""
    try:
        return alarms.start_timer(round(minutes * 60) + seconds, label)
    except alarms.AlarmError as e:
        raise ToolError(str(e)) from e


@tool(Risk.ACT, "Cancel a timer")
def timer_cancel(timer_id: str) -> dict:
    """Stop a running timer (by id, from alarm_list) before it rings."""
    try:
        return alarms.cancel_timer(timer_id)
    except alarms.AlarmError as e:
        raise ToolError(str(e)) from e


# --- contacts, calls and SMS (ADR 0031) -------------------------------------------------------

def _phone(fn, *args):
    try:
        return fn(*args)
    except (phone.PhoneError, contacts.ContactError) as e:
        raise ToolError(str(e)) from e
    except subprocess.TimeoutExpired as e:
        raise ToolError("oFono (the modem) did not answer in time") from e


@tool(Risk.READ, "Find a contact")
def contacts_search(query: str) -> dict:
    """The owner's contacts matching a name, part of one, a nickname, a number, or the owner's
    word for someone ("far", "mamma", "sjefen"): name, numbers, e-mail, relations. Best match
    first."""
    return {"contacts": _phone(contacts.search, query)[:20]}


@tool(Risk.ACT, "Add a contact")
def contacts_add(name: str, phones: list[str], email: str = "", relation: str = "") -> dict:
    """Add a person to the owner's contacts. relation: the owner's word for them, if they
    said ("far", "kjæresten"), so asking for them that way finds them later."""
    return _phone(contacts.add, name, phones, email, relation)


@tool(Risk.ACT, "Remember who someone is")
def contacts_set_relation(name: str, relation: str) -> dict:
    """Remember the owner's word for one of their contacts: when they ask for "min far" and
    no contact is known by that, ask who it is, then save it here (contact by name) so the
    next time works by itself. One person per word: it moves from anyone who had it."""
    return _phone(contacts.set_relation, name, relation)


@tool(Risk.ACT, "Add a number to a contact")
def contacts_add_phone(name: str, number: str, kind: str = "cell") -> dict:
    """Give an existing contact another phone number (kind: cell, home, work)."""
    return _phone(contacts.add_phone, name, number, kind)


@tool(Risk.CONFIRM, "Delete a contact")
def contacts_delete(name: str) -> dict:
    """Delete a contact (by name)."""
    return _phone(contacts.delete, name)


@tool(Risk.READ, "Phone status")
def phone_status() -> dict:
    """The SIM, the mobile network (operator, 5G/4G, signal) and the calls going now."""
    return _phone(phone.status)


@tool(Risk.CONFIRM, "Make a call")
def phone_call(who: str) -> dict:
    """Call someone: a phone number, or a contact by name, nickname or the owner's word for
    them ("far"). Several matches, or none: it says so; ask the owner which, or who that is
    (then contacts_set_relation). The owner talks on the phone; the call shows in the Phone
    app."""
    result = _phone(phone.dial, who)
    # the Phone app on the screen (hang up, speaker); the call goes on without it
    entry = _desktop_entries().get("org.mobarchy.Phone")
    if entry and not locked():
        try:
            compositor().launch(entry["exec"])
        except (ToolError, comp.CompositorError):
            pass
    return result


@tool(Risk.ACT, "Hang up")
def phone_hangup() -> dict:
    """End the call (all calls going now)."""
    return _phone(phone.hangup)


@tool(Risk.CONFIRM, "Send a text message")
def sms_send(to: str, text: str) -> dict:
    """Send an SMS: to a phone number, or a contact by name, nickname or the owner's word for
    them ("far"). Read the message back to the owner before sending; it shows in Messages."""
    return _phone(phone.send_sms, to, text)


@tool(Risk.READ, "Read text messages")
def sms_list(who: str = "", limit: int = 20) -> dict:
    """The newest text messages (SMS), in and out, of one person (name, number or the owner's
    word for them) or of everyone; with who it is from or to. For a code that just arrived:
    the newest few."""
    return _phone(phone.list_sms, who, limit)


@tool(Risk.CONFIRM, "Run shell command")
def run_shell(command: str, timeout_s: int = 60) -> dict:
    """Run a shell command on the device as the session user. Returns exit code, stdout and stderr (truncated).
    For root, prefix it with pkexec (never sudo): it waits in the foreground for the owner's
    finger or PIN on the phone, so give it timeout_s=180 or more (installs: more)."""
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
