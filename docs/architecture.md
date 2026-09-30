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
│ Mobile shell / UX                             │  os/config, later os/shell
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

## Development loop

The simulator (`devices/sim-x86_64`, driven by `sim/mobsim`) is the primary loop until a real device boots. Host-side observation (QMP screendump) and input (QMP multitouch) are the simulator's equivalent of the webcam rig and USB automation planned for physical phones (Track C).
