import json

import pytest

from mobarchy_mcp import themes

MOBARCHY = {"bg": "#1a1b26", "bgDark": "#16161e", "panel": "#24283b", "line": "#292e42",
            "fg": "#c0caf5", "muted": "#565f89", "accent": "#7aa2f7", "green": "#9ece6a",
            "red": "#f7768e", "magenta": "#bb9af7"}


@pytest.fixture
def dirs(tmp_path, monkeypatch):
    system = tmp_path / "system"
    system.mkdir()
    (system / "mobarchy.json").write_text(json.dumps({"name": "Mobarchy", "colors": MOBARCHY}))
    monkeypatch.setattr(themes, "SYSTEM_DIR", system)
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "data"))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setattr(themes, "installed_fonts", lambda: {"JetBrainsMono Nerd Font"})
    return tmp_path


def test_mobarchy_is_the_default(dirs):
    assert themes.current_id() == "mobarchy"
    assert themes.current()["colors"] == MOBARCHY
    assert themes.check(MOBARCHY, "JetBrainsMono Nerd Font") == []


def test_create_switches_and_lists(dirs):
    pika = dict(MOBARCHY, bg="#101014", accent="#ffcb05", red="#ff5c5c")
    themes.create_theme("pikachu", "Pikachu", pika, description="yellow")
    assert themes.current_id() == "pikachu"
    assert (dirs / "config/mobarchy/theme").read_text().strip() == "pikachu"
    listed = {t["id"]: t for t in themes.list_themes()["themes"]}
    assert listed["pikachu"]["own"] and listed["pikachu"]["active"]
    assert not listed["mobarchy"]["own"]


def test_unreadable_themes_are_refused(dirs):
    grey = dict(MOBARCHY, fg="#2a2b36")
    with pytest.raises(themes.ThemeError, match="text on the background"):
        themes.create_theme("murky", "Murky", grey)
    with pytest.raises(themes.ThemeError, match="missing colours"):
        themes.create_theme("half", "Half", {"bg": "#000000"})
    with pytest.raises(themes.ThemeError, match="not #rrggbb"):
        themes.create_theme("bad", "Bad", dict(MOBARCHY, red="crimson"))
    with pytest.raises(themes.ThemeError, match="not installed"):
        themes.create_theme("comic", "Comic", MOBARCHY, font="Comic Sans")
    assert themes.current_id() == "mobarchy"


def test_system_themes_stay(dirs):
    with pytest.raises(themes.ThemeError):
        themes.create_theme("mobarchy", "Mine", MOBARCHY)
    with pytest.raises(themes.ThemeError):
        themes.delete_theme("mobarchy")


def test_delete_active_falls_back(dirs):
    themes.create_theme("mine", "Mine", MOBARCHY)
    assert themes.delete_theme("mine")["active"] == "mobarchy"


def test_contrast_matches_wcag():
    assert round(themes.contrast("#000000", "#ffffff"), 1) == 21.0
    assert round(themes.contrast("#777777", "#ffffff"), 2) == 4.48


SVG = '<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10"><rect width="10" height="10" fill="#ffcb05"/></svg>'


def test_wallpaper_from_svg_and_file(dirs):
    themes.create_theme("pika", "Pika", MOBARCHY, wallpaper=SVG)
    t = json.loads((dirs / "data/mobarchy/themes/pika.json").read_text())
    assert t["wallpaper"] == "pika.wallpaper.svg"
    assert (dirs / "data/mobarchy/themes/pika.wallpaper.svg").read_text().startswith("<svg")
    png = dirs / "photo.png"
    png.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\0" * 32)
    assert themes.set_wallpaper("pika", str(png))["wallpaper"] == "pika.wallpaper.png"
    assert not (dirs / "data/mobarchy/themes/pika.wallpaper.svg").exists()
    themes.set_wallpaper("pika", "")
    assert "wallpaper" not in json.loads((dirs / "data/mobarchy/themes/pika.json").read_text())
    assert not list((dirs / "data/mobarchy/themes").glob("pika.wallpaper.*"))


def test_wallpaper_must_be_an_image(dirs, tmp_path):
    page = tmp_path / "page.html"
    page.write_text("<html><body>not an image</body></html>")
    with pytest.raises(themes.ThemeError, match="not a JPEG"):
        themes.create_theme("x", "X", MOBARCHY, wallpaper=str(page))
    with pytest.raises(themes.ThemeError, match="no such file"):
        themes.create_theme("x", "X", MOBARCHY, wallpaper="/nope.jpg")


def test_delete_takes_the_wallpaper(dirs):
    themes.create_theme("pika", "Pika", MOBARCHY, wallpaper=SVG)
    themes.delete_theme("pika")
    assert not list((dirs / "data/mobarchy/themes").glob("pika*"))


def test_cards_filled_or_outline(dirs):
    assert themes.current()["cards"] == "filled"
    themes.create_theme("airy", "Airy", MOBARCHY, cards="outline")
    assert themes.current()["cards"] == "outline"
    assert themes.set_cards("airy", "filled") == {"id": "airy", "cards": "filled"}
    with pytest.raises(themes.ThemeError):
        themes.set_cards("airy", "glass")
    with pytest.raises(themes.ThemeError):
        themes.set_cards("mobarchy", "outline")     # (Mobarchy's own stays as it is)
