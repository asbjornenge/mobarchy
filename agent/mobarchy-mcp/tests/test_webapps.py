from mobarchy_mcp import webapps

PAGE = """<html><head><title> Yr </title>
<link rel="manifest" href="/manifest.webmanifest">
<link rel="icon" href="/favicon-32.png" sizes="32x32">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
</head><body></body></html>"""


def test_head():
    h = webapps._Head()
    h.feed(PAGE)
    assert h.title.strip() == "Yr"
    assert h.manifest == "/manifest.webmanifest"
    assert (180, "/apple-touch-icon.png") in h.icons and (32, "/favicon-32.png") in h.icons


def test_best_icon_prefers_large_raster():
    icons = [{"src": "i/192.png", "sizes": "192x192"}, {"src": "i/512.png", "sizes": "512x512"},
             {"src": "i/any.svg", "sizes": "any", "type": "image/svg+xml"}]
    assert webapps.best_icon(icons, [(180, "https://yr.no/a.png")], "https://yr.no/m.json") == "https://yr.no/i/512.png"
    assert webapps.best_icon([], [], "https://yr.no/") == "https://yr.no/favicon.ico"


def test_slug_and_entry():
    assert webapps.slug("Yr – været!") == "yr-v-ret"
    entry = webapps.desktop_entry("Yr", "https://www.yr.no/", "/icons/yr.png", "mobarchy-webapp-yr")
    assert "Exec=mobarchy-browser --app=https://www.yr.no/ --app-id=mobarchy-webapp-yr" in entry
    assert "StartupWMClass=mobarchy-webapp-yr" in entry and "Icon=/icons/yr.png" in entry
    entry = webapps.desktop_entry("Yr", "https://www.yr.no/", "/icons/yr.png", "mobarchy-webapp-yr", "chromium")
    assert "Exec=chromium --app=https://www.yr.no/" in entry and "StartupWMClass=chrome-www.yr.no__-Default" in entry


def test_wm_class():
    assert webapps.wm_class("https://www.yr.no/nb") == "chrome-www.yr.no__nb-Default"
    assert webapps.wm_class("https://example.com") == "chrome-example.com__-Default"
