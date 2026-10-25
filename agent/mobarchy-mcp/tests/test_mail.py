import email
import email.policy
import time

from mobarchy_mcp import mail


def msg(headers: str, body: str = "Hei!\n") -> email.message.EmailMessage:
    return email.message_from_string(headers + "\n" + body, policy=email.policy.default)


def test_category():
    assert mail.category(msg("From: Kari <kari@example.no>\n")) == "people"
    assert mail.category(msg("From: Shop <news@shop.no>\nList-Unsubscribe: <mailto:u@shop.no>\n")) == "newsletters"
    assert mail.category(msg("From: GitHub <noreply@github.com>\n")) == "notifications"
    assert mail.category(msg("From: Bank <varsel@bank.no>\nAuto-Submitted: auto-generated\n")) == "notifications"
    assert mail.category(msg("From: Ola <ola@example.no>\nPrecedence: bulk\n")) == "newsletters"


def test_body_snippet_and_attachments():
    m = email.message.EmailMessage()
    m["From"] = "Kari <kari@example.no>"
    m.set_content("> earlier\nKommer du i kveld?\n\nKari")
    m.add_alternative("<html><style>p{}</style><p>Kommer du <b>i kveld</b>?</p></html>", subtype="html")
    m.add_attachment(b"%PDF", maintype="application", subtype="pdf", filename="billett.pdf")
    text, rich, files = mail.body_of(m)
    assert text.startswith("> earlier") and "i kveld" in rich
    assert files == [{"index": files[0]["index"], "name": "billett.pdf", "type": "application/pdf", "size": 4}]
    assert mail.snippet_of(text) == "Kommer du i kveld? Kari"


def test_html_only_becomes_text():
    m = email.message.EmailMessage()
    m.set_content("<div>Hei</div><div>p&aring; deg</div><script>x()</script>", subtype="html")
    assert mail.body_of(m)[0] == "Hei\npå deg"


def test_listing_newest_first_with_snoozed_back_on_top(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    monkeypatch.setattr(mail, "mail_accounts", lambda: [{"id": "g", "user": "me@x.no", "type": "google"}])
    now = int(time.time())
    c = mail.db()
    rows = [("g", "INBOX", 1, "<a@x>", "Kari", "kari@x", "", "Old one", now - 9000, "", 0, 0, "people", 0),
            ("g", "INBOX", 2, "<b@x>", "Ola", "ola@x", "", "Newer", now - 100, "", 1, 0, "people", 0),
            ("g", "INBOX", 3, "<c@x>", "Shop", "n@shop", "", "Sale", now - 50, "", 0, 0, "newsletters", 0)]
    c.executemany("INSERT INTO messages VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,'')", rows)
    c.execute("INSERT INTO back VALUES ('g', '<a@x>', ?)", (now,))      # (snoozed, back now)
    c.commit()
    out = mail.listing()
    assert [m["subject"] for m in out["messages"]] == ["Old one", "Sale", "Newer"]
    assert out["unread"] == {"people": 1, "newsletters": 1}
    assert [m["subject"] for m in mail.listing(cat="people")["messages"]] == ["Old one", "Newer"]
    assert out["messages"][0]["id"] == "g|INBOX|1"


def test_changes_count_at_once_and_stay_until_the_server_has_them(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    monkeypatch.setattr(mail, "mail_accounts", lambda: [{"id": "g", "user": "me@x.no", "type": "google"}])
    now = int(time.time())
    c = mail.db()
    rows = [("g", "INBOX", 1, "<a@x>", "Kari", "kari@x", "", "Archive me", now - 100, "", 0, 0, "people", 0),
            ("g", "INBOX", 2, "<b@x>", "Ola", "ola@x", "", "Read me", now - 50, "", 0, 0, "people", 0)]
    c.executemany("INSERT INTO messages VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,'')", rows)
    c.commit()

    # the server out of reach: the changes still count, and wait
    def offline(message_id, how):
        raise mail.MailError("no network")
    monkeypatch.setattr(mail, "_act", offline)
    assert "pending" in mail.archive("g|INBOX|1")
    assert "pending" in mail.set_seen("g|INBOX|2", True)
    out = mail.listing()
    assert [m["subject"] for m in out["messages"]] == ["Read me"]
    assert out["messages"][0]["seen"] is True
    assert out["unread"] == {}
    # a sync that brings the old state back from the server doesn't undo them
    c.execute("UPDATE messages SET seen=0 WHERE uid=2")
    c.commit()
    assert [(m["subject"], m["seen"]) for m in mail.listing()["messages"]] == [("Read me", True)]

    # the server reached: settled (the archived one forgotten)
    monkeypatch.setattr(mail, "_act", lambda message_id, how: {"done": message_id})
    assert mail.archive("g|INBOX|1") == {"done": "g|INBOX|1"}
    assert c.execute("SELECT COUNT(*) FROM messages WHERE uid=1").fetchone()[0] == 0
    assert c.execute("SELECT COUNT(*) FROM pending WHERE action='archive'").fetchone()[0] == 0


def test_a_conversation_is_listed_once_and_read_whole(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    monkeypatch.setattr(mail, "mail_accounts", lambda: [{"id": "g", "user": "me@x.no", "type": "google"}])
    now = int(time.time())
    c = mail.db()
    t = "<first@anna>"
    rows = [("g", "INBOX", 1, "<first@anna>", "Anna", "anna@x", "", "Plans", now - 900, "Shall we?", 1, 0, "people", 0, t),
            ("g", "Sent", 7, "<reply@me>", "Me", "me@x.no", "", "Re: Plans", now - 600, "Yes!", 1, 0, "people", 0, t),
            ("g", "INBOX", 2, "<second@anna>", "Anna", "anna@x", "", "Re: Plans", now - 300, "Great", 0, 0, "people", 0, t),
            ("g", "INBOX", 3, "<other@x>", "Ola", "ola@x", "", "Other", now - 500, "", 1, 0, "people", 0, "")]
    c.executemany("INSERT INTO messages VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
    c.commit()
    out = mail.listing()["messages"]
    assert [(m["subject"], m["count"], m["seen"]) for m in out] == [("Re: Plans", 3, False), ("Other", 1, True)]
    assert sorted(out[0]["thread_ids"]) == ["g|INBOX|1", "g|INBOX|2"]
    conv = mail.thread("g|INBOX|2")["messages"]
    assert [m["snippet"] for m in conv] == ["Shall we?", "Yes!", "Great"]


def test_thread_of():
    m = email.message_from_string("Message-ID: <C@x>\nIn-Reply-To: <b@x>\nReferences: <A@x> <b@x>\n\nhi")
    assert mail.thread_of(m) == "<a@x>"
    assert mail.thread_of(email.message_from_string("Message-ID: <Solo@x>\n\nhi")) == "<solo@x>"
