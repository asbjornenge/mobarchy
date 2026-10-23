// mobarchy-spotify: Mobarchy's Spotify service. A Spotify Connect device ("Mobarchy") on
// librespot, playing through PulseAudio, controlled locally (no Web API round trip), with
// its state from the player's events. On the session bus: org.mobarchy.Spotify (state,
// controls, sign-in, the library through the Web API) and MPRIS
// (org.mpris.MediaPlayer2.mobarchy_spotify: the lock screen, Frigg, headset keys).
// See docs/decisions/0042-own-spotify-service.md.

mod auth;
mod dbus;
mod mpris;
mod player;
mod service;
mod state;
mod webapi;

use std::path::PathBuf;
use tokio::sync::mpsc;

const HELP: &str = "\
mobarchy-spotify: Mobarchy's Spotify service (a Spotify Connect device, D-Bus, MPRIS)

usage: mobarchy-spotify [--help | --version]

On the session bus: org.mobarchy.Spotify (/org/mobarchy/Spotify) and
org.mpris.MediaPlayer2.mobarchy_spotify. Sign-in and credentials in
~/.cache/mobarchy/spotify; the Web API client ID (SPOTIFY_CLIENT_ID=...) in
~/.config/mobarchy/spotify.env. Logging: RUST_LOG (default info).
";

fn main() -> anyhow::Result<()> {
    match std::env::args().nth(1).as_deref() {
        Some("-h" | "--help") => {
            print!("{HELP}");
            return Ok(());
        }
        Some("-V" | "--version") => {
            println!("mobarchy-spotify {}", env!("CARGO_PKG_VERSION"));
            return Ok(());
        }
        Some(other) => anyhow::bail!("unknown argument: {other} (see --help)"),
        None => {}
    }
    // librespot's PulseAudio stream says whose it is from these (before any thread starts)
    unsafe {
        if std::env::var_os("PULSE_PROP_application.name").is_none() {
            std::env::set_var("PULSE_PROP_application.name", "mobarchy-spotify");
        }
        if std::env::var_os("PULSE_PROP_stream.description").is_none() {
            std::env::set_var("PULSE_PROP_stream.description", "Spotify");
        }
    }
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info,librespot=warn,librespot_connect=warn,zbus=warn".into()),
        )
        .with_writer(std::io::stderr)
        // (journald adds its own time)
        .without_time()
        .with_target(false)
        .with_ansi(false)
        .init();
    tokio::runtime::Builder::new_multi_thread().enable_all().build()?.block_on(run())
}

async fn run() -> anyhow::Result<()> {
    let home = PathBuf::from(std::env::var_os("HOME").ok_or_else(|| anyhow::anyhow!("no HOME"))?);
    let cache = std::env::var_os("XDG_CACHE_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".cache"));
    let config = std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config"));
    let paths = service::Paths {
        cache: cache.join("mobarchy/spotify"),
        env: config.join("mobarchy/spotify.env"),
    };
    let (signals_tx, signals_rx) = mpsc::unbounded_channel();
    let service = service::Service::new(paths, signals_tx);

    let bus = dbus::serve(service.clone(), signals_rx).await?;
    tracing::info!("on the session bus as {} and {}", dbus::BUS_NAME, mpris::BUS_NAME);
    service.start().await;

    let mut term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())?;
    tokio::select! {
        _ = term.recv() => {}
        _ = tokio::signal::ctrl_c() => {}
    }
    tracing::info!("stopping");
    service.disconnect().await;
    drop(bus);
    Ok(())
}
