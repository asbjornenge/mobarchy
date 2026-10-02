# 0011: DRM video and music in the browser (Widevine), fetched on the phone

- Status: accepted
- Date: 2026-10-02

## Context

The owner wants Spotify, Netflix and other streaming services. Spotify's official Linux
client is x86-only; the touch-friendly third-party clients are unmaintained (Spot: no code
changes since 2024-11), and the terminal client (spotify-player) is no phone app. The
services' own web players run in Firefox and Chromium, but need a Content Decryption
Module: Widevine. Google ships no Widevine for aarch64 Linux, only for ChromeOS.

Asahi Linux's `widevine-installer` (MIT) downloads the ChromeOS ARM64 browser image from
Google's mirror, extracts `libwidevinecdm.so`, adapts its binary format for regular glibc
(the CDM itself is not modified) and sets up Firefox and Chromium. On comet, Spotify's
mobile web player plays with it (tested 2026-10-02).

## Decision

- The installer is a Mobarchy package (`os/packages/widevine-installer`, pinned commit),
  with Arch's paths: Firefox prefs, `MOZ_GMP_PATH` through environment.d, Chromium's
  `WidevineCdm` link.
- The CDM itself is proprietary: it is never in the repo or the image. 
  `mobarchy-widevine.service` downloads and installs it on the phone, once, when the
  network is up (by running the installer, which accepts Widevine's license on the
  owner's behalf).
- Spotify is the web player (`open.spotify.com`, mobile layout) in Firefox. The agent
  keeps spotify-player as its way to search and start music (ADR 0010's tools); the
  shell's "now playing" card and the media keys work with either (MPRIS).

## Consequences

- Netflix and the like work in the browser at the resolution and security level the
  ChromeOS CDM gets on Linux (L3).
- A distributed Mobarchy downloads Google's proprietary CDM on first boot; that must be
  stated (and could become opt-in) before distribution.
- The CDM version is the installer's pin (4.10.2662.3); services may require newer ones.
  Updating means bumping the package.
