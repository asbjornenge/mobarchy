# 0007: comet uses the vendor display driver (DRM/KMS) and gets our own kernel build

- Status: accepted for v0.1; the kernel build path is proposed
- Date: 2026-09-30
- Amends: [0005](0005-comet-boot-strategy.md). It keeps "replace little" for v0.1 and drops "keep the stock kernel" for v0.2.

## Context

Firmware analysis (see `devices/comet/README.md`) found:

1. **The display is a standard DRM/KMS driver** (`exynos-drm` plus panel drivers) in the vendor kernel. Android's composer is a libdrm client, so a Linux compositor can drive the screen without hwcomposer or libhybris.
2. **The stock GKI kernel lacks options a Linux userspace needs:** DEVTMPFS, FHANDLE, AUTOFS, TMPFS ACL/XATTR, SYSVIPC, USER/PID namespaces.
3. **LineageOS builds the GKI kernel and every Pixel module from source** with Kleaf (`android_kernel_google_gs-6.1_manifest`).

## Decision

- **v0.1** keeps the stock kernel and all vendor images, and replaces only `init_boot` with the busybox debug initramfs (`os/initramfs`). `mdev` covers the missing devtmpfs.
- **v0.2** uses a Mobarchy kernel. It is the LineageOS gs-6.1 tree plus a Mobarchy config fragment, with the kernel and all modules built together, so there is no KMI mismatch. We flash `boot`, `vendor_kernel_boot`, `vendor_dlkm` and `system_dlkm` from that build.
- **Display:** the compositor talks to exynos-drm over KMS directly. Rendering starts in software. GPU acceleration via libhybris (Mali blob) is a later, separate step. This means Halium is needed for GPU acceleration, modem and other HALs, but not for getting pixels on screen.
- **The build runs in a container** (`tools/kernel-build`) on external storage. Flags: `--config=use_source_tree_aosp --download_prebuilt_gki_fips140=false --notrim --defconfig_fragment=//mobarchy:mobarchy_defconfig`. Without `--notrim`, USER_NS makes `make_kuid` and friends undefined for modules.

Status on 2026-09-30:
- Both the stock build and the Mobarchy build succeed: 337 modules; the fragment options are verified in the embedded config.
- Neither build has booted on hardware yet.
- LineageOS's GKI source is 6.1.145, older than the factory 6.1.162, so we need to track upstream updates.

## Consequences

- v0.2 no longer depends on libhybris or HWC3, so the risky part shrinks. Hyprland (or any wlroots compositor) is viable on the phone. See [0003](0003-compositor-hyprland-provisional.md).
- Software rendering at 2076x2152 will be slow. That is acceptable for v0.2 and makes libhybris GPU work the performance milestone.
- Adding kernel options can change struct layouts (for example, SYSVIPC changes `task_struct`). Building all modules from the same tree handles this. The prebuilt vendor *userspace* blobs are unaffected.
- We own kernel updates. We must track LineageOS's gs-6.1 branch for security fixes.
