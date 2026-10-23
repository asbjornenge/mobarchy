# 0041: Spotify, as a cassette deck

- Status: accepted
- Date: 2026-10-23

## Context

The owner wants Spotify on Mobarchy, as an app of its own in Mobarchy's style, laid out
like a cassette player. Spotify's own Linux client is x86-only and a desktop app; its web
player works in the browser but is neither native nor a background player. Spotify Connect
and playback on the phone need Spotify Premium.

Arch Linux ARM packages **spotify-player** (`spotify_player`, 0.25): a Spotify client in
Rust on librespot, with a daemon mode (`-d`), streaming (a Spotify Connect device), MPRIS,
and a CLI that drives the running instance and answers in JSON (`get key playback`,
`get key user-playlists`, `search`, `playback start context ...`, ...). It signs in with
OAuth in the browser (twice: its Web API client, and librespot's streaming credentials) and
keeps the tokens in its cache.

## Decision

- **spotify-player is the engine.** `mobarchy-spotify.service` (user) runs it as a daemon
  with Mobarchy's config (`/usr/share/mobarchy/spotify-player/app.toml`: the device
  "Mobarchy", a smartphone, 320 kbit/s, normalised, no notifications of its own). It is
  started by the app once signed in, and stays up: Spotify plays on with the app closed,
  the phone is a device in Spotify's other apps, and MPRIS gives the lock screen's player,
  Frigg and a headset's buttons the controls.
- **The app** (`mobarchy-shell --spotify`, `SpotifyApp.qml`, `Spotify` in C++) drives it
  through the CLI. It follows what plays once a second while open (moving the position on
  in between), and shows it as a cassette (`Cassette.qml`): the album's cover on the
  label, the reels turning while it plays, the tape going over from the left reel to the
  right one as the track goes on. A deck's piano keys play it (eject, previous, rewind,
  play, pause, forward, next; PLAY stays down while it plays), with a counter and a tape to
  drag along. Eject opens the shelf: the playlists, the liked songs, the saved albums and a
  search, each a tape's spine with its label; a tape picked goes into the deck.
- **Signing in** is spotify-player's (`authenticate`): the app runs it, and the browser
  opens Spotify's page (the URL it prints, once each).

## Consequences

- Spotify Premium is needed (librespot plays only for Premium accounts).
- spotify-player's default Web API client is shared (ncspot's): heavy use may meet its rate
  limit. A client ID of the owner's own (Spotify's developer dashboard) goes in the config
  if that happens.
- The CLI's JSON is spotify-player's own serialisation of its state; the app reads it
  leniently (ids with or without `spotify:` URIs, durations in ms or as `{secs, nanos}`).
