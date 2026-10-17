# 0032: Internet accounts — contacts from iCloud and Google over CardDAV

- Status: accepted (contacts; read-only)
- Date: 2026-10-17

## Context

The owner's contacts live in their accounts: one iCloud account and three Google accounts.
Adding an account must be easy for anyone: signing in, not setting things up.
The phone keeps contacts as vCard files (ADR 0031) that the Phone and Messages apps and the
agent read. They should not have to be exported and copied by hand, and they should stay
current.

## Decision

**Our own small, read-only CardDAV client, `mobarchy-accounts` (in the agent's Python
package), fetches each account's address books into a vCard file of its own.**

- **CardDAV** (RFC 6352) for every provider: iCloud, Google, Nextcloud and any other server
  (Fastmail, Radicale…). Discovery: `current-user-principal` → `addressbook-home-set` →
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
  Nextcloud's app password, other servers' passwords, Google's refresh tokens and, if the owner
  uses their own Google client, its secret. Passwords
  reach the command on stdin, never in an argument or a file.
- **Sign-in:**
  - iCloud: Apple has no OAuth for other apps' access to iCloud contacts, so the Apple ID and
    an **app-specific password**, checked against the server before it is kept. The app makes
    it as short as it can: a button opens account.apple.com, and the password is taken as
    copied (`xxxx-xxxx-xxxx-xxxx`, spaces or not).
  - Google: **OAuth** in the phone's browser (installed-app flow, a loopback redirect on
    127.0.0.1, PKCE, offline access), scopes `carddav openid email`, with **Mobarchy's own
    OAuth client**: the project registers one "Desktop app" client in Google Cloud (CardDAV
    API on, the consent screen published so refresh tokens don't expire after 7 days as in
    "testing"), and every phone uses it, so the owner only signs in. Its id and secret are
    in the agent package (`/usr/share/mobarchy/accounts/google-client.json`), put there by the
    build from the builder's `agent/private/google-client.json` (ignored by git): a Desktop
    client's secret is in every copy of the app and so is no secret in Google's sense, but the
    source stays free of it, and forks register their own client (using someone else's is
    against Google's terms). Until Google has verified the app (contacts are a sensitive
    scope) sign-in shows an "unverified app" warning and is limited to 100 users; verification
    is for when Mobarchy has users. A build without the file, or an owner who prefers it,
    uses the owner's own client (`mobarchy-accounts google-client`; id in `accounts.json`,
    secret in the keyring), which then wins.
  - Nextcloud: **Login Flow v2** in the browser. The server gives a login page and a poll
    token; the owner signs in and allows the phone, and the poll answers with an app password
    of the phone's own, revocable in Nextcloud. CardDAV at `<server>/remote.php/dav`.
- **Sync:** `mobarchy-accounts-sync.timer` (2 min after login, then every 30 min) and the
  Accounts app's "Sync now". An account that fails keeps its last contacts and shows why.
- **The Accounts app** (`mobarchy-shell --accounts`): the accounts, their address books with
  a switch each, adding Google / iCloud / Nextcloud / CardDAV, removing.

## Consequences

- The keyring must be open for a sync. It opens with the phone's PIN (pam_gnome_keyring,
  ADR 0013).
- Calendars can come the same way (CalDAV), and two-way contacts later, if wanted.
- Contact photos are not kept yet.
