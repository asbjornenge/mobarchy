# 0014: Telephony through an Android container (Halium-style)

- Status: accepted
- Date: 2026-10-03

## Context

The comet's modem is a Samsung Shannon 5400 behind the open-source cpif kernel driver
(`/dev/umts_*`). On Android, vendor daemons bring it up: cbd boots it, rfsd serves its NV
data (IMEI, RF calibration) from the `efs` partition, and the vendor radio HAL (rild)
talks to it. Nobody has cellular working on a Tensor Pixel under Linux yet.

A lab run on 2026-10-03 started cbd and rfsd outside Android, against a copy of efs.
rfsd deleted the NV files within a second. It treats the missing property
`persist.vendor.modem.efs.clear.action` as a factory reset. Outside Android's init and
property service, these daemons don't behave as designed. Run on the real efs, they
would destroy data that can't be recreated.

Writing our own open-source replacements for cbd and rfsd was considered and set aside.

The established way, used by Ubuntu Touch, Droidian, Sailfish OS and LuneOS on other
phones, is Halium. The vendor's own Android userspace runs unchanged in a container with
Android's init, property service and service managers. oFono reaches the radio HAL over
binder through ofono-binder-plugin (mer-hybris), which supports the AIDL radio HAL that
current Android uses.

## Decision

- **An Android container runs the radio stack.** It is an LXC container whose PID 1 is
  Android's own init. It gets binderfs (binder, hwbinder, vndbinder) and the cpif device
  nodes it needs, and nothing else from `/dev`.
- **The Android userspace is the phone's own.** The container's system, system_ext,
  product, vendor and odm come from the phone's logical partitions in `super`. They are
  mapped **read-only** with device-mapper (dm-linear from the super metadata), never
  copied. No Google code goes into the repo or the image, and the versions always match
  the installed vendor firmware.
- **A trimmed boot.** The container starts only what telephony needs: the property
  service, servicemanager, hwservicemanager and vndservicemanager, cbd, rfsd, the radio
  HAL, and their dependencies. In stage 1 that means no zygote, no surfaceflinger and no
  apps; stage 2 (below) adds the framework, still without a display. We provide the
  trimmed init configuration; the vendor's rc files stay untouched underneath.
- **efs is guarded.** The container never sees the real efs or efs_backup block devices,
  and Android never formats them (fs_mgr's `formattable` is not used). efs is a directory
  bind-mounted from the host:
  - At first it's a copy, with the real partitions read-only.
  - Moving to the real efs is a separate step. It needs a fresh backup, a checksum check
    and the owner's explicit OK.
  - Android's persistent properties, which steer rfsd's erase logic, live in the
    container's `/data` and persist across restarts.
- **SELinux: Android's policy, permissive.**
  - Android's service managers won't run without SELinux. android-container loads the
    vendor's precompiled policy into the kernel, as Halium does.
  - It refuses to do so unless the kernel is permissive.
  - The container gets selinuxfs with `load` and `enforce` masked, so it can't load
    another policy or switch to enforcing.
  - Nothing is denied anywhere; the policy stays loaded until reboot.
- **Binder is shared.** The container's binderfs instance is mounted on the host and
  linked as /dev/binder, /dev/hwbinder and /dev/vndbinder there. Linux-side clients
  (libgbinder) reach Android's service managers directly.
- **Linux side:** oFono with ofono-binder-plugin. The shell (SMS, calls, signal) and
  mobarchy-mcp talk to oFono over D-Bus, not to Android.
- **Device-specific parts live in `devices/comet/`**:
  - which partitions to map;
  - the container's init configuration;
  - which device nodes to pass in.

  The generic container tooling reads them from `device.toml`.

## Consequences

- Telephony needs the vendor partitions as flashed. The container breaks if they no
  longer match the kernel, as Android itself would.
- **Calls need VoLTE.** Ice and Telia have shut 2G; Telenor (and Talkmore on its network)
  keeps 2G only until the end of 2027. A 2G call is a test step, not a target.
  - Stage 1: SMS, mobile data and call signalling. These go through the native radio
    services (`rild_exynos`, `modem_ml_svc_sit`) and oFono. VoLTE builds on them.
  - Stage 2: VoLTE. On the comet, IMS is Java apps in system_ext (ShannonIms,
    PixelImsMediaService, TelephonyGoogle), so it needs Android's framework: zygote,
    system_server and telephony, running headless in the same container. Calls are then
    bridged to the Linux side. Nobody has done this on a Pixel; the container must be
    built so that stage 2 can be added on top.
- **rfsd clears NV data in more cases than the factory-reset one.**
  - It also clears when the stored modem version (`persist.vendor.modem.baseband.version`,
    kept in the container's /data) is empty or differs from the firmware's.
  - After clearing, it restores nv_protected (IMEI, calibration) from efs_backup and
    creates a fresh nv_normal, as Android does after a factory reset.
  - Before the real efs is used, that property is seeded so this never happens there.
- **Cost:** the container costs memory and some battery, measured once it runs.
- **Dependencies:** libgbinder, libglibutil, libgbinder-radio, ofono-binder-plugin and
  LXC become Mobarchy packages. The mer-hybris components are packaged from source.
