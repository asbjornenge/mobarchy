# 0032: Internet accounts — contacts from iCloud and Google over CardDAV

- Status: accepted (contacts; read-only)
- Date: 2026-10-17

## Context

The owner's contacts live in their accounts: one iCloud account and three Google accounts.
The phone keeps contacts as vCard files (ADR 0031) that the Phone and Messages apps and the
agent read. They should not have to be exported and copied by hand, and they should stay
current.

## Decision

**Our own small, read-only CardDAV client, `mobarchy-accounts` (in the agent's Python
package), fetches each account's address books into a vCard file of its own.**

- **CardDAV** (RFC 6352) for every provider: iCloud, Google, and any other server
  (Nextcloud, Fastmail…). Discovery: `current-user-principal` → `addressbook-home-set` →
  the address books; then each book's etags, and `addressbook-multiget` for the cards that
  changed. Redirects are followed for every method (iCloud's root and Google's
  `.well-known/carddav` redirect).
- **Not vdirsyncer.** It would do, but it brings a tree of Python packages from a mirror
  that is no longer in step with the phone (Qt 6.12); a read-only
  client is a few hundred lines of the standard library, and the vCard handling is ours anyway
  (contacts.py).
- **Read-only.** Nothing is ever written to an account. Contacts made on the phone, and the
  owner's words for people ("far"), are kept on the phone: `contacts.vcf` and
  `relations.json`. A person in several places is one contact, with the numbers of all.
- **Files:** `~/.config/mobarchy/accounts.json` (the accounts, which books are on),
  `~/.local/share/mobarchy/contacts/<id>.vcf` (each account's enabled books, photos left out:
  nothing shows them yet, and they are most of the bytes), `~/.cache/mobarchy/carddav/<id>.json`
  (etags and cards).
- **Secrets** in the keyring (Secret Service, `secret-tool`): iCloud's app-specific password,
  other servers' passwords, Google's refresh tokens and the Google client's secret. Passwords
  reach the command on stdin, never in an argument or a file.
- **Sign-in:**
  - iCloud: the Apple ID and an **app-specific password** (appleid.apple.com), checked
    against the server before it is kept.
  - Google: **OAuth** in the phone's browser (installed-app flow, a loopback redirect on
    127.0.0.1, PKCE, offline access), scopes `carddav openid email`. Google needs a client:
    the owner's own, made once in Google Cloud (a "Desktop app" OAuth client, the consent
    screen published so refresh tokens don't expire after 7 days as in "testing"). Using
    someone else's client id is against Google's terms; the id is in `accounts.json`, its
    secret in the keyring.
- **Sync:** `mobarchy-accounts-sync.timer` (2 min after login, then every 30 min) and the
  Accounts app's "Sync now". An account that fails keeps its last contacts and shows why.
- **The Accounts app** (`mobarchy-shell --accounts`): the accounts, their address books with
  a switch each, adding iCloud / Google / CardDAV, removing.

## Consequences

- The keyring must be open for a sync. It opens with the phone's PIN (pam_gnome_keyring,
  ADR 0013).
- Calendars can come the same way (CalDAV), and two-way contacts later, if wanted.
- Contact photos are not kept yet.
