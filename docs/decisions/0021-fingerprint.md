# 0021: The fingerprint sensor, through the TEE, as fprintd offers it

- Status: accepted
- Date: 2026-10-05

## Context

comet's fingerprint sensor is an FPC sensor in the power key. Its SPI bus belongs to Trusty,
the TEE, where the vendor's matcher (a Trusty app, `1540a.app`) reads it, keeps the
templates and matches. Linux can't read the sensor itself. On Android, the vendor's
fingerprint HAL drives the matcher, and these need more of Android:
- **Trusty's secure storage.** Its storage proxy (`storageproxyd`) relays to the UFS RPMB
  and to the `trusty_userdata` and `trusty_persist` partitions. The app loader and the
  matcher need it before they start.
- **KeyMint's HMAC key.** Keystore2 negotiates it at boot (ISharedSecret). It signs and
  checks hardware auth tokens (HATs), and the matcher won't start without it.
- **Gatekeeper.** It issues the HAT that enrolling a finger needs, for a fresh challenge,
  when the owner's credential is verified.

Linux desktops know fingerprints through fprintd (`net.reactivated.Fprint`): pam_fprintd,
GNOME and KDE settings, polkit.

## Decision

- **The TEE's storage runs in the Android container.** The container gets exactly the two
  trusty partitions writable, each through its own loop device (device.toml
  `[android.writable_partitions]`), and the RPMB LU (`/dev/sg1`). No other block device
  is writable there. Both partitions were backed up before first use.
  - `storageproxyd` asks vold whether /data is in an update checkpoint. vold isn't there:
    mobarchy-android-services answers "no checkpoint" (the container's /data is never
    checkpointed).
- **KeyMint (Trusty) and Gatekeeper (Trusty)** run as the vendor's HALs.
  `mobarchy-sharedsecret` negotiates the HMAC key once per boot, as keystore2 does, with
  Trusty's KeyMint only. The Titan M2's StrongBox isn't used.
- **A fingerprint bridge**, in the pattern of the camera (ADR 0018) and the sensors (ADR
  0020):
  - `mobarchy-fingerprintd` in the container is a client of the vendor's `IFingerprint`.
    It serves list, enrol, verify, remove and reset-lockout on a socket that only root can
    open. (Android's system user is uid 1000, which on the host is the owner's.)
  - On the Linux side, `mobarchy-fingerprint` offers fprintd's D-Bus interface and polkit
    actions. Verifying is allowed in the active session. Adding or removing a finger asks
    for the owner's own password, which is the lock screen PIN (ADR 0013).
- **Gatekeeper's credential is the bridge's own**: a random secret in the container's
  /data, readable only by root. The bridge verifies it for the HAT an enrolment needs.
  The owner's consent comes from polkit before that. This is Linux's model, where fprintd
  is trusted with enrolment, rather than Android's, where the PIN itself is the Gatekeeper
  credential.
  - The trade-off: root can enrol a finger without the PIN. Root can already do anything
    on the device.
- **All enrolments are Android user 0's**, the phone's. Linux users name theirs in
  `/var/lib/mobarchy/fingerprint/<user>`, and a match counts only for the user it belongs
  to.

## Consequences

- The lock screen verifies through fprintd's interface. sudo takes a finger through
  pam_fprintd, which ships with fprintd (replaced here), so `mobarchy-pam-fprintd` builds the
  module alone. Over ssh it steps aside: then the password. polkit would too, once the
  shell has a polkit agent.
- A finger can't unlock the keyring: that still takes the PIN. The same is true on
  Android after a reboot.
- Trusty panicked the phone once (`KP: trusty crashed`), on the storage's first use, after
  its requests had waited minutes for the proxy (before the vold stand-in). That has not
  happened again. Since a TEE panic takes the whole phone down, android-container has a
  **boot guard**: if a boot ends within 3 minutes of the container's start without a clean
  stop, the next start is in safe mode (`ro.mobarchy.safe_mode=1`), and comet's init.rc
  doesn't start the TEE's storage then. A crash can't become a boot loop. In safe mode the
  finger doesn't unlock; the PIN does.
- The shell asks for the PIN before it enrols a finger. With no polkit agent to ask again,
  a polkit rule lets the owner (wheel) enrol, as ADR 0013 does for the PIN itself.
- After too many wrong fingers the matcher locks them out; a PIN unlock lets them in again
  (org.mobarchy.Fingerprint.ResetLockout, Mobarchy's own call next to fprintd's; the
  bridge's Gatekeeper token authorizes it in the TEE), as on Android.
- After a restart the PIN unlocks first (it opens the keyring), then fingers do.
