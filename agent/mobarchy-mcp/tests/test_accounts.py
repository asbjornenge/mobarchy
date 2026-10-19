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
                                   "<a:calendar-color>#FF8800FF</a:calendar-color><cal:supported-calendar-component-set>"
                                   '<cal:comp name="VEVENT"/></cal:supported-calendar-component-set></d:prop></d:propstat></d:response>'
                                   "<d:response><d:href>/cal/tasks/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/>"
                                   "<cal:calendar/></d:resourcetype><d:displayname>Gjøremål</d:displayname><cal:supported-calendar-component-set>"
                                   '<cal:comp name="VTODO"/></cal:supported-calendar-component-set></d:prop></d:propstat></d:response>')
                if p == "/cal/work/":
                    return self.ms('<d:response><d:href>/cal/work/</d:href></d:response><d:response><d:href>/cal/work/e1.ics</d:href>'
                                   "<d:propstat><d:prop><d:getetag>c1</d:getetag></d:prop></d:propstat></d:response>")
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
                if self.path == "/cal/work/e1.ics":
                    return self.reply(200, EVENT.replace("&#13;", "\r"))
                self.reply(404)

            def do_REPORT(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                outer.reports += 1
                if outer.no_reports and "calendar-multiget" in body:
                    return self.reply(403, '<D:error xmlns:D="DAV:"/>')
                if "calendar-multiget" in body:
                    return self.ms("<d:response><d:href>/cal/work/e1.ics</d:href><d:propstat><d:prop><d:getetag>c1</d:getetag>"
                                   "<cal:calendar-data>" + EVENT + "</cal:calendar-data></d:prop></d:propstat></d:response>")
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
