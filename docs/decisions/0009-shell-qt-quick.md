# 0009: The shell UI is Qt Quick (QML) on layer-shell

- Status: accepted
- Date: 2026-10-01

## Context

Mobarchy needs a real phone shell on top of Hyprland:

- an agent-first home screen (an agent feed with an Ask field)
- the look of Omarchy (Tokyo Night, JetBrainsMono)
- swipe navigation: up for apps, down for quick settings

The owner's requirement is that it be fast, use few resources, and animate smoothly at 120 Hz.

The current bar (`mobarchy-bar`, Python + GTK4, ADR 0006) was a stopgap. GTK runs on cairo on comet (`GSK_RENDERER=cairo`) and is not built for gesture-driven animation.

Options considered:

- **Qt Quick/QML.** A GPU scene graph with a render thread, and touch and gesture handling built in. Layer-shell support comes from `layer-shell-qt` (KDE). Plasma Mobile and Lomiri use it.
- **GTK4 + gtk4-layer-shell.** Already in use. Its animation and gesture story is weak, and GSK on Panfrost is not proven here.
- **Quickshell** (QML for Hyprland desktops). It is built on Qt Quick anyway, but adds a dependency aimed at desktop bars.
- **Custom (Rust/C + wgpu or Skia, or a compositor plugin).** Maximum control, but text input, fonts and accessibility would all have to be built from scratch.
- **Web (WebKit/Electron).** Too heavy.

## Measurements (spike, `os/shell/spike/main.qml`, comet, Panfrost, 2026-10-01)

The spike consists of a layer-shell status bar, a home window with the feed and Ask field, and a swipe-up drawer driven by a MouseArea that follows the finger and then snaps open or closed. It uses Qt 6.11.2 and layer-shell-qt 6.7.5, launched with the stock `qml` runner. The swipes were synthetic (uinput through MCP) on the inner panel at 120 Hz.

| | |
|---|---|
| Startup to mapped window | 383 ms (cold `qml` runner, QML compiled at load) |
| Memory | 150 MB RSS: ~58 MB anonymous, ~95 MB shared libraries (Qt, Mesa). `mobarchy-bar` (GTK): 184 MB |
| Idle CPU | ~0.2 % (only the clock timer runs; nothing renders when nothing changes) |
| CPU per frame during a swipe | sync + render ≈ 0 ms on the render thread. The GPU does the work. |
| Frame interval during a swipe | mostly 7–9 ms (120 Hz), with some at 14–16 ms. Pacing still has to be checked with a real finger. |

Touch reaches Qt correctly: `wl_touch` becomes QTouchEvent and is delivered to QML items, including on the rotated inner panel.

## Decision

- The shell is written in **Qt Quick/QML** in `os/shell/`. Layer surfaces (status bar, gesture edges, overlays) use `layer-shell-qt`. Anything that needs text input stays a normal xdg window, because Hyprland does not deliver input-method text to layer surfaces (ADR 0006).
- Logic that is not UI (agent event stream, system state) lives in small C++ or Python helpers, or comes over MCP. QML stays declarative.
- The shell talks to the agent through the same JSON event stream as today (ADR 0006), so it replaces `mobarchy-bar` without touching the agent.
- The shell is generic. It reads screen and fold data from `device.toml` and the running compositor; it does not hard-code comet.

## Consequences

- Qt 6 and layer-shell-qt join the image (≈ 60 MB on disk). GTK stays for apps that use it.
- Continuous animations (for example the spike's `FrameAnimation` FPS counter) keep the GPU awake. Shell code must animate only in response to the user or real events.
- Startup can be cut further by compiling the QML ahead of time (qmlcachegen or a small C++ host) instead of using the `qml` runner.
- Next steps: an MCP-exposed shell (the agent can read and drive it), the agent feed wired to `mobarchy-agent --json`, an app drawer from `.desktop` files, quick settings, notifications and a lock screen.
