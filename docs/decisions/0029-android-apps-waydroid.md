# 0029: Android apps through Waydroid (experiment)

- Status: experimental (branch `feature/android-apps`)
- Date: 2026-10-12

## Context

Some things on a phone only exist as Android apps: Vipps and BankID are the ones the owner
needs. Payments by NFC are out of reach (they need Play Integrity's strong verdict, which an
unlocked bootloader never gets, and faking it is not something we do), but the apps
themselves may run: whether they do is up to each app's own checks, and only trying tells.

The phone already runs an Android container (ADR 0014), but that one is the phone's own
vendor Android, read-only, running only the HALs telephony and the cameras need, with no
framework and no apps. Apps need a whole Android: framework, Google Play services, the Play
Store, and a window per app on our screen.

## Decision

**Waydroid**, as packaged by Arch Linux ARM: LineageOS 20 (Android 13) with GApps in LXC,
drawing its windows as Wayland clients of Hyprland.

- **Its own binder devices.** Waydroid's default is `/dev/binder`, `/dev/vndbinder` and
  `/dev/hwbinder`, which are the Android container's (ADR 0014); two Androids sharing a
  context manager break each other. `waydroid-prepare` (run before the container,
  `waydroid-container.service.d/50-mobarchy.conf`) makes `anbox-binder`,
  `anbox-vndbinder` and `anbox-hwbinder` in a binderfs instance of their own
  (`/run/waydroid-binderfs`) and points Waydroid's config at them.
- **The GPU.** Waydroid's vendor image has Mesa 26.0, which knows no Mali-G715, so
  Android rendered on the CPU (llvmpipe): slow everywhere, and the sign-in pages (a
  WebView) slow enough to time out. `os/android-mesa/build waydroid` builds our Mesa (the
  same tree as the host's, with panfrost for the G715 on Panthor) for Android 13, named as
  the vendor image names its own; `mobarchy-waydroid-mesa` puts it in Waydroid's overlay
  (`/var/lib/waydroid/overlay/vendor/lib64`), which Waydroid mounts over the vendor image.
  softpipe is the fallback, not llvmpipe: no LLVM needed for Android 13.
  `waydroid-prepare` points Waydroid's gralloc at the GPU's render node (the one whose
  driver renders; on comet renderD129, Panthor; renderD128 is the display controller).
  SurfaceFlinger and apps then render with "Mali-G715 MC7 (Panfrost)", and boot is ~15 s.
- **The vendor image's broken promise.** Its HAL manifest declares the OMX codec HAL, but
  no service provides it. Android waits for a declared HAL for ever, so the media server
  hung on its codec list. MediaProvider hung on the media server, and the shared storage
  (`/storage/emulated`) never mounted: apps and the Google sign-in waited on it.
  `waydroid-prepare` overlays the image's manifest without the HALs it lacks.
- **Networking.** Waydroid's lxc-net script uses nftables NAT if nftables works, and
  iptables' CHECKSUM target. The comet kernel has neither (`NFT_NAT`, `NFT_MASQ`,
  `NETFILTER_XT_TARGET_CHECKSUM`); they are now in `mobarchy_defconfig`. Until a kernel
  with them is flashed, the script on the phone is edited by hand (`LXC_USE_NFT="false"`,
  `|| true` on the CHECKSUM rules), which a waydroid upgrade undoes.
- **Images and Google.** The images are not in Mobarchy's image: `waydroid init -s GAPPS`
  downloads them (~1 GB). The device must be registered with Google
  (google.com/android/uncertified, with GSF's Android ID) for the Play Store to sign in.
  Google Play services in the image is old; it is updated from Google's own signed APKs
  (Android accepts an update only with the same signature).
- **Location (2026-10-19).** Waydroid has no location of its own. `android-location`
  (started by `mobarchy-android` with the session) asks GeoClue where the phone is (ADR
  0033; the shell's location agent trusts `org.mobarchy.Android`, and Android's own
  per-app permission decides from there) and gives it to Android as a test provider named
  "gps" (`cmd location providers set-test-provider-location`) through
  `android-location-set`, a root helper the owner's session may run without the PIN
  (`sudoers.d/zz-mobarchy-android`; numbers only). Root's commands get the mock-location
  app-op; adb was tried first, but Waydroid's adbd authorised and then dropped the
  connection. Android marks those fixes as from a test provider: an app that refuses them,
  refuses.
- **No spoofing.** Play Integrity will say the device is not certified. We don't fake
  certification or fingerprints to get past that: if an app refuses, it refuses.

**On demand, not always on (decided 2026-10-13).** Vipps — the app this was for — refuses at
its attestation check (see ADR 0030), so Android is an occasional tool, not something worth
~2.5 GB of memory held all day. So it is off by default:

- One app-drawer entry, **Android** (`org.mobarchy.Android.desktop` → `mobarchy-android`),
  starts the Waydroid session on demand, shows its full UI in a window, and stops the session
  when that window is closed — zero memory when unused. Waydroid's own three launcher icons
  are hidden (NoDisplay overrides).
- An always-on model (a user service keeping the session alive, with Android apps listed
  individually in the drawer) is held in reserve if several people want it; not built.

**Keeping the phone-side setup in the repo.** `waydroid-prepare` (the container's
ExecStartPre) now also reapplies the lxc-net workaround (iptables + non-fatal CHECKSUM), so a
waydroid or pacman upgrade can't silently kill Android's networking. The remaining manual,
one-time steps are external and documented, not scripted: `waydroid init -s GAPPS` (the ~1 GB
images), the Google device registration, and the GMS/Play Store APK updates.

## Consequences

- Two Androids run side by side when Android is open: Waydroid's holds ~2.5 GB (with its page
  cache) while running, on a phone with 16 GB. On-demand start bounds that to when the owner
  is actually using it. A hard memory cap on the container (in the spirit of ADR 0027) is a
  possible follow-up: Waydroid's lxc payload runs in its own cgroup, not the shell's
  `app.slice`, so it needs an lxc `memory.max`, not the app drop-in.
- The Mesa overlay must be rebuilt for Waydroid's Android version if its images move past
  Android 13 (API 33).
- The lxc-net workaround goes away once a kernel with `NFT_NAT`/`NFT_MASQ`/`XT CHECKSUM`
  (now in `mobarchy_defconfig`) is flashed; iptables NAT keeps working regardless, so there
  is no rush and no breakage when that kernel lands.
