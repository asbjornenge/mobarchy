import http.server
import json
import threading

import pytest

from mobarchy_mcp import accounts, carddav, contacts

CARD = ("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:{name}\r\nTEL;type=CELL:{tel}\r\n"
        "PHOTO;ENCODING=b;TYPE=JPEG:AAAA\r\nEND:VCARD\r\n")


EVENT = ("BEGIN:VCALENDAR&#13;\nVERSION:2.0&#13;\nBEGIN:VEVENT&#13;\nUID:e1&#13;\nSUMMARY:Lansering&#13;\n"
         "DTSTART:20261021T080000Z&#13;\nDTEND:20261021T090000Z&#13;\nEND:VEVENT&#13;\nEND:VCALENDAR&#13;\n")


class Server:
    """A CardDAV server like iCloud's: a redirect at the root, a principal, a home with two
    books, PROPFIND for etags, addressbook-multiget for cards; Basic auth."""

    def __init__(self):
        self.cards = {"/home/book1/a.vcf": ("e1", CARD.format(name="Per Olsen", tel="+4791234567")),
                      "/home/book2/b.vcf": ("e2", CARD.format(name="Kari Nordmann", tel="22334455"))}
        self.reports = 0
        self.polls = 0
        self.calendars_off = False      # (as Google's, with the CalDAV API not turned on)
        self.no_reports = False         # (as Google's with calendar.readonly: no REPORT)
        self.read_only = False          # (a calendar shared without changes)
        self.events = {"/cal/work/e1.ics": ("c1", EVENT.replace("&#13;", "\r"))}
        self.puts = []
        self.keyring_locked = False
        outer = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def reply(self, status, body="", headers=None):
                self.send_response(status)
                for k, v in (headers or {}).items():
                    self.send_header(k, v)
                self.send_header("Content-Length", str(len(body.encode())))
                self.end_headers()
                self.wfile.write(body.encode())

            def ms(self, responses):
                return self.reply(207, '<d:multistatus xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:carddav" '
                                  'xmlns:cal="urn:ietf:params:xml:ns:caldav" xmlns:a="http://apple.com/ns/ical/">'
                                  + responses + "</d:multistatus>")

            def do_PROPFIND(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                if self.headers.get("Authorization") != accounts.basic("me@icloud.com", "app-pw"):
                    return self.reply(401)
                p = self.path
                if p == "/":
                    return self.reply(301, headers={"Location": "/dav/"})
                if p in ("/dav/", "/remote.php/dav"):
                    return self.ms("<d:response><d:href>/dav/</d:href><d:propstat><d:prop><d:current-user-principal>"
                                   "<d:href>/principal/</d:href></d:current-user-principal></d:prop></d:propstat></d:response>")
                if outer.calendars_off and (p == "/cal/" or "calendar-home-set" in body):
                    return self.reply(403, "<errors><error><code>accessNotConfigured</code></error></errors>")
                if p == "/principal/" and "calendar-home-set" in body:
                    return self.ms("<d:response><d:href>/principal/</d:href><d:propstat><d:prop><cal:calendar-home-set>"
                                   "<d:href>/cal/</d:href></cal:calendar-home-set></d:prop></d:propstat></d:response>")
                if p == "/cal/":
                    return self.ms("<d:response><d:href>/cal/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/>"
                                   "</d:resourcetype></d:prop></d:propstat></d:response>"
                                   "<d:response><d:href>/cal/work/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/>"
                                   "<cal:calendar/></d:resourcetype><d:displayname>Jobb</d:displayname>"
                                   "<a:calendar-color>#FF8800FF</a:calendar-color>"
                                   + ("<d:current-user-privilege-set><d:privilege><d:read/></d:privilege>"
                                      "</d:current-user-privilege-set>" if outer.read_only else "")
                                   + "<cal:supported-calendar-component-set>"
                                   '<cal:comp name="VEVENT"/></cal:supported-calendar-component-set></d:prop></d:propstat></d:response>'
                                   "<d:response><d:href>/cal/tasks/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/>"
                                   "<cal:calendar/></d:resourcetype><d:displayname>Gjøremål</d:displayname><cal:supported-calendar-component-set>"
                                   '<cal:comp name="VTODO"/></cal:supported-calendar-component-set></d:prop></d:propstat></d:response>')
                if p == "/cal/work/":
                    return self.ms('<d:response><d:href>/cal/work/</d:href></d:response>' + "".join(
                        f"<d:response><d:href>{h}</d:href><d:propstat><d:prop><d:getetag>{e}</d:getetag>"
                        "</d:prop></d:propstat></d:response>" for h, (e, _) in outer.events.items()))
                if p == "/principal/":
                    return self.ms("<d:response><d:href>/principal/</d:href><d:propstat><d:prop><c:addressbook-home-set>"
                                   "<d:href>/home/</d:href></c:addressbook-home-set></d:prop></d:propstat></d:response>")
                if p == "/home/":
                    books = "".join(
                        f"<d:response><d:href>/home/{b}/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/>"
                        f"<c:addressbook/></d:resourcetype><d:displayname>{n}</d:displayname></d:prop></d:propstat></d:response>"
                        for b, n in (("book1", "Contacts"), ("book2", "Work")))
                    return self.ms("<d:response><d:href>/home/</d:href><d:propstat><d:prop><d:resourcetype>"
                                   "<d:collection/></d:resourcetype></d:prop></d:propstat></d:response>" + books)
                if p.startswith("/home/book"):
                    items = "".join(f"<d:response><d:href>{h}</d:href><d:propstat><d:prop><d:getetag>{e}</d:getetag>"
                                    f"</d:prop></d:propstat></d:response>"
                                    for h, (e, _) in outer.cards.items() if h.startswith(p))
                    return self.ms(f"<d:response><d:href>{p}</d:href></d:response>" + items)
                self.reply(404)

            def do_POST(self):
                # Nextcloud's Login Flow v2: the poll answers once the owner has signed in
                form = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                if self.path == "/index.php/login/v2":
                    return self.reply(200, json.dumps({"poll": {"token": "t0k", "endpoint": outer.url + "poll"},
                                                       "login": outer.url + "login/flow"}))
                if self.path == "/poll" and form == "token=t0k":
                    outer.polls += 1
                    if outer.polls < 2:
                        return self.reply(404)
                    return self.reply(200, json.dumps({"server": outer.url.rstrip("/"), "loginName": "me@icloud.com",
                                                       "appPassword": "app-pw"}))
                self.reply(404)

            def do_GET(self):
                if self.path in outer.events:
                    return self.reply(200, outer.events[self.path][1])
                self.reply(404)

            def do_PUT(self):
                ics = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                have = outer.events.get(self.path)
                if outer.read_only:
                    return self.reply(403)
                if (self.headers.get("If-None-Match") == "*" and have) or \
                        (self.headers.get("If-Match") and (not have or have[0] != self.headers["If-Match"])):
                    return self.reply(412)
                outer.puts.append((self.path, ics))
                outer.events[self.path] = (f"v{len(outer.puts)}", ics)
                self.reply(204 if have else 201, headers={"ETag": f"v{len(outer.puts)}"})

            def do_DELETE(self):
                have = outer.events.get(self.path)
                if not have:
                    return self.reply(404)
                if self.headers.get("If-Match") and have[0] != self.headers["If-Match"]:
                    return self.reply(412)
                del outer.events[self.path]
                self.reply(204)

            def do_REPORT(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                outer.reports += 1
                if outer.no_reports and "calendar-multiget" in body:
                    return self.reply(403, '<D:error xmlns:D="DAV:"/>')
                if "calendar-multiget" in body:
                    from xml.sax.saxutils import escape
                    return self.ms("".join(
                        f"<d:response><d:href>{h}</d:href><d:propstat><d:prop><d:getetag>{e}</d:getetag>"
                        f"<cal:calendar-data>{escape(ics).replace(chr(13), '&#13;')}</cal:calendar-data>"
                        "</d:prop></d:propstat></d:response>"
                        for h, (e, ics) in outer.events.items() if f"<d:href>{h}</d:href>" in body))
                items = "".join(f"<d:response><d:href>{h}</d:href><d:propstat><d:prop><d:getetag>{e}</d:getetag>"
                                f"<c:address-data>{c}</c:address-data></d:prop></d:propstat></d:response>"
                                for h, (e, c) in outer.cards.items() if f"<d:href>{h}</d:href>" in body)
                self.ms(items)

        self.httpd = http.server.HTTPServer(("127.0.0.1", 0), H)
        self.url = f"http://127.0.0.1:{self.httpd.server_port}/"
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()


@pytest.fixture
def server(tmp_path, monkeypatch):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var))
    keyring = {}
    monkeypatch.setattr(accounts, "secret_store", lambda k, v, label: keyring.__setitem__(k, v))
    monkeypatch.setattr(accounts, "secret_get", lambda k: keyring[k])
    monkeypatch.setattr(accounts, "secret_clear", lambda k: keyring.pop(k, None))
    s = Server()
    monkeypatch.setattr(accounts, "keyring_locked", lambda: s.keyring_locked)
    monkeypatch.setattr(accounts, "ICLOUD", s.url)
    monkeypatch.setattr(accounts, "ICLOUD_CAL", s.url)
    yield s
    s.httpd.shutdown()



def test_add_icloud_syncs_and_strips_photos(server):
    out = accounts.add_icloud("me@icloud.com", "app-pw")
    acc = out["accounts"][0]
    assert acc["id"] == "icloud-me-icloud-com" and acc["error"] == "" and acc["contacts"] == 2
    assert [b["name"] for b in acc["books"]] == ["Contacts", "Work"]
    text = (accounts.contacts_dir() / "icloud-me-icloud-com.vcf").read_text()
    assert "Per Olsen" in text and "PHOTO" not in text
    assert contacts.search("Kari")[0]["name"] == "Kari Nordmann"  # the reader sees accounts too


def test_wrong_password_is_not_kept(server):
    with pytest.raises(accounts.AccountError, match="no longer accepted"):
        accounts.add_icloud("me@icloud.com", "wrong")
    assert accounts.list_accounts()["accounts"] == []


def test_sync_fetches_only_what_changed(server):
    accounts.add_icloud("me@icloud.com", "app-pw")
    before = server.reports
    accounts.sync()
    assert server.reports == before  # nothing changed: no REPORT
    server.cards["/home/book1/a.vcf"] = ("e3", CARD.format(name="Per Olsen", tel="+4799999999"))
    accounts.sync()
    assert server.reports == before + 1
    assert contacts.search("99999999")[0]["name"] == "Per Olsen"


def test_book_off_hides_its_contacts(server):
    accounts.add_icloud("me@icloud.com", "app-pw")
    book2 = server.url + "home/book2/"
    out = accounts.set_book("icloud-me-icloud-com", book2, False)
    assert out["contacts"] == 1
    assert contacts.search("Kari") == []


def test_remove(server):
    accounts.add_icloud("me@icloud.com", "app-pw")
    accounts.remove("icloud-me-icloud-com")
    assert not (accounts.contacts_dir() / "icloud-me-icloud-com.vcf").exists()
    assert contacts.search("Per") == []


def test_jwt_email():
    import base64
    payload = base64.urlsafe_b64encode(json.dumps({"email": "a@gmail.com"}).encode()).rstrip(b"=").decode()
    assert accounts._jwt_email(f"x.{payload}.y") == "a@gmail.com"


def test_google_client_id_is_checked(server):
    with pytest.raises(accounts.AccountError):
        accounts.set_google_client("nope", "secret")
    assert accounts.set_google_client("123-abc.apps.googleusercontent.com", "s\n") == {"google_client": True}
    assert accounts.list_accounts()["google_client"]


def test_nextcloud_login_flow(server):
    opened = []
    out = accounts.add_nextcloud(server.url, open_browser=opened.append, interval=0)
    assert opened == [server.url + "login/flow"]
    acc = out["accounts"][0]
    assert acc["type"] == "nextcloud" and acc["contacts"] == 2 and acc["error"] == ""


def test_nextcloud_must_be_https(server):
    with pytest.raises(accounts.AccountError, match="https"):
        accounts.add_nextcloud("http://cloud.example.org")


def test_mobarchy_google_client(server, tmp_path, monkeypatch):
    f = tmp_path / "google-client.json"
    monkeypatch.setenv("MOBARCHY_GOOGLE_CLIENT", str(f))
    assert not accounts.list_accounts()["google_client"]
    f.write_text(json.dumps({"client_id": "1-m.apps.googleusercontent.com", "client_secret": "GOCSPX-m"}))
    assert accounts.list_accounts()["google_client"]
    assert accounts.google_client() == ("1-m.apps.googleusercontent.com", "GOCSPX-m")
    accounts.set_google_client("2-own.apps.googleusercontent.com", "own\n")  # the owner's own wins
    assert accounts.google_client() == ("2-own.apps.googleusercontent.com", "own")


def test_icloud_password_as_copied(server):
    assert accounts.add_icloud("me@icloud.com", " app- pw \n")["accounts"][0]["error"] == ""


def test_icloud_calendars(server):
    import datetime as dt
    from mobarchy_mcp import calendars
    out = accounts.add_icloud("me@icloud.com", "app-pw")
    [cal] = out["accounts"][0]["calendars"]          # (the task list is left out)
    assert cal["name"] == "Jobb" and cal["color"] == "#FF8800" and cal["enabled"]
    tz = dt.timezone(dt.timedelta(hours=2))
    start = dt.datetime(2026, 10, 21, tzinfo=tz)
    [ev] = calendars.events(start, start + dt.timedelta(days=1))
    assert ev["title"] == "Lansering" and ev["calendar_name"] == "Jobb"
    accounts.set_calendar("icloud-me-icloud-com", cal["url"], False)
    assert calendars.events(start, start + dt.timedelta(days=1)) == []


def test_calendars_found_once_the_server_lets_them(server):
    server.calendars_off = True
    out = accounts.add_icloud("me@icloud.com", "app-pw")["accounts"][0]
    assert out["contacts"] == 2 and out["calendars"] == []
    assert "not turned on" in out["error"]
    server.calendars_off = False
    out = accounts.sync()["accounts"][0]
    assert out["error"] == "" and [c["name"] for c in out["calendars"]] == ["Jobb"]


def test_events_one_by_one_when_reports_are_refused(server):
    import datetime as dt
    from mobarchy_mcp import calendars
    server.no_reports = True
    assert accounts.add_icloud("me@icloud.com", "app-pw")["accounts"][0]["error"] == ""
    start = dt.datetime(2026, 10, 21, tzinfo=dt.timezone.utc)
    [ev] = calendars.events(start, start + dt.timedelta(days=1))
    assert ev["title"] == "Lansering"


def _day_events():
    import datetime as dt
    from mobarchy_mcp import calendars
    start = dt.datetime(2026, 10, 21, tzinfo=dt.timezone.utc)
    return calendars.events(start, start + dt.timedelta(days=1))


def test_account_events_changed_and_deleted(server):
    from mobarchy_mcp import calendars
    accounts.add_icloud("me@icloud.com", "app-pw")
    [ev] = _day_events()
    assert ev["editable"]
    calendars.update_event("e1", title="Lansering!")
    path, ics = server.puts[-1]
    assert path == "/cal/work/e1.ics" and "SUMMARY:Lansering!" in ics
    assert [e["title"] for e in _day_events()] == ["Lansering!"]     # (before a sync)
    calendars.delete_event("e1")
    assert "/cal/work/e1.ics" not in server.events and _day_events() == []
    accounts.sync()
    assert _day_events() == []


def test_event_added_to_an_account(server):
    from mobarchy_mcp import calendars
    cal = accounts.add_icloud("me@icloud.com", "app-pw")["accounts"][0]["calendars"][0]
    out = calendars.add_event("Planlegging", "2026-10-21T13:00", calendar=f"icloud-me-icloud-com|{cal['url']}")
    [(path, ics)] = server.puts
    assert path.startswith("/cal/work/") and out["added"] in ics
    assert sorted(e["title"] for e in _day_events()) == ["Lansering", "Planlegging"]
    accounts.sync()      # (what the server has now, with its etag)
    assert sorted(e["title"] for e in _day_events()) == ["Lansering", "Planlegging"]


def test_read_only_calendar_is_not_written(server):
    from mobarchy_mcp import calendars
    server.read_only = True
    accounts.add_icloud("me@icloud.com", "app-pw")
    [ev] = _day_events()
    assert not ev["editable"]
    with pytest.raises(calendars.CalendarError, match="read only"):
        calendars.delete_event("e1")
    assert "/cal/work/e1.ics" in server.events


def test_google_read_only_sign_in_cannot_write():
    assert not accounts._may_write({"type": "google", "scope": "https://www.googleapis.com/auth/calendar.readonly"})
    assert accounts._may_write({"type": "google", "scope": "openid https://www.googleapis.com/auth/calendar"})
    assert accounts._may_write({"type": "icloud"})


def test_no_sync_while_the_keyring_is_locked(server):
    accounts.add_icloud("me@icloud.com", "app-pw")
    server.keyring_locked = True
    server.reports = 0
    out = accounts.sync()
    assert out["locked"] and out["accounts"][0]["error"] == "" and server.reports == 0
