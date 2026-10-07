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


def test_wake_waits_talks_and_waits_again(monkeypatch):
    import asyncio

    class Wake:
        n = 0

        def feed(self, pcm):
            self.n += 1
            return [0.9 if self.n == 5 else 0.0]

        def reset(self):
            pass

    class Session:
        running = False

        async def start(self):
            self.running = True

        async def close(self):
            self.running = False

    class Vad:
        def prob(self, window):
            return 0.0

    async def mic():
        while True:
            yield bytes(1024)
            await asyncio.sleep(0.002)

    async def nothing(*a):
        pass

    monkeypatch.setattr(talk, "Vad", Vad)
    monkeypatch.setattr(talk, "microphone", mic)
    monkeypatch.setattr(talk, "FOLLOW_UP_S", 0.05)
    monkeypatch.setattr(talk.Speaker, "play", nothing)
    monkeypatch.setattr(talk.Speaker, "drain", nothing)
    states = []
    t = talk.Talk(Session(), out=lambda kind, **f: states.append(f.get("state")), wake=Wake())

    async def run():
        task = asyncio.create_task(t.listen())
        while states.count("waiting") < 2:
            await asyncio.sleep(0.01)
        t.stopping = True
        await task

    asyncio.run(asyncio.wait_for(run(), 5))
    assert states == ["waiting", "listening", "waiting"]


def test_frigg_goes_on_with_the_chats_session(monkeypatch, tmp_path):
    import asyncio
    from mobarchy_agent import agents, conversations

    monkeypatch.setattr(conversations, "ROOT", tmp_path)
    conv = conversations.Conversation()
    conv.sessions["claude"] = "chat-1"
    monkeypatch.setattr(agents, "current_conversation", lambda new=False: conv)

    class Session:
        def __init__(self):
            self.running, self.session_id, self.starts = False, None, []

        async def start(self, resume=None):
            self.running, self.session_id = True, resume
            self.starts.append(resume)

        async def close(self):
            self.running, self.session_id = False, None

    monkeypatch.setattr(talk, "Vad", lambda: None)
    t = talk.Talk(Session(), out=lambda *a, **k: None)
    asyncio.run(t.join_chat())
    asyncio.run(t.join_chat())          # (the same: no new start)
    conv.sessions["claude"] = "chat-2"  # the chat went on meanwhile
    asyncio.run(t.join_chat())
    assert t.session.starts == ["chat-1", "chat-2"]


def test_who_is_speaking(monkeypatch, tmp_path):
    class Voice:
        def __init__(self, score):
            self.s = score

        def score(self, pcm):
            return self.s

    monkeypatch.setattr(talk, "Vad", lambda: None)
    monkeypatch.setattr(talk, "VOICE_PROFILE", tmp_path / "voice-profile.json")
    monkeypatch.setattr(talk, "phone_locked", lambda: False)
    t = talk.Talk(talk.ClaudeSession([]), guest=talk.ClaudeSession([]), voice=None)
    assert t.who(b"") == (True, None)            # unlocked: the owner
    monkeypatch.setattr(talk, "phone_locked", lambda: True)
    assert t.who(b"") == (True, None)            # it locked itself during the conversation
    t.set_state("waiting")                       # a new conversation
    assert t.who(b"") == (False, None)           # locked, no voice learnt: a guest
    t.voice = Voice(0.8)
    assert t.who(b"") == (True, 0.8)
    t.voice = Voice(0.3)
    assert t.who(b"") == (False, 0.3)


def test_voice_match_features_shape():
    import numpy as np
    from mobarchy_agent import speaker
    f = speaker.features(np.sin(np.arange(16000) / 10).astype(np.float32))
    assert f.shape == (80, 101)
    assert abs(float(f.mean())) < 1e-3


def test_verifier_learns_a_pattern():
    import numpy as np
    from mobarchy_agent import wake
    rng = np.random.default_rng(1)
    pos = rng.normal(1.0, 0.3, (20, 16, 96))
    neg = rng.normal(0.0, 0.3, (200, 16, 96))
    v = wake.Verifier.train(pos, neg)
    assert v.prob(rng.normal(1.0, 0.3, (16, 96))) > 0.9
    assert v.prob(rng.normal(0.0, 0.3, (16, 96))) < 0.1
    again = wake.Verifier.from_json(v.to_json())
    assert abs(again.prob(pos[0]) - v.prob(pos[0])) < 1e-3
