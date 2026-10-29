import json

import pytest

from mobarchy_mcp import widgets

RSS = b"""<?xml version="1.0"?>
<rss version="2.0" xmlns:media="http://search.yahoo.com/mrss/"><channel><title>NRK</title>
<item><title>Nyhet &amp; mer</title><link>https://www.nrk.no/a</link>
<description>&lt;p&gt;Det skjedde &lt;b&gt;noe&lt;/b&gt;&lt;/p&gt;</description>
<media:content url="https://gfx.nrk.no/a.jpg" medium="image"/></item>
<item><title>To</title><link>javascript:alert(1)</link>
<enclosure url="https://gfx.nrk.no/b.jpg" type="image/jpeg"/></item>
</channel></rss>"""

ATOM = b"""<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom"><title>Blog</title>
<entry><title>Post</title><link rel="alternate" href="https://blog.example/post"/>
<summary>Short</summary></entry></feed>"""


@pytest.fixture(autouse=True)
def dirs(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("XDG_STATE_HOME", str(tmp_path / "state"))
    # images are not fetched in tests
    monkeypatch.setattr(widgets, "_store_image", lambda wid, url, again=False: f"/stored/{url.rsplit('/', 1)[-1]}")


def test_parse_rss():
    items = widgets.parse_feed(RSS)
    assert items[0] == {"title": "Nyhet & mer", "subtitle": "Det skjedde noe", "url": "https://www.nrk.no/a",
                        "image": "https://gfx.nrk.no/a.jpg"}
    # (a link that isn't http(s) is dropped)
    assert items[1]["url"] == "" and items[1]["image"] == "https://gfx.nrk.no/b.jpg"


def test_parse_atom():
    assert widgets.parse_feed(ATOM) == [{"title": "Post", "subtitle": "Short", "url": "https://blog.example/post",
                                         "image": ""}]


def test_parse_not_a_feed():
    with pytest.raises(widgets.WidgetError):
        widgets.parse_feed(b"<html><body>")


def test_json_sources():
    doc = json.dumps({"data": {"children": [{"t": "A", "u": "https://a"}, {"t": "", "u": "x"}]},
                      "list": [{"main": {"temp": 3.5}}], "note": "**hi**"}).encode()
    src = {"items": "data.children", "fields": {"title": "t", "url": "u"}}
    assert widgets.from_json(doc, src, "list") == {"items": [{"title": "A", "subtitle": "", "url": "https://a", "image": ""}]}
    assert widgets.from_json(doc, {"value": "list.0.main.temp"}, "value") == {"value": "3.5", "caption": ""}
    assert widgets.from_json(doc, {"text": "note"}, "text") == {"text": "**hi**"}
    with pytest.raises(widgets.WidgetError):
        widgets.from_json(doc, {"items": "note"}, "list")


def test_check():
    spec = widgets.check({"title": "NRK", "template": "list", "source": {"type": "feed", "url": "https://x/rss"},
                          "refresh_minutes": 1, "board": {"screens": "wide"}})
    assert spec["refresh_minutes"] == widgets.MIN_REFRESH
    assert spec["board"] == {"cols": 4, "rows": 2, "screens": "wide"}
    for bad in ({"title": "", "source": {"type": "static", "content": {}}},
                {"title": "x", "template": "html", "source": {"type": "static", "content": {}}},
                {"title": "x", "source": {"type": "feed", "url": "file:///etc/passwd"}},
                {"title": "x", "source": {"type": "static", "content": {}}, "board": {"cols": 3, "rows": 3}},
                {"title": "x", "source": {"type": "static", "content": {}}, "board": {"screens": "big"}}):
        with pytest.raises(widgets.WidgetError):
            widgets.check(bad)


def test_create_update_remove(monkeypatch):
    monkeypatch.setattr(widgets, "fetch", lambda url, limit=0: (RSS, "application/rss+xml"))
    made = widgets.create({"title": "NRK nyheter", "template": "list",
                           "source": {"type": "feed", "url": "https://www.nrk.no/toppsaker.rss"},
                           "board": {"screens": "wide"}})
    assert made["id"] == "nrk-nyheter" and made["error"] is None and made["shown"] == {"items": 2}
    content = widgets.read_content("nrk-nyheter")
    assert content["items"][0]["image"] == "/stored/a.jpg"

    # no board of the owner's: Mobarchy's, with the widget at the end
    listed = widgets.list_widgets()
    assert listed["board"][-1] == {"kind": "widget:nrk-nyheter", "cols": 4, "rows": 2, "screens": "wide"}
    assert listed["widgets"][0]["on_board"]

    # the owner's board: the widget is added to it, and follows its changes
    widgets.set_board([{"kind": "frigg", "cols": 4, "rows": 2}])
    second = widgets.create({"title": "NRK nyheter", "template": "text",
                             "source": {"type": "static", "content": {"text": "hei"}}})
    assert second["id"] == "nrk-nyheter-2"
    widgets.update("nrk-nyheter-2", {"board": {"cols": 4, "rows": 4, "screens": "narrow"}})
    home = widgets.load_home()
    assert home["board"][-1] == {"kind": "widget:nrk-nyheter-2", "cols": 4, "rows": 4, "screens": "narrow"}
    # another template: its default size
    widgets.update("nrk-nyheter-2", {"template": "value", "source": {"type": "static", "content": {"value": "1"}}})
    assert widgets.load_home()["widgets"]["nrk-nyheter-2"]["board"] == {"cols": 2, "rows": 1, "screens": "narrow"}

    widgets.remove("nrk-nyheter-2")
    home = widgets.load_home()
    assert "nrk-nyheter-2" not in home["widgets"]
    assert all(e["kind"] != "widget:nrk-nyheter-2" for e in home["board"])
    assert widgets.read_content("nrk-nyheter-2") == {}


def test_failure_keeps_content(monkeypatch):
    monkeypatch.setattr(widgets, "fetch", lambda url, limit=0: (RSS, ""))
    widgets.create({"title": "n", "source": {"type": "feed", "url": "https://x/rss"}})

    def down(url, limit=0):
        raise OSError("network is unreachable")
    monkeypatch.setattr(widgets, "fetch", down)
    out = widgets.refresh(["n"])
    assert out["refreshed"]["n"]["error"] == "network is unreachable"
    assert len(widgets.read_content("n")["items"]) == 2


def test_board_checks():
    with pytest.raises(widgets.WidgetError):
        widgets.set_board([{"kind": "widget:nope", "cols": 4, "rows": 2}])
    with pytest.raises(widgets.WidgetError):
        widgets.set_board([{"kind": "frigg", "cols": 4, "rows": 2}, {"kind": "frigg", "cols": 4, "rows": 2}])
    widgets.set_board([{"kind": "weather", "cols": 2, "rows": 1, "screens": "wide"}])
    assert widgets.load_home()["board"] == [{"kind": "weather", "cols": 2, "rows": 1, "screens": "wide"}]
    widgets.set_board([])
    assert "board" not in widgets.load_home()


def test_due():
    spec = widgets.check({"title": "n", "source": {"type": "static", "content": {"text": "x"}}})
    assert widgets.due("n", spec)
    widgets.refresh_one("n", spec)
    assert not widgets.due("n", spec)
