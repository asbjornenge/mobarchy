import json

import pytest

from mobarchy_mcp import contacts, phone


@pytest.fixture(autouse=True)
def home(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))
    contacts.import_vcards("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Per Olsen\r\nTEL;TYPE=HOME:22 33 44 55\r\n"
                           "TEL;TYPE=CELL:+47 912 34 567\r\nEND:VCARD\r\n")
    contacts.set_relation("Per Olsen", "far")
    contacts.add("Per Hansen", ["99887766"])


def test_resolve_by_relation_prefers_mobile():
    assert phone.resolve("far") == ("+4791234567", "Per Olsen")


def test_resolve_number_and_ambiguity():
    assert phone.resolve("912 34 567") == ("91234567", "Per Olsen")
    with pytest.raises(phone.PhoneError, match="several"):
        phone.resolve("Per")
    with pytest.raises(phone.PhoneError, match="no contact"):
        phone.resolve("mamma")


def test_send_sms_stores_in_the_existing_thread(monkeypatch):
    sent = []
    monkeypatch.setattr(phone, "modem", lambda: "/ril_0")
    monkeypatch.setattr(phone, "_busctl", lambda *a, **k: sent.append(a))
    phone.messages_path().parent.mkdir(parents=True, exist_ok=True)
    phone.messages_path().write_text(json.dumps({"+4791234567": [{"text": "hei", "incoming": True, "time": 1}]}))
    assert phone.send_sms("far", "hei pappa") == {"sent_to": "Per Olsen", "number": "+4791234567"}
    assert sent[0][-3:] == ("ss", "+4791234567", "hei pappa")
    data = json.loads(phone.messages_path().read_text())
    assert list(data) == ["+4791234567"] and [m["text"] for m in data["+4791234567"]] == ["hei", "hei pappa"]
    msgs = phone.list_sms("far")["messages"]
    assert msgs[0]["to"] == "Per Olsen" and msgs[1]["from"] == "Per Olsen"
