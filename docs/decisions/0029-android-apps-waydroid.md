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
- **No spoofing.** Play Integrity will say the device is not certified. We don't fake
  certification or fingerprints to get past that: if an app refuses, it refuses.

## Consequences

- The session (`waydroid session start`) is started by hand for now; a user service that
  keeps it running, and Android apps in the app drawer, come if the experiment works.
- Two Androids run side by side: Waydroid's holds ~2.5 GB (with its page cache) while
  running, on a phone with 16 GB.
- The Mesa overlay must be rebuilt for Waydroid's Android version if its images move past
  Android 13 (API 33).
