"""A small CardDAV (RFC 6352) and CalDAV (RFC 4791) client: enough to find a user's address
books and calendars, fetch their contacts and events, and write events, with iCloud, Google
and other servers. The standard library only.

  discover(base) -> [AddressBook]   current-user-principal -> addressbook-home-set -> books
  list_cards(book) -> {href: etag}  what is in a book or calendar (PROPFIND, depth 1)
  fetch(book, hrefs) -> {href: (etag, vcard)}   addressbook-multiget, in batches
  discover_calendars(base) -> [Calendar]        ... -> calendar-home-set -> event calendars
  fetch_events(calendar, hrefs) -> {href: (etag, ics)}  calendar-multiget (or GET, one by one)
  put_event(url, ics, etag) -> etag, delete(url, etag)  an event written or deleted

Authentication is the caller's: a function that returns the Authorization header's value
(Basic for iCloud's app-specific passwords, Bearer for Google's OAuth tokens)."""

from __future__ import annotations

import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from typing import Callable

DAV = "DAV:"
CARD = "urn:ietf:params:xml:ns:carddav"
CAL = "urn:ietf:params:xml:ns:caldav"
APPLE = "http://apple.com/ns/ical/"
NS = {"d": DAV, "c": CARD, "cal": CAL, "a": APPLE}
BATCH = 100
USER_AGENT = "Mobarchy-Accounts/0.1"


class CardDAVError(Exception):
    def __init__(self, message: str, status: int = 0):
        super().__init__(message)
        self.status = status    # the HTTP status, if that was it


@dataclass
class AddressBook:
    url: str
    name: str


@dataclass
class Calendar:
    url: str
    name: str
    color: str      # "#rrggbb", or ""
    writable: bool = True   # (what the server says the user may do; the sign-in may say less)


class Client:
    def __init__(self, auth: Callable[[], str], timeout: float = 30):
        self.auth = auth
        self.timeout = timeout

    def request(self, method: str, url: str, body: str = "", depth: str | None = None,
                redirects: int = 5, headers: dict | None = None,
                answer: dict | None = None) -> tuple[int, bytes, str]:
        """(status, body, final url); redirects followed for any method (urllib only does it
        for GET), keeping the method and body. headers: more to send; answer: filled with
        the response's."""
        sent = {"Authorization": self.auth(), "User-Agent": USER_AGENT,
                "Content-Type": "application/xml; charset=utf-8", **(headers or {})}
        if depth is not None:
            sent["Depth"] = depth
        req = urllib.request.Request(url, data=body.encode() if body else None, method=method,
                                     headers=sent)
        opener = urllib.request.build_opener(NoRedirect)
        try:
            with opener.open(req, timeout=self.timeout) as r:
                if answer is not None:
                    answer.update(r.headers.items())
                return r.status, r.read(), url
        except urllib.error.HTTPError as e:
            if e.code in (301, 302, 307, 308) and redirects and e.headers.get("Location"):
                return self.request(method, urllib.parse.urljoin(url, e.headers["Location"]), body,
                                    depth, redirects - 1, headers, answer)
            if e.code == 403 and b"accessNotConfigured" in (e.read() or b""):
                # (Google: the sign-in is fine, the API is not turned on in the client's project)
                raise CardDAVError(f"{urllib.parse.urlsplit(url).netloc} is not turned on for this "
                                   f"app's sign-in (Google: enable the CalDAV/CardDAV API in "
                                   f"the client's project)") from e
            if e.code in (401, 403):
                raise CardDAVError(f"not allowed ({e.code}): the password or the sign-in is no "
                                   f"longer accepted", e.code) from e
            raise CardDAVError(f"{method} {url}: HTTP {e.code}", e.code) from e
        except urllib.error.URLError as e:
            raise CardDAVError(f"cannot reach {urllib.parse.urlsplit(url).netloc}: {e.reason}") from e

    def propfind(self, url: str, props: str, depth: str) -> tuple[ET.Element, str]:
        body = (f'<?xml version="1.0"?><d:propfind xmlns:d="DAV:" xmlns:c="{CARD}" xmlns:cal="{CAL}" '
                f'xmlns:a="{APPLE}"><d:prop>{props}</d:prop></d:propfind>')
        status, data, final = self.request("PROPFIND", url, body, depth)
        if status != 207:
            raise CardDAVError(f"PROPFIND {url}: HTTP {status}")
        return ET.fromstring(data), final

    def discover(self, base: str) -> list[AddressBook]:
        root, url = self.propfind(base, "<d:current-user-principal/>", "0")
        principal = _href(root, "d:current-user-principal")
        if not principal:
            raise CardDAVError("the server did not say who the user is (current-user-principal)")
        principal = urllib.parse.urljoin(url, principal)
        root, url = self.propfind(principal, "<c:addressbook-home-set/>", "0")
        home = _href(root, "c:addressbook-home-set")
        if not home:
            raise CardDAVError("the server has no address books for this user")
        home = urllib.parse.urljoin(url, home)
        root, url = self.propfind(home, "<d:resourcetype/><d:displayname/>", "1")
        books = []
        for resp in root.findall("d:response", NS):
            if resp.find(".//d:resourcetype/c:addressbook", NS) is None:
                continue
            href = urllib.parse.urljoin(url, resp.findtext("d:href", "", NS))
            name = (resp.findtext(".//d:displayname", "", NS) or "").strip()
            books.append(AddressBook(href, name or _last_segment(href)))
        return books

    def discover_calendars(self, base: str) -> list[Calendar]:
        root, url = self.propfind(base, "<d:current-user-principal/>", "0")
        principal = _href(root, "d:current-user-principal")
        if not principal:
            raise CardDAVError("the server did not say who the user is (current-user-principal)")
        principal = urllib.parse.urljoin(url, principal)
        root, url = self.propfind(principal, "<cal:calendar-home-set/>", "0")
        home = _href(root, "cal:calendar-home-set")
        if not home:
            raise CardDAVError("the server has no calendars for this user")
        home = urllib.parse.urljoin(url, home)
        root, url = self.propfind(home, "<d:resourcetype/><d:displayname/><a:calendar-color/>"
                                        "<cal:supported-calendar-component-set/>"
                                        "<d:current-user-privilege-set/>", "1")
        out = []
        for resp in root.findall("d:response", NS):
            if resp.find(".//d:resourcetype/cal:calendar", NS) is None:
                continue
            comps = [c.get("name", "").upper() for c in resp.findall(".//cal:supported-calendar-component-set/cal:comp", NS)]
            if comps and "VEVENT" not in comps:    # (task lists and the like)
                continue
            href = urllib.parse.urljoin(url, resp.findtext("d:href", "", NS))
            name = (resp.findtext(".//d:displayname", "", NS) or "").strip()
            color = (resp.findtext(".//a:calendar-color", "", NS) or "").strip()[:7]
            privileges = resp.find(".//d:current-user-privilege-set", NS)
            writable = privileges is None or any(
                privileges.find(f".//d:privilege/d:{p}", NS) is not None for p in ("all", "write", "write-content"))
            out.append(Calendar(href, name or _last_segment(href), color if color.startswith("#") else "",
                                writable))
        return out

    def put_event(self, url: str, ics: str, etag: str | None = None) -> str:
        """Write an event at url: over the one with etag ("": whatever is there), or a new one
        (None: none may be there). Its new etag, or "" if the server did not say."""
        answer: dict = {}
        condition = {"If-None-Match": "*"} if etag is None else {"If-Match": etag} if etag else {}
        status, _, _ = self.request("PUT", url, ics, headers={
            "Content-Type": "text/calendar; charset=utf-8", **condition}, answer=answer)
        if status not in (200, 201, 204):
            raise CardDAVError(f"PUT {url}: HTTP {status}", status)
        return {k.lower(): v for k, v in answer.items()}.get("etag", "")

    def delete(self, url: str, etag: str = "") -> None:
        try:
            self.request("DELETE", url, headers={"If-Match": etag} if etag else {})
        except CardDAVError as e:
            if e.status != 404:      # (gone already)
                raise

    def fetch_events(self, calendar: str, hrefs: list[str]) -> dict[str, tuple[str, str]]:
        try:
            return self._multiget_events(calendar, hrefs)
        except CardDAVError as e:
            # Google's CalDAV with read-only access (calendar.readonly) refuses every REPORT
            # but lets each event be read: one at a time then, a few at once.
            if e.status != 403:
                raise
        with ThreadPoolExecutor(8) as pool:
            return dict(zip(hrefs, pool.map(self._get_event, hrefs)))

    def _get_event(self, href: str) -> tuple[str, str]:
        """("", the event): its etag is the one listed. (Thousands of them on a first sync:
        a connection that fails is tried again, once.)"""
        try:
            status, data, _ = self.request("GET", href)
        except CardDAVError as e:
            if e.status:
                raise
            status, data, _ = self.request("GET", href)
        if status != 200:
            raise CardDAVError(f"GET {href}: HTTP {status}", status)
        return "", data.decode()

    def _multiget_events(self, calendar: str, hrefs: list[str]) -> dict[str, tuple[str, str]]:
        out = {}
        for i in range(0, len(hrefs), BATCH):
            paths = "".join(f"<d:href>{_xml(urllib.parse.urlsplit(h).path)}</d:href>" for h in hrefs[i:i + BATCH])
            body = (f'<?xml version="1.0"?><cal:calendar-multiget xmlns:d="DAV:" xmlns:cal="{CAL}">'
                    f"<d:prop><d:getetag/><cal:calendar-data/></d:prop>{paths}</cal:calendar-multiget>")
            status, data, url = self.request("REPORT", calendar, body, "1")
            if status != 207:
                raise CardDAVError(f"REPORT {calendar}: HTTP {status}")
            for resp in ET.fromstring(data).findall("d:response", NS):
                ics = resp.findtext(".//cal:calendar-data", None, NS)
                if ics:
                    href = urllib.parse.urljoin(url, resp.findtext("d:href", "", NS))
                    out[href] = (resp.findtext(".//d:getetag", "", NS), ics)
        return out

    def list_cards(self, book: str) -> dict[str, str]:
        root, url = self.propfind(book, "<d:getetag/><d:resourcetype/>", "1")
        cards = {}
        for resp in root.findall("d:response", NS):
            href = urllib.parse.urljoin(url, resp.findtext("d:href", "", NS))
            if href.rstrip("/") == book.rstrip("/") or resp.find(".//d:resourcetype/d:collection", NS) is not None:
                continue
            etag = resp.findtext(".//d:getetag", "", NS)
            if etag:
                cards[href] = etag
        return cards

    def fetch(self, book: str, hrefs: list[str]) -> dict[str, tuple[str, str]]:
        out = {}
        for i in range(0, len(hrefs), BATCH):
            paths = "".join(f"<d:href>{_xml(urllib.parse.urlsplit(h).path)}</d:href>" for h in hrefs[i:i + BATCH])
            body = (f'<?xml version="1.0"?><c:addressbook-multiget xmlns:d="DAV:" xmlns:c="{CARD}">'
                    f"<d:prop><d:getetag/><c:address-data/></d:prop>{paths}</c:addressbook-multiget>")
            status, data, url = self.request("REPORT", book, body, "1")
            if status != 207:
                raise CardDAVError(f"REPORT {book}: HTTP {status}")
            for resp in ET.fromstring(data).findall("d:response", NS):
                card = resp.findtext(".//c:address-data", None, NS)
                if card:
                    href = urllib.parse.urljoin(url, resp.findtext("d:href", "", NS))
                    out[href] = (resp.findtext(".//d:getetag", "", NS), card)
        return out


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None  # the caller follows redirects itself, keeping the method


def _href(root: ET.Element, prop: str) -> str:
    el = root.find(f".//{prop}/d:href", NS)
    return (el.text or "").strip() if el is not None else ""


def _last_segment(url: str) -> str:
    return urllib.parse.unquote(urllib.parse.urlsplit(url).path.rstrip("/").rsplit("/", 1)[-1])


def _xml(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
