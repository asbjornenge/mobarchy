# Devices

Every supported device gets a directory named after its codename (`comet`, `sim-x86_64`, ...).
The simulator is a device like any other. Generic code must work the same on it and on real phones.

## Contract

Each device directory contains:

| File | Purpose |
|---|---|
| `device.toml` | Machine-readable description: arch, boot method, displays, input, hardware quirks. |
| `README.md` | Human notes: porting status, how to flash/boot, known issues. (optional until the port starts) |
| `initramfs.conf` | Optional settings for the debug initramfs (`os/initramfs`), e.g. USB strings. |
| `scripts/` | Optional device build scripts (e.g. boot image assembly). |
| `rootfs/` | Optional overlay of files installed on top of the generic `os/rootfs/`, for example `/etc/mobarchy/mcp.env`. |

Generic layers (`os/`, `agent/`) may read `device.toml` but must never branch on a codename.
If generic code needs to know something about the hardware, add a field to `device.toml` instead.

## `device.toml` fields

```toml
[device]
codename = "..."      # directory name
name = "..."          # marketing name
arch = "aarch64"      # or x86_64
kind = "hardware"     # or "virtual"
boot = "native"       # native (vendor kernel build) | halium | qemu | mainline

[[display]]           # one entry per screen; foldables have several
id = "main"
connector = "DSI-1"   # optional: DRM connector name
width = 1080
height = 2424
scale = 2.4           # optional
top_inset = 24        # optional: logical px at the top that content keeps clear of
                      # (camera cutout, rounded corners); the status bar covers it
side_inset = 0        # optional: the same at the left and right edges (panel under the bezel)
bottom_inset = 0      # optional: the same at the bottom edge

[input]
touch = "..."

[rotation]            # optional: the screen follows how the device is held (ADR 0020)
hook = "..."          # turns the screen in use: called with normal, left-up, bottom-up or
                      # right-up; [[display]] rotation_locked = true: locked by default

[haptics]             # optional: the click the shell plays on buttons, as a force-feedback
click_bank = 0        # custom effect (the actuator's own waveform: bank, index); without
click_index = 2       # it, a short buzz
tick_bank = 0         # the lighter tick when a gesture takes hold (the app switcher);
tick_index = 9        # without it, a shorter, weaker buzz (or the click's waveform)
```

Fields will be added as the porting work needs them. Keep this document in sync.

## Current devices

| Codename | Device | Status |
|---|---|---|
| `sim-x86_64` | QEMU/KVM simulator | Working: Hyprland, touch, screenshots, mobarchy-mcp |
| `comet` | Google Pixel 9 Pro Fold | Boots Mobarchy (Hyprland, terminal, on-screen keyboard, MCP). See [comet/README.md](comet/README.md), ADR 0005/0007 |
