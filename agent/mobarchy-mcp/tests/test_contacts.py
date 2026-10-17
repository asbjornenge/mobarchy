import pytest

from mobarchy_mcp import contacts

ICLOUD = (
    "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Olsen;Per;;;\r\nFN:Per Olsen\r\n"
    "item1.TEL;type=CELL;type=VOICE;type=pref:+47 912 34 567\r\n"
    "item1.X-ABLabel:mobil\r\nEMAIL;type=INTERNET:per@example.no\r\n"
    "NOTE:a long note that is folded across lines because it goes on and on and on and on\r\n "
    " and on\r\nEND:VCARD\r\n"
    "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Kari Nordmann\r\nTEL;TYPE=HOME:22 33 44 55\r\n"
    "NICKNAME:Kari-mor\r\nEND:VCARD\r\n"
)


@pytest.fixture(autouse=True)
def home(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))


def test_parse_icloud_export():
    cards = contacts.parse(ICLOUD)
    assert [c.name for c in cards] == ["Per Olsen", "Kari Nordmann"]
    assert cards[0].phones == [{"number": "+47 912 34 567", "type": "cell"}]
    assert cards[0].emails == ["per@example.no"]
    assert cards[0].first("NOTE").endswith("on and on")  # unfolded
    assert cards[1].nicknames == ["Kari-mor"]


def test_import_search_and_relation():
    assert contacts.import_vcards(ICLOUD)["added"] == 2
    assert contacts.search("per")[0]["name"] == "Per Olsen"
    assert contacts.search("91234567")[0]["name"] == "Per Olsen"  # however the number is written
    assert contacts.search("far") == []
    contacts.set_relation("Per", "far")
    assert contacts.search("Far")[0]["name"] == "Per Olsen"
    # one person per word: it moves
    contacts.set_relation("Kari Nordmann", "far")
    assert [c["name"] for c in contacts.search("far")] == ["Kari Nordmann"]
    assert "relations" not in contacts.search("Per Olsen")[0]


def test_rewrite_keeps_what_it_does_not_know():
    contacts.import_vcards(ICLOUD)
    contacts.set_relation("Per Olsen", "pappa")
    text = contacts.path().read_text()
    assert "item1.X-ABLabel:mobil" in text
    assert contacts.parse(text)[0].first("NOTE").endswith("on and on")


def test_add_and_name_for():
    contacts.add("Ola Hansen", ["+4799887766"], relation="sjef")
    assert contacts.name_for("99 88 77 66") == "Ola Hansen"
    with pytest.raises(contacts.ContactError):
        contacts.add("ola hansen", ["123"])


def test_import_merges_numbers():
    contacts.add("Per Olsen", ["+4791234567"])
    r = contacts.import_vcards(ICLOUD)
    assert r == {"added": 1, "numbers_merged": 0, "total": 2}


def test_long_line_folding_is_utf8_safe():
    contacts.add("Ærlige Østen Åsheim med et veldig langt navn som går over syttifem tegn ja", ["1234567"])
    assert contacts.load()[0].name.startswith("Ærlige Østen Åsheim")
