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
  minutes of a cold start. A systemd service (DynamicUser).
- **GeoClue** (Arch's, in our repository with `libmm-glib`): its NMEA source reads that socket
  (`nmea-socket`). Its network sources (IP, Wi-Fi, 3G, CDMA, ModemManager's GPS) are off:
  nothing is sent anywhere to find the phone.
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
- Assistance over the network (SUPL, Google's predicted orbits) is not set up: without a SIM
  the GNSS works alone; with the time and the last fix it starts warm.
- Frigg's location tool goes through GeoClue too (as `org.mobarchy.Agent`).
