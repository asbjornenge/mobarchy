# 0033: Location — the phone's own GNSS, through GeoClue

- Status: accepted (the chain works end to end; the first fix outdoors still to be seen)
- Date: 2026-10-17

## Context

The weather on the home board, Frigg ("where am I?", "the weather here"), maps and the
browser want to know where the phone is. Linux's standard for that is GeoClue2: apps ask it
over D-Bus, an agent decides which app may know, and GeoClue gets the position from its
sources. Its usual sources ask a server (IP address, Wi-Fi networks seen), which tells the
server where the owner is. The phone has its own GNSS (GPS, Galileo, BeiDou, GLONASS, NavIC):
Samsung's, in the Tensor next to the modem, run by the vendor's `gnssd` and served by its
GNSS HAL (AIDL `android.hardware.gnss.IGnss/vendor`, version 3) in the Android container we
already run for the modem (ADR 0014).

## Decision

**The phone's GNSS, through the Android container's HAL, served to GeoClue; GeoClue is what
everything on the Linux side asks.**

- **In the container** (`devices/comet`): `gnssd` and the GNSS HAL, as the vendor's
  `init.gnss.rc` has them (with seclabels), started with the rest; the GNSS device nodes
  (`/dev/gnss_boot`, `gnss_ipc`, `gnss_dump`) for them. `gnssd` loads the GNSS firmware
  (`/vendor/firmware/kepler.bin`) and takes assistance from the modem (SIT RIL).
- **`mobarchy-location`** (`os/location`, C with libgbinder, as oFono talks to the modem's
  HAL): it registers a callback with the HAL (a VINTF-stability binder object, answering
  the AIDL version and hash), and serves the fixes as NMEA (GGA, RMC) on
  `/run/mobarchy-location/nmea.sock`. The GNSS runs only while someone listens: the first
  client starts it, the last one gone stops it. Before starting it gives the GNSS the time
  and the last fix (kept in its state directory), so later fixes come in seconds, not the
  minutes of a cold start. A systemd service, as a user of its own (`mobarchy-location`).
- **Predicted orbits (PSDS):** a GNSS decodes each satellite's orbit from its signal, which
  takes minutes of open sky; Android gives it days of predicted orbits instead, from the
  phone's PSDS server (Samsung's, `https://1.ssiloc.com/p4/42F3`, in Android's carrier
  settings for this phone; the file is encrypted for the GNSS). `mobarchy-location-psds`
  fetches it daily over the network (a systemd timer; the URL from `device.toml`
  `[location] psds_url`), and `mobarchy-location`, which has no network itself, gives it to
  the GNSS (`IGnssPsds.injectPsdsData`, long-term) before it starts when it is new to it.
  The server is told nothing but that a phone wants the file.
- **GeoClue** (Arch's, in our repository with `libmm-glib`): its NMEA source reads that socket
  (`nmea-socket`). Indoors the GNSS hears no satellites; there GeoClue's Wi-Fi source finds
  the phone from the networks around (wpa_supplicant on D-Bus, `-u`), looked up in
  **BeaconDB** (open, non-profit, the successor to Mozilla's location service), as phones
  do: the owner chose it (2026-10-18). It is told the networks around, not who asks; nothing
  is submitted to it. The IP, 3G, CDMA and ModemManager sources are off.
- **The shell is GeoClue's agent** (`LocationAgent`): GeoClue answers no app while there is
  no agent. Allowed for now: the shell and the agent layer (`org.mobarchy.Agent`), and apps
  the owner allowed in `~/.config/mobarchy/location.json`. Asking the owner ("Allow X to
  know where you are?") comes with the first app that wants it.
- **The weather card** uses where the phone is, by default, asking for one fix every half
  hour (GeoClue client started, first location, stopped: the GNSS is not left on). It asks
  for an exact fix: GeoClue asks an NMEA source for nothing less, and the card rounds it. The
  place's name comes from OpenStreetMap's Nominatim (reverse), the weather from Open-Meteo;
  both are told the place rounded to two decimals (about a kilometre). A chosen city still
  overrides it.

## Consequences

- A cold first fix needs open sky (minutes); indoors there may be none.
- SUPL (assistance through the operator) is not set up; the predicted orbits, the time and
  the last fix do most of what it would.
- Frigg's location tool goes through GeoClue too (as `org.mobarchy.Agent`).
