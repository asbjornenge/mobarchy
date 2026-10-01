# comet: Google Pixel 9 Pro Fold

Status: **v0.2 boots to Arch Linux + systemd + Hyprland** (2026-09-30), with ssh over USB.
The outer panel shows a terminal (foot), the Mobarchy bar and the on-screen keyboard
(squeekboard), and the panel in use follows the fold.
**v0.1 boots** (2026-09-30). Our init runs as PID 1 on the stock kernel and loads all vendor modules. It shows its log on both panels via KMS and exposes USB NCM and ACM. The host gets 172.16.42.2 over DHCP, and `telnet 172.16.42.1` gives a root shell. This is the porting notebook, and it records what we know.

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

## Bring-up lessons (v0.1)

Each of these cost a boot loop to find:

1. **`fastboot boot` only accepts a kernel-only boot image.** Any boot.img with a ramdisk bounces back to fastboot, even with Google's own ramdisk. Our ramdisk therefore goes into `init_boot` via `fastboot flash init_boot_<slot>`.
2. **`modules.load` is not a safe insmod order.** Load like `modprobe`: dependencies from `modules.dep` first (last listed = most basic), plus `modules.softdep` pre-deps.
3. **Module parameters come from the kernel command line** (`<module>.<param>=...`), and the loader must pass them the way modprobe does. Without them:
   - UFS inline crypto's FIPS self test panics the kernel ("KP: FMP self test failed"), because the `ufs_pixel_fips140.fips_*` LBAs are missing.
   - exynos-drm doesn't know the panels (`exynos_drm.panel_name=dsim1:google-ct3b…`, `sec_panel_name=dsim0:google-ct3d…`).
4. **The kernel runs `/system/bin/modprobe` itself** (CONFIG_MODPROBE_PATH) before `/init`, to load the pKVM modules in `kvm-arm.protected_modules=exynos-pd,pkvm_s2mpu-v9`. The ramdisk must provide it (`os/initramfs/root/system/bin/modprobe`). Otherwise the S2MPU/IOMMUs fail later.
5. **The watchdog.** Load `s3c2410_wdt` with its dependencies first (`PRELOAD_MODULES`). Its probe stops the bootloader-armed watchdog. We pet `/dev/watchdog0` from userspace, as Android's `gs_watchdogd` does.
6. **USB device mode.** Bind the gadget to `11210000.dwc3` after *all* modules are loaded, then set `/sys/devices/platform/11210000.usb/dwc3_exynos_otg_b_sess` to 1, as `init.recovery.comet.rc` does.
7. **Displays.** DSI-0 is the inner panel at 2152x2076 (`google-ct3d`). DSI-1 is the outer panel at 1080x2424 (`google-ct3b`). Plain KMS dumb buffers with SETCRTC and page flip work on both (`mobarchy-screenlog`).

Debug tooling that made this possible:
- **The on-screen log.** `mobarchy-screenlog` draws the log on both panels, which is readable with the webcam.
- **Magisk-rooted stock Android.** It lets us read `/sys/fs/pstore/console-ramoops-0`, the previous boot's kernel log, including our `mobarchy:` lines. Panics are preserved; some reset types clear it.
- **`dmesg` from stock Android.** It shows exactly which modules first-stage init loads, with which arguments.

Open issues:
- The early pKVM `modprobe` is a slow shell script, so `/init` only starts at about 11 s.
- `hardlockup-watchdog` and `ehld` are skipped for now.

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

What works (2026-09-30):
- our kernel (6.1.145 + Mobarchy config) with 213 modules
- switch_root to Arch Linux on userdata and systemd
- USB NCM + networkd + ssh as `mob`
- Hyprland (llvmpipe, `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast`) drives both panels, and `grim` screenshots are correct
- Goodix touch shows up as an input device once `goodix_brl_touch` is loaded

Fixes found on the device, now in the repo:
- The fold (hall sensor) is a lid switch, so logind must ignore it or the phone suspends.
- There are no VTs, so the session is a systemd service (`mobarchy-session`) instead of getty autologin.
- The late modules from `init.insmod.comet.cfg` (touch, haptics, audio, wifi) must be loaded.
- The locale must be set to UTF-8.
- pam_systemd gives the session an ambient `CAP_WAKE_ALARM`. bubblewrap refuses to run with it, which breaks glycin, GTK's sandboxed image loader. The visible symptom was squeekboard keys labelled "icon". `mobarchy-session` drops the ambient set with `setpriv`.

**Display driver fixes** (`kernel/patches/private/google-modules/display/`). With these, Hyprland renders correctly on both panels:
1. **RGB byte order.** exynos-drm's format table gives the DPU the byte-mirrored twin of every 32-bit RGB format (XRGB8888 is fetched as BGRX, and so on). Android never noticed because HWC picks the formats to match. Symptom: an all-blue screen, because the X/alpha byte lands in the blue channel.
2. **FB pitch.** The DPP programmed the source width from `fb->width` and ignored `pitches[0]`. Compositors allocate padded buffers (a stride of 4352 bytes for 1080 pixels), so every line was fetched from the wrong offset. Symptom: skewed, partial lines. Dumb buffers without padding hid it.
3. **CPU caches.** Framebuffers come from the cached system DMA heap, and with software rendering the CPU writes them. Nothing cleaned the caches before scanout, so the DPU fetched stale lines from RAM. Symptom: garbling and flicker that grow over time and appear on content changes, with no errors in dmesg. `prepare_fb` now syncs every framebuffer for the device.

**Fold and panels** (`rootfs/usr/lib/mobarchy/fold`, `rootfs/etc/mobarchy/hyprland-device.conf`): one panel at a time. The hall sensor (`hall effect sensor`, SW_LID) picks it at session start and through Hyprland switch binds on every change.
- Folded: the outer panel DSI-2, 1080x2424, scale 2.4.
- Unfolded: the inner panel DSI-1, 2152x2076, scale 2.
- A strip at the top is reserved for the camera cutout.
- On every change, workspaces move to the active panel, and the bar and keyboard (layer surfaces tied to an output) are restarted.
- Touch mapping: `goodix_ts0` → DSI-1 and `synaptics_tcm_touch` → DSI-2. The touch drivers suspend and resume with their panel.

**Driving the phone through MCP.** `mobarchy-mcp` runs in the session and listens on the phone's loopback only. Run `tools/device/mcp-tunnel` on the host, then use `uv run mobarchy-mcp-call --url http://127.0.0.1:18765/mcp ...` from `agent/mobarchy-mcp`. Verified on the phone (2026-10-01): `device_info`, `screenshot`, `list_windows`, `tap`, `type_text`, `press_key` and `close_window`.

**Host setup for USB networking.** The gadget uses fixed MACs (phone `02:6d:6f:62:00:02`, host side `02:6d:6f:62:00:01`), so the host interface is always `enx026d6f620001`. After a reboot, NetworkManager's DHCP profile raced networkd's DHCP server, so the host uses a static profile instead:
`nmcli con add type ethernet con-name mobarchy-usb ifname enx026d6f620001 ipv4.method manual ipv4.addresses 172.16.42.2/24 ipv6.method link-local`.

Open points for v0.2:
- **Display setup:** which DRM connector belongs to which panel (inner/outer), and the panel power sequencing without Android's HWC.
- **Touch firmware:** touch firmware loading and the touch driver's userspace expectations.
- **Resizing userdata:** growing the filesystem to fill userdata on first boot.

## Sources

- LineageOS device trees: `android_device_google_comet`, `android_device_google_zumapro` (branch `lineage-23.2`)
- Kernel: `android_kernel_google_gs-6.1_manifest` (Kleaf; GKI `android14-6.1` + `google-modules`, built from source)
- Research notes: [docs/research/2026-09-30-halium-comet.md](../../docs/research/2026-09-30-halium-comet.md)
