# 0019: Firmware for the vendor's HALs comes from the phone's own vendor partition

- Status: accepted
- Date: 2026-10-04

## Context

The Android container runs the vendor's HALs from the phone's own vendor partition, mapped
read-only from `super` (ADR 0014). Some of those HALs drive coprocessors whose firmware
the Linux kernel loads:
- Pixel's camera HAL runs its ML stages on the GXP (a DSP) and the TPU;
- the thermal HAL runs a model on the TPU.

The kernel looks for that firmware in /usr/lib/firmware. That copy was taken from the
factory image the rootfs was built with (comet: CP3A), not from the build on the phone
(CP2A). The vendor's libraries (libgxp, the DSP programs in the camera APEX) and the
firmware they talk to are built as a pair. With them a month apart, the GXP faulted in the
camera HAL's face detector (`picko_dsp_gxp_lib`, postprocess) at the start of every
rear-camera session. The GXP stayed dead until it powered off, and the next session
crashed the HAL. Firmware from the phone's own build fixed it: no GXP or HAL crashes
in a stress test switching cameras and taking photos.

The pairing breaks again whenever the two drift: after an OTA on the Android side, or a
rootfs built from another factory image.

## Decision

- **While the container runs, the kernel loads firmware from the vendor partition
  first.** android-container sets the firmware loader's search path
  (`/sys/module/firmware_class/parameters/path`) to the mounted vendor's `firmware`
  directory when it starts, and clears it when it stops.
  - Firmware the vendor partition doesn't have is still found in /usr/lib/firmware.
  - The GXP and TPU load their firmware when they power up, which happens on demand by
    the HALs, so they always get the vendor's.
- Generic: android-container does it for any device. A vendor partition without
  `firmware` changes nothing.

## Consequences

- The coprocessor firmware always matches the HALs that use it, across OTAs and rootfs
  builds.
- Firmware loaded before the container starts (early boot, e.g. the AOC's) still comes
  from /usr/lib/firmware, so it can differ from the vendor's build. Moving that needs the
  vendor partition mapped in early boot; not done yet.
- The firmware is read from a partition Mobarchy doesn't write: nothing to keep in sync,
  and nothing extra in the image.
