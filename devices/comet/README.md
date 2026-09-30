# comet: Google Pixel 9 Pro Fold

Status: **not booted yet.** The v0.1 images are built and ready. This is the porting notebook, and it records what we know.

Analysis source: factory image `cp3a.260905.009` (Android 17, September 2026 patch, bootloader `ripcurrentpro-17.0`), unpacked with `tools/android-images/unpack`. There are no binaries in this repo. Everything is reproducible from Google's download.

## Boot chain (GKI, header v4)

| Partition | Contents |
|---|---|
| `boot` | GKI kernel only (LZ4, `6.1.162-android14-11`), no ramdisk |
| `init_boot` | Generic ramdisk (Android first-stage init). **This is the partition Mobarchy replaces in v0.1.** |
| `vendor_boot` | Vendor ramdisk (recovery), vendor cmdline, bootconfig |
| `vendor_kernel_boot` | DTB and 220 first-stage modules with `modules.load` under `/lib/modules` |
| `vendor_dlkm`, `system_dlkm` | The rest of the modules (erofs) |

The bootloader concatenates the vendor ramdisks and the `init_boot` ramdisk. Our `/init` therefore wins, and it sees `/lib/modules` from `vendor_kernel_boot`.

The kernel cmdline from `vendor_boot` includes `console=ttySAC0,115200 earlycon=exynos4210,0x10870000`. The kernel itself sets `console=ttynull` and has pstore/ramoops enabled.

## Hardware as Linux sees it

- **Display: DRM/KMS.** `exynos-drm.ko`, `gs-drm-connector.ko`, `gs-panel.ko`, and the comet panel drivers `panel-gs-ct3a…e.ko` are all first-stage modules. Android's composer (`android.hardware.composer.hwc3-service.pixel` → `libexynosdisplay`) drives these through libdrm. A Linux compositor can use `/dev/dri/card0` directly. Rendering is in software to begin with.
- **GPU:** Mali (`mali_kbase.ko`, `mali_pixel.ko`). The userspace driver is the Android blob `vendor/lib64/egl/libGLES_mali.so`, reachable only through libhybris.
- **Touch:** kernel input drivers (`goodix_brl_touch`, `syna_touch`, `sec_touch`, `goog_touch_interface`), so evdev and libinput work.
- **USB device mode:** `dwc3-exynos-usb`, with configfs gadget functions NCM, ECM, ACM and FFS built in. There is **no RNDIS**, so we use NCM.
- **Storage:** userdata is f2fs on UFS with a zoned companion device, protected by metadata encryption and FBE with hardware-wrapped keys. Linux cannot read Android's `/data`, so Mobarchy formats userdata itself.

## Kernel config gaps (stock GKI)

These are missing from the stock kernel and matter for a normal Linux userspace: `DEVTMPFS`, `FHANDLE`, `AUTOFS_FS`, `TMPFS_POSIX_ACL`, `TMPFS_XATTR` (all needed by systemd); `SYSVIPC` (libhybris and many programs); `USER_NS` and `PID_NS` (sandboxing); `VT`; and `NF_TABLES`.

- **v0.1** works with the stock kernel: the busybox init uses `mdev` instead of devtmpfs.
- **v0.2** (systemd rootfs) needs our own kernel build. See [ADR 0007](../../docs/decisions/0007-comet-kernel.md). It builds today with `tools/kernel-build/build --device comet --src <dir>`, which produces `boot`, `vendor_kernel_boot`, `vendor_dlkm`, `system_dlkm` and dtbo. It has not been booted yet.

## v0.1: debug shell over USB

Build (host only, nothing touches the phone):

```sh
tools/android-images/unpack <factory>/image <unpacked>          # once
devices/comet/scripts/mkimages --stock <unpacked> --out <dir>
```

This produces `init_boot-mobarchy.img` (flash target) and `boot-mobarchy.img` (for a `fastboot boot` attempt). The init is tested on x86 with `os/initramfs/test-sim`.

Expected result on the phone: the host sees a USB device "Mobarchy debug (comet)" with an NCM network interface (host gets `172.16.42.2`) and a serial port (`/dev/ttyACM0`). Then `telnet 172.16.42.1` gives a root shell.

### Procedure (every phone-touching step needs Asbjørn's sign-off)

1. **Read-only inventory** with the phone in Android and USB debugging on: `adb shell getprop ro.build.fingerprint`, `ro.bootloader`, `ro.boot.slot_suffix`. Compare with the analysed build. If the phone runs another build, re-run the analysis for that build. Never flash an older bootloader (anti-rollback).
2. **Unlock the bootloader.** This **wipes the phone**: enable OEM unlocking, then run `fastboot flashing unlock`.
3. **Try without flashing:** `fastboot boot boot-mobarchy.img`. Nothing is written. A reboot returns to stock. It is unverified whether Pixel 9 accepts this for GKI and init_boot.
4. **If step 3 is refused:** `fastboot flash init_boot_<slot> init_boot-mobarchy.img`. Only the generic ramdisk changes. Recovery: the bootloader and fastboot are untouched, so hold Power + Volume Down and run `fastboot flash init_boot_<slot> <stock init_boot.img>`.
5. **If it doesn't show up on USB:** return to stock and check `/sys/fs/pstore` for the `mobarchy:` log lines.

## v0.2: Mobarchy on the phone (kernel + rootfs)

```sh
tools/kernel-build/build --device comet --src <kernel-src>        # our kernel (ADR 0007)
os/image/build --device comet --base <ArchLinuxARM-aarch64-latest.tar.gz> --out <dir> \
    --extra kernel=<kernel-src>/out/comet-mobarchy --extra firmware=<unpacked>/fs/vendor/firmware
devices/comet/scripts/mkimages --stock <unpacked> --kernel <kernel-src>/out/comet-mobarchy \
    --rootfs <dir>/rootfs.img --out <v0.2-dir>
```

The boot flow is:
1. The initramfs loads the first-stage modules.
2. It brings up the USB gadget.
3. It mounts the `userdata` partition and switch_roots into systemd.
4. systemd loads the remaining modules (`/etc/modules-load.d/mobarchy-comet.conf`), and networkd serves 172.16.42.1 on USB.
5. The owner is logged in automatically, and Hyprland starts on the DRM display.

If anything before switch_root fails, the debug shell from v0.1 stays up. `mobarchy.debug` on the kernel command line always stops at the debug shell.

Flashing commands are generated in `<v0.2-dir>/FLASHING.md` and need sign-off. `vendor_dlkm` and `system_dlkm` are logical partitions, so they are flashed from fastbootd.

Open points for v0.2:
- **Display setup:** which DRM connector belongs to which panel (inner/outer), and the panel power sequencing without Android's HWC.
- **Touch firmware:** touch firmware loading and the touch driver's userspace expectations.
- **Resizing userdata:** growing the filesystem to fill userdata on first boot.

## Sources

- LineageOS device trees: `android_device_google_comet`, `android_device_google_zumapro` (branch `lineage-23.2`)
- Kernel: `android_kernel_google_gs-6.1_manifest` (Kleaf; GKI `android14-6.1` + `google-modules`, built from source)
- Research notes: [docs/research/2026-09-30-halium-comet.md](../../docs/research/2026-09-30-halium-comet.md)
