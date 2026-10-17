"""The owner's internet accounts (ADR 0032): their contacts from iCloud, Google and other CardDAV
servers, read-only, kept on the phone beside the owner's own.

  ~/.config/mobarchy/accounts.json        the accounts and which address books are on
  the keyring (secret-tool)               passwords, OAuth refresh tokens, the Google client
  ~/.local/share/mobarchy/contacts/<id>.vcf   each account's contacts (the enabled books)
  ~/.cache/mobarchy/carddav/<id>.json     etags and cards, to fetch only what changed

Sign-in:
- Google: OAuth in the phone's browser (installed-app flow, loopback redirect, PKCE), with
  Mobarchy's own OAuth client (in the package, /usr/share/mobarchy/accounts, put there when the
  package is built: it is not in the source). An owner may use their own client instead
  (`google-client`).
- Nextcloud: its Login Flow v2 in the browser, which gives the phone an app password.
- iCloud: Apple has no sign-in for others' apps; an app-specific password (made at
  account.apple.com), checked against the server before it is kept.
- Other CardDAV servers: a user name and a password, checked the same way.

Nothing is ever written to the servers. `mobarchy-accounts`, the command, prints JSON."""

from __future__ import annotations

import argparse
import base64
import contextlib
import fcntl
import hashlib
import http.server
import json
import os
import re
import secrets
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from . import carddav, contacts

ICLOUD = "https://contacts.icloud.com/"
GOOGLE_DAV = "https://www.googleapis.com/.well-known/carddav"
GOOGLE_AUTH = "https://accounts.google.com/o/oauth2/v2/auth"
GOOGLE_TOKEN = "https://oauth2.googleapis.com/token"
GOOGLE_SCOPES = "https://www.googleapis.com/auth/carddav openid email"
KEYRING = "mobarchy-accounts"
# Mobarchy's Google OAuth client: {"client_id", "client_secret"}. A "Desktop app" client's
# secret is not a secret in Google's sense (every copy of the app has it), but it stays out
# of the source all the same: the package build puts it here.
SYSTEM_GOOGLE_CLIENT = "/usr/share/mobarchy/accounts/google-client.json"


class AccountError(Exception):
    pass


# --- where things are -----------------------------------------------------------------------

def _xdg(var: str, default: str) -> Path:
    return Path(os.environ.get(var, Path.home() / default))


def config_path() -> Path:
    return _xdg("XDG_CONFIG_HOME", ".config") / "mobarchy/accounts.json"


def contacts_dir() -> Path:
    return _xdg("XDG_DATA_HOME", ".local/share") / "mobarchy/contacts"


def cache_dir() -> Path:
    return _xdg("XDG_CACHE_HOME", ".cache") / "mobarchy/carddav"


def load_config() -> dict:
    try:
        return json.loads(config_path().read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {"accounts": []}


def save_config(cfg: dict) -> None:
    p = config_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".tmp")
    tmp.write_text(json.dumps(cfg, indent=2, ensure_ascii=False))
    os.replace(tmp, p)


@contextlib.contextmanager
def locked():
    """One change or sync at a time (the timer and the app may both start one)."""
    p = config_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    with open(f"{p}.lock", "w") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        yield


# --- the keyring ----------------------------------------------------------------------------

def secret_store(key: str, value: str, label: str) -> None:
    r = subprocess.run(["secret-tool", "store", f"--label={label}", "service", KEYRING, "key", key],
                       input=value, text=True, capture_output=True, timeout=30)
    if r.returncode != 0:
        raise AccountError(f"the keyring did not take it: {r.stderr.strip() or 'locked?'}")


def secret_get(key: str) -> str:
    r = subprocess.run(["secret-tool", "lookup", "service", KEYRING, "key", key],
                       text=True, capture_output=True, timeout=30)
    if r.returncode != 0 or not r.stdout:
        raise AccountError(f"no secret for {key} in the keyring (sign in again)")
    return r.stdout


def secret_clear(key: str) -> None:
    subprocess.run(["secret-tool", "clear", "service", KEYRING, "key", key],
                   capture_output=True, timeout=30)


# --- authentication -------------------------------------------------------------------------

def basic(user: str, password: str) -> str:
    return "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()


def _post(url: str, form: dict) -> dict:
    req = urllib.request.Request(url, data=urllib.parse.urlencode(form).encode(), method="POST",
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        try:
            detail = json.loads(e.read()).get("error_description") or e.reason
        except (ValueError, AttributeError):
            detail = e.reason
        raise AccountError(f"Google said no: {detail}") from e
    except urllib.error.URLError as e:
        raise AccountError(f"cannot reach Google: {e.reason}") from e


def _system_google_client() -> dict:
    try:
        c = json.loads(Path(os.environ.get("MOBARCHY_GOOGLE_CLIENT", SYSTEM_GOOGLE_CLIENT)).read_text())
    except (OSError, ValueError):
        return {}
    return c if c.get("client_id") and c.get("client_secret") else {}


def google_client() -> tuple[str, str]:
    """The owner's own client if they set one, else Mobarchy's."""
    own = load_config().get("google", {})
    if own.get("client_id"):
        return own["client_id"], secret_get("google-client-secret")
    system = _system_google_client()
    if system:
        return system["client_id"], system["client_secret"]
    raise AccountError("this build has no Google client: set one up in Google Cloud (a Desktop app "
                       "OAuth client) and give its id and secret (mobarchy-accounts google-client)")


class _Bearer:
    """An access token from the refresh token, renewed when it runs out."""

    def __init__(self, account: dict):
        self.account = account
        self.token, self.expires = "", 0.0

    def __call__(self) -> str:
        if time.time() > self.expires - 60:
            cid, csecret = google_client()
            r = _post(GOOGLE_TOKEN, {"client_id": cid, "client_secret": csecret, "grant_type": "refresh_token",
                                     "refresh_token": secret_get(self.account["id"])})
            self.token, self.expires = r["access_token"], time.time() + int(r.get("expires_in", 3600))
        return "Bearer " + self.token


def auth_for(account: dict):
    if account["type"] == "google":
        return _Bearer(account)
    password = secret_get(account["id"])
    header = basic(account["user"], password)
    return lambda: header


def base_for(account: dict) -> str:
    return {"icloud": ICLOUD, "google": GOOGLE_DAV}.get(account["type"], account.get("url", ""))


# --- Google sign-in in the browser ----------------------------------------------------------

def _open_browser(url: str) -> None:
    for cmd in (["mobarchy-open", url], ["xdg-open", url]):
        try:
            subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return
        except FileNotFoundError:
            continue


def google_sign_in(open_browser=_open_browser, timeout: float = 300) -> dict:
    """The installed-app flow: the browser goes to Google, Google comes back to a one-off server
    on 127.0.0.1 with a code; the code (and the PKCE verifier) buy a refresh token."""
    cid, csecret = google_client()
    verifier = secrets.token_urlsafe(64)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
    state = secrets.token_urlsafe(16)
    got: dict = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            q = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
            if q.get("state", [""])[0] == state:
                got.update({k: v[0] for k, v in q.items()})
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            msg = "Signed in. You can close this tab." if "code" in got else "Sign-in did not finish."
            self.wfile.write(f"<html><body style='font-family:sans-serif;padding:3em'><h2>{msg}</h2>"
                             f"</body></html>".encode())

        def log_message(self, *a):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    server.timeout = 1
    redirect = f"http://127.0.0.1:{server.server_port}"
    url = GOOGLE_AUTH + "?" + urllib.parse.urlencode({
        "client_id": cid, "redirect_uri": redirect, "response_type": "code", "scope": GOOGLE_SCOPES,
        "code_challenge": challenge, "code_challenge_method": "S256", "state": state,
        "access_type": "offline", "prompt": "consent select_account"})
    print(json.dumps({"open": url}), flush=True)
    open_browser(url)
    deadline = time.time() + timeout
    while "code" not in got and "error" not in got and time.time() < deadline:
        server.handle_request()
    server.server_close()
    if "code" not in got:
        raise AccountError(f"Google sign-in did not finish ({got.get('error', 'no answer in time')})")
    tokens = _post(GOOGLE_TOKEN, {"client_id": cid, "client_secret": csecret, "code": got["code"],
                                  "code_verifier": verifier, "grant_type": "authorization_code",
                                  "redirect_uri": redirect})
    if "refresh_token" not in tokens:
        raise AccountError("Google gave no refresh token (remove the app's access in the Google "
                           "account and sign in again)")
    email = _jwt_email(tokens.get("id_token", ""))
    return {"email": email, "refresh_token": tokens["refresh_token"]}


def _jwt_email(token: str) -> str:
    try:
        payload = token.split(".")[1]
        payload += "=" * (-len(payload) % 4)
        return json.loads(base64.urlsafe_b64decode(payload)).get("email", "")
    except (IndexError, ValueError):
        return ""


# --- the accounts ---------------------------------------------------------------------------

def _summary(a: dict) -> dict:
    return {"id": a["id"], "type": a["type"], "user": a["user"], "last_sync": a.get("last_sync"),
            "error": a.get("error", ""), "contacts": a.get("count", 0),
            "books": [{"url": u, "name": b["name"], "enabled": b["enabled"]} for u, b in a.get("books", {}).items()]}


def list_accounts() -> dict:
    cfg = load_config()
    return {"google_client": bool(cfg.get("google", {}).get("client_id") or _system_google_client()),
            "accounts": [_summary(a) for a in cfg.get("accounts", [])]}


def set_google_client(client_id: str, client_secret: str) -> dict:
    client_id = client_id.strip()
    if not client_id.endswith(".apps.googleusercontent.com"):
        raise AccountError("that does not look like a Google OAuth client id (…apps.googleusercontent.com)")
    secret_store("google-client-secret", client_secret.strip(), "Mobarchy: Google OAuth client")
    with locked():
        cfg = load_config()
        cfg["google"] = {"client_id": client_id}
        save_config(cfg)
    return {"google_client": True}


def _add(account: dict, secret: str) -> dict:
    """Keep the secret, find the address books (all on), then fetch the contacts."""
    with locked():
        cfg = load_config()
        if any(a["id"] == account["id"] for a in cfg["accounts"]):
            raise AccountError(f"{account['user']} is already here")
        secret_store(account["id"], secret, f"Mobarchy: {account['type']} {account['user']}")
        try:
            books = carddav.Client(auth_for(account)).discover(base_for(account))
        except carddav.CardDAVError as e:
            secret_clear(account["id"])
            raise AccountError(str(e)) from e
        account["books"] = {b.url: {"name": b.name, "enabled": True} for b in books}
        cfg["accounts"].append(account)
        save_config(cfg)
    return sync(account["id"])


def _id(kind: str, user: str) -> str:
    return f"{kind}-" + re.sub(r"[^a-z0-9]+", "-", user.lower()).strip("-")


def add_icloud(apple_id: str, app_password: str) -> dict:
    apple_id = apple_id.strip()
    pw = re.sub(r"\s+", "", app_password)  # as copied: "abcd-efgh-ijkl-mnop", spaces or not
    if not apple_id or not pw:
        raise AccountError("give the Apple ID and an app-specific password (appleid.apple.com)")
    return _add({"id": _id("icloud", apple_id), "type": "icloud", "user": apple_id}, pw)


def add_carddav(url: str, user: str, password: str) -> dict:
    if not url.startswith("https://"):
        raise AccountError("the server's address must start with https://")
    return _add({"id": _id("carddav", f"{user}@{urllib.parse.urlsplit(url).netloc}"), "type": "carddav",
                 "user": user.strip(), "url": url.strip()}, password)


# --- Nextcloud: Login Flow v2 ---------------------------------------------------------------

def _nc_post(url: str, form: dict | None = None) -> tuple[int, dict]:
    req = urllib.request.Request(url, data=urllib.parse.urlencode(form or {}).encode(), method="POST",
                                 headers={"User-Agent": "Mobarchy", "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, {}
    except (urllib.error.URLError, ValueError) as e:
        raise AccountError(f"cannot reach {urllib.parse.urlsplit(url).netloc}: {getattr(e, 'reason', e)}") from e


def nextcloud_sign_in(url: str, open_browser=_open_browser, timeout: float = 600,
                      interval: float = 2) -> dict:
    """Login Flow v2: the server gives a login page and a poll token; the owner signs in (and
    allows the phone) in the browser, and the poll then answers with an app password that is
    the phone's alone (it shows, and can be revoked, in Nextcloud's security settings)."""
    url = url.strip().rstrip("/")
    if not url.startswith("http"):
        url = "https://" + url
    if not url.startswith("https://") and urllib.parse.urlsplit(url).hostname not in ("127.0.0.1", "localhost"):
        raise AccountError("the server's address must start with https://")
    status, start = _nc_post(url + "/index.php/login/v2")
    if status != 200 or "login" not in start:
        raise AccountError(f"{urllib.parse.urlsplit(url).netloc} does not look like a Nextcloud server")
    print(json.dumps({"open": start["login"]}), flush=True)
    open_browser(start["login"])
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, got = _nc_post(start["poll"]["endpoint"], {"token": start["poll"]["token"]})
        if status == 200 and got.get("appPassword"):
            return {"server": got["server"].rstrip("/"), "user": got["loginName"], "password": got["appPassword"]}
        time.sleep(interval)
    raise AccountError("Nextcloud sign-in did not finish in time")


def add_nextcloud(url: str, **kwargs) -> dict:
    got = nextcloud_sign_in(url, **kwargs)
    host = urllib.parse.urlsplit(got["server"]).netloc
    return _add({"id": _id("nextcloud", f"{got['user']}@{host}"), "type": "nextcloud", "user": got["user"],
                 "url": got["server"] + "/remote.php/dav"}, got["password"])


def add_google(**kwargs) -> dict:
    got = google_sign_in(**kwargs)
    email = got["email"] or "google"
    return _add({"id": _id("google", email), "type": "google", "user": email}, got["refresh_token"])


def remove(account_id: str) -> dict:
    with locked():
        cfg = load_config()
        account = next((a for a in cfg["accounts"] if a["id"] == account_id), None)
        if account is None:
            raise AccountError(f"no account {account_id}")
        cfg["accounts"].remove(account)
        save_config(cfg)
        secret_clear(account_id)
        (contacts_dir() / f"{account_id}.vcf").unlink(missing_ok=True)
        (cache_dir() / f"{account_id}.json").unlink(missing_ok=True)
    return {"removed": account_id}


def set_book(account_id: str, url: str, enabled: bool) -> dict:
    with locked():
        cfg = load_config()
        account = next((a for a in cfg["accounts"] if a["id"] == account_id), None)
        if account is None or url not in account.get("books", {}):
            raise AccountError("no such address book")
        account["books"][url]["enabled"] = enabled
        save_config(cfg)
        _write_contacts(account)
        save_config(cfg)
    return _summary(account)


# --- sync -----------------------------------------------------------------------------------

def _cache(account_id: str) -> dict:
    try:
        return json.loads((cache_dir() / f"{account_id}.json").read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


PHOTO = re.compile(r"^(?:[^:;]*\.)?PHOTO[;:]", re.IGNORECASE)


def _strip_photos(card: str) -> str:
    """Photos are most of a card's bytes and nothing here shows them yet."""
    lines = contacts.unfold(card)
    return "\r\n".join(line for line in lines if not PHOTO.match(line)) + "\r\n"


def _write_contacts(account: dict) -> None:
    """The account's contacts file: the cards of its enabled books."""
    cache = _cache(account["id"])
    cards = [c["card"] for url, book in sorted(cache.items())
             if account.get("books", {}).get(url, {}).get("enabled") for c in book.values()]
    d = contacts_dir()
    d.mkdir(parents=True, exist_ok=True)
    tmp = d / f".{account['id']}.vcf.tmp"
    tmp.write_text("".join(cards), encoding="utf-8")
    os.replace(tmp, d / f"{account['id']}.vcf")
    account["count"] = len(cards)


def _sync_account(account: dict) -> None:
    client = carddav.Client(auth_for(account))
    cache = _cache(account["id"])
    for url, book in account.get("books", {}).items():
        if not book["enabled"]:
            continue
        have = cache.get(url, {})
        listed = client.list_cards(url)
        changed = [h for h, etag in listed.items() if have.get(h, {}).get("etag") != etag]
        fetched = client.fetch(url, changed) if changed else {}
        cache[url] = {h: ({"etag": etag, "card": _strip_photos(fetched[h][1])} if h in fetched else have[h])
                      for h, etag in listed.items() if h in fetched or h in have}
    cache_dir().mkdir(parents=True, exist_ok=True)
    tmp = cache_dir() / f".{account['id']}.json.tmp"
    tmp.write_text(json.dumps(cache, ensure_ascii=False))
    os.replace(tmp, cache_dir() / f"{account['id']}.json")
    _write_contacts(account)


def sync(account_id: str = "") -> dict:
    """Fetch what changed, for one account or all; an account that fails keeps its last
    contacts and says why."""
    with locked():
        cfg = load_config()
        for account in cfg["accounts"]:
            if account_id and account["id"] != account_id:
                continue
            try:
                _sync_account(account)
                account["error"] = ""
            except (carddav.CardDAVError, AccountError) as e:
                account["error"] = str(e)
            account["last_sync"] = int(time.time())
        save_config(cfg)
    return list_accounts()


# --- the command ----------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-accounts", description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    g = sub.add_parser("google-client", help="the owner's Google OAuth client (secret on stdin)")
    g.add_argument("client_id")
    sub.add_parser("add-google")
    n = sub.add_parser("add-nextcloud", help="signing in in the browser (Login Flow v2)")
    n.add_argument("url")
    i = sub.add_parser("add-icloud", help="the app-specific password on stdin")
    i.add_argument("apple_id")
    c = sub.add_parser("add-carddav", help="the password on stdin")
    c.add_argument("url")
    c.add_argument("user")
    r = sub.add_parser("remove")
    r.add_argument("id")
    b = sub.add_parser("book")
    b.add_argument("id")
    b.add_argument("url")
    b.add_argument("state", choices=["on", "off"])
    s = sub.add_parser("sync")
    s.add_argument("id", nargs="?", default="")
    a = p.parse_args(argv)
    try:
        if a.cmd == "list":
            out = list_accounts()
        elif a.cmd == "google-client":
            out = set_google_client(a.client_id, sys.stdin.readline())
        elif a.cmd == "add-google":
            out = add_google()
        elif a.cmd == "add-nextcloud":
            out = add_nextcloud(a.url)
        elif a.cmd == "add-icloud":
            out = add_icloud(a.apple_id, sys.stdin.readline())
        elif a.cmd == "add-carddav":
            out = add_carddav(a.url, a.user, sys.stdin.readline())
        elif a.cmd == "remove":
            out = remove(a.id)
        elif a.cmd == "book":
            out = set_book(a.id, a.url, a.state == "on")
        else:
            out = sync(a.id)
    except AccountError as e:
        print(json.dumps({"error": str(e)}), flush=True)
        return 1
    print(json.dumps(out, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
