# Architecture

```
┌──────────────────────────────────────────────┐
│ Agent runtime (cloud LLM + local LLM router)  │  agent/
├──────────────────────────────────────────────┤
│ OS tool layer (MCP: windows, input, screen,   │  agent/
│ telephony, sensors, settings, shell)          │
├──────────────────────────────────────────────┤
│ Compositor adapter (Hyprland today)           │  agent/  (backend behind the tool API)
├──────────────────────────────────────────────┤
│ Mobile shell / UX                             │  os/shell (mobarchy-shell), os/config
├──────────────────────────────────────────────┤
│ Userspace rootfs                              │  os/
├──────────────────────────────────────────────┤
│ Hardware adaptation (Halium + libhybris)      │  devices/<codename>/
│ Device layer (kernel, DT, boot image)         │  devices/<codename>/
└──────────────────────────────────────────────┘
```

Mobarchy's own value lives in the top two layers. Everything below is reused, and fixes go upstream.

## Rules

1. **Device isolation.** Only `devices/<codename>/` knows about a specific phone. Generic layers read `device.toml` and never branch on a codename.
2. **Compositor-agnostic tool API.** The MCP tool schemas describe phone concepts (apps, windows, screens, touch, text), not Hyprland concepts. Hyprland is one backend. See [ADR 0004](decisions/0004-compositor-agnostic-tool-api.md).
3. **Risk levels in the tool API.** Every tool declares a risk level from day one: read, act, or confirm. See [ADR 0004](decisions/0004-compositor-agnostic-tool-api.md).
4. **Everything testable by agents.** Every feature must be verifiable on the simulator using `sim/mobsim`, with no human in the loop.

## Shell and on-device assistant

`mobarchy-shell` (`os/shell`, Qt Quick, [ADR 0009](decisions/0009-shell-qt-quick.md)) is the phone UI:

- a status bar (layer surface, top);
- the bottom edge (layer surface): swipe up for the open apps (the switcher, from wlr-foreign-toplevel-management), tap for home, and a keyboard toggle;
- quick settings (layer surface, from the status bar): brightness, Wi-Fi (wpa_supplicant), flight mode (rfkill), restart / power off;
- the lock screen and the power key (screen power and idle through standard Wayland protocols);
- the notification server (org.freedesktop.Notifications);
- home (a normal window, so text input works, [ADR 0006](decisions/0006-assistant-bar-and-keyboard.md)): three pages side by side ([ADR 0044](decisions/0044-home-pages.md)): the notifications, the widgets (where home opens), and the installed apps (desktop entries, each started in its own systemd scope). The Frigg card opens the conversation with the agent; asking runs `mobarchy-agent --json`, and the agent talks to `mobarchy-mcp`.

The on-screen keyboard is the shell's own: the shell is the session's input method ([ADR 0016](decisions/0016-own-keyboard.md)).

```
owner ── mobarchy-shell (home) ──(JSON events)── mobarchy-agent ──(MCP)── mobarchy-mcp ── compositor/uinput/...
                                                      └── Claude API
```

## Development loop

The simulator (`devices/sim-x86_64`, driven by `sim/mobsim`) is the primary loop until a real device boots. Host-side observation (QMP screendump) and input (QMP multitouch) are the simulator's equivalent of the webcam rig and USB automation planned for physical phones (Track C).
