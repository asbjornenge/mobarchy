"""Speech to text on the phone: whisper.cpp with NB-Whisper (the National Library of
Norway's Whisper, for Norwegian and English). Nothing leaves the phone, and it works offline.

A recording of a few seconds takes a couple of seconds on comet (small, q5_0, 4 threads):
the encoder only looks at as much audio as was recorded (audio_ctx), not Whisper's 30 s.
"""

from __future__ import annotations

import array
import math
import shutil
import subprocess
import wave
from pathlib import Path

MODELS = Path.home() / ".local/share/mobarchy/models"
MODEL_FILE = MODELS / "nb-whisper-small-q5_0.bin"
MODEL_URL = "https://huggingface.co/NbAiLab/nb-whisper-small/resolve/main/ggml-model-q5_0.bin"
MODEL_BYTES = 175_209_680
THREADS = 4  # the fast cores; more threads are slower here
# Whisper makes up text for silence ("Teksting av ..."): below this loudness (RMS of the
# loudest 100 ms, 16-bit samples) nothing was said. Speech measures in the hundreds or more.
SPEECH_RMS = 150


def whisper_bin() -> str | None:
    return shutil.which("whisper-cli") or next(
        (str(p) for p in (Path("/opt/mobarchy/whisper/bin/whisper-cli"),) if p.exists()), None)


def ready() -> bool:
    return whisper_bin() is not None and MODEL_FILE.exists()


def audio_ctx(seconds: float) -> int:
    """Whisper's encoder frames for this much audio (50 a second), with some margin;
    1500 is its whole 30 s window."""
    return min(1500, int(seconds * 50) + 64)


def loudest(samples, rate: int) -> float:
    """RMS of the loudest 100 ms."""
    step = rate // 10
    best = 0.0
    for i in range(0, len(samples), step):
        chunk = samples[i:i + step]
        if chunk:
            best = max(best, math.sqrt(sum(x * x for x in chunk) / len(chunk)))
    return best


def transcribe(path: Path, language: str = "no") -> str:
    exe = whisper_bin()
    if not exe:
        raise RuntimeError("whisper.cpp (whisper-cli) is not installed")
    if not MODEL_FILE.exists():
        raise RuntimeError(f"the speech model is missing: run `mobarchy-agent setup voice`")
    with wave.open(str(path)) as w:
        rate = w.getframerate()
        samples = array.array("h", w.readframes(w.getnframes())) if w.getsampwidth() == 2 else None
    seconds = len(samples) / rate if samples is not None else 0
    if seconds < 0.3 or loudest(samples, rate) < SPEECH_RMS:
        return ""
    r = subprocess.run([exe, "-m", str(MODEL_FILE), "-f", str(path), "-l", language,
                        "-t", str(THREADS), "-ac", str(audio_ctx(seconds)), "-nt", "-np"],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "whisper failed")
    return " ".join(r.stdout.split())


def setup() -> None:
    """Download the model (175 MB) if it isn't there."""
    if MODEL_FILE.exists():
        return
    MODELS.mkdir(parents=True, exist_ok=True)
    part = MODEL_FILE.with_suffix(".part")
    subprocess.run(["curl", "-fsSL", "-C", "-", "-o", str(part), MODEL_URL], check=True)
    if part.stat().st_size != MODEL_BYTES:
        raise RuntimeError("the download is incomplete")
    part.rename(MODEL_FILE)
