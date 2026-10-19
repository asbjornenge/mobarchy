# 0036: Mobile data through oFono, set up by our own small service

- Status: accepted (works on comet: Talkmore on Telenor's LTE, 2026-10-19)
- Date: 2026-10-19

## Context

Telephony (ADR 0031) runs through oFono (Sailfish's, with the binder plugin talking to the
Android container's radio HAL). Calls and SMS work; the phone's only internet was Wi-Fi.
The owner now uses the phone every day, away from Wi-Fi too.

oFono sets up a data call (`ConnectionManager` attached, an internet context `Active`) and
reports how the network is to be configured (the context's `Settings` and `IPv6.Settings`:
the interface, the address, the gateway, the name servers). It does not configure the
interface itself; on Sailfish, ConnMan does. Mobarchy uses systemd-networkd and
systemd-resolved (Wi-Fi with wpa_supplicant), which know nothing of oFono. The container
shares the host's network namespace, so the modem's data interfaces are the host's.

## Decision

**`mobarchy-mobile-data`, a system service (`/usr/lib/mobarchy/mobile-data`, Python and
GIO), does what ConnMan would.**

- It follows oFono's modems: for the one with a SIM it powers the `ConnectionManager` and,
  while attached, turns the internet context on (again after 15 s if the network says no).
  The APN comes from oFono's provisioning (mobile-broadband-provider-info). oFono leaves it
  empty when several providers share the SIM's network code and none has its name (a
  virtual operator: Talkmore, on Telenor's 242 01); the service then takes the network's
  own operator's (listed first; `telenor.smart`), which virtual operators mostly use.
- When the context is up, its interface gets the address (`ip`), a default route with
  metric 1000 (Wi-Fi's is 600, so Wi-Fi wins when there is one), and its name servers in
  systemd-resolved (`resolvectl`). When the context goes down (or the service stops),
  all of it is taken away.
- Data goes with the mobile radio: the shell's Mobile switch and flight mode take the
  modem off line, and the data with it. Roaming stays as oFono has it (off).
- Nothing device-specific: any device whose modem oFono drives gets it.

## Not done (yet)

- A switch for data alone (the radio on for calls, data off), and data roaming.
- Data usage, and the status bar's mobile-data indicator (4G/5G next to the signal).
- Which SIM carries data on a phone with two.

## Tested (comet, 2026-10-19)

Talkmore SIM on Telenor LTE: the context came up on `rmnet1` (a /24 with no gateway, the
link itself; two name servers), and HTTPS over it worked (a Telenor address, 0.7 s to
google.com) while Wi-Fi kept the default route.
