# 0006: On-device assistant: bar, composer window and squeekboard

- Status: accepted
- Date: 2026-09-30

## Context

v1 on the phone needs a way for the owner to give the agent a task on the phone itself, with an on-screen keyboard. The simulator runs Hyprland.

What we found on Hyprland 0.56:

- A layer-shell surface in on-demand keyboard mode does not get keyboard focus from a touch.
- Hyprland does not deliver input-method text (text-input-v3) to layer surfaces. squeekboard's key presses into a layer-shell text field were lost.
- A brief exclusive grab plus remapping tricks made typing into a layer surface work, but it was fragile: the keyboard hid itself, and the stacking order raced.
- Normal (xdg) windows get focus from touch, and input-method text works in them.

## Decision

- **squeekboard** is the on-screen keyboard. It is available in Arch and Debian and is the de facto standard on mobile Linux. It shows automatically when a text field gets focus: `screen-keyboard-enabled=true` is set via a gschema override in `os/rootfs`.
- **mobarchy-bar** (`os/rootfs/usr/bin/mobarchy-bar`, GTK4 + gtk4-layer-shell) is a thin layer-shell panel at the bottom of the screen. It shows agent status and has two buttons: **✦ Ask** and **⌨** (toggles the keyboard by hand). It has no text field.
- **✦ Ask** opens the *composer*, a normal window with a text field. Typing works there as in any app. Submitting closes it and starts `mobarchy-agent --json` as a subprocess, and the bar shows its events.
- The agent runtime and the UI are separate processes. The UI only depends on the JSON event stream, so it can be rewritten (for example in a native toolkit) without touching the agent.

## Consequences

- A terminal (foot) always has text-input enabled, so the keyboard comes up whenever the terminal is focused. This is fine for a terminal, but it needs thought once there are more apps.
- The design avoids compositor-specific behavior, so it should carry over to Phoc on Halium (0003).
- Voice input (v1.1) fits in as a microphone button next to ✦ Ask that feeds the same composer.
