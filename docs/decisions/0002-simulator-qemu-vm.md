# 0002: The simulator is a QEMU/KVM VM treated as a device

- Status: accepted
- Date: 2026-09-30

## Context

Agents need a fast loop for the shell and the tool layer before any phone boots Mobarchy. The options were:

- a nested compositor on the dev host, or
- a full VM.

Nested Hyprland on the dev host (NVIDIA) failed at EGL initialization. Even when it works, it shares GPU, session, and sockets with the developer's desktop.

## Decision

The simulator is an x86_64 QEMU/KVM VM, described as the device `devices/sim-x86_64` and driven by `sim/mobsim`.

- Arch Linux cloud image, provisioned with cloud-init on first boot.
- `virtio-vga` at phone resolution (1080x2424), rendered in software (llvmpipe). This way there is no host GPU dependency.
- `virtio-multitouch-pci` for real multitouch.
- Screenshots and touch/key input are injected from the host through QMP. This means they work even when the guest is broken.

## Consequences

- The simulator is fully isolated from the host desktop. It runs headless and is reproducible (`mobsim create --force`).
- It is x86_64, not aarch64. Architecture-specific issues only show up on real hardware. aarch64 under TCG is possible later, but slow.
- There is no Halium/libhybris in the simulator. Hardware adaptation must be tested on devices.
- `virtio-gpu` supports multiple outputs, so fold (dual screen) scenarios can be simulated later.
