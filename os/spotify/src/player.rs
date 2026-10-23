// librespot: the session, the player (PulseAudio) and the Spotify Connect device ("Spirc").
// Control goes to the Spirc directly (no Web API in between), and what plays comes from
// the player's events, as they happen.

use crate::state::{Repeat, State, Track, now_ms};
use librespot_connect::{ConnectConfig, Spirc};
use librespot_core::{
    Error, Session, SessionConfig, SpotifyUri, authentication::Credentials, cache::Cache,
    config::DeviceType, dealer::protocol::Message,
};
use librespot_metadata::{Metadata as _, audio::AudioItem, audio::UniqueFields};
use librespot_playback::{
    audio_backend,
    config::{AudioFormat, Bitrate, PlayerConfig},
    mixer::{Mixer, MixerConfig, softmixer::SoftMixer},
    player::{Player, PlayerEvent},
};
use librespot_protocol::connect::ClusterUpdate;
use futures_util::StreamExt as _;
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use tokio::sync::watch;

pub const DEVICE_NAME: &str = "Mobarchy";

/// A connected Spotify Connect device.
pub struct Connection {
    pub spirc: Spirc,
    pub session: Session,
    // Play asked for while another device had the playback: it was transferred here, and
    // plays once it arrives.
    pub play_on_activation: AtomicBool,
}

pub type Task = Pin<Box<dyn Future<Output = ()> + Send>>;

/// Connects with `credentials` (kept in `cache` once accepted). The task runs the device;
/// it ends when the session does (signed out, or the connection lost).
pub async fn connect(
    cache: Cache,
    device_id: &str,
    credentials: Credentials,
    state: watch::Sender<State>,
) -> Result<(Arc<Connection>, Task), Error> {
    let session_config = SessionConfig { device_id: device_id.to_string(), ..SessionConfig::default() };
    let session = Session::new(session_config, Some(cache));

    let player_config = PlayerConfig {
        bitrate: Bitrate::Bitrate320,
        normalisation: true,
        ..PlayerConfig::default()
    };
    let mixer = Arc::new(SoftMixer::open(MixerConfig::default())?);
    let backend = audio_backend::find(Some("pulseaudio".to_string()))
        .ok_or_else(|| Error::unavailable(anyhow_err("no PulseAudio output in librespot")))?;
    let player = Player::new(player_config, session.clone(), mixer.get_soft_volume(), move || {
        backend(None, AudioFormat::default())
    });
    let events = player.get_player_event_channel();
    // The context that plays: from Spotify's cluster updates (the Spirc keeps its own to itself).
    let cluster = session
        .dealer()
        .listen_for("hm://connect-state/v1/cluster", Message::from_raw::<ClusterUpdate>)?;

    let connect_config = ConnectConfig {
        name: DEVICE_NAME.to_string(),
        device_type: DeviceType::Smartphone,
        initial_volume: u16::MAX,
        ..ConnectConfig::default()
    };
    let (spirc, spirc_task) = Spirc::new(connect_config, session.clone(), credentials, player, mixer).await?;
    let conn = Arc::new(Connection { spirc, session: session.clone(), play_on_activation: AtomicBool::new(false) });

    let user = session.username();
    tracing::info!("connected to Spotify as {user}, device {device_id}");
    state.send_modify(|s| {
        s.liked_songs_uri = format!("spotify:user:{user}:collection");
        s.volume = 1.0;
    });

    tokio::spawn(follow_events(Arc::downgrade(&conn), events, state.clone()));
    tokio::spawn(follow_cluster(session.device_id().to_string(), cluster, state));
    Ok((conn, Box::pin(spirc_task)))
}

fn anyhow_err(msg: &str) -> std::io::Error {
    std::io::Error::other(msg.to_string())
}

// The player's events into the state.
async fn follow_events(
    conn: std::sync::Weak<Connection>,
    mut events: librespot_playback::player::PlayerEventChannel,
    state: watch::Sender<State>,
) {
    while let Some(event) = events.recv().await {
        match event {
            PlayerEvent::TrackChanged { audio_item } => {
                let track = track_of(&audio_item);
                tracing::info!("track: {} - {}", track.artists.join(", "), track.title);
                // (the position comes with Loading and Playing)
                state.send_modify(|s| s.track = Some(track));
                // The album's URI: not in the audio item, but in the track's metadata (cached).
                if let (Some(c), SpotifyUri::Track { .. }) = (conn.upgrade(), &audio_item.track_id) {
                    let (session, uri, state) = (c.session.clone(), audio_item.track_id.clone(), state.clone());
                    tokio::spawn(async move {
                        if let Ok(t) = librespot_metadata::Track::get(&session, &uri).await {
                            let album_uri = t.album.id.to_uri().unwrap_or_default();
                            let track_uri = uri.to_uri().unwrap_or_default();
                            state.send_if_modified(|s| match &mut s.track {
                                Some(t) if t.uri == track_uri && t.album_uri != album_uri => {
                                    t.album_uri = album_uri;
                                    true
                                }
                                _ => false,
                            });
                        }
                    });
                }
            }
            PlayerEvent::Playing { position_ms, .. } => state.send_modify(|s| {
                let jumped = s.playing && s.position_now().abs_diff(position_ms) > 1500;
                s.playing = true;
                s.set_position(position_ms);
                if jumped {
                    s.seeks += 1;
                }
            }),
            PlayerEvent::Paused { position_ms, .. } => state.send_modify(|s| {
                s.playing = false;
                s.set_position(position_ms);
            }),
            PlayerEvent::Loading { position_ms, .. } => state.send_modify(|s| s.set_position(position_ms)),
            PlayerEvent::Seeked { position_ms, .. } | PlayerEvent::PositionCorrection { position_ms, .. } => {
                state.send_modify(|s| {
                    s.set_position(position_ms);
                    s.seeks += 1;
                })
            }
            PlayerEvent::Stopped { .. } => state.send_modify(|s| {
                s.playing = false;
                s.position_at = now_ms();
            }),
            PlayerEvent::Unavailable { track_id, .. } => {
                tracing::warn!("unavailable: {}", track_id.to_uri().unwrap_or_default())
            }
            PlayerEvent::ShuffleChanged { shuffle } => state.send_modify(|s| s.shuffle = shuffle),
            PlayerEvent::RepeatChanged { context, track } => state.send_modify(|s| {
                s.repeat = if track {
                    Repeat::Track
                } else if context {
                    Repeat::Context
                } else {
                    Repeat::Off
                }
            }),
            PlayerEvent::VolumeChanged { volume } => {
                state.send_modify(|s| s.volume = volume as f64 / u16::MAX as f64)
            }
            // This device became the active one (it plays here now), or stopped being it.
            PlayerEvent::SessionConnected { .. } => {
                tracing::info!("active device");
                state.send_modify(|s| s.active = true);
                if let Some(c) = conn.upgrade()
                    && c.play_on_activation.swap(false, Ordering::AcqRel)
                {
                    let _ = c.spirc.play();
                }
            }
            PlayerEvent::SessionDisconnected { .. } => {
                tracing::info!("no longer the active device");
                state.send_modify(|s| s.clear_playback());
            }
            _ => {}
        }
    }
}

// Spotify's cluster updates: the context that plays while this device is the active one.
async fn follow_cluster(
    device_id: String,
    mut updates: librespot_core::dealer::manager::BoxedStreamResult<ClusterUpdate>,
    state: watch::Sender<State>,
) {
    while let Some(update) = updates.next().await {
        let Ok(update) = update else { continue };
        let cluster = &update.cluster;
        if cluster.active_device_id != device_id {
            continue;
        }
        let uri = cluster.player_state.context_uri.clone();
        if !uri.is_empty() {
            state.send_if_modified(|s| {
                if s.context_uri != uri {
                    s.context_uri = uri;
                    true
                } else {
                    false
                }
            });
        }
    }
}

pub fn track_of(item: &AudioItem) -> Track {
    let (artists, album) = match &item.unique_fields {
        UniqueFields::Track { artists, album, .. } => {
            (artists.iter().map(|a| a.name.clone()).collect(), album.clone())
        }
        UniqueFields::Local { artists, album, .. } => {
            (artists.iter().cloned().collect(), album.clone().unwrap_or_default())
        }
        UniqueFields::Episode { show_name, .. } => (vec![show_name.clone()], show_name.clone()),
    };
    let image = item
        .covers
        .iter()
        .max_by_key(|c| c.width.max(0) as i64 * c.height.max(0) as i64)
        .map(|c| c.url.clone())
        .unwrap_or_default();
    Track {
        uri: item.uri.clone(),
        id: item.track_id.to_id().unwrap_or_default(),
        title: item.name.clone(),
        artists,
        album,
        album_uri: String::new(),
        image,
        duration_ms: item.duration_ms,
    }
}
