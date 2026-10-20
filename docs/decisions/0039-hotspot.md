# 0039: Sharing the internet over Wi-Fi (hotspot)

- Status: accepted
- Date: 2026-10-20

## Context

The owner wants to share the phone's internet with a laptop or another phone. comet's Wi-Fi
chip (bcmdhd) offers an access point beside the client: `iw list` gives "#{ AP } <= 2,
#{ managed } <= 4, #channels <= 2", and it already has a second interface, wlan1.

## Decision

**hostapd on a second Wi-Fi interface, networkd's DHCP server, iptables NAT; a root helper
the shell calls (`/usr/lib/mobarchy/hotspot`), and a Hotspot tile and page in quick
settings.**

- **The interface:** the device's `[wifi] ap_interface` (comet: wlan1), else one added
  beside wlan0 (`ap0`). On wlan0's channel while it is connected (one radio): the phone stays
  on its Wi-Fi and shares at once.
- **Addresses:** a `.network` written to /run while on: 192.168.44.1/24 and networkd's DHCP
  server, which hands out the uplink's name servers.
- **Out:** iptables(-legacy) MASQUERADE and forwarding for 192.168.44.0/24, through whatever the
  phone is on (mobile data, or Wi-Fi). iptables, not nftables: the kernel has no nftables
  NAT (the same as Waydroid, ADR 0029).
- **The name and password:** made up the first time (Mobarchy-XXXX, three groups of four
  hex digits), kept in /var/lib/mobarchy/hotspot.json (root's, 0600); "New password" makes
  a new one (and drops those who had the old). WPA2-PSK.
- **The shell:** `Hotspot` runs the helper through sudo (sudoers.d/zz-mobarchy-hotspot, no
  PIN); quick settings has a Hotspot tile (off, on, how many have joined) and a page with
  the switch, the name and password, a QR code to join (qrencode) and who has joined.

## Not done (yet)

- (Renaming: a tap on the name types a new one with the shell's keyboard, captured as the drawer's search.)
- USB tethering (the USB gadget is there), Bluetooth (PAN).
- 5 GHz on its own when the phone's Wi-Fi is not connected.
