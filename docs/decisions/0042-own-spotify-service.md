# 0042: Spotify, a service of Mobarchy's own on librespot

- Status: accepted (2026-10-23; supersedes 0041's engine)
- Date: 2026-10-23

## Context

ADR 0041 runs spotify-player as the Spotify engine. It routes every control through
Spotify's Web API, even for its own librespot device: play/pause took 1.5 to 3 s, and what
plays came back late (the app held its keys down for up to 6 s to hide it). Its CLI's
JSON is its own serialisation, its MPRIS has no track ids, it couldn't start a playlist of
one track, and its sign-in needed an xdg-open of Mobarchy's to reach the app.

librespot 0.8 (the library spotify-player is built on) controls its Spotify Connect
device locally (`Spirc`: play, pause, seek, `load` a context at a track with shuffle,
`activate`, `transfer`), and its player tells what happens as it happens
(`PlayerEvent`).

## Decision

- **mobarchy-spotify** (`os/spotify`, Rust): one binary on librespot (core, connect,
  playback with PulseAudio, metadata). The device "Mobarchy" (a smartphone, 320 kbit/s,
  normalised, at full volume). Controls go to the Spirc; when the phone isn't Spotify's
  active device, it is made the active one first (`activate`; play takes over what plays
  elsewhere with `transfer`). What plays comes from the player's events (the track, play
  and pause with the position and when it was measured, seeks, shuffle, repeat, volume),
  the album's URI from the track's metadata, the context from Spotify's cluster updates.
- **D-Bus**, `org.mobarchy.Spotify` (`/org/mobarchy/Spotify`): the state as properties
  with PropertiesChanged (State, Error, Playing, Track, PositionMs + PositionAt, Shuffle,
  Repeat, ContextUri, Volume, LikedSongsUri, Active), the controls, Load(context, start
  track, shuffle), PlayTrack, and the library through the Web API (Playlists,
  LikedTracks, SavedAlbums, Search, ContextTracks) as JSON of Mobarchy's own shape.
- **MPRIS** (`org.mpris.MediaPlayer2.mobarchy_spotify`) with track ids from the Spotify
  URIs, for the lock screen, Frigg and headset keys.
- **Signing in** is the service's, both approvals (the owner's client ID for the Web API,
  `~/.config/mobarchy/spotify.env`; Spotify's own for streaming, as librespot), one after
  the other on 127.0.0.1:8989, OAuth with PKCE. The pages go to the app as a signal
  (SignInUrl): the service opens no browser. Each answer is checked against its page's
  `state` (an older page's answer is turned away). Kept in `~/.cache/mobarchy/spotify`
  (librespot's credentials.json, webapi-token.json, mode 0600).
- `mobarchy-spotify.service` (user, Type=dbus), started by the app.

## Consequences

- Controls are local: no Web API round trip, and the state is pushed, not polled.
- The Web API is still needed for the library and search (and its rate limit applies to
  them only).
- librespot's Spirc ignores controls while the device isn't active; volume set then is
  ignored too.
- The app (`os/shell/src/spotify.cpp`) moves from spotify-player's CLI and MPRIS to this
  interface; spotify-player, its config, the xdg-open shim and the rootfs unit go.
