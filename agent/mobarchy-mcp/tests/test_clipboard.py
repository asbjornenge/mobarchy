import subprocess

from mobarchy_mcp import clipboard


def fake_run(stdout=b"", returncode=0, calls=None):
    def run(cmd, **kw):
        if calls is not None:
            calls.append((cmd, kw))
        return subprocess.CompletedProcess(cmd, returncode, stdout, b"")
    return run


def test_get_text(monkeypatch):
    monkeypatch.setattr(subprocess, "run", fake_run("hei på deg".encode()))
    assert clipboard.get() == {"text": "hei på deg"}


def test_get_empty_when_no_text(monkeypatch):
    monkeypatch.setattr(subprocess, "run", fake_run(returncode=1))
    assert clipboard.get() == {"text": ""}


def test_get_truncates(monkeypatch):
    monkeypatch.setattr(subprocess, "run", fake_run(b"x" * (clipboard.MAX_CHARS + 5)))
    out = clipboard.get()
    assert out["truncated"] and out["length"] == clipboard.MAX_CHARS + 5
    assert len(out["text"]) == clipboard.MAX_CHARS


def test_set_does_not_hold_pipes(monkeypatch):
    calls = []
    monkeypatch.setattr(subprocess, "run", fake_run(calls=calls))
    assert clipboard.set_text("kode 1234") == {"ok": True, "length": 9}
    cmd, kw = calls[0]
    assert cmd[0] == "wl-copy" and kw["input"] == b"kode 1234"
    assert kw["stdout"] is subprocess.DEVNULL and kw["start_new_session"]
