import http.server
import json
import threading

import pytest

from mobarchy_mcp import accounts, carddav, contacts

CARD = ("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:{name}\r\nTEL;type=CELL:{tel}\r\n"
        "PHOTO;ENCODING=b;TYPE=JPEG:AAAA\r\nEND:VCARD\r\n")


class Server:
    """A CardDAV server like iCloud's: a redirect at the root, a principal, a home with two
    books, PROPFIND for etags, addressbook-multiget for cards; Basic auth."""

    def __init__(self):
        self.cards = {"/home/book1/a.vcf": ("e1", CARD.format(name="Per Olsen", tel="+4791234567")),
                      "/home/book2/b.vcf": ("e2", CARD.format(name="Kari Nordmann", tel="22334455"))}
        self.reports = 0
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
                return self.reply(207, '<d:multistatus xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:carddav">'
                                  + responses + "</d:multistatus>")

            def do_PROPFIND(self):
                self.rfile.read(int(self.headers.get("Content-Length", 0)))
                if self.headers.get("Authorization") != accounts.basic("me@icloud.com", "app-pw"):
                    return self.reply(401)
                p = self.path
                if p == "/":
                    return self.reply(301, headers={"Location": "/dav/"})
                if p == "/dav/":
                    return self.ms("<d:response><d:href>/dav/</d:href><d:propstat><d:prop><d:current-user-principal>"
                                   "<d:href>/principal/</d:href></d:current-user-principal></d:prop></d:propstat></d:response>")
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

            def do_REPORT(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
                outer.reports += 1
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
