# 0013: The lock screen PIN is the owner's password

- Status: accepted
- Date: 2026-10-03

## Context

The lock screen only kept pockets from using the phone: a swipe unlocked it. Saved logins
(the keyring, needed by Chromium, Fractal, Authenticator, ...) were stored unencrypted for
lack of a password to encrypt them with (the phone logs in by itself). The agent can drive
the screen through mobarchy-mcp, so it could also have unlocked the phone.

## Decision

- **The PIN is the account password.** Setting it sets the owner's password (crypt(3),
  yescrypt) through AccountsService; removing it locks the password again (`!`). Checking
  it is PAM (`/etc/pam.d/mobarchy-lock`: pam_unix through its helper), in the shell.
  No separate PIN store, and every Linux tool that asks for the password takes the PIN.
- **The keyring follows the PIN.** Setting, changing or removing the PIN re-encrypts the
  default keyring ("login") with it; unlocking the phone unlocks the keyring
  (pam_gnome_keyring in the same PAM stack). Without a PIN it stays unencrypted (0012's
  keyring-setup), as before.
- **Wrong PINs cost time.** After five in a row, each attempt waits: 30 s, doubling, at
  most an hour; kept on disk, so restarting the shell doesn't reset it (pam_unix adds its
  own ~2 s per failure).
- **The agent can't unlock.** While the lock screen is up and a PIN is set, the shell
  publishes `$XDG_RUNTIME_DIR/mobarchy/locked`, and mobarchy-mcp's screen tools
  (screenshot, tap, swipe, type, keys, apps, windows) refuse.
- Setting, changing (current PIN first) and removing are in quick settings ("Screen lock").
  A polkit rule lets the owner (wheel) do it without a second prompt.

## Consequences

- Within the session there is no privilege separation yet (the owner has passwordless
  sudo, apps run as the owner): the PIN protects against someone holding the phone, not
  against malicious software already on it.
- The keyring stays unlocked after the first unlock until reboot (as Android's credential
  storage); locking the screen doesn't lock it.
- Fingerprint (the in-display sensor) can come later as another PAM module.
