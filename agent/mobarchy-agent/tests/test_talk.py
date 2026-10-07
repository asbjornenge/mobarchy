from mobarchy_agent import talk


def test_sentences_come_as_they_complete():
    s = talk.Sentences()
    assert s.feed("Hei! Jeg er Fri") == ["Hei!"]
    assert s.feed("gg. Hva kan jeg") == ["Jeg er Frigg."]
    assert s.feed(" gjøre for deg") == []
    assert s.flush() == ["Hva kan jeg gjøre for deg"]


def test_sentences_lose_markdown_and_links():
    s = talk.Sentences()
    assert s.feed("**Oslo** i dag: [yr](https://yr.no) sier sol. ") == ["Oslo i dag:", "yr sier sol."]


def test_long_runs_break_at_a_comma():
    s = talk.Sentences()
    out = s.feed("a" * 120 + ", " + "b" * 120)
    assert len(out) == 1 and out[0].endswith(",")


def test_endpointer_finds_a_turn():
    ep = talk.Endpointer()
    probs = [0.1] * 5 + [0.9] * 30 + [0.1] * 40
    events = [(i, e) for i, p in enumerate(probs) if (e := ep.step(p))]
    assert events[0] == (7, "start")          # third window over the threshold
    assert events[1][1] == "end"
    silence_windows = events[1][0] - 34
    assert abs(silence_windows * talk.WINDOW / talk.RATE - talk.END_SILENCE_S) < 0.05


def test_stream_lines():
    line = '{"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Hei"}}}'
    assert talk.parse_stream_line(line) == [("text", "Hei")]
    tool = '{"type":"assistant","message":{"content":[{"type":"tool_use","name":"mcp__mobarchy__battery_status"}]}}'
    assert talk.parse_stream_line(tool) == [("tool", "battery_status")]
    assert talk.parse_stream_line('{"type":"result","is_error":false}') == [("end", "")]
    assert talk.parse_stream_line("not json") == []


def test_blocks_are_not_glued():
    s = talk.Sentences()
    stop = '{"type":"stream_event","event":{"type":"content_block_stop"}}'
    out = s.feed("Jeg sjekker") + s.feed(talk.parse_stream_line(stop)[0][1]) + s.feed("været nå. ")
    assert out == ["Jeg sjekker været nå."]


def test_language(monkeypatch):
    monkeypatch.delenv("MOBARCHY_TALK_LANGUAGE", raising=False)
    monkeypatch.setenv("LANG", "nb_NO.UTF-8")
    assert talk.talk_language() == "nb"
    monkeypatch.setenv("LANG", "C")
    assert talk.talk_language() == "en"
    monkeypatch.setenv("MOBARCHY_TALK_LANGUAGE", "no")
    assert talk.talk_language() == "no"


def test_whisper_text():
    from mobarchy_agent import voice
    out = "\n[00:00:00.000 --> 00:00:02.500]   Set an alarm.\n[00:00:02.500 --> 00:00:03.000]   Now.\n"
    assert voice.text_of(out) == "Set an alarm. Now."


def _voice(key, quality, speakers=1):
    family = key.split("_")[0]
    path = f"{family}/{key.rsplit('-', 2)[0]}/{key.split('-')[1]}/{quality}"
    return {"key": key, "language": {"family": family}, "quality": quality, "num_speakers": speakers,
            "files": {f"{path}/{key}.onnx": {}, f"{path}/{key}.onnx.json": {}}}


def test_choose_voice():
    cat = {v["key"]: v for v in [_voice("de_DE-mls-medium", "medium", 236), _voice("de_DE-kerstin-low", "low"),
                                 _voice("de_DE-thorsten-medium", "medium"), _voice("en_US-amy-medium", "medium"),
                                 _voice("en_US-lessac-medium", "medium")]}
    assert talk.choose_voice(cat, "de") == ("de_DE-thorsten-medium", "de/de_DE/thorsten/medium")
    assert talk.choose_voice(cat, "en")[0] == "en_US-lessac-medium"
    assert talk.choose_voice(cat, "xx") is None


def test_installed_voice(tmp_path, monkeypatch):
    monkeypatch.setattr(talk, "MODELS", tmp_path / "system")
    monkeypatch.setattr(talk, "USER_MODELS", tmp_path)
    (tmp_path / "no_NO-talesyntese-medium.onnx").touch()
    (tmp_path / "de_DE-thorsten-medium.onnx").touch()
    assert talk.installed_voice("nb") == "no_NO-talesyntese-medium"
    assert talk.installed_voice("de") == "de_DE-thorsten-medium"
    assert talk.installed_voice("en") is None


def test_services(monkeypatch):
    for k in ("MOBARCHY_TALK_STT", "MOBARCHY_TALK_TTS", "OPENAI_API_KEY", "ELEVENLABS_API_KEY"):
        monkeypatch.delenv(k, raising=False)
    assert (talk.stt(), talk.tts()) == ("whisper", "piper")
    monkeypatch.setenv("MOBARCHY_TALK_TTS", "elevenlabs")
    assert talk.tts() == "piper"   # (no key: the phone's)
    monkeypatch.setenv("ELEVENLABS_API_KEY", "k")
    assert talk.tts() == "elevenlabs"


def test_pcm_chunks_are_whole_samples():
    import io
    out = list(talk.pcm_chunks(io.BytesIO(b"\x01" * 9), 22050, size=3))
    assert [len(a) for a, _ in out] == [2, 4, 2] and all(len(a) % 2 == 0 for a, _ in out)
