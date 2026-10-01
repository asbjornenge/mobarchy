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

[input]
touch = "..."
```

Fields will be added as the porting work needs them. Keep this document in sync.

## Current devices

| Codename | Device | Status |
|---|---|---|
| `sim-x86_64` | QEMU/KVM simulator | Working: Hyprland, touch, screenshots, mobarchy-mcp |
| `comet` | Google Pixel 9 Pro Fold | Boots Mobarchy (Hyprland, terminal, on-screen keyboard, MCP). See [comet/README.md](comet/README.md), ADR 0005/0007 |
