import pytest

from mobarchy_mcp import browser


def test_web_addresses_only():
    assert browser.check_url("vg.no") == "https://vg.no"
    assert browser.check_url("http://example.com/x") == "http://example.com/x"
    for bad in ("file:///etc/passwd", "javascript:alert(1)", "about:blank"):
        with pytest.raises(browser.BrowserError):
            browser.check_url(bad)


def test_tabs_parsed():
    tabs = browser.parse_tabs(["VG\thttps://www.vg.no/", "Yr\thttps://www.yr.no/"], 1)
    assert tabs == [{"title": "VG", "url": "https://www.vg.no/", "current": False},
                    {"title": "Yr", "url": "https://www.yr.no/", "current": True}]
