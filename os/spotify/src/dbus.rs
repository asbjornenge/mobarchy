// org.mobarchy.Spotify on the session bus: the state as properties (PropertiesChanged as
// it changes), the controls, signing in, and the library through the Web API (JSON).

use crate::mpris;
use crate::service::{Service, Signal};
use crate::state::{Repeat, State, Track};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::mpsc;
use zbus::fdo;
use zbus::object_server::SignalEmitter;
use zbus::zvariant::{OwnedValue, Value};

pub const BUS_NAME: &str = "org.mobarchy.Spotify";
pub const PATH: &str = "/org/mobarchy/Spotify";

pub struct SpotifyIface {
    svc: Arc<Service>,
}

fn failed(e: anyhow::Error) -> fdo::Error {
    fdo::Error::Failed(format!("{e:#}"))
}

pub fn owned<'a>(v: impl Into<Value<'a>>) -> OwnedValue {
    v.into().try_to_owned().expect("a value without file descriptors")
}

// The track as a{sv} (empty when nothing plays).
pub fn track_map(track: Option<&Track>) -> HashMap<String, OwnedValue> {
    let mut m = HashMap::new();
    if let Some(t) = track {
        m.insert("kind".into(), owned(t.kind.as_str()));
        m.insert("uri".into(), owned(t.uri.as_str()));
        m.insert("id".into(), owned(t.id.as_str()));
        m.insert("title".into(), owned(t.title.as_str()));
        m.insert("artists".into(), owned(t.artists.clone()));
        m.insert("album".into(), owned(t.album.as_str()));
        m.insert("albumUri".into(), owned(t.album_uri.as_str()));
        m.insert("image".into(), owned(t.image.as_str()));
        m.insert("durationMs".into(), owned(t.duration_ms as i64));
    }
    m
}

impl SpotifyIface {
    fn state(&self) -> State {
        self.svc.state.borrow().clone()
    }
}

#[zbus::interface(name = "org.mobarchy.Spotify")]
impl SpotifyIface {
    /// Asks for Spotify's approvals that are missing (SignInUrl for each), then connects
    /// (SignedIn).
    async fn sign_in(&self) {
        self.svc.sign_in().await
    }

    async fn sign_out(&self) {
        self.svc.sign_out().await
    }

    fn play(&self) -> fdo::Result<()> {
        self.svc.play().map_err(failed)
    }

    fn pause(&self) -> fdo::Result<()> {
        self.svc.pause().map_err(failed)
    }

    fn play_pause(&self) -> fdo::Result<()> {
        self.svc.play_pause().map_err(failed)
    }

    fn next(&self) -> fdo::Result<()> {
        self.svc.next().map_err(failed)
    }

    fn previous(&self) -> fdo::Result<()> {
        self.svc.previous().map_err(failed)
    }

    /// To a position in the track (ms).
    fn seek(&self, position_ms: i64) -> fdo::Result<()> {
        self.svc.seek(position_ms).map_err(failed)
    }

    fn set_shuffle(&self, shuffle: bool) -> fdo::Result<()> {
        self.svc.set_shuffle(shuffle).map_err(failed)
    }

    /// "off", "context" or "track".
    fn set_repeat(&self, repeat: &str) -> fdo::Result<()> {
        let r = Repeat::parse(repeat).ok_or_else(|| fdo::Error::InvalidArgs(format!("repeat: {repeat}")))?;
        self.svc.set_repeat(r).map_err(failed)
    }

    /// 0..1
    fn set_volume(&self, volume: f64) -> fdo::Result<()> {
        self.svc.set_volume(volume).map_err(failed)
    }

    /// Plays a context (playlist, album, artist, the liked songs: LikedSongsUri, or
    /// spotify:collection) from a track (its URI or index; empty: the first, or any with
    /// shuffle).
    fn load(&self, context_uri: &str, start_track: &str, shuffle: bool) -> fdo::Result<()> {
        self.svc.load(context_uri, start_track, shuffle).map_err(failed)
    }

    /// Like Load, from position_ms in the start track; without a context (empty), the
    /// start track (a URI) alone. For episodes and audiobook chapters: a show's or an
    /// audiobook's URI as the context.
    async fn load_at(&self, context_uri: &str, start_track: &str, position_ms: i64) -> fdo::Result<()> {
        self.svc.load_at(context_uri, start_track, position_ms).await.map_err(failed)
    }

    fn play_track(&self, uri: &str) -> fdo::Result<()> {
        self.svc.play_track(uri).map_err(failed)
    }

    /// [{uri,id,name,owner,image}]
    async fn playlists(&self) -> fdo::Result<String> {
        Ok(self.svc.web.playlists().await.map_err(failed)?.to_string())
    }

    /// [{uri,id,name,artists,album,albumUri,image,durationMs}]
    async fn liked_tracks(&self) -> fdo::Result<String> {
        Ok(self.svc.web.liked_tracks().await.map_err(failed)?.to_string())
    }

    /// [{uri,id,name,artists,image}]
    async fn saved_albums(&self) -> fdo::Result<String> {
        Ok(self.svc.web.saved_albums().await.map_err(failed)?.to_string())
    }

    /// {tracks,albums,playlists,artists}
    async fn search(&self, query: &str) -> fdo::Result<String> {
        Ok(self.svc.web.search(query).await.map_err(failed)?.to_string())
    }

    /// What was played lately, on any device: {episodes: [{...episode, showUri,
    /// showName}], contexts: [{kind: playlist|album|artist|liked|track, uri, name, image,
    /// playedAt, ...}]}
    async fn recent(&self) -> fdo::Result<String> {
        Ok(self.svc.web.recent().await.map_err(failed)?.to_string())
    }

    /// The saved podcasts: [{uri,id,name,publisher,image}]
    async fn shows(&self) -> fdo::Result<String> {
        Ok(self.svc.web.shows().await.map_err(failed)?.to_string())
    }

    /// The saved audiobooks: [{uri,id,name,authors,narrators,image}]
    async fn audiobooks(&self) -> fdo::Result<String> {
        Ok(self.svc.web.audiobooks().await.map_err(failed)?.to_string())
    }

    /// A show's episodes, newest first (200 at most):
    /// [{uri,id,name,description,image,durationMs,releaseDate,resumeMs,fullyPlayed}]
    async fn episodes(&self, show_uri: &str) -> fdo::Result<String> {
        Ok(self.svc.web.episodes(show_uri).await.map_err(failed)?.to_string())
    }

    /// An audiobook's chapters, in order:
    /// [{uri,id,name,number,image,durationMs,resumeMs,fullyPlayed}]
    async fn chapters(&self, audiobook_uri: &str) -> fdo::Result<String> {
        Ok(self.svc.web.chapters(audiobook_uri).await.map_err(failed)?.to_string())
    }

    /// The tracks of a playlist, album, artist (top tracks) or the liked songs.
    async fn context_tracks(&self, context_uri: &str) -> fdo::Result<String> {
        Ok(self.svc.web.context_tracks(context_uri).await.map_err(failed)?.to_string())
    }

    /// signedOut, signingIn, connecting, ready, error
    #[zbus(property, name = "State")]
    fn status(&self) -> String {
        self.state().status.as_str().to_string()
    }

    #[zbus(property)]
    fn error(&self) -> String {
        self.state().error
    }

    #[zbus(property)]
    fn playing(&self) -> bool {
        self.state().playing
    }

    #[zbus(property)]
    fn track(&self) -> HashMap<String, OwnedValue> {
        track_map(self.state().track.as_ref())
    }

    /// Where the track was at PositionAt (epoch ms); on from there while Playing.
    #[zbus(property)]
    fn position_ms(&self) -> i64 {
        self.state().position_ms as i64
    }

    #[zbus(property)]
    fn position_at(&self) -> i64 {
        self.state().position_at
    }

    #[zbus(property)]
    fn shuffle(&self) -> bool {
        self.state().shuffle
    }

    #[zbus(property)]
    fn repeat(&self) -> String {
        self.state().repeat.as_str().to_string()
    }

    #[zbus(property)]
    fn context_uri(&self) -> String {
        self.state().context_uri
    }

    #[zbus(property)]
    fn volume(&self) -> f64 {
        self.state().volume
    }

    /// The liked songs as a context (for Load, and to compare with ContextUri).
    #[zbus(property)]
    fn liked_songs_uri(&self) -> String {
        self.state().liked_songs_uri
    }

    /// This phone is Spotify's active device.
    #[zbus(property)]
    fn active(&self) -> bool {
        self.state().active
    }

    /// A page for the browser: one of Spotify's approvals.
    #[zbus(signal)]
    async fn sign_in_url(emitter: &SignalEmitter<'_>, url: &str) -> zbus::Result<()>;

    #[zbus(signal)]
    async fn signed_in(emitter: &SignalEmitter<'_>) -> zbus::Result<()>;
}

/// On the bus: both names, both objects, and what changes sent as it changes.
pub async fn serve(svc: Arc<Service>, mut signals: mpsc::UnboundedReceiver<Signal>) -> anyhow::Result<zbus::Connection> {
    let conn = zbus::connection::Builder::session()?
        .serve_at(PATH, SpotifyIface { svc: svc.clone() })?
        .serve_at(mpris::PATH, mpris::Root)?
        .serve_at(mpris::PATH, mpris::Player { svc: svc.clone() })?
        .name(BUS_NAME)?
        .name(mpris::BUS_NAME)?
        .build()
        .await?;

    let iface = conn.object_server().interface::<_, SpotifyIface>(PATH).await?;
    let player = conn.object_server().interface::<_, mpris::Player>(mpris::PATH).await?;

    // SignInUrl, SignedIn
    {
        let iface = iface.clone();
        tokio::spawn(async move {
            while let Some(signal) = signals.recv().await {
                let e = iface.signal_emitter();
                let r = match &signal {
                    Signal::SignInUrl(url) => {
                        tracing::info!("sign-in page out");
                        SpotifyIface::sign_in_url(e, url).await
                    }
                    Signal::SignedIn => SpotifyIface::signed_in(e).await,
                };
                if let Err(err) = r {
                    tracing::warn!("D-Bus signal {signal:?}: {err}");
                }
            }
        });
    }

    // PropertiesChanged (and MPRIS's Seeked) for what changed
    let mut rx = svc.state.subscribe();
    tokio::spawn(async move {
        let mut prev = rx.borrow_and_update().clone();
        while rx.changed().await.is_ok() {
            let cur = rx.borrow_and_update().clone();
            if let Err(e) = emit_changes(&iface, &player, &prev, &cur).await {
                tracing::warn!("D-Bus PropertiesChanged: {e}");
            }
            prev = cur;
        }
    });
    Ok(conn)
}

async fn emit_changes(
    iface: &zbus::object_server::InterfaceRef<SpotifyIface>,
    player: &zbus::object_server::InterfaceRef<mpris::Player>,
    prev: &State,
    cur: &State,
) -> zbus::Result<()> {
    let (i, e) = (iface.get().await, iface.signal_emitter());
    if prev.status != cur.status {
        i.state_changed(e).await?;
    }
    if prev.error != cur.error {
        i.error_changed(e).await?;
    }
    if prev.playing != cur.playing {
        i.playing_changed(e).await?;
    }
    if prev.track != cur.track {
        i.track_changed(e).await?;
    }
    if prev.position_ms != cur.position_ms || prev.position_at != cur.position_at {
        i.position_ms_changed(e).await?;
        i.position_at_changed(e).await?;
    }
    if prev.shuffle != cur.shuffle {
        i.shuffle_changed(e).await?;
    }
    if prev.repeat != cur.repeat {
        i.repeat_changed(e).await?;
    }
    if prev.context_uri != cur.context_uri {
        i.context_uri_changed(e).await?;
    }
    if prev.volume != cur.volume {
        i.volume_changed(e).await?;
    }
    if prev.liked_songs_uri != cur.liked_songs_uri {
        i.liked_songs_uri_changed(e).await?;
    }
    if prev.active != cur.active {
        i.active_changed(e).await?;
    }
    drop(i);
    mpris::emit_changes(player, prev, cur).await
}
