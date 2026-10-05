# comet fingerprint: where it stands (2026-10-05)

The Pixel 9 Pro Fold's fingerprint sensor is an FPC sensor in the power button (side FPS).
The image is read and matched inside Trusty, the TEE. Linux never sees a fingerprint, only
the result.

## What was found

- **Kernel.** `fpc1020_platform_tee` is loaded and bound to `odm:fp_fpc1020`. Its sysfs
  (`/sys/devices/platform/odm/odm:fp_fpc1020/`: `irq`, `irq_enable`, `hw_reset`,
  `clk_enable`, `regulator_enable`, `wakeup_enable`, `device_prepare`) gives power, reset and
  the finger-down interrupt. The SPI bus belongs to Trusty. A `goodix_fp` class also exists,
  for the other sensor SKU; on this phone it is empty.
- **HAL.** The vendor's `android.hardware.biometrics.fingerprint-service.fpc42_fw49` is the
  AIDL `IFingerprint/default`, version 3, with Google's side-FPS extensions. Its rc
  (`vendor/etc/init/fingerprint-fpc42_fw49.rc`) does this:

      on property:ro.vendor.trusty.storage.fs_ready=1
          exec_background - system shell -- /vendor/bin/trusty_apploader /vendor/firmware/1540a.app
          enable fps_hal

  It uses `/dev/trusty-ipc-dev0` (Trusty port `com.android.trusty.fpctzapp`), `/dev/uinput`
  and `/dev/input/event*`. It keeps its data in `/data/fpc` and `/data/fpc_tpl/`, where the
  templates are sealed by the TA.
- **The matcher is not built into Trusty.** It is a separate Trusty app (`1540a.app`), which
  `trusty_apploader` loads at runtime. When run in the container without Trusty's secure
  storage, the apploader blocks: the app loader (rollback versions) and the TA need storage
  first.
- **Secure storage** is `storageproxyd`, as in `init.zumapro.board.rc`:

      storageproxyd -d /dev/trusty-ipc-dev0 -r /dev/sg1 -p /data/vendor/ss -t ufs
                    -f 0:/dev/block/.../by-name/trusty_userdata
                    -f persist/0:/dev/block/.../by-name/trusty_persist
                    -m /dev/block/.../by-name/trusty_userdata

  It relays Trusty's writes to the UFS RPMB (through `/dev/sg1`), to the `trusty_userdata`
  and `trusty_persist` partitions, and to files under `/data/vendor/ss`.
- **Enrolment needs a hardware auth token (HAT)** for a fresh challenge. Android gets one
  from Gatekeeper when the user confirms their PIN. On comet that is
  `android.hardware.gatekeeper-service.trusty`, and its records live in Trusty's secure
  storage. The HMAC key that signs HATs is shared between the TAs (KeyMint, Gatekeeper,
  fingerprint) through KeyMint's shared-secret negotiation. Keystore2 does that at boot,
  in memory. The Titan M2 (citadel) HALs (weaver, authsecret, oemlock, its own KeyMint)
  are not needed for this, and stay untouched.

## Plan

1. **Trusty storage in the container (needs Asbjørn's sign-off).** `storageproxyd` with RPMB
   and the two trusty partitions, `ro.vendor.trusty.storage.fs_ready`. This writes the RPMB
   and the `trusty_*` partitions, as stock Android does on every boot. `trusty_persist` is
   backed up (2026-10-03). `trusty_userdata` and the RPMB are not, and the RPMB can't be.
2. **KeyMint shared secret and Gatekeeper.** A small NDK helper does the shared-secret
   negotiation (`ISharedSecret`) between the Trusty KeyMint, Gatekeeper and the fingerprint
   HAL, as keystore2 does. The PIN of the Linux lock screen becomes the Gatekeeper
   credential, so enrolling a finger needs the PIN, as on Android.
3. **The fingerprint bridge** follows the pattern of the camera (ADR 0018) and the sensors
   (ADR 0020):
   - `mobarchy-fingerprintd` in the container: an NDK client of `IFingerprint`, which
     handles enrol, authenticate, enumerate and remove, with the HAT from Gatekeeper.
   - On the Linux side, fprintd's D-Bus interface (`net.reactivated.Fprint`). Then
     `pam_fprintd` gives the lock screen, `sudo` and polkit the thumb unchanged.
4. **Shell:** finger enrolment in settings, and unlock with the finger on the lock screen
   (the PIN still works).

## Progress (2026-10-05 evening)

- `trusty_userdata` and `trusty_persist` were backed up (sha256-verified, private). The
  container gets them writable through their own loops (`[android.writable_partitions]`),
  plus `/dev/sg1`, the RPMB W-LUN (0:0:0:49476).
- `storageproxyd` waits for vold's `IVold.registerCheckpointListener`. mobarchy-android-services
  now answers it ("checkpointing complete"). Then `ro.vendor.trusty.storage.fs_ready=1`.
- The first run panicked the phone (`KP: trusty crashed`). Trusty's storage app hit
  `Assert fail: !block_cache_entry_data_is_dirty(entry)` in `block_get_cleared_super`
  on the matcher's first file write (`failed_count`). This was right after its requests
  had timed out for minutes while the proxy waited for vold. After the reboot the file is
  there, and further starts have not crashed. Until this is understood, the storage proxy
  is started by hand on the phone, not at boot.
- KeyMint (Trusty, rust HAL) runs, and `mobarchy-sharedsecret` negotiates its HMAC key.
  Without it the matcher fails (`GetAuthTokenKey ... hmac_key is not available`).
- The HAL starts with `ro.hardware.fingerprint=fpc` and the power key (`/dev/input/event0`,
  not grabbed: Linux still gets it). The HW and SW self tests pass (hwid 1f12, fw
  sw49.0.6), and `IFingerprint/default` is registered.

Then the rest of ADR 0021 was built: Gatekeeper (Trusty) for the enrolment's token, the
bridge (`mobarchy-fingerprintd`), fprintd's interface (`mobarchy-fingerprint`), and the
shell (settings: add and remove fingers; the lock screen unlocks with one). The owner
enrolled a thumb and unlocked with it the same evening.
- The FPC HAL enumerates "none" as one enrolment with id 0. The bridge drops it.
- The FPC HAL needs `/data/vendor_de/0/fpdata`, which Android's vold makes at unlock.
- Gatekeeper's `enroll` takes empty arrays, not null, for "no current credential".
- Enrolling touches the same spot again and again ("too_immobile") adds nothing; the
  shell asks for the finger to be moved.
