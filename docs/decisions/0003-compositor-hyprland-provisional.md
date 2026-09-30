# 0003: Hyprland as the provisional compositor

- Status: accepted (provisional)
- Date: 2026-09-30

## Context

Omarchy uses Hyprland, and Mobarchy takes Omarchy as its inspiration. Two things count against Hyprland on a phone:

- **Halium devices have no DRM/KMS.** On devices such as `comet`, display goes through Android's hwcomposer via libhybris. Hyprland (aquamarine) needs DRM/KMS and GBM. Existing Halium distributions use compositors with a hwcomposer backend: Phoc/wlroots (Droidian) and Mir (Ubuntu Touch).
- **The config format is changing.** Hyprland 0.56 warns that the `.conf` format will be removed in 0.57.

## Decision

Use Hyprland for the prototype shell in the simulator. Keep the choice open for real devices. Keep compositor coupling behind an adapter (see 0004), so that a switch to a wlroots- or Mir-based compositor, or a purpose-built one, only touches the adapter and `os/config`.

## Consequences

- Before Milestone A2 we must decide one of three paths: write a hwcomposer backend for aquamarine, use Phoc/wlroots on Halium, or build our own compositor.
- The config in `os/config/hypr/` must be migrated when Hyprland 0.57 lands.
