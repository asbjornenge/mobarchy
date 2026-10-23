// Spotify's Web API, for what librespot doesn't give: the library (playlists, liked songs,
// saved albums), search, and a context's tracks. With the owner's own client ID and its
// token (kept in webapi-token.json, refreshed when it runs out). Answers are normalised to
// a small JSON of Mobarchy's own (see the norm_* functions), so the app reads one shape.

use crate::auth::{self, Token};
use anyhow::{Context as _, Result, bail};
use futures_util::{StreamExt as _, stream};
use serde_json::{Value, json};
use std::path::PathBuf;
use std::time::Duration;
use tokio::sync::Mutex;

const API: &str = "https://api.spotify.com/v1/";
const PAGE: usize = 50;

pub struct WebApi {
    http: reqwest::Client,
    client_id: std::sync::Mutex<String>,
    token_path: PathBuf,
    token: Mutex<Option<Token>>,
}

impl WebApi {
    pub fn new(http: reqwest::Client, token_path: PathBuf) -> WebApi {
        let token = std::fs::read(&token_path)
            .ok()
            .and_then(|b| serde_json::from_slice::<Token>(&b).ok());
        WebApi { http, client_id: std::sync::Mutex::new(String::new()), token_path, token: Mutex::new(token) }
    }

    pub fn set_client_id(&self, id: &str) {
        *self.client_id.lock().unwrap() = id.to_string();
    }

    pub fn client_id(&self) -> String {
        self.client_id.lock().unwrap().clone()
    }

    pub async fn has_token(&self) -> bool {
        self.token.lock().await.is_some()
    }

    pub async fn set_token(&self, token: Token) -> Result<()> {
        write_private(&self.token_path, &serde_json::to_vec_pretty(&token)?)?;
        *self.token.lock().await = Some(token);
        Ok(())
    }

    pub async fn forget(&self) {
        *self.token.lock().await = None;
        let _ = std::fs::remove_file(&self.token_path);
    }

    // A valid access token: the kept one, refreshed when it ran out.
    async fn access_token(&self, force_refresh: bool) -> Result<String> {
        let mut guard = self.token.lock().await;
        let Some(token) = guard.as_ref() else { bail!("not signed in to Spotify's Web API") };
        if !force_refresh && !token.expired() {
            return Ok(token.access_token.clone());
        }
        tracing::info!("refreshing the Web API token");
        match auth::refresh(&self.http, &self.client_id(), token).await {
            Ok(new) => {
                write_private(&self.token_path, &serde_json::to_vec_pretty(&new)?)?;
                let access = new.access_token.clone();
                *guard = Some(new);
                Ok(access)
            }
            Err(e) => {
                if e.is::<auth::InvalidGrant>() {
                    // (a new approval is needed: SignIn asks for it)
                    *guard = None;
                    let _ = std::fs::remove_file(&self.token_path);
                }
                Err(e)
            }
        }
    }

    /// GET a path under /v1/ (or a full URL, e.g. a page's "next").
    pub async fn get(&self, path: &str) -> Result<Value> {
        let url = if path.starts_with("https://") { path.to_string() } else { format!("{API}{path}") };
        let mut refreshed = false;
        let mut waits = 0;
        loop {
            let token = self.access_token(false).await?;
            let resp = self.http.get(&url).bearer_auth(&token).send().await.with_context(|| format!("GET {path}"))?;
            let status = resp.status().as_u16();
            match status {
                200..=299 => return resp.json().await.with_context(|| format!("GET {path}: JSON")),
                401 if !refreshed => {
                    refreshed = true;
                    self.access_token(true).await?;
                }
                // Spotify's rate limit: wait as it says, a few times, not long
                429 if waits < 3 => {
                    let secs = resp
                        .headers()
                        .get("retry-after")
                        .and_then(|v| v.to_str().ok())
                        .and_then(|v| v.parse::<u64>().ok())
                        .unwrap_or(2);
                    if secs > 30 {
                        bail!("Spotify's rate limit: try again in {secs} s");
                    }
                    waits += 1;
                    tracing::info!("rate limited: {path} again in {secs} s");
                    tokio::time::sleep(Duration::from_secs(secs.max(1))).await;
                }
                _ => {
                    let body = resp.text().await.unwrap_or_default();
                    bail!("GET {path}: {status}: {}", body.chars().take(300).collect::<String>());
                }
            }
        }
    }

    // Every item of a paged list. The first page says how many there are; the rest are
    // fetched a few at a time (a big library is many pages), in order.
    async fn all_items(&self, path: &str) -> Result<Vec<Value>> {
        let sep = if path.contains('?') { '&' } else { '?' };
        let first = self.get(&format!("{path}{sep}limit={PAGE}&offset=0")).await?;
        let mut items = first["items"].as_array().cloned().unwrap_or_default();
        let total = first["total"].as_u64().unwrap_or(0) as usize;
        if first["next"].is_null() || total <= items.len() {
            return Ok(items);
        }
        let offsets: Vec<usize> = (PAGE..total).step_by(PAGE).collect();
        let pages: Vec<Result<Value>> = stream::iter(offsets)
            .map(|offset| self.get_owned(format!("{path}{sep}limit={PAGE}&offset={offset}")))
            .buffered(4)
            .collect()
            .await;
        for page in pages {
            items.extend(page?["items"].as_array().cloned().unwrap_or_default());
        }
        Ok(items)
    }

    async fn get_owned(&self, path: String) -> Result<Value> {
        self.get(&path).await
    }

    pub async fn playlists(&self) -> Result<Value> {
        let items = self.all_items("me/playlists").await?;
        Ok(Value::Array(items.iter().filter_map(norm_playlist).collect()))
    }

    pub async fn liked_tracks(&self) -> Result<Value> {
        let items = self.all_items("me/tracks").await?;
        Ok(Value::Array(items.iter().filter_map(|i| norm_track(item_of(i), None)).collect()))
    }

    pub async fn saved_albums(&self) -> Result<Value> {
        let items = self.all_items("me/albums").await?;
        Ok(Value::Array(items.iter().filter_map(|i| norm_album(&i["album"])).collect()))
    }

    pub async fn search(&self, query: &str) -> Result<Value> {
        let q: String = url::form_urlencoded::byte_serialize(query.as_bytes()).collect();
        // (10: the most a development-mode app may ask for)
        let r = self.get(&format!("search?q={q}&type=track,album,playlist,artist&limit=10")).await?;
        Ok(normalise_search(&r))
    }

    /// The tracks of a playlist, an album, an artist (their top tracks) or the liked songs.
    pub async fn context_tracks(&self, uri: &str) -> Result<Value> {
        let tracks: Vec<Value> = match context_kind(uri) {
            Some(("playlist", id)) => self
                .all_items(&format!("playlists/{id}/items"))
                .await?
                .iter()
                .filter_map(|i| norm_track(item_of(i), None))
                .collect(),
            Some(("album", id)) => {
                let album = self.get(&format!("albums/{id}")).await?;
                self.all_items(&format!("albums/{id}/tracks"))
                    .await?
                    .iter()
                    .filter_map(|t| norm_track(t, Some(&album)))
                    .collect()
            }
            Some(("artist", id)) => {
                let r = self.get(&format!("artists/{id}/top-tracks")).await?;
                r["tracks"].as_array().into_iter().flatten().filter_map(|t| norm_track(t, None)).collect()
            }
            Some(("collection", _)) => return self.liked_tracks().await,
            Some(("track", id)) => {
                let t = self.get(&format!("tracks/{id}")).await?;
                norm_track(&t, None).into_iter().collect()
            }
            _ => bail!("not a context Mobarchy knows: {uri}"),
        };
        Ok(Value::Array(tracks))
    }
}

/// (kind, id) of a context URI; the liked songs are ("collection", "").
pub fn context_kind(uri: &str) -> Option<(&str, &str)> {
    let parts: Vec<&str> = uri.split(':').collect();
    match parts.as_slice() {
        ["spotify", "collection", ..] => Some(("collection", "")),
        ["spotify", "user", _, "collection", ..] => Some(("collection", "")),
        ["spotify", "user", _, "playlist", id] => Some(("playlist", id)),
        ["spotify", kind, id] if !id.is_empty() => Some((kind, id)),
        _ => None,
    }
}

// A playlist item's track: "item" now, "track" before Spotify renamed it.
fn item_of(i: &Value) -> &Value {
    if i["item"].is_object() { &i["item"] } else { &i["track"] }
}

// The largest of Spotify's images (they come largest first, but not always with sizes).
pub fn largest_image(images: &Value) -> String {
    let Some(list) = images.as_array() else { return String::new() };
    list.iter()
        .filter(|i| i["width"].as_u64().is_some())
        .max_by_key(|i| i["width"].as_u64().unwrap_or(0) * i["height"].as_u64().unwrap_or(1))
        .or_else(|| list.first())
        .and_then(|i| i["url"].as_str())
        .unwrap_or_default()
        .to_string()
}

fn names(list: &Value) -> Vec<String> {
    list.as_array()
        .into_iter()
        .flatten()
        .filter_map(|a| a["name"].as_str().map(str::to_string))
        .collect()
}

fn s(v: &Value) -> &str {
    v.as_str().unwrap_or_default()
}

/// A track: {uri,id,name,artists,album,albumUri,image,durationMs}. `album` stands in for an
/// album's simplified tracks, which have none of their own. Local files and episodes: no.
pub fn norm_track(t: &Value, album: Option<&Value>) -> Option<Value> {
    if !t.is_object() || t["type"].as_str().is_some_and(|ty| ty != "track") || t["is_local"] == true {
        return None;
    }
    let uri = s(&t["uri"]);
    if uri.is_empty() {
        return None;
    }
    let al = if t["album"].is_object() { &t["album"] } else { album.unwrap_or(&Value::Null) };
    Some(json!({
        "uri": uri,
        "id": s(&t["id"]),
        "name": s(&t["name"]),
        "artists": names(&t["artists"]),
        "album": s(&al["name"]),
        "albumUri": s(&al["uri"]),
        "image": largest_image(&al["images"]),
        "durationMs": t["duration_ms"].as_u64().unwrap_or(0),
    }))
}

/// A playlist: {uri,id,name,owner,image}.
pub fn norm_playlist(p: &Value) -> Option<Value> {
    let uri = s(&p["uri"]);
    if uri.is_empty() {
        return None;
    }
    let owner = p["owner"]["display_name"].as_str().or(p["owner"]["id"].as_str()).unwrap_or_default();
    Some(json!({
        "uri": uri,
        "id": s(&p["id"]),
        "name": s(&p["name"]),
        "owner": owner,
        "image": largest_image(&p["images"]),
    }))
}

/// An album: {uri,id,name,artists,image}.
pub fn norm_album(a: &Value) -> Option<Value> {
    let uri = s(&a["uri"]);
    if uri.is_empty() {
        return None;
    }
    Some(json!({
        "uri": uri,
        "id": s(&a["id"]),
        "name": s(&a["name"]),
        "artists": names(&a["artists"]),
        "image": largest_image(&a["images"]),
    }))
}

/// An artist: {uri,id,name,image}.
pub fn norm_artist(a: &Value) -> Option<Value> {
    let uri = s(&a["uri"]);
    if uri.is_empty() {
        return None;
    }
    Some(json!({ "uri": uri, "id": s(&a["id"]), "name": s(&a["name"]), "image": largest_image(&a["images"]) }))
}

/// Search: {tracks,albums,playlists,artists}. (Spotify's lists may hold nulls.)
pub fn normalise_search(r: &Value) -> Value {
    let list = |key: &str, f: fn(&Value) -> Option<Value>| -> Value {
        Value::Array(r[key]["items"].as_array().into_iter().flatten().filter_map(f).collect())
    };
    json!({
        "tracks": list("tracks", |t| norm_track(t, None)),
        "albums": list("albums", norm_album),
        "playlists": list("playlists", norm_playlist),
        "artists": list("artists", norm_artist),
    })
}

// A file only the owner may read (tokens).
pub fn write_private(path: &std::path::Path, data: &[u8]) -> Result<()> {
    use std::io::Write as _;
    use std::os::unix::fs::OpenOptionsExt as _;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let tmp = path.with_extension("tmp");
    let mut f = std::fs::OpenOptions::new().write(true).create(true).truncate(true).mode(0o600).open(&tmp)?;
    f.write_all(data)?;
    f.sync_all()?;
    std::fs::rename(&tmp, path)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn album() -> Value {
        json!({
            "uri": "spotify:album:al1", "id": "al1", "name": "Album",
            "artists": [{"name": "A1"}],
            "images": [{"url": "small", "width": 64, "height": 64}, {"url": "big", "width": 640, "height": 640}]
        })
    }

    #[test]
    fn track_with_its_album() {
        let t = json!({"type": "track", "uri": "spotify:track:t1", "id": "t1", "name": "Song",
            "artists": [{"name": "A1"}, {"name": "A2"}], "duration_ms": 1234, "album": album()});
        assert_eq!(
            norm_track(&t, None).unwrap(),
            json!({"uri": "spotify:track:t1", "id": "t1", "name": "Song", "artists": ["A1", "A2"],
                   "album": "Album", "albumUri": "spotify:album:al1", "image": "big", "durationMs": 1234})
        );
    }

    #[test]
    fn simplified_track_gets_the_album() {
        let t = json!({"uri": "spotify:track:t2", "id": "t2", "name": "S2", "artists": [], "duration_ms": 5});
        let n = norm_track(&t, Some(&album())).unwrap();
        assert_eq!(n["album"], "Album");
        assert_eq!(n["image"], "big");
    }

    #[test]
    fn episodes_locals_and_nulls_are_left_out() {
        assert!(norm_track(&json!({"type": "episode", "uri": "spotify:episode:e"}), None).is_none());
        assert!(norm_track(&json!({"uri": "spotify:local:x", "is_local": true}), None).is_none());
        assert!(norm_track(&Value::Null, None).is_none());
    }

    #[test]
    fn playlist_items_old_and_new() {
        let t = json!({"type": "track", "uri": "spotify:track:t", "id": "t"});
        assert_eq!(item_of(&json!({"item": t.clone()}))["id"], "t");
        assert_eq!(item_of(&json!({"track": t}))["id"], "t");
    }

    #[test]
    fn playlist() {
        let p = json!({"uri": "spotify:playlist:p", "id": "p", "name": "P", "owner": {"id": "o", "display_name": "Owner"},
            "images": [{"url": "u", "width": null, "height": null}]});
        assert_eq!(norm_playlist(&p).unwrap(),
            json!({"uri": "spotify:playlist:p", "id": "p", "name": "P", "owner": "Owner", "image": "u"}));
        let p = json!({"uri": "spotify:playlist:p", "owner": {"id": "o"}, "images": null});
        assert_eq!(norm_playlist(&p).unwrap()["owner"], "o");
        assert_eq!(norm_playlist(&p).unwrap()["image"], "");
    }

    #[test]
    fn search_with_nulls() {
        let r = json!({
            "tracks": {"items": [{"type": "track", "uri": "spotify:track:t", "id": "t", "album": album()}]},
            "albums": {"items": [album()]},
            "playlists": {"items": [null, {"uri": "spotify:playlist:p", "name": "P"}]},
            "artists": {"items": [{"uri": "spotify:artist:a", "id": "a", "name": "Ar", "images": []}]}
        });
        let n = normalise_search(&r);
        assert_eq!(n["tracks"].as_array().unwrap().len(), 1);
        assert_eq!(n["albums"][0]["artists"], json!(["A1"]));
        assert_eq!(n["playlists"].as_array().unwrap().len(), 1);
        assert_eq!(n["artists"][0], json!({"uri": "spotify:artist:a", "id": "a", "name": "Ar", "image": ""}));
    }

    #[test]
    fn context_kinds() {
        assert_eq!(context_kind("spotify:playlist:abc"), Some(("playlist", "abc")));
        assert_eq!(context_kind("spotify:album:x"), Some(("album", "x")));
        assert_eq!(context_kind("spotify:user:bob:collection"), Some(("collection", "")));
        assert_eq!(context_kind("spotify:collection:tracks"), Some(("collection", "")));
        assert_eq!(context_kind("spotify:user:bob:playlist:p1"), Some(("playlist", "p1")));
        assert_eq!(context_kind("nonsense"), None);
    }
}
