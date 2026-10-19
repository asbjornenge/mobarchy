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
    c.executemany("INSERT INTO messages VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
    c.execute("INSERT INTO back VALUES ('g', '<a@x>', ?)", (now,))      # (snoozed, back now)
    c.commit()
    out = mail.listing()
    assert [m["subject"] for m in out["messages"]] == ["Old one", "Sale", "Newer"]
    assert out["unread"] == {"people": 1, "newsletters": 1}
    assert [m["subject"] for m in mail.listing(cat="people")["messages"]] == ["Old one", "Newer"]
    assert out["messages"][0]["id"] == "g|INBOX|1"
