# 0005: Boot strategy for comet (Pixel 9 Pro Fold)

- Status: **proposed**. Needs Asbjørn's sign-off before any step that touches the phone.
- Date: 2026-09-30

## Context

See [research notes](../research/2026-09-30-halium-comet.md). In short:

- Nobody has ported Halium to Tensor.
- The Pixel 8 Pro pmOS work shows that keeping the stock kernel and vendor images and replacing only `init_boot` plus the rootfs gives a working display, audio, wifi and GPU.
- libhybris now supports the AIDL composer.
- Halium 14 is proven on GKI 6.1 with Android 15 vendors. Halium 16 is weeks old.

## Decision (proposed)

1. **Never flash an older bootloader or firmware than what the phone has.** Pin the firmware version we develop against. Record it in `devices/comet/`.
2. **Keep stock and replace little.** Use the stock or LineageOS kernel, `vendor_boot`, `vendor_kernel_boot` and `vendor_dlkm` untouched. Mobarchy replaces only `init_boot` (a Halium-style initramfs) and puts its rootfs on userdata. This means no kernel rebuild to reach v0.1 and v0.2, with rebuilding as a fallback if the kernel config lacks what Halium needs.
3. **Halium base follows the vendor.** The Halium version must match the Android version of the vendor partitions actually on the phone. Use Halium 14 if we can run an Android 15 vendor without downgrading the bootloader. Otherwise use Halium 16.
4. **Recovery path first.** Before the first Mobarchy flash, verify that a stock factory image can restore the phone, and prefer `fastboot boot` (no flashing) if it works on the Pixel 9.
5. **Backups.** Before unlocking, record the firmware version, and back up anything the user needs, because unlocking wipes the phone. After unlocking and rooting, dump the device-unique partitions (modem/EFS and persist).

## Milestones

| Version | Goal |
|---|---|
| v0.1 | Custom `init_boot`: shell over USB (RNDIS/telnet), logs to pstore |
| v0.2 | Display and touch via libhybris and HWC3, using an existing hwcomposer compositor |
| v0.3 | `mobarchy-mcp` on the phone, reachable over USB networking |
| v1 | On-screen keyboard and a simple agent app |
| v1.1 | Audio/microphone and wifi |

## Open questions

- Which firmware or Android version is on the phone right now? This is the first thing to check when it is connected. The check is read-only (`fastboot getvar all` / `adb shell getprop`).
- Does `fastboot boot` work on the Pixel 9?
- Which compositor do we use for v0.2? See 0003.
