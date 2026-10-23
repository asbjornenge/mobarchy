// The service: signing in (two approvals), keeping the Spotify Connect device connected
// (again after a lost connection), and the controls, which go to the device's Spirc.

use crate::auth::{self, Approval};
use crate::player::{self, Connection};
use crate::state::{Repeat, State, Status};
use crate::webapi::{WebApi, context_kind};
use anyhow::{Result, anyhow, bail};
use librespot_connect::{LoadContextOptions, LoadRequest, LoadRequestOptions, Options, PlayingTrack};
use librespot_core::{Error as SpotError, authentication::Credentials, cache::Cache, error::ErrorKind};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::sync::{mpsc, watch};
use tokio::task::JoinHandle;

/// The service's D-Bus signals.
#[derive(Debug, Clone)]
pub enum Signal {
    SignInUrl(String),
    SignedIn,
}

pub struct Paths {
    // ~/.cache/mobarchy/spotify: librespot's credentials.json, webapi-token.json, device-id
    pub cache: PathBuf,
    // ~/.config/mobarchy/spotify.env: SPOTIFY_CLIENT_ID=...
    pub env: PathBuf,
}

pub struct Service {
    pub state: watch::Sender<State>,
    pub web: WebApi,
    http: reqwest::Client,
    paths: Paths,
    signals: mpsc::UnboundedSender<Signal>,
    conn: Mutex<Option<Arc<Connection>>>,
    supervisor: Mutex<Option<JoinHandle<()>>>,
    signin: Mutex<Option<JoinHandle<()>>>,
    signin_url: Mutex<Option<String>>,
    // a new one for each connection started: an old one's loop ends when it sees it changed
    generation: AtomicU64,
}

const SIGN_IN_TIMEOUT: Duration = Duration::from_secs(15 * 60);

impl Service {
    pub fn new(paths: Paths, signals: mpsc::UnboundedSender<Signal>) -> Arc<Service> {
        let http = reqwest::Client::builder()
            .user_agent(concat!("mobarchy-spotify/", env!("CARGO_PKG_VERSION")))
            .timeout(Duration::from_secs(30))
            .build()
            .expect("an HTTP client");
        let web = WebApi::new(http.clone(), paths.cache.join("webapi-token.json"));
        let (state, _) = watch::channel(State::default());
        Arc::new(Service {
            state,
            web,
            http,
            paths,
            signals,
            conn: Mutex::new(None),
            supervisor: Mutex::new(None),
            signin: Mutex::new(None),
            signin_url: Mutex::new(None),
            generation: AtomicU64::new(0),
        })
    }

    fn set_status(&self, status: Status, error: &str) {
        self.state.send_if_modified(|s| {
            let changed = s.status != status || s.error != error;
            s.status = status;
            s.error = error.to_string();
            changed
        });
    }

    fn status(&self) -> Status {
        self.state.borrow().status
    }

    fn cache(&self) -> Result<Cache> {
        std::fs::create_dir_all(&self.paths.cache)?;
        Ok(Cache::new(Some(&self.paths.cache), None, None, None)?)
    }

    // The owner's client ID: $SPOTIFY_CLIENT_ID, or SPOTIFY_CLIENT_ID= in spotify.env.
    fn read_client_id(&self) -> Option<String> {
        if let Ok(id) = std::env::var("SPOTIFY_CLIENT_ID")
            && !id.trim().is_empty()
        {
            return Some(id.trim().to_string());
        }
        let text = std::fs::read_to_string(&self.paths.env).ok()?;
        parse_env_client_id(&text)
    }

    fn check_client_id(&self) -> bool {
        match self.read_client_id() {
            Some(id) => {
                self.web.set_client_id(&id);
                true
            }
            None => {
                self.set_status(
                    Status::Error,
                    &format!(
                        "No Spotify client ID: put SPOTIFY_CLIENT_ID=<your app's client ID> in {} \
                         (an app at developer.spotify.com with the redirect URI {})",
                        self.paths.env.display(),
                        auth::REDIRECT_URI
                    ),
                );
                false
            }
        }
    }

    // A device ID of its own, kept: the phone stays the same device in Spotify's apps.
    fn device_id(&self) -> String {
        let path = self.paths.cache.join("device-id");
        if let Ok(id) = std::fs::read_to_string(&path) {
            let id = id.trim();
            if id.len() >= 16 && id.chars().all(|c| c.is_ascii_hexdigit()) {
                return id.to_string();
            }
        }
        let mut bytes = [0u8; 20];
        rand::fill(&mut bytes);
        let id: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
        let _ = std::fs::create_dir_all(&self.paths.cache);
        let _ = std::fs::write(&path, &id);
        id
    }

    /// At start: with what was kept from the last sign-in, connect straight away.
    pub async fn start(self: &Arc<Self>) {
        if !self.check_client_id() {
            return;
        }
        let credentials = self.cache().ok().and_then(|c| c.credentials());
        match credentials {
            Some(creds) if self.web.has_token().await => self.start_connection(creds),
            _ => self.set_status(Status::SignedOut, ""),
        }
    }

    fn connected(&self) -> Option<Arc<Connection>> {
        self.conn.lock().unwrap().clone()
    }

    fn connecting(&self) -> bool {
        self.supervisor.lock().unwrap().as_ref().is_some_and(|h| !h.is_finished())
    }

    // Keeps the device connected: connects, and again (backing off) when it drops.
    fn start_connection(self: &Arc<Self>, credentials: Credentials) {
        let generation = self.generation.fetch_add(1, Ordering::AcqRel) + 1;
        self.set_status(Status::Connecting, "");
        let this = self.clone();
        let handle = tokio::spawn(async move { this.keep_connected(generation, credentials).await });
        if let Some(old) = self.supervisor.lock().unwrap().replace(handle) {
            old.abort();
        }
    }

    async fn keep_connected(self: Arc<Self>, generation: u64, credentials: Credentials) {
        let current = || self.generation.load(Ordering::Acquire) == generation;
        let mut backoff = 2;
        while current() {
            let cache = match self.cache() {
                Ok(c) => c,
                Err(e) => return self.set_status(Status::Error, &format!("Spotify's cache: {e}")),
            };
            // (the kept ones once there are: a sign-in's access token runs out within the hour)
            let creds = cache.credentials().unwrap_or_else(|| credentials.clone());
            match player::connect(cache, &self.device_id(), creds, self.state.clone()).await {
                Ok((conn, task)) => {
                    if !current() {
                        let _ = conn.spirc.shutdown();
                        return;
                    }
                    *self.conn.lock().unwrap() = Some(conn);
                    let status = if self.web.has_token().await { Status::Ready } else { Status::SignedOut };
                    self.set_status(status, "");
                    backoff = 2;
                    task.await;
                    *self.conn.lock().unwrap() = None;
                    self.state.send_modify(|s| s.clear_playback());
                    if !current() {
                        return;
                    }
                    tracing::warn!("the Spotify session ended: connecting again");
                    self.set_status(Status::Connecting, "");
                }
                Err(e) if is_rejected(&e) => {
                    // the kept credentials are no good any more: a new sign-in
                    tracing::warn!("Spotify rejected the credentials: {e}");
                    let _ = std::fs::remove_file(self.paths.cache.join("credentials.json"));
                    return self.set_status(Status::SignedOut, "Spotify asks to sign in again");
                }
                Err(e) => {
                    tracing::warn!("connecting to Spotify: {e} (again in {backoff} s)");
                    self.set_status(Status::Connecting, &format!("Couldn't connect to Spotify: {e}"));
                    tokio::time::sleep(Duration::from_secs(backoff)).await;
                    backoff = (backoff * 2).min(60);
                }
            }
        }
    }

    /// SignIn(): asks for what is missing (the Web API's approval, the streaming's), then
    /// connects. Again while it runs: the current page once more.
    pub async fn sign_in(self: &Arc<Self>) {
        if self.signin.lock().unwrap().as_ref().is_some_and(|h| !h.is_finished()) {
            if let Some(url) = self.signin_url.lock().unwrap().clone() {
                let _ = self.signals.send(Signal::SignInUrl(url));
            }
            return;
        }
        if !self.check_client_id() {
            return;
        }
        if self.status() == Status::Ready {
            let _ = self.signals.send(Signal::SignedIn);
            return;
        }
        let this = self.clone();
        let handle = tokio::spawn(async move {
            let result = tokio::time::timeout(SIGN_IN_TIMEOUT, this.sign_in_flow()).await;
            *this.signin_url.lock().unwrap() = None;
            match result {
                Ok(Ok(())) => {}
                Ok(Err(e)) => {
                    tracing::warn!("signing in: {e:#}");
                    this.set_status(Status::SignedOut, &format!("{e:#}"));
                }
                Err(_) => this.set_status(Status::SignedOut, "Signing in took too long"),
            }
        });
        *self.signin.lock().unwrap() = Some(handle);
    }

    async fn sign_in_flow(self: &Arc<Self>) -> Result<()> {
        self.set_status(Status::SigningIn, "");
        if !self.web.has_token().await {
            tracing::info!("signing in: the Web API");
            let token = self.approve(&self.web.client_id(), auth::WEB_API_SCOPES).await?;
            self.web.set_token(token).await?;
        }
        if self.connected().is_some() {
            self.set_status(Status::Ready, "");
        } else {
            let kept = self.cache()?.credentials();
            let creds = match kept {
                Some(c) => c,
                None => {
                    tracing::info!("signing in: streaming");
                    let token = self.approve(auth::STREAMING_CLIENT_ID, auth::STREAMING_SCOPES).await?;
                    Credentials::with_access_token(token.access_token)
                }
            };
            self.start_connection(creds);
            // until connected (or given up)
            let mut rx = self.state.subscribe();
            let s = rx
                .wait_for(|s| matches!(s.status, Status::Ready | Status::SignedOut | Status::Error))
                .await?
                .clone();
            if s.status != Status::Ready {
                bail!("{}", if s.error.is_empty() { "Couldn't connect to Spotify" } else { &s.error });
            }
        }
        tracing::info!("signed in");
        let _ = self.signals.send(Signal::SignedIn);
        Ok(())
    }

    // One approval: its page out (SignInUrl), the answer on the callback, the token.
    async fn approve(&self, client_id: &str, scopes: &[&str]) -> Result<auth::Token> {
        let approval = Approval::new(client_id, scopes);
        let listener = auth::bind_callback().await?;
        *self.signin_url.lock().unwrap() = Some(approval.url.clone());
        let _ = self.signals.send(Signal::SignInUrl(approval.url.clone()));
        let token = approval.finish(listener, &self.http).await;
        *self.signin_url.lock().unwrap() = None;
        token
    }

    /// SignOut(): the device off, and everything kept forgotten.
    pub async fn sign_out(&self) {
        if let Some(h) = self.signin.lock().unwrap().take() {
            h.abort();
        }
        *self.signin_url.lock().unwrap() = None;
        self.disconnect().await;
        self.web.forget().await;
        let _ = std::fs::remove_file(self.paths.cache.join("credentials.json"));
        self.state.send_modify(|s| {
            s.clear_playback();
            s.liked_songs_uri.clear();
        });
        if self.read_client_id().is_some() {
            self.set_status(Status::SignedOut, "");
        }
        tracing::info!("signed out");
    }

    /// The device off (Spotify is told it's gone), e.g. at the end.
    pub async fn disconnect(&self) {
        self.generation.fetch_add(1, Ordering::AcqRel);
        if let Some(conn) = self.conn.lock().unwrap().take() {
            let _ = conn.spirc.shutdown();
        }
        let handle = self.supervisor.lock().unwrap().take();
        if let Some(mut h) = handle {
            // (the Spirc says goodbye first; not for long)
            if tokio::time::timeout(Duration::from_secs(3), &mut h).await.is_err() {
                h.abort();
            }
        }
    }

    fn device(&self) -> Result<Arc<Connection>> {
        self.connected().ok_or_else(|| {
            anyhow!(if self.connecting() { "Spotify is connecting" } else { "not signed in to Spotify" })
        })
    }

    fn active(&self) -> bool {
        self.state.borrow().active
    }

    // The device, for controls of what plays here. While another device plays, they would
    // do nothing (the Spirc ignores them), and making this one active would stop the other
    // with nothing to play here: Play (which brings the playback here) or Load first.
    fn active_device(&self) -> Result<Arc<Connection>> {
        let conn = self.device()?;
        if !self.active() {
            bail!("Spotify isn't playing on this phone: Play or Load first");
        }
        Ok(conn)
    }

    // Load's (and PlayTrack's) way in: this becomes the active device first (the Spirc
    // takes a load only from the active one).
    fn activate(&self, conn: &Connection) {
        if !self.active() {
            let _ = conn.spirc.activate();
        }
    }

    pub fn play(&self) -> Result<()> {
        let conn = self.device()?;
        if self.active() {
            conn.spirc.play()?;
        } else {
            // What plays elsewhere comes here, and plays.
            conn.play_on_activation.store(true, Ordering::Release);
            conn.spirc.transfer(None)?;
        }
        Ok(())
    }

    pub fn pause(&self) -> Result<()> {
        let conn = self.device()?;
        if self.active() {
            conn.spirc.pause()?;
        }
        Ok(())
    }

    pub fn play_pause(&self) -> Result<()> {
        if self.active() {
            Ok(self.device()?.spirc.play_pause()?)
        } else {
            self.play()
        }
    }

    pub fn next(&self) -> Result<()> {
        let conn = self.active_device()?;
        Ok(conn.spirc.next()?)
    }

    pub fn previous(&self) -> Result<()> {
        let conn = self.active_device()?;
        Ok(conn.spirc.prev()?)
    }

    pub fn seek(&self, ms: i64) -> Result<()> {
        let conn = self.active_device()?;
        let ms = ms.clamp(0, u32::MAX as i64) as u32;
        conn.spirc.set_position_ms(ms)?;
        Ok(())
    }

    pub fn set_shuffle(&self, on: bool) -> Result<()> {
        let conn = self.active_device()?;
        Ok(conn.spirc.shuffle(on)?)
    }

    pub fn set_repeat(&self, repeat: Repeat) -> Result<()> {
        let conn = self.active_device()?;
        match repeat {
            Repeat::Off => {
                conn.spirc.repeat_track(false)?;
                conn.spirc.repeat(false)?;
            }
            Repeat::Context => {
                conn.spirc.repeat_track(false)?;
                conn.spirc.repeat(true)?;
            }
            Repeat::Track => conn.spirc.repeat_track(true)?,
        }
        Ok(())
    }

    pub fn set_volume(&self, volume: f64) -> Result<()> {
        let conn = self.active_device()?;
        let v = (volume.clamp(0.0, 1.0) * u16::MAX as f64).round() as u16;
        Ok(conn.spirc.set_volume(v)?)
    }

    /// Load(context, start track (a URI or an index, may be empty), shuffle).
    pub fn load(&self, context_uri: &str, start: &str, shuffle: bool) -> Result<()> {
        let conn = self.device()?;
        let context_uri = self.context_uri_for(&conn, context_uri)?;
        let start = start.trim();
        let playing_track = if start.is_empty() {
            None
        } else if let Ok(i) = start.parse::<u32>() {
            Some(PlayingTrack::Index(i))
        } else {
            Some(PlayingTrack::Uri(start.to_string()))
        };
        let options = LoadRequestOptions {
            start_playing: true,
            seek_to: 0,
            context_options: Some(LoadContextOptions::Options(Options { shuffle, repeat: false, repeat_track: false })),
            playing_track,
        };
        tracing::info!("load {context_uri} at {start:?}, shuffle {shuffle}");
        self.activate(&conn);
        conn.spirc.load(LoadRequest::from_context_uri(context_uri.clone(), options))?;
        self.state.send_modify(|s| s.context_uri = context_uri);
        Ok(())
    }

    /// LoadAt(context (may be empty: the track alone), start track, position): from that
    /// position in it. A context librespot can't resolve, or one where the track isn't to
    /// be found (a long show's episodes come a page at a time), and the track plays alone.
    pub async fn load_at(&self, context_uri: &str, start: &str, position_ms: i64) -> Result<()> {
        let conn = self.device()?;
        let seek_to = position_ms.clamp(0, u32::MAX as i64) as u32;
        let start = start.trim();
        let start_uri = start.starts_with("spotify:").then_some(start);
        let playing_track = if start.is_empty() {
            None
        } else if let Ok(i) = start.parse::<u32>() {
            Some(PlayingTrack::Index(i))
        } else {
            Some(PlayingTrack::Uri(start.to_string()))
        };
        let context = if context_uri.trim().is_empty() {
            None
        } else {
            let wanted = self.context_uri_for(&conn, context_uri.trim())?;
            let found = resolvable_context(&conn, &wanted, start_uri).await;
            if found.is_none() {
                tracing::info!("{wanted} doesn't resolve with {start:?} in it: {start:?} alone");
            }
            found
        };
        let (request, playing) = match (context, start_uri) {
            (Some(ctx), _) => {
                let options = LoadRequestOptions { start_playing: true, seek_to, context_options: None, playing_track };
                (LoadRequest::from_context_uri(ctx.clone(), options), ctx)
            }
            (None, Some(uri)) => {
                let options = LoadRequestOptions { start_playing: true, seek_to, ..Default::default() };
                (LoadRequest::from_tracks(vec![uri.to_string()], options), uri.to_string())
            }
            (None, None) => bail!("nothing to play: no context that resolves, and no track URI"),
        };
        tracing::info!("load {playing} at {start:?}, {seek_to} ms");
        self.activate(&conn);
        conn.spirc.load(request)?;
        self.state.send_modify(|s| s.context_uri = playing);
        Ok(())
    }

    /// PlayTrack(uri): that track alone.
    pub fn play_track(&self, uri: &str) -> Result<()> {
        if !uri.starts_with("spotify:track:") && !uri.starts_with("spotify:episode:") {
            bail!("not a track: {uri}");
        }
        let conn = self.device()?;
        let options = LoadRequestOptions { start_playing: true, ..Default::default() };
        self.activate(&conn);
        conn.spirc.load(LoadRequest::from_tracks(vec![uri.to_string()], options))?;
        self.state.send_modify(|s| s.context_uri = uri.to_string());
        Ok(())
    }

    // The liked songs as Spotify takes them (spotify:user:<name>:collection), from any of
    // their names; other contexts as they are.
    fn context_uri_for(&self, conn: &Connection, uri: &str) -> Result<String> {
        match context_kind(uri) {
            Some(("collection", _)) => Ok(format!("spotify:user:{}:collection", conn.session.username())),
            Some(_) => Ok(uri.to_string()),
            None => bail!("not a Spotify context: {uri}"),
        }
    }
}

// SPOTIFY_CLIENT_ID=... in an env file (quotes and `export` allowed).
pub fn parse_env_client_id(text: &str) -> Option<String> {
    text.lines()
        .map(str::trim)
        .map(|l| l.strip_prefix("export ").unwrap_or(l))
        .find_map(|l| l.strip_prefix("SPOTIFY_CLIENT_ID="))
        .map(|v| v.trim().trim_matches(|c| c == '"' || c == '\'').to_string())
        .filter(|v| !v.is_empty())
}

// The context as librespot will get it (Spotify's context-resolve), if it resolves, with
// `track` in its pages when one is given. An audiobook may go by its show URI.
async fn resolvable_context(conn: &Connection, uri: &str, track: Option<&str>) -> Option<String> {
    let mut candidates = vec![uri.to_string()];
    if let Some(("audiobook", id)) = context_kind(uri) {
        candidates.push(format!("spotify:show:{id}"));
    }
    for candidate in candidates {
        match conn.session.spclient().get_context(&candidate).await {
            Ok(ctx) => {
                let has = |t: &str| ctx.pages.iter().any(|p| p.tracks.iter().any(|x| x.uri() == t));
                if track.is_none_or(has) {
                    return Some(candidate);
                }
                tracing::info!("{candidate}: resolves, without {track:?} (of {} pages)", ctx.pages.len());
            }
            Err(e) => tracing::info!("{candidate} doesn't resolve: {e}"),
        }
    }
    None
}

fn is_rejected(e: &SpotError) -> bool {
    matches!(e.kind, ErrorKind::PermissionDenied | ErrorKind::Unauthenticated)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn client_id_from_env_file() {
        assert_eq!(parse_env_client_id("SPOTIFY_CLIENT_ID=abc\n"), Some("abc".into()));
        assert_eq!(parse_env_client_id("# x\nexport SPOTIFY_CLIENT_ID=\"abc\"\n"), Some("abc".into()));
        assert_eq!(parse_env_client_id("SPOTIFY_CLIENT_ID=\n"), None);
        assert_eq!(parse_env_client_id("OTHER=1\n"), None);
    }
}
