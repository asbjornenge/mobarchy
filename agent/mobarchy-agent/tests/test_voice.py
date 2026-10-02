import array
import wave

from mobarchy_agent import voice


def test_audio_ctx():
    assert voice.audio_ctx(10) == 564
    assert voice.audio_ctx(60) == 1500


def test_short_recording_is_empty(tmp_path, monkeypatch):
    f = tmp_path / "short.wav"
    with wave.open(str(f), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
        w.writeframes(b"\0\0" * 1600)  # 0.1 s
    monkeypatch.setattr(voice, "whisper_bin", lambda: "/bin/true")
    monkeypatch.setattr(voice, "MODEL_FILE", f)
    assert voice.transcribe(f) == ""


def test_silence_is_empty(tmp_path, monkeypatch):
    f = tmp_path / "quiet.wav"
    with wave.open(str(f), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
        w.writeframes(array.array("h", [5, -5] * 16000).tobytes())  # 2 s of near silence
    monkeypatch.setattr(voice, "whisper_bin", lambda: "/bin/false")  # would fail if run
    monkeypatch.setattr(voice, "MODEL_FILE", f)
    assert voice.transcribe(f) == ""
