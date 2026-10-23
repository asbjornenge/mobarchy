// What the service knows: signed in or not, and what plays. One value behind a watch
// channel: the player's events write it, D-Bus and MPRIS follow it and send what changed.

use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    SignedOut,
    SigningIn,
    Connecting,
    Ready,
    Error,
}

impl Status {
    pub fn as_str(self) -> &'static str {
        match self {
            Status::SignedOut => "signedOut",
            Status::SigningIn => "signingIn",
            Status::Connecting => "connecting",
            Status::Ready => "ready",
            Status::Error => "error",
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Repeat {
    #[default]
    Off,
    Context,
    Track,
}

impl Repeat {
    pub fn as_str(self) -> &'static str {
        match self {
            Repeat::Off => "off",
            Repeat::Context => "context",
            Repeat::Track => "track",
        }
    }

    pub fn parse(s: &str) -> Option<Repeat> {
        match s {
            "off" | "none" | "None" => Some(Repeat::Off),
            "context" | "playlist" | "Playlist" => Some(Repeat::Context),
            "track" | "Track" => Some(Repeat::Track),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Track {
    pub uri: String,
    pub id: String,
    pub title: String,
    pub artists: Vec<String>,
    pub album: String,
    pub album_uri: String,
    pub image: String,
    pub duration_ms: u32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct State {
    pub status: Status,
    pub error: String,
    pub playing: bool,
    pub track: Option<Track>,
    // where the track was (ms) at position_at (epoch ms): clients go on from there while it plays
    pub position_ms: u32,
    pub position_at: i64,
    pub shuffle: bool,
    pub repeat: Repeat,
    pub context_uri: String,
    pub volume: f64,
    // the liked songs as a context (spotify:user:<name>:collection), once connected
    pub liked_songs_uri: String,
    // this device is Spotify's active one (control is local)
    pub active: bool,
    // counts jumps of the position (seeks, corrections): MPRIS's Seeked
    pub seeks: u64,
}

impl Default for State {
    fn default() -> Self {
        State {
            status: Status::SignedOut,
            error: String::new(),
            playing: false,
            track: None,
            position_ms: 0,
            position_at: now_ms(),
            shuffle: false,
            repeat: Repeat::Off,
            context_uri: String::new(),
            volume: 1.0,
            liked_songs_uri: String::new(),
            active: false,
            seeks: 0,
        }
    }
}

impl State {
    // The position now: on from position_ms while it plays, no further than the track's end.
    pub fn position_now(&self) -> u32 {
        let mut pos = self.position_ms as i64;
        if self.playing {
            pos += (now_ms() - self.position_at).max(0);
        }
        if let Some(t) = &self.track
            && t.duration_ms > 0
        {
            pos = pos.min(t.duration_ms as i64);
        }
        pos.clamp(0, u32::MAX as i64) as u32
    }

    pub fn set_position(&mut self, ms: u32) {
        self.position_ms = ms;
        self.position_at = now_ms();
    }

    // Back to nothing playing (signed out, or the session gone).
    pub fn clear_playback(&mut self) {
        self.playing = false;
        self.track = None;
        self.set_position(0);
        self.context_uri.clear();
        self.active = false;
    }
}

pub fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn position_extrapolates_only_while_playing() {
        let mut s = State {
            track: Some(Track { duration_ms: 10_000, ..Default::default() }),
            ..Default::default()
        };
        s.position_ms = 1000;
        s.position_at = now_ms() - 2000;
        assert_eq!(s.position_now(), 1000);
        s.playing = true;
        let p = s.position_now();
        assert!((2990..3100).contains(&p), "{p}");
        s.position_at = now_ms() - 60_000;
        assert_eq!(s.position_now(), 10_000);
    }

    #[test]
    fn repeat_names() {
        assert_eq!(Repeat::parse("context"), Some(Repeat::Context));
        assert_eq!(Repeat::parse("Track"), Some(Repeat::Track));
        assert_eq!(Repeat::parse("None"), Some(Repeat::Off));
        assert_eq!(Repeat::parse("x"), None);
        assert_eq!(Repeat::Context.as_str(), "context");
    }
}
