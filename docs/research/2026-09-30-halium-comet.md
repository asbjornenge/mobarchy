# Halium on the Pixel 9 Pro Fold (comet): state of the art

- Date: 2026-09-30. Web research. Items marked **unverified** were not confirmed from a primary source.

## Halium versions

- `Halium/android` has branches up to `halium-13.0`, plus `halium-14.0` and `halium-16.0`. There is no 15.0. https://api.github.com/repos/Halium/android/branches
- **Halium 14** is in production: the Ubuntu Touch port for Volla Phone Plinius (Dimensity 7300, kernel 6.1 GKI, Android 15 vendor) is built on it and has "Advanced support". https://devices.ubuntu-touch.io/device/ansuz/release/noble/
- **Halium 16** is new: Android 16 adaptation started in July 2026, and AIDL GNSS was added in August 2026.
- UBports porting docs cover GKI with boot header v3/v4, but they do not mention `init_boot`, `vendor_kernel_boot` or `vendor_dlkm`. https://docs.ubports.com/en/latest/porting/build_and_boot/standalone_kernel_build.html
- Droidian's porting guide is stale (2024) and covers toolchains only up to Android 13. Its tooling is active, though: `initramfs-tools-halium` has "GKI device support tweaks" (Sept 2026), and `common_fragments` has a `6.1-android-common` branch. https://github.com/droidian/initramfs-tools-halium

## Prior art on Tensor

- No Halium/Droidian/Ubuntu Touch port exists for any Tensor Pixel. The Pixel 6 attempts were never finished. https://forums.ubports.com/topic/10901/google-pixel-6-port-device-donation
- **postmarketOS on the Pixel 8 Pro (husky, Tensor G3), around Aug 2026** is the closest precedent. A custom `init_boot` replaces Android init, loads the stock modules, and mounts the pmOS root from userdata. It keeps the stock (GrapheneOS) kernel and vendor images. Screen, audio, wifi and GPU acceleration work. https://xdaforums.com/t/pmos-on-pixel-8-pro.4799433/ (read from search snippets only)
- Mainline gs101 (Pixel 6) has been upstream since Linux 6.8 and boots to a shell. There is no mainline work on zuma or zumapro.

## Pixel 9 boot specifics

- All Pixel 9 models share the bootloader `ripcurrentpro`.
- GKI with boot header v4. The ramdisk is in `init_boot`. The kernel is android14-6.1, with vendor modules in `vendor_kernel_boot` and `vendor_dlkm` (the kernel and module locations are **unverified**).
- Unlocking: enable OEM unlocking, then run `fastboot flashing unlock`. This **wipes the device**.
- **Anti-rollback:** Google raised the anti-rollback level on the Pixel 6/8 (May 2025) and the Pixel 10 (May 2026). No confirmed event exists for the Pixel 9, but one could come at any time (**unverified**). Downgrading after an anti-rollback bump bricks the phone.
- `fastboot boot` (booting without flashing) is **unverified** on the Pixel 9. There are reports that it fails with patched `init_boot` images on Pixels.

## Display under libhybris

- libhybris PR #578 ("hwc2: import HWC2 changes from Android 14 to support AIDL HAL") was merged 2026-01-19. It is needed for the AIDL composer (HWC3) on modern devices. https://github.com/libhybris/libhybris/pull/578
- The `libhwc2_compat_layer` interface is unchanged, so the existing Qt and Phosh hwcomposer backends can use it.
- No reports of HWC3 on Tensor under libhybris (**unverified**).

## Jolla Phone (2026)

- Dimensity 7100, 8/12 GB RAM, Sailfish OS 5.x. The Android base of its hardware adaptation is **unverified**. For comparison, the Jolla C2 uses BSP 5.4 on Android 13.

## Implications

See [ADR 0005](../decisions/0005-comet-boot-strategy.md).
