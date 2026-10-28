# 0045: A Hyprland plugin: the screen turns as the device does

- Status: accepted (tried and kept, 2026-10-28)
- Date: 2026-10-28

## Context

When the device turns, its rotation hook rewrites the monitor rules and reloads them
(ADR 0020). Hyprland applies the new transform at once and the apps redraw at the new size:
the picture jumps. The shell first hid the jump behind a black curtain, which the owner
found worse than the jump (a black blink each time).

The owner wanted the screen to turn, as a phone's does. Two ways were weighed:
- **A picture:** capture the screen before the turn and animate the still picture in the
  shell. Simple, but what turns is a stale picture, not the apps, and the shell would have
  to know about the turn before Hyprland applies it.
- **Turning what is rendered:** a compositor plugin turns each live frame as Hyprland draws
  it. More work and tied to Hyprland's (unstable) plugin API, but it is the proper way and
  needs nothing from the shell.

The owner chose the second.

## Decision

- **Mobarchy has its own Hyprland plugin**, `os/hyprland-plugin` (package
  `mobarchy-hyprland`, installed to `/usr/lib/mobarchy/hyprland/`, loaded by
  `plugin =` in `hyprland.conf`).
- **The turn:** before a config reload the plugin remembers each monitor's transform. After
  it (or a layout change), a monitor whose transform changed turns for
  `plugin:mobarchy:turn_ms` (350 ms by default). Each frame, as the last element of the
  render pass, the frame drawn so far is copied (a framebuffer blit) and drawn back turned
  by the quarter turns still left and shrunk to fit while crosswise, over black, easing
  (cubic out) round to rest.
- While turning, the monitor draws whole frames and direct scanout is blocked. A timer
  asks for frames every 8 ms until 150 ms after the turn, so the last, plain frame is
  always drawn. (Asking for the next frame from each frame lost one now and then on the
  inner screen, and the turn stopped short of rest until the screen was touched.)
- The plugin is built against one Hyprland version (`depends=("hyprland=<version>")`)
  and refuses to load against another. A Hyprland update means rebuilding it.
- The shell's curtain (`Curtain.qml`) is gone.

## Consequences

- The apps keep drawing through the turn: it is live, not a picture.
- Hyprland updates are pinned to the plugin: each one needs a rebuild (the API changes
  between versions, and sometimes the code with it).
- A broken plugin can take the compositor down; unloading it must leave nothing behind
  (its listeners go with it).
- The plugin is the place for further compositor work of this kind (folding, for one).
