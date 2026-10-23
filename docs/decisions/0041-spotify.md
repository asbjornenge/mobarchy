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
- **Signing in** is the daemon's: without credentials it opens Spotify's pages in the
  browser (its Web API client's, then the streaming's), takes each answer on its own
  callback (127.0.0.1:8989), and keeps the credentials once connected. The app starts it
  and waits for them; then it closes the pages and comes back to the front.
  (`spotify_player authenticate` is no use here: it gets the streaming's token and drops it
  without keeping credentials. Two other ways failed first: the app opening the pages too
  gave two of each, and one approved twice sent the first's code to the second's
  callback; a callback of the app's own took the port the daemon needs.)

## Consequences

- Spotify Premium is needed (librespot plays only for Premium accounts).
- spotify-player's default Web API client is shared (ncspot's) and was at Spotify's rate
  limit at once (and the app's first version, asking for the playback every second through
  the Web API, made it worse). The owner's own Spotify app (developer.spotify.com, redirect
  `http://127.0.0.1:8989/login`) gives a client ID of its own: `SPOTIFY_CLIENT_ID` in
  `~/.config/mobarchy/spotify.env`, outside the image (the service passes it). What plays
  is read from the daemon's MPRIS (local), and the transport keys go through MPRIS too; the
  Web API is for the library, search and starting something.
- Starting something goes to the active device Spotify knows: the app moves playback to
  "Mobarchy" first (`connect`). A sign-out (the shelf) stops the daemon and forgets its
  credentials and tokens.
- The CLI's JSON is spotify-player's own serialisation of its state; the app reads it
  leniently (ids with or without `spotify:` URIs, durations in ms or as `{secs, nanos}`).
