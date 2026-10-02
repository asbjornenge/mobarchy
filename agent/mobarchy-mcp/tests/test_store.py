import gzip
import xml.etree.ElementTree as ET

from mobarchy_mcp import store

COMPONENT = """<component type="desktop-application">
  <id>io.bassi.Amberol</id>
  <pkgname>amberol</pkgname>
  <name>Amberol</name>
  <name xml:lang="nb">Amberol</name>
  <summary>Plays music, and nothing else</summary>
  <summary xml:lang="ar">x</summary>
  <description>
    <p>Amberol is a music player.</p>
    <p xml:lang="ar">x</p>
    <ul><li>Drag and drop</li><li>Shuffle</li></ul>
  </description>
  <icon type="cached" width="64" height="64">amberol_io.bassi.Amberol.jxl</icon>
  <icon type="cached" width="128" height="128">amberol_io.bassi.Amberol.jxl</icon>
  <launchable type="desktop-id">io.bassi.Amberol.desktop</launchable>
  <screenshots><screenshot type="default"><image type="source">https://example.org/a.png</image></screenshot></screenshots>
</component>"""

CATALOG = """
[[app]]
id = "amberol"
kind = "package"
package = "amberol"
category = "music"
tested = ["comet"]

[[app]]
id = "yr"
kind = "webapp"
name = "Yr"
summary = "Weather"
url = "https://www.yr.no"
category = "maps"
"""


def test_parse_component():
    info = store.parse_component(ET.fromstring(COMPONENT))
    assert info["name"] == "Amberol" and info["summary"] == "Plays music, and nothing else"
    assert info["description"] == "Amberol is a music player.\n\n• Drag and drop\n• Shuffle"
    assert info["icon_size"] == 128 and info["desktop_id"] == "io.bassi.Amberol"
    assert info["screenshots"] == ["https://example.org/a.png"]


def test_entries(tmp_path, monkeypatch):
    xml = tmp_path / "swcatalog/xml"
    xml.mkdir(parents=True)
    with gzip.open(xml / "extra.xml.gz", "wt") as f:
        f.write(f"<components>{COMPONENT}</components>")
    cat = tmp_path / "catalog.toml"
    cat.write_text(CATALOG)
    monkeypatch.setattr(store, "APPSTREAM", tmp_path / "swcatalog")
    monkeypatch.setattr(store, "CACHE", tmp_path / "cache")
    monkeypatch.setattr(store, "installed_packages", lambda: {"amberol"})
    monkeypatch.setattr(store, "webapp_icon", lambda app: "/icons/yr.png")
    monkeypatch.setattr(store.webapps, "listed", lambda: [{"app_id": "chrome-x-Default", "name": "Yr", "installed_by": "chromium"}])
    apps = {a["id"]: a for a in store.entries(store.load_catalog(cat))}
    assert apps["amberol"]["name"] == "Amberol" and apps["amberol"]["installed"]
    assert apps["amberol"]["app_id"] == "io.bassi.Amberol"
    assert apps["yr"]["installed"] and apps["yr"]["app_id"] == "chrome-x-Default"
    assert apps["yr"]["category_name"] == "Maps & weather"
