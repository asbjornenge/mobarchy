# mobarchy-mcp

The Mobarchy OS tool layer: an MCP server that runs on the phone inside the user's Wayland session and exposes the phone as tools.

## Tools

| Tool | Risk | What it does |
|---|---|---|
| `device_info` | read | Device model, screens (physical/logical size), compositor, active policy |
| `screenshot` | read | PNG of the screen at logical resolution |
| `list_windows` | read | Open windows with id, app_id, title, focus, geometry |
| `list_apps` | read | Installed apps (from `.desktop` files) |
| `tap` | act | Tap at (x, y). A long press is `hold_ms` of about 600 |
| `swipe` | act | One-finger swipe |
| `type_text` | act | Type text into the focused window |
| `press_key` | act | Press an XKB key with optional modifiers |
| `launch_app` | act | Launch an app by id |
| `focus_window` / `close_window` | act | Window management |
| `run_shell` | confirm | Run a shell command (denied by default) |

**Coordinates** are logical pixels, which is the pixel space of the `screenshot` image. An agent can therefore tap exactly where it sees something.

## Design

- **Risk levels and policy** (`policy.py`, [ADR 0004](../../docs/decisions/0004-compositor-agnostic-tool-api.md)). Every tool declares `read`, `act` or `confirm`, and the policy in `/etc/mobarchy/policy.toml` is enforced before the tool runs. `confirm` is denied by default until there is an on-device confirmation UI. The risk level is published in the tool's `_meta["mobarchy/risk"]` and in the MCP annotations.
- **Compositor adapters** (`compositor/`). Tools use the `Compositor` interface. `hyprland.py` is the only adapter today.
- **Touch through uinput** (`touch.py`). The server creates a virtual multitouch screen, which works under any compositor and on any device. It needs `/dev/uinput` access: the session user must be in the `input` group (see `os/rootfs/etc/udev/rules.d/`).
- **Text and keys through `wtype`** (Wayland virtual-keyboard protocol). **Screenshots through `grim`** (wlr-screencopy).

## Running

On a device the server runs as the systemd user unit `mobarchy-mcp.service`, which the session starts (see `os/config/hypr/hyprland.conf`). It listens on `127.0.0.1:8765`. A device can override this in `/etc/mobarchy/mcp.env`: the simulator listens on the VM NIC, and QEMU forwards host `127.0.0.1:8765` to it.

From the dev host:

```sh
cd agent/mobarchy-mcp
uv run mobarchy-mcp-call tools
uv run mobarchy-mcp-call call screenshot --image-out shot.png
uv run mobarchy-mcp-call call tap '{"x": 225, "y": 500}'
```

Or add it to an MCP client (e.g. Claude Code):

```sh
claude mcp add --transport http mobarchy-sim http://127.0.0.1:8765/mcp
```

## Tests

```sh
uv run pytest
```

The tests run the server in-process with a fake compositor and touchscreen. To test against a real session, run `sim/mobsim up` and use `mobarchy-mcp-call`.

## Known gaps

- **No authentication.** The server must only listen on interfaces that the owner controls: loopback, USB networking, or the simulator's NAT. Auth is required before any network-facing setup.
- Only one screen: tools use the first output. Foldables need screen selection.
- No notifications, settings, telephony or sensors yet.
