// Spotify's approvals: OAuth 2 authorization code with PKCE, the answer on a callback of
// our own (127.0.0.1:8989/login). Both sign-ins go this way: the Web API's (the owner's
// client ID) and the streaming's (Spotify's own client ID, as librespot uses). The page is
// for the client to open (SignInUrl); this service never opens a browser.
//
// (librespot-oauth does the same, but prints its URL instead of handing it out, and takes
// the first request on the port as the answer: a browser's favicon fetch broke it.)

use anyhow::{Context as _, Result, anyhow, bail};
use base64::Engine as _;
use serde::{Deserialize, Serialize};
use sha2::{Digest as _, Sha256};
use std::net::SocketAddr;
use std::time::Duration;
use tokio::io::{AsyncReadExt as _, AsyncWriteExt as _};
use tokio::net::{TcpListener, TcpStream};

pub const REDIRECT_URI: &str = "http://127.0.0.1:8989/login";
const AUTHORIZE_URL: &str = "https://accounts.spotify.com/authorize";
const TOKEN_URL: &str = "https://accounts.spotify.com/api/token";

// Spotify's own client ID (its desktop client's), as librespot signs in for streaming.
pub const STREAMING_CLIENT_ID: &str = "65b708073fc0480ea92a077233ca87bd";
// What spotify-player asks for with that client ID (it works for streaming).
pub const STREAMING_SCOPES: &[&str] = &[
    "streaming",
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing",
    "app-remote-control",
    "playlist-read-private",
    "playlist-read-collaborative",
    "playlist-modify-private",
    "playlist-modify-public",
    "user-follow-modify",
    "user-follow-read",
    "user-read-playback-position",
    "user-top-read",
    "user-read-recently-played",
    "user-library-modify",
    "user-library-read",
    "user-personalized",
];
// The Web API's: the library, search, and what plays.
pub const WEB_API_SCOPES: &[&str] = &[
    "user-library-read",
    "user-library-modify",
    "playlist-read-private",
    "playlist-read-collaborative",
    "user-read-private",
    "user-follow-read",
    "user-top-read",
    "user-read-recently-played",
    "user-read-playback-state",
    "user-modify-playback-state",
];

const DONE: &str = "Signed in to Spotify &mdash; back to Mobarchy&hellip;";

fn page(text: &str) -> String {
    format!(
        "<!doctype html><html><head><meta charset=\"utf-8\">\
         <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
         <title>Spotify</title></head>\
         <body style=\"font-family: sans-serif; text-align: center; margin-top: 30vh\">\
         <p>{text}</p></body></html>"
    )
}

/// A PKCE verifier and its challenge (RFC 7636, S256).
pub struct Pkce {
    pub verifier: String,
    pub challenge: String,
}

impl Pkce {
    pub fn new() -> Pkce {
        Pkce::from_verifier(random_url_safe(48))
    }

    pub fn from_verifier(verifier: String) -> Pkce {
        let challenge = b64(&Sha256::digest(verifier.as_bytes()));
        Pkce { verifier, challenge }
    }
}

fn b64(bytes: &[u8]) -> String {
    base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(bytes)
}

pub fn random_url_safe(n: usize) -> String {
    let mut bytes = vec![0u8; n];
    rand::fill(bytes.as_mut_slice());
    b64(&bytes)
}

pub fn authorize_url(client_id: &str, scopes: &[&str], challenge: &str, state: &str) -> String {
    let scope = scopes.join(" ");
    url::Url::parse_with_params(
        AUTHORIZE_URL,
        &[
            ("response_type", "code"),
            ("client_id", client_id),
            ("redirect_uri", REDIRECT_URI),
            ("scope", scope.as_str()),
            ("code_challenge_method", "S256"),
            ("code_challenge", challenge),
            ("state", state),
        ],
    )
    .expect("a valid URL")
    .into()
}

/// What a request to the callback was.
#[derive(Debug, PartialEq, Eq)]
pub enum Redirect {
    Code(String),
    // Spotify's answer: not approved (access_denied, ...)
    Denied(String),
    // an answer to another sign-in (an older page approved late)
    OtherState,
    // not an answer at all (favicon, ...)
    Stray,
}

/// The request target of a callback request (`/login?code=...&state=...`).
pub fn parse_redirect(target: &str, state: &str) -> Redirect {
    let Ok(url) = url::Url::parse(&format!("http://127.0.0.1{target}")) else {
        return Redirect::Stray;
    };
    if url.path() != "/login" {
        return Redirect::Stray;
    }
    let get = |key: &str| {
        url.query_pairs()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.into_owned())
    };
    let (code, error) = (get("code"), get("error"));
    if code.is_none() && error.is_none() {
        return Redirect::Stray;
    }
    if get("state").as_deref() != Some(state) {
        return Redirect::OtherState;
    }
    match (code, error) {
        (Some(code), _) if !code.is_empty() => Redirect::Code(code),
        (_, Some(error)) => Redirect::Denied(error),
        _ => Redirect::Stray,
    }
}

/// One approval's page and its callback.
pub struct Approval {
    pub url: String,
    client_id: String,
    pkce: Pkce,
    state: String,
}

impl Approval {
    pub fn new(client_id: &str, scopes: &[&str]) -> Approval {
        let pkce = Pkce::new();
        let state = random_url_safe(16);
        let url = authorize_url(client_id, scopes, &pkce.challenge, &state);
        Approval { url, client_id: client_id.to_string(), pkce, state }
    }

    /// Listens on the callback until Spotify answers, then exchanges the code for a token.
    /// The port is the callback's only while this runs.
    pub async fn finish(&self, listener: TcpListener, http: &reqwest::Client) -> Result<Token> {
        let (code, mut stream) = wait_for_code(listener, &self.state).await?;
        let params = [
            ("grant_type", "authorization_code"),
            ("code", code.as_str()),
            ("redirect_uri", REDIRECT_URI),
            ("client_id", self.client_id.as_str()),
            ("code_verifier", self.pkce.verifier.as_str()),
        ];
        // (the page answers once the code is exchanged: it says whether it worked)
        let token = token_request(http, &params, None).await;
        let text = match &token {
            Ok(_) => DONE.to_string(),
            Err(e) => format!("Spotify sign-in failed: {}", html_escape(&format!("{e:#}"))),
        };
        respond(&mut stream, "200 OK", &page(&text)).await;
        token
    }
}

fn html_escape(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
}

/// The callback's port. (Taken before the page goes out, so that the answer finds it.)
pub async fn bind_callback() -> Result<TcpListener> {
    let addr: SocketAddr = "127.0.0.1:8989".parse().unwrap();
    // (a moment's grace: the previous approval's listener, or a client closing)
    for attempt in 0..10 {
        match TcpListener::bind(addr).await {
            Ok(l) => return Ok(l),
            Err(e) if attempt < 9 => {
                tracing::debug!("callback port busy ({e}), again");
                tokio::time::sleep(Duration::from_millis(500)).await;
            }
            Err(e) => return Err(e).context("the sign-in callback port 127.0.0.1:8989 is taken"),
        }
    }
    unreachable!()
}

async fn wait_for_code(listener: TcpListener, state: &str) -> Result<(String, TcpStream)> {
    loop {
        let (mut stream, _) = listener.accept().await.context("sign-in callback")?;
        let target = match tokio::time::timeout(Duration::from_secs(10), read_target(&mut stream)).await {
            Ok(Ok(t)) => t,
            _ => continue,
        };
        match parse_redirect(&target, state) {
            Redirect::Code(code) => return Ok((code, stream)),
            Redirect::Denied(error) => {
                respond(&mut stream, "200 OK", &page("Not signed in to Spotify.")).await;
                bail!("Spotify didn't approve ({error})");
            }
            Redirect::OtherState => {
                tracing::info!("an answer to an older sign-in page: ignored");
                respond(&mut stream, "400 Bad Request", "An old sign-in page: use the newest one.").await;
            }
            Redirect::Stray => respond(&mut stream, "404 Not Found", "").await,
        }
    }
}

// The request line's target (GET <target> HTTP/1.1).
async fn read_target(stream: &mut TcpStream) -> Result<String> {
    let mut buf = Vec::with_capacity(1024);
    let mut chunk = [0u8; 1024];
    while !buf.windows(2).any(|w| w == b"\r\n") && buf.len() < 16 * 1024 {
        let n = stream.read(&mut chunk).await?;
        if n == 0 {
            break;
        }
        buf.extend_from_slice(&chunk[..n]);
    }
    let text = String::from_utf8_lossy(&buf);
    let line = text.lines().next().unwrap_or_default();
    line.split_whitespace()
        .nth(1)
        .map(str::to_string)
        .ok_or_else(|| anyhow!("not an HTTP request"))
}

async fn respond(stream: &mut TcpStream, status: &str, body: &str) {
    let kind = if body.starts_with("<!doctype") { "text/html" } else { "text/plain" };
    let response = format!(
        "HTTP/1.1 {status}\r\ncontent-type: {kind}; charset=utf-8\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{body}",
        body.len()
    );
    let _ = stream.write_all(response.as_bytes()).await;
    let _ = stream.shutdown().await;
}

/// An OAuth token as kept on disk (expires_at: epoch seconds).
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Token {
    pub access_token: String,
    #[serde(default)]
    pub refresh_token: String,
    pub expires_at: i64,
    #[serde(default)]
    pub scope: String,
}

impl Token {
    pub fn expired(&self) -> bool {
        // (a minute early: it must last the request)
        crate::state::now_ms() / 1000 + 60 >= self.expires_at
    }
}

#[derive(Deserialize)]
struct TokenResponse {
    access_token: String,
    #[serde(default)]
    refresh_token: Option<String>,
    #[serde(default)]
    expires_in: Option<i64>,
    #[serde(default)]
    scope: Option<String>,
}

pub async fn refresh(http: &reqwest::Client, client_id: &str, old: &Token) -> Result<Token> {
    if old.refresh_token.is_empty() {
        bail!("no refresh token");
    }
    let params = [
        ("grant_type", "refresh_token"),
        ("refresh_token", old.refresh_token.as_str()),
        ("client_id", client_id),
    ];
    token_request(http, &params, Some(old)).await
}

/// Raised when Spotify no longer takes a refresh token: a new approval is needed.
#[derive(Debug)]
pub struct InvalidGrant(pub String);

impl std::fmt::Display for InvalidGrant {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Spotify no longer accepts the sign-in ({})", self.0)
    }
}

impl std::error::Error for InvalidGrant {}

async fn token_request(http: &reqwest::Client, params: &[(&str, &str)], old: Option<&Token>) -> Result<Token> {
    let resp = http.post(TOKEN_URL).form(params).send().await.context("Spotify's token request")?;
    let status = resp.status();
    let body = resp.text().await.unwrap_or_default();
    if !status.is_success() {
        if status.as_u16() == 400 && body.contains("invalid_grant") {
            return Err(InvalidGrant(body).into());
        }
        bail!("Spotify's token request: {status}: {body}");
    }
    let r: TokenResponse = serde_json::from_str(&body).context("Spotify's token answer")?;
    Ok(Token {
        access_token: r.access_token,
        // (a refresh may come without a new refresh token: the old one stays good)
        refresh_token: r
            .refresh_token
            .or_else(|| old.map(|t| t.refresh_token.clone()))
            .unwrap_or_default(),
        expires_at: crate::state::now_ms() / 1000 + r.expires_in.unwrap_or(3600),
        scope: r.scope.or_else(|| old.map(|t| t.scope.clone())).unwrap_or_default(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pkce_matches_rfc7636() {
        let p = Pkce::from_verifier("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk".into());
        assert_eq!(p.challenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    }

    #[test]
    fn pkce_verifier_is_valid() {
        let p = Pkce::new();
        // 43..128 characters of [A-Za-z0-9-._~]
        assert!((43..=128).contains(&p.verifier.len()));
        assert!(p.verifier.chars().all(|c| c.is_ascii_alphanumeric() || "-._~".contains(c)));
    }

    #[test]
    fn authorize_url_has_everything() {
        let u = url::Url::parse(&authorize_url("cid", &["a", "b-c"], "chal", "st")).unwrap();
        let q: std::collections::HashMap<_, _> = u.query_pairs().into_owned().collect();
        assert_eq!(q["client_id"], "cid");
        assert_eq!(q["redirect_uri"], REDIRECT_URI);
        assert_eq!(q["scope"], "a b-c");
        assert_eq!(q["code_challenge"], "chal");
        assert_eq!(q["code_challenge_method"], "S256");
        assert_eq!(q["state"], "st");
        assert_eq!(q["response_type"], "code");
    }

    #[test]
    fn redirects() {
        assert_eq!(parse_redirect("/login?code=abc&state=s1", "s1"), Redirect::Code("abc".into()));
        assert_eq!(parse_redirect("/login?state=s1&code=a%2Fb", "s1"), Redirect::Code("a/b".into()));
        assert_eq!(parse_redirect("/login?code=abc&state=old", "s1"), Redirect::OtherState);
        assert_eq!(parse_redirect("/login?code=abc", "s1"), Redirect::OtherState);
        assert_eq!(
            parse_redirect("/login?error=access_denied&state=s1", "s1"),
            Redirect::Denied("access_denied".into())
        );
        assert_eq!(parse_redirect("/favicon.ico", "s1"), Redirect::Stray);
        assert_eq!(parse_redirect("/login", "s1"), Redirect::Stray);
        assert_eq!(parse_redirect("/other?code=abc&state=s1", "s1"), Redirect::Stray);
    }

    #[test]
    fn token_expiry() {
        let now = crate::state::now_ms() / 1000;
        let t = Token { access_token: "a".into(), refresh_token: "r".into(), expires_at: now + 3600, scope: "".into() };
        assert!(!t.expired());
        let t = Token { expires_at: now + 30, ..t };
        assert!(t.expired());
    }
}
