// MPRIS (org.mpris.MediaPlayer2.mobarchy_spotify): the same state and controls for the
// shell's lock-screen player, Frigg and a headset's keys. Track ids are object paths of
// the Spotify URI, so SetPosition can tell the track it is meant for.

use crate::dbus::owned;
use crate::service::Service;
use crate::state::{Repeat, State, Status, Track};
use std::collections::HashMap;
use std::sync::Arc;
use zbus::fdo;
use zbus::object_server::{InterfaceRef, SignalEmitter};
use zbus::zvariant::{ObjectPath, OwnedValue};

pub const BUS_NAME: &str = "org.mpris.MediaPlayer2.mobarchy_spotify";
pub const PATH: &str = "/org/mpris/MediaPlayer2";
const NO_TRACK: &str = "/org/mpris/MediaPlayer2/TrackList/NoTrack";

fn failed(e: anyhow::Error) -> fdo::Error {
    fdo::Error::Failed(format!("{e:#}"))
}

/// spotify:track:<id> -> /org/mobarchy/spotify/track/<id>
pub fn track_path(uri: &str) -> String {
    let parts: Vec<String> = uri
        .split(':')
        .skip(1)
        .filter(|p| !p.is_empty())
        .map(|p| p.chars().map(|c| if c.is_ascii_alphanumeric() { c } else { '_' }).collect())
        .collect();
    if parts.is_empty() {
        return NO_TRACK.to_string();
    }
    format!("/org/mobarchy/spotify/{}", parts.join("/"))
}

fn metadata(track: Option<&Track>) -> HashMap<String, OwnedValue> {
    let mut m = HashMap::new();
    let Some(t) = track else {
        m.insert("mpris:trackid".into(), owned(ObjectPath::from_static_str_unchecked(NO_TRACK)));
        return m;
    };
    let path = ObjectPath::try_from(track_path(&t.uri)).unwrap_or(ObjectPath::from_static_str_unchecked(NO_TRACK));
    m.insert("mpris:trackid".into(), owned(path));
    m.insert("mpris:length".into(), owned(t.duration_ms as i64 * 1000));
    if !t.image.is_empty() {
        m.insert("mpris:artUrl".into(), owned(t.image.as_str()));
    }
    m.insert("xesam:title".into(), owned(t.title.as_str()));
    m.insert("xesam:artist".into(), owned(t.artists.clone()));
    m.insert("xesam:album".into(), owned(t.album.as_str()));
    if let Some((kind, id)) = t.uri.strip_prefix("spotify:").and_then(|r| r.split_once(':')) {
        m.insert("xesam:url".into(), owned(format!("https://open.spotify.com/{kind}/{id}")));
    }
    m
}

// The device is up (a Web API sign-in may be missing: playing doesn't need it).
fn connected(s: &State) -> bool {
    matches!(s.status, Status::Ready | Status::SignedOut) && !s.liked_songs_uri.is_empty()
}

fn playback_status(s: &State) -> &'static str {
    if s.playing {
        "Playing"
    } else if s.track.is_some() {
        "Paused"
    } else {
        "Stopped"
    }
}

fn loop_status(r: Repeat) -> &'static str {
    match r {
        Repeat::Off => "None",
        Repeat::Context => "Playlist",
        Repeat::Track => "Track",
    }
}

pub struct Root;

#[zbus::interface(name = "org.mpris.MediaPlayer2")]
impl Root {
    fn raise(&self) {}

    fn quit(&self) {}

    #[zbus(property)]
    fn can_quit(&self) -> bool {
        false
    }

    #[zbus(property)]
    fn can_raise(&self) -> bool {
        false
    }

    #[zbus(property)]
    fn has_track_list(&self) -> bool {
        false
    }

    #[zbus(property)]
    fn identity(&self) -> String {
        "Spotify".into()
    }

    #[zbus(property)]
    fn desktop_entry(&self) -> String {
        "org.mobarchy.Spotify".into()
    }

    #[zbus(property)]
    fn supported_uri_schemes(&self) -> Vec<String> {
        vec!["spotify".into()]
    }

    #[zbus(property)]
    fn supported_mime_types(&self) -> Vec<String> {
        vec![]
    }
}

pub struct Player {
    pub svc: Arc<Service>,
}

impl Player {
    fn state(&self) -> State {
        self.svc.state.borrow().clone()
    }

    fn connected(&self) -> bool {
        connected(&self.state())
    }
}

#[zbus::interface(name = "org.mpris.MediaPlayer2.Player")]
impl Player {
    fn next(&self) -> fdo::Result<()> {
        self.svc.next().map_err(failed)
    }

    fn previous(&self) -> fdo::Result<()> {
        self.svc.previous().map_err(failed)
    }

    fn pause(&self) -> fdo::Result<()> {
        self.svc.pause().map_err(failed)
    }

    fn play_pause(&self) -> fdo::Result<()> {
        self.svc.play_pause().map_err(failed)
    }

    fn stop(&self) -> fdo::Result<()> {
        self.svc.pause().map_err(failed)
    }

    fn play(&self) -> fdo::Result<()> {
        self.svc.play().map_err(failed)
    }

    /// By an offset (µs).
    fn seek(&self, offset: i64) -> fdo::Result<()> {
        let s = self.state();
        if s.track.is_none() {
            return Ok(());
        }
        let to = s.position_now() as i64 + offset / 1000;
        let duration = s.track.as_ref().map(|t| t.duration_ms as i64).unwrap_or(0);
        if duration > 0 && to >= duration {
            // (past the end: the next track, as MPRIS says)
            return self.svc.next().map_err(failed);
        }
        self.svc.seek(to.max(0)).map_err(failed)
    }

    /// To a position (µs) in the given track (ignored for another one).
    fn set_position(&self, track_id: ObjectPath<'_>, position: i64) -> fdo::Result<()> {
        let s = self.state();
        let Some(t) = s.track.as_ref() else { return Ok(()) };
        if track_id.as_str() != track_path(&t.uri) || position < 0 || position / 1000 > t.duration_ms as i64 {
            return Ok(());
        }
        self.svc.seek(position / 1000).map_err(failed)
    }

    fn open_uri(&self, uri: &str) -> fdo::Result<()> {
        if uri.starts_with("spotify:track:") || uri.starts_with("spotify:episode:") {
            self.svc.play_track(uri).map_err(failed)
        } else {
            self.svc.load(uri, "", false).map_err(failed)
        }
    }

    #[zbus(property)]
    fn playback_status(&self) -> String {
        playback_status(&self.state()).into()
    }

    #[zbus(property)]
    fn loop_status(&self) -> String {
        loop_status(self.state().repeat).into()
    }

    #[zbus(property)]
    fn set_loop_status(&self, value: String) -> zbus::Result<()> {
        let r = Repeat::parse(&value).ok_or_else(|| fdo::Error::InvalidArgs(value.clone()))?;
        self.svc.set_repeat(r).map_err(|e| fdo::Error::Failed(format!("{e:#}")))?;
        Ok(())
    }

    #[zbus(property)]
    fn rate(&self) -> f64 {
        1.0
    }

    #[zbus(property)]
    fn set_rate(&self, _rate: f64) {}

    #[zbus(property)]
    fn shuffle(&self) -> bool {
        self.state().shuffle
    }

    #[zbus(property)]
    fn set_shuffle(&self, value: bool) -> zbus::Result<()> {
        self.svc.set_shuffle(value).map_err(|e| fdo::Error::Failed(format!("{e:#}")))?;
        Ok(())
    }

    #[zbus(property)]
    fn metadata(&self) -> HashMap<String, OwnedValue> {
        metadata(self.state().track.as_ref())
    }

    #[zbus(property)]
    fn volume(&self) -> f64 {
        self.state().volume
    }

    #[zbus(property)]
    fn set_volume(&self, value: f64) -> zbus::Result<()> {
        self.svc.set_volume(value).map_err(|e| fdo::Error::Failed(format!("{e:#}")))?;
        Ok(())
    }

    /// µs (not sent with PropertiesChanged: Seeked says when it jumps).
    #[zbus(property(emits_changed_signal = "false"))]
    fn position(&self) -> i64 {
        self.state().position_now() as i64 * 1000
    }

    #[zbus(property)]
    fn minimum_rate(&self) -> f64 {
        1.0
    }

    #[zbus(property)]
    fn maximum_rate(&self) -> f64 {
        1.0
    }

    #[zbus(property)]
    fn can_go_next(&self) -> bool {
        self.connected()
    }

    #[zbus(property)]
    fn can_go_previous(&self) -> bool {
        self.connected()
    }

    #[zbus(property)]
    fn can_play(&self) -> bool {
        self.connected()
    }

    #[zbus(property)]
    fn can_pause(&self) -> bool {
        self.connected()
    }

    #[zbus(property)]
    fn can_seek(&self) -> bool {
        self.connected() && self.state().track.is_some()
    }

    #[zbus(property(emits_changed_signal = "const"))]
    fn can_control(&self) -> bool {
        true
    }

    #[zbus(signal)]
    async fn seeked(emitter: &SignalEmitter<'_>, position: i64) -> zbus::Result<()>;
}

pub async fn emit_changes(player: &InterfaceRef<Player>, prev: &State, cur: &State) -> zbus::Result<()> {
    let (p, e) = (player.get().await, player.signal_emitter());
    if playback_status(prev) != playback_status(cur) {
        p.playback_status_changed(e).await?;
    }
    if prev.track != cur.track {
        p.metadata_changed(e).await?;
    }
    if prev.repeat != cur.repeat {
        p.loop_status_changed(e).await?;
    }
    if prev.shuffle != cur.shuffle {
        p.shuffle_changed(e).await?;
    }
    if prev.volume != cur.volume {
        p.volume_changed(e).await?;
    }
    let can = |s: &State| (connected(s), s.track.is_some());
    if can(prev) != can(cur) {
        p.can_go_next_changed(e).await?;
        p.can_go_previous_changed(e).await?;
        p.can_play_changed(e).await?;
        p.can_pause_changed(e).await?;
        p.can_seek_changed(e).await?;
    }
    if prev.seeks != cur.seeks {
        Player::seeked(e, cur.position_now() as i64 * 1000).await?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn track_paths() {
        assert_eq!(track_path("spotify:track:4uLU6hMCjMI75M1A2tKUQC"), "/org/mobarchy/spotify/track/4uLU6hMCjMI75M1A2tKUQC");
        assert_eq!(track_path("spotify:local:a:b:c%20d:12"), "/org/mobarchy/spotify/local/a/b/c_20d/12");
        assert_eq!(track_path(""), NO_TRACK);
        assert!(ObjectPath::try_from(track_path("spotify:episode:xyz")).is_ok());
    }

    #[test]
    fn metadata_of_a_track() {
        let t = Track {
            uri: "spotify:track:abc".into(),
            title: "T".into(),
            artists: vec!["A".into()],
            duration_ms: 2000,
            image: "http://i".into(),
            ..Default::default()
        };
        let m = metadata(Some(&t));
        assert_eq!(i64::try_from(m["mpris:length"].clone()).unwrap(), 2_000_000);
        assert_eq!(String::try_from(m["xesam:url"].clone()).unwrap(), "https://open.spotify.com/track/abc");
        assert!(metadata(None).contains_key("mpris:trackid"));
    }
}
