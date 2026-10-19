"""The owner's email (ADR 0038): the accounts' (ADR 0032) mailboxes over IMAP and SMTP,
kept on the phone for the Mail app, the home board and Frigg.

  ~/.cache/mobarchy/mail/mail.db     the inbox's and the snoozed messages' headers and a
                                     snippet (SQLite), what was snoozed until when

- Accounts: Google (OAuth, XOAUTH2: the sign-in asks for mail too) and iCloud (the
  app-specific password); the servers are known by the account's type.
- The inbox in three: people, notifications (no-reply senders, automatic mail) and
  newsletters (mailing lists, bulk), from the headers (`category`).
- Archive: out of the inbox, into the account's archive (Gmail's All Mail).
- Snooze: into a "Snoozed" folder on the server, and back in the inbox (at the top) when
  its time comes; `watch` and every sync bring back what is due.
- `watch`: the inbox followed (IMAP IDLE), each account in a thread of its own; a new
  message is synced and shown as a notification. The user service mobarchy-mail runs it.

`mobarchy-mail`, the command, prints JSON."""

from __future__ import annotations

import argparse
import base64
import contextlib
import email
import email.header
import email.policy
import email.utils
import html
import imaplib
import json
import os
import re
import smtplib
import sqlite3
import subprocess
import sys
import threading
import time
from email.message import EmailMessage
from pathlib import Path

from . import accounts

SERVERS = {     # account type: (IMAP, SMTP), both TLS (993; 587 with STARTTLS)
    "google": ("imap.gmail.com", "smtp.gmail.com"),
    "icloud": ("imap.mail.me.com", "smtp.mail.me.com"),
}
GOOGLE_MAIL_SCOPE = accounts.GOOGLE_MAIL_SCOPE
SNOOZED = "Snoozed"
KEEP_DAYS = 45          # the inbox, as far back as this (older ones are fetched when asked)
FIRST_FETCH = 300       # at most this many on a first sync
HEADERS = ("FROM TO CC REPLY-TO SUBJECT DATE MESSAGE-ID IN-REPLY-TO REFERENCES LIST-UNSUBSCRIBE LIST-ID "
           "PRECEDENCE AUTO-SUBMITTED X-AUTO-RESPONSE-SUPPRESS")
IDLE_S = 9 * 60         # IDLE is renewed before servers drop it (29 min, some 10)


class MailError(Exception):
    pass


def _xdg(var: str, default: str) -> Path:
    return Path(os.environ.get(var, Path.home() / default))


def db_path() -> Path:
    return _xdg("XDG_CACHE_HOME", ".cache") / "mobarchy/mail/mail.db"


def db() -> sqlite3.Connection:
    p = db_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    c = sqlite3.connect(p, timeout=30)
    c.row_factory = sqlite3.Row
    c.executescript("""
        CREATE TABLE IF NOT EXISTS messages (
            account TEXT, folder TEXT, uid INTEGER, msgid TEXT,
            from_name TEXT, from_addr TEXT, to_addrs TEXT, subject TEXT, date INTEGER,
            snippet TEXT, seen INTEGER, flagged INTEGER, category TEXT, attachments INTEGER,
            PRIMARY KEY (account, folder, uid));
        CREATE TABLE IF NOT EXISTS folders (account TEXT, folder TEXT, validity INTEGER,
            PRIMARY KEY (account, folder));
        CREATE TABLE IF NOT EXISTS snoozes (account TEXT, msgid TEXT, until INTEGER, subject TEXT,
            PRIMARY KEY (account, msgid));
        CREATE TABLE IF NOT EXISTS back (account TEXT, msgid TEXT, at INTEGER,
            PRIMARY KEY (account, msgid));
    """)
    return c


# --- what a message is ------------------------------------------------------------------------

NOREPLY = re.compile(r"(^|[._-])(no-?reply|do-?not-?reply|notifications?|notify|alerts?|mailer-daemon|"
                     r"bounces?|automated|robot)([._+-]|@)", re.I)


def category(headers: email.message.Message) -> str:
    """people, notifications or newsletters, from the headers alone."""
    if headers.get("List-Unsubscribe") or headers.get("List-Id") \
            or (headers.get("Precedence") or "").lower() in ("bulk", "list"):
        return "newsletters"
    sender = email.utils.parseaddr(headers.get("From", ""))[1]
    if (headers.get("Auto-Submitted") or "no").lower() != "no" or headers.get("X-Auto-Response-Suppress") \
            or NOREPLY.search(sender) or (headers.get("Precedence") or "").lower() == "junk":
        return "notifications"
    return "people"


def text_of(html_text: str) -> str:
    """Readable text from an HTML part: no styles or scripts, block ends as new lines."""
    t = re.sub(r"(?is)<(style|script|head)[^>]*>.*?</\1>", " ", html_text)
    t = re.sub(r"(?i)<br\s*/?>|</(p|div|tr|li|h[1-6])>", "\n", t)
    t = re.sub(r"<[^>]+>", " ", t)
    t = html.unescape(t)
    t = re.sub(r"[ \t\r\f\v\u00a0]+", " ", t)
    t = re.sub(r" *\n *", "\n", t)
    return re.sub(r"\n{3,}", "\n\n", t).strip()


def body_of(msg: email.message.EmailMessage) -> tuple[str, str, list[dict]]:
    """(the text, the HTML if any, the attachments: [{index, name, type, size}])."""
    plain = rich = ""
    files = []
    for i, part in enumerate(msg.walk()):
        if part.is_multipart():
            continue
        disposition = part.get_content_disposition()
        name = part.get_filename()
        if disposition == "attachment" or (name and disposition != "inline"):
            payload = part.get_payload(decode=True) or b""
            files.append({"index": i, "name": name or "attachment", "type": part.get_content_type(),
                          "size": len(payload)})
            continue
        try:
            content = part.get_content()
        except (LookupError, ValueError):
            content = (part.get_payload(decode=True) or b"").decode("utf-8", "replace")
        if part.get_content_type() == "text/plain" and not plain:
            plain = content
        elif part.get_content_type() == "text/html" and not rich:
            rich = content
    return (plain.strip() or text_of(rich)), rich, files


def snippet_of(text: str) -> str:
    lines = [ln for ln in text.splitlines() if ln.strip() and not ln.lstrip().startswith(">")]
    return re.sub(r"\s+", " ", " ".join(lines))[:200]


def decoded(value: str) -> str:
    try:
        return str(email.header.make_header(email.header.decode_header(value or "")))
    except (ValueError, LookupError):
        return value or ""


# --- the servers -----------------------------------------------------------------------------

def mail_accounts() -> list[dict]:
    """The accounts with mail: Google's signed in with mail, iCloud's."""
    out = []
    for a in accounts.load_config().get("accounts", []):
        if a["type"] == "google" and GOOGLE_MAIL_SCOPE in a.get("scope", "").split():
            out.append(a)
        elif a["type"] == "icloud":
            out.append(a)
    return out


def account_by_id(account_id: str) -> dict:
    for a in mail_accounts():
        if a["id"] == account_id:
            return a
    raise MailError(f"no mail account {account_id}")


def _xoauth2(account: dict) -> bytes:
    token = accounts.auth_for(account)().split(" ", 1)[1]
    return f"user={account['user']}\x01auth=Bearer {token}\x01\x01".encode()


def imap(account: dict) -> imaplib.IMAP4_SSL:
    host = SERVERS[account["type"]][0]
    try:
        m = imaplib.IMAP4_SSL(host, timeout=60)
        if account["type"] == "google":
            m.authenticate("XOAUTH2", lambda _: _xoauth2(account))
        else:
            m.login(account["user"], accounts.secret_get(account["id"]))
    except (imaplib.IMAP4.error, OSError) as e:
        raise MailError(f"{host}: {e}") from e
    except accounts.AccountError as e:
        raise MailError(str(e)) from e
    return m


def _ok(r, what: str):
    typ, data = r
    if typ != "OK":
        raise MailError(f"{what}: {data[0].decode(errors='replace') if data and data[0] else typ}")
    return data


def _quote(folder: str) -> str:
    return '"' + folder.replace("\\", "\\\\").replace('"', '\\"') + '"'


def special_folder(m: imaplib.IMAP4_SSL, flag: str) -> str | None:
    """The folder with a special use (RFC 6154: \\Archive, \\All, \\Trash, \\Sent)."""
    for line in _ok(m.list(), "LIST"):
        text = line.decode(errors="replace") if isinstance(line, bytes) else ""
        found = re.match(r'\((?P<flags>[^)]*)\) "(?P<sep>[^"]*)" (?P<name>.+)$', text)
        if found and flag.lower() in found["flags"].lower():
            name = found["name"].strip()
            return name[1:-1] if name.startswith('"') else name
    return None


def ensure_folder(m: imaplib.IMAP4_SSL, folder: str) -> None:
    typ, _ = m.select(_quote(folder), readonly=True)
    if typ != "OK":
        m.create(_quote(folder))


def _move(m: imaplib.IMAP4_SSL, uid: int, to: str) -> None:
    typ, data = m.uid("MOVE", str(uid), _quote(to))
    if typ != "OK":     # (no MOVE: copy, then away)
        _ok(m.uid("COPY", str(uid), _quote(to)), "COPY")
        _ok(m.uid("STORE", str(uid), "+FLAGS.SILENT", "(\\Deleted)"), "STORE")
        m.expunge()


# --- sync ------------------------------------------------------------------------------------

def _row(account_id: str, folder: str, uid: int, flags: str, raw: bytes) -> dict:
    msg = email.message_from_bytes(raw, policy=email.policy.default)
    name, addr = email.utils.parseaddr(decoded(msg.get("From", "")))
    try:
        date = int(email.utils.parsedate_to_datetime(msg.get("Date")).timestamp())
    except (TypeError, ValueError):
        date = int(time.time())
    text, _, files = body_of(msg)
    return {"account": account_id, "folder": folder, "uid": uid, "msgid": (msg.get("Message-ID") or "").strip(),
            "from_name": name or addr, "from_addr": addr, "to_addrs": decoded(msg.get("To", "")),
            "subject": decoded(msg.get("Subject", "")) or "(no subject)", "date": date,
            "snippet": snippet_of(text), "seen": int("\\Seen" in flags), "flagged": int("\\Flagged" in flags),
            "category": category(msg), "attachments": len(files)}


def sync_folder(m: imaplib.IMAP4_SSL, c: sqlite3.Connection, account_id: str, folder: str) -> int:
    """The folder's last KEEP_DAYS into the database; what is gone from it, out. New ones."""
    data = _ok(m.select(_quote(folder), readonly=True), f"SELECT {folder}")
    validity = int((m.response("UIDVALIDITY")[1] or [b"0"])[0] or 0)
    old = c.execute("SELECT validity FROM folders WHERE account=? AND folder=?", (account_id, folder)).fetchone()
    if old and old["validity"] != validity:     # (the server numbered it anew)
        c.execute("DELETE FROM messages WHERE account=? AND folder=?", (account_id, folder))
    c.execute("INSERT OR REPLACE INTO folders VALUES (?,?,?)", (account_id, folder, validity))
    if int(data[0] or 0) == 0:
        c.execute("DELETE FROM messages WHERE account=? AND folder=?", (account_id, folder))
        return 0
    since = time.strftime("%d-%b-%Y", time.gmtime(time.time() - KEEP_DAYS * 86400))
    uids = [int(u) for u in _ok(m.uid("SEARCH", None, "SINCE", since), "SEARCH")[0].split()]
    have = {r["uid"] for r in c.execute("SELECT uid FROM messages WHERE account=? AND folder=?", (account_id, folder))}
    gone = have - set(uids)
    c.executemany("DELETE FROM messages WHERE account=? AND folder=? AND uid=?",
                  [(account_id, folder, u) for u in gone])
    new = sorted(set(uids) - have)[-FIRST_FETCH:]
    for i in range(0, len(new), 25):
        chunk = ",".join(str(u) for u in new[i:i + 25])
        # the headers and the start of the message (enough for a snippet), not marked as read
        resp = _ok(m.uid("FETCH", chunk, "(UID FLAGS BODY.PEEK[]<0.65536>)"), "FETCH")
        for item in resp:
            if not isinstance(item, tuple):
                continue
            meta = item[0].decode(errors="replace")
            uid = int(re.search(r"UID (\d+)", meta).group(1))
            flags = (re.search(r"FLAGS \(([^)]*)\)", meta) or [None, ""])[1]
            c.execute("INSERT OR REPLACE INTO messages VALUES (:account,:folder,:uid,:msgid,:from_name,:from_addr,"
                      ":to_addrs,:subject,:date,:snippet,:seen,:flagged,:category,:attachments)",
                      _row(account_id, folder, uid, flags, item[1]))
    # the flags of the rest (read elsewhere, flagged)
    known = sorted(set(uids) & have)
    for i in range(0, len(known), 500):
        for item in _ok(m.uid("FETCH", ",".join(str(u) for u in known[i:i + 500]), "(UID FLAGS)"), "FETCH"):
            meta = item.decode(errors="replace") if isinstance(item, bytes) else ""
            uid, flags = re.search(r"UID (\d+)", meta), re.search(r"FLAGS \(([^)]*)\)", meta)
            if not uid or not flags:
                continue
            uid, flags = int(uid.group(1)), flags.group(1)
            c.execute("UPDATE messages SET seen=?, flagged=? WHERE account=? AND folder=? AND uid=?",
                      (int("\\Seen" in flags), int("\\Flagged" in flags), account_id, folder, uid))
    return len(new)


def wake_due(m: imaplib.IMAP4_SSL, c: sqlite3.Connection, account_id: str) -> int:
    """Snoozed messages whose time has come: back in the inbox, unread, at the top."""
    due = c.execute("SELECT msgid FROM snoozes WHERE account=? AND until<=?", (account_id, int(time.time()))).fetchall()
    if not due:
        return 0
    ensure_folder(m, SNOOZED)
    _ok(m.select(_quote(SNOOZED)), f"SELECT {SNOOZED}")
    woken = 0
    for r in due:
        found = _ok(m.uid("SEARCH", None, "HEADER", "Message-ID", r["msgid"]), "SEARCH")[0].split()
        for uid in found:
            m.uid("STORE", uid.decode(), "-FLAGS.SILENT", "(\\Seen)")
            _move(m, int(uid), "INBOX")
        c.execute("DELETE FROM snoozes WHERE account=? AND msgid=?", (account_id, r["msgid"]))
        c.execute("INSERT OR REPLACE INTO back VALUES (?,?,?)", (account_id, r["msgid"], int(time.time())))
        woken += 1
    return woken


def sync(account_id: str = "") -> dict:
    out = {}
    with contextlib.closing(db()) as c:
        for a in mail_accounts():
            if account_id and a["id"] != account_id:
                continue
            try:
                m = imap(a)
                try:
                    woken = wake_due(m, c, a["id"])
                    new = sync_folder(m, c, a["id"], "INBOX")
                    ensure_folder(m, SNOOZED)
                    sync_folder(m, c, a["id"], SNOOZED)
                    c.commit()
                    out[a["id"]] = {"new": new, "woken": woken, "error": ""}
                finally:
                    with contextlib.suppress(Exception):
                        m.logout()
            except MailError as e:
                out[a["id"]] = {"error": str(e)}
    return {"accounts": out}


# --- reading ---------------------------------------------------------------------------------

def _id(r: sqlite3.Row) -> str:
    return f"{r['account']}|{r['folder']}|{r['uid']}"


def _split(message_id: str) -> tuple[str, str, int]:
    try:
        account_id, folder, uid = message_id.rsplit("|", 2)
        return account_id, folder, int(uid)
    except ValueError as e:
        raise MailError(f"not a message: {message_id!r}") from e


def listing(which: str = "inbox", cat: str = "", limit: int = 200) -> dict:
    """The inbox (or the snoozed), newest first (a snoozed one back counts from then)."""
    folder = SNOOZED if which == "snoozed" else "INBOX"
    with contextlib.closing(db()) as c:
        rows = c.execute(
            "SELECT m.*, b.at AS back_at, s.until AS until FROM messages m "
            "LEFT JOIN back b ON b.account=m.account AND b.msgid=m.msgid "
            "LEFT JOIN snoozes s ON s.account=m.account AND s.msgid=m.msgid "
            "WHERE m.folder=? AND (?='' OR m.category=?) "
            "ORDER BY MAX(m.date, COALESCE(b.at, 0)) DESC LIMIT ?", (folder, cat, cat, limit)).fetchall()
        counts = {r["category"]: r["n"] for r in c.execute(
            "SELECT category, COUNT(*) AS n FROM messages WHERE folder='INBOX' AND seen=0 GROUP BY category")}
    names = {a["id"]: a["user"] for a in mail_accounts()}
    return {"messages": [{"id": _id(r), "account": r["account"], "account_name": names.get(r["account"], ""),
                          "from": r["from_name"], "from_addr": r["from_addr"], "to": r["to_addrs"],
                          "subject": r["subject"], "date": max(r["date"], r["back_at"] or 0),
                          "snippet": r["snippet"], "seen": bool(r["seen"]), "flagged": bool(r["flagged"]),
                          "category": r["category"], "attachments": r["attachments"],
                          "snoozed_until": r["until"]} for r in rows],
            "unread": counts, "accounts": [{"id": i, "user": u} for i, u in names.items()]}


def _fetch(m: imaplib.IMAP4_SSL, folder: str, uid: int, peek: bool) -> email.message.EmailMessage:
    _ok(m.select(_quote(folder), readonly=peek), f"SELECT {folder}")
    resp = _ok(m.uid("FETCH", str(uid), "(BODY.PEEK[])" if peek else "(BODY[])"), "FETCH")
    raw = next((i[1] for i in resp if isinstance(i, tuple)), None)
    if raw is None:
        raise MailError("the message is not there any more (moved or deleted elsewhere)")
    return email.message_from_bytes(raw, policy=email.policy.default)


def read(message_id: str, mark_seen: bool = True) -> dict:
    account_id, folder, uid = _split(message_id)
    m = imap(account_by_id(account_id))
    try:
        msg = _fetch(m, folder, uid, peek=not mark_seen)
    finally:
        with contextlib.suppress(Exception):
            m.logout()
    text, rich, files = body_of(msg)
    if mark_seen:
        with contextlib.closing(db()) as c:
            c.execute("UPDATE messages SET seen=1 WHERE account=? AND folder=? AND uid=?", (account_id, folder, uid))
            c.commit()
    return {"id": message_id, "from": decoded(msg.get("From", "")), "to": decoded(msg.get("To", "")),
            "cc": decoded(msg.get("Cc", "")), "reply_to": decoded(msg.get("Reply-To", "")),
            "subject": decoded(msg.get("Subject", "")), "date": msg.get("Date", ""),
            "message_id": (msg.get("Message-ID") or "").strip(), "references": msg.get("References", ""),
            "text": text, "html": rich, "attachments": files}


def save_attachment(message_id: str, index: int, directory: str = "") -> dict:
    account_id, folder, uid = _split(message_id)
    m = imap(account_by_id(account_id))
    try:
        msg = _fetch(m, folder, uid, peek=True)
    finally:
        with contextlib.suppress(Exception):
            m.logout()
    for i, part in enumerate(msg.walk()):
        if i == index:
            name = Path(part.get_filename() or "attachment").name
            target = Path(directory or (Path.home() / "Downloads")) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            n = 1
            while target.exists():
                target = target.with_name(f"{Path(name).stem} ({n}){Path(name).suffix}")
                n += 1
            target.write_bytes(part.get_payload(decode=True) or b"")
            return {"saved": str(target)}
    raise MailError("no such attachment")


# --- acting ----------------------------------------------------------------------------------

def _act(message_id: str, how) -> dict:
    account_id, folder, uid = _split(message_id)
    m = imap(account_by_id(account_id))
    try:
        _ok(m.select(_quote(folder)), f"SELECT {folder}")
        how(m, uid)
    finally:
        with contextlib.suppress(Exception):
            m.logout()
    return {"done": message_id}


def _forget(message_id: str) -> None:
    account_id, folder, uid = _split(message_id)
    with contextlib.closing(db()) as c:
        c.execute("DELETE FROM messages WHERE account=? AND folder=? AND uid=?", (account_id, folder, uid))
        c.commit()


def archive(message_id: str) -> dict:
    def go(m, uid):
        where = special_folder(m, "\\Archive") or special_folder(m, "\\All")
        if not where:
            ensure_folder(m, "Archive")
            where = "Archive"
        _ok(m.select(_quote(_split(message_id)[1])), "SELECT")
        _move(m, uid, where)
    out = _act(message_id, go)
    _forget(message_id)
    return out


def trash(message_id: str) -> dict:
    def go(m, uid):
        where = special_folder(m, "\\Trash") or "Trash"
        _ok(m.select(_quote(_split(message_id)[1])), "SELECT")
        _move(m, uid, where)
    out = _act(message_id, go)
    _forget(message_id)
    return out


def set_seen(message_id: str, seen: bool) -> dict:
    out = _act(message_id, lambda m, uid: _ok(m.uid("STORE", str(uid), "+FLAGS.SILENT" if seen else "-FLAGS.SILENT",
                                                     "(\\Seen)"), "STORE"))
    account_id, folder, uid = _split(message_id)
    with contextlib.closing(db()) as c:
        c.execute("UPDATE messages SET seen=? WHERE account=? AND folder=? AND uid=?", (int(seen), account_id, folder, uid))
        c.commit()
    return out


def snooze(message_id: str, until: int) -> dict:
    """Out of the inbox until then (into "Snoozed" on the server; back by the sync)."""
    if until <= time.time():
        raise MailError("snooze until a time to come")
    account_id, folder, uid = _split(message_id)
    with contextlib.closing(db()) as c:
        r = c.execute("SELECT msgid, subject FROM messages WHERE account=? AND folder=? AND uid=?",
                      (account_id, folder, uid)).fetchone()
    if r is None or not r["msgid"]:
        raise MailError("this message cannot be snoozed (no Message-ID)")

    def go(m, uid):
        ensure_folder(m, SNOOZED)
        _ok(m.select(_quote(folder)), "SELECT")
        _move(m, uid, SNOOZED)
    _act(message_id, go)
    with contextlib.closing(db()) as c:
        c.execute("INSERT OR REPLACE INTO snoozes VALUES (?,?,?,?)", (account_id, r["msgid"], int(until), r["subject"]))
        c.execute("DELETE FROM messages WHERE account=? AND folder=? AND uid=?", (account_id, folder, uid))
        c.commit()
    return {"snoozed": message_id, "until": int(until)}


def unsnooze(message_id: str) -> dict:
    account_id, folder, uid = _split(message_id)
    with contextlib.closing(db()) as c:
        r = c.execute("SELECT msgid FROM messages WHERE account=? AND folder=? AND uid=?",
                      (account_id, folder, uid)).fetchone()
        if r:
            c.execute("UPDATE snoozes SET until=0 WHERE account=? AND msgid=?", (account_id, r["msgid"]))
            c.commit()
    return sync(account_id)


def send(account_id: str, to: str, subject: str, body: str, cc: str = "", reply_to_id: str = "",
         attachments: list[str] | None = None) -> dict:
    a = account_by_id(account_id)
    msg = EmailMessage()
    msg["From"] = a["user"]
    msg["To"] = to
    if cc:
        msg["Cc"] = cc
    msg["Subject"] = subject
    msg["Date"] = email.utils.formatdate(localtime=True)
    msg["Message-ID"] = email.utils.make_msgid(domain=a["user"].split("@")[-1])
    if reply_to_id:     # (in the same thread)
        original = read(reply_to_id, mark_seen=False)
        if original["message_id"]:
            msg["In-Reply-To"] = original["message_id"]
            msg["References"] = (original["references"] + " " + original["message_id"]).strip()
    msg.set_content(body)
    for path in attachments or []:
        p = Path(path)
        import mimetypes
        kind = (mimetypes.guess_type(p.name)[0] or "application/octet-stream").split("/", 1)
        msg.add_attachment(p.read_bytes(), maintype=kind[0], subtype=kind[1], filename=p.name)
    host = SERVERS[a["type"]][1]
    try:
        with smtplib.SMTP(host, 587, timeout=60) as s:
            s.starttls()
            if a["type"] == "google":
                code, resp = s.docmd("AUTH", "XOAUTH2 " + base64.b64encode(_xoauth2(a)).decode())
                if code != 235:
                    raise MailError(f"{host}: {resp.decode(errors='replace')}")
            else:
                s.login(a["user"], accounts.secret_get(a["id"]))
            s.send_message(msg)
    except (smtplib.SMTPException, OSError) as e:
        raise MailError(f"{host}: {e}") from e
    # (Gmail keeps what is sent by itself; others: into Sent)
    if a["type"] != "google":
        with contextlib.suppress(MailError, imaplib.IMAP4.error, OSError):
            m = imap(a)
            sent = special_folder(m, "\\Sent") or "Sent"
            m.append(_quote(sent), "\\Seen", imaplib.Time2Internaldate(time.time()), msg.as_bytes())
            m.logout()
    return {"sent": msg["Message-ID"], "to": to}


# --- following -------------------------------------------------------------------------------

def notify(summary: str, body: str) -> None:
    """A notification (the shell's server); a tap opens Mail (the desktop entry)."""
    subprocess.run(["gdbus", "call", "--session", "--dest", "org.freedesktop.Notifications",
                    "--object-path", "/org/freedesktop/Notifications",
                    "--method", "org.freedesktop.Notifications.Notify", "Mail", "0", "org.mobarchy.Mail",
                    summary, body, "[]", "{'desktop-entry': <'org.mobarchy.Mail'>}", "-1"],
                   capture_output=True, timeout=10)


def _announce(account_id: str, since: int) -> None:
    with contextlib.closing(db()) as c:
        rows = c.execute("SELECT from_name, subject, category FROM messages WHERE account=? AND folder='INBOX' "
                         "AND seen=0 AND date>=? AND category='people' ORDER BY date", (account_id, since)).fetchall()
    for r in rows[-3:]:
        notify(r["from_name"], r["subject"])


def _idle(m: imaplib.IMAP4_SSL, seconds: int) -> bool:
    """Wait for the server to say something changed (IMAP IDLE); True if it did."""
    tag = m._new_tag()
    m.send(tag + b" IDLE\r\n")
    if not m.readline().startswith(b"+"):
        return False
    m.sock.settimeout(seconds)
    changed = False
    try:
        line = m.readline()
        changed = bool(line) and (b"EXISTS" in line or b"EXPUNGE" in line or b"FETCH" in line)
    except (TimeoutError, OSError):
        pass
    finally:
        m.sock.settimeout(60)
        m.send(b"DONE\r\n")
        while True:     # (to the tagged end of IDLE)
            line = m.readline()
            if not line or line.startswith(tag):
                break
    return changed


def _follow(a: dict, stop: threading.Event) -> None:
    failures = 0
    while not stop.is_set():
        try:
            m = imap(a)
            m.select("INBOX", readonly=True)
            failures = 0
            while not stop.is_set():
                if _idle(m, IDLE_S):
                    started = int(time.time()) - 120
                    sync(a["id"])
                    _announce(a["id"], started)
                    print(json.dumps({"event": "changed", "account": a["id"]}), flush=True)
                m.noop()
        except (MailError, imaplib.IMAP4.error, OSError) as e:
            failures += 1
            print(json.dumps({"event": "error", "account": a["id"], "error": str(e)}), flush=True)
            stop.wait(min(600, 15 * failures))


def watch() -> int:
    """Follow every account's inbox (until stopped); every minute, bring back what is due."""
    stop = threading.Event()
    sync()
    for a in mail_accounts():
        threading.Thread(target=_follow, args=(a, stop), daemon=True).start()
    try:
        while True:
            time.sleep(60)
            with contextlib.closing(db()) as c:
                due = c.execute("SELECT DISTINCT account FROM snoozes WHERE until<=?", (int(time.time()),)).fetchall()
            for r in due:
                sync(r["account"])
                print(json.dumps({"event": "changed", "account": r["account"]}), flush=True)
    except KeyboardInterrupt:
        stop.set()
    return 0


# --- the command -----------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="mobarchy-mail", description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sync")
    s.add_argument("--account", default="")
    ls = sub.add_parser("list")
    ls.add_argument("--snoozed", action="store_true")
    ls.add_argument("--category", default="")
    ls.add_argument("--limit", type=int, default=200)
    r = sub.add_parser("read")
    r.add_argument("id")
    r.add_argument("--peek", action="store_true")
    for name in ("archive", "trash", "unsnooze"):
        sub.add_parser(name).add_argument("id")
    seen = sub.add_parser("seen")
    seen.add_argument("id")
    seen.add_argument("state", choices=("on", "off"))
    z = sub.add_parser("snooze")
    z.add_argument("id")
    z.add_argument("until", type=int, help="seconds since the epoch")
    att = sub.add_parser("attachment")
    att.add_argument("id")
    att.add_argument("index", type=int)
    att.add_argument("--dir", default="")
    snd = sub.add_parser("send")
    snd.add_argument("--account", required=True)
    snd.add_argument("--to", required=True)
    snd.add_argument("--cc", default="")
    snd.add_argument("--subject", default="")
    snd.add_argument("--reply-to", default="")
    snd.add_argument("--attach", action="append", default=[])
    snd.add_argument("--body", default=None, help="(else stdin)")
    sub.add_parser("watch")
    a = p.parse_args(argv)
    try:
        if a.cmd == "watch":
            return watch()
        out = {"sync": lambda: sync(a.account),
               "list": lambda: listing("snoozed" if a.snoozed else "inbox", a.category, a.limit),
               "read": lambda: read(a.id, not a.peek),
               "archive": lambda: archive(a.id), "trash": lambda: trash(a.id),
               "unsnooze": lambda: unsnooze(a.id),
               "seen": lambda: set_seen(a.id, a.state == "on"),
               "snooze": lambda: snooze(a.id, a.until),
               "attachment": lambda: save_attachment(a.id, a.index, a.dir),
               "send": lambda: send(a.account, a.to, a.subject, a.body if a.body is not None else sys.stdin.read(),
                                    a.cc, a.reply_to, a.attach)}[a.cmd]()
    except (MailError, accounts.AccountError) as e:
        print(json.dumps({"error": str(e)}), flush=True)
        return 1
    print(json.dumps(out, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
