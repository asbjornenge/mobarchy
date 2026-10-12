import base64
import json
import urllib.parse

import pytest

from mobarchy_agent import scribe


def test_url_asks_for_norwegian_and_the_names(monkeypatch):
    monkeypatch.setenv("MOBARCHY_TALK_KEYTERMS", "Kari, Stavanger")
    q = urllib.parse.parse_qs(urllib.parse.urlparse(scribe.url("nb")).query)
    assert q["model_id"] == ["scribe_v2_realtime"]
    assert q["language_code"] == ["no"]
    assert q["commit_strategy"] == ["manual"]
    assert q["audio_format"] == ["pcm_16000"]
    assert "Frigg" in q["keyterms"] and "Kari" in q["keyterms"] and "Stavanger" in q["keyterms"]
    assert "language_code" not in urllib.parse.parse_qs(urllib.parse.urlparse(scribe.url("auto")).query)


def test_audio_chunks():
    m = json.loads(scribe.chunk(b"\x01\x02", commit=True))
    assert m["message_type"] == "input_audio_chunk" and m["commit"] is True
    assert base64.b64decode(m["audio_base_64"]) == b"\x01\x02" and m["sample_rate"] == 16000


def test_messages():
    assert scribe.committed_text({"message_type": "committed_transcript", "text": " Hei Frigg "}) == "Hei Frigg"
    assert scribe.committed_text({"message_type": "partial_transcript", "text": "Hei"}) is None
    assert scribe.committed_text({"message_type": "session_started"}) is None
    with pytest.raises(scribe.ScribeError, match="quota_exceeded"):
        scribe.committed_text({"message_type": "quota_exceeded", "error": "out of credits"})
    with pytest.raises(scribe.ScribeError):
        scribe.committed_text({"message_type": "auth_error", "error": "bad key"})


def test_context_goes_with_the_first_audio():
    assert json.loads(scribe.chunk(b"x", previous="Hvilket tema vil du ha?"))["previous_text"] == "Hvilket tema vil du ha?"
    assert "previous_text" not in json.loads(scribe.chunk(b"x"))
