"""Speech to text on the phone: whisper.cpp with NB-Whisper (the National Library of
Norway's Whisper, for Norwegian and English). Nothing leaves the phone, and it works offline.

A recording of a few seconds takes a couple of seconds on comet (small, q5_0, 4 threads):
the encoder only looks at as much audio as was recorded (audio_ctx), not Whisper's 30 s.
"""

from __future__ import annotations

import array
import math
import re
import shutil
import subprocess
import wave
from pathlib import Path

MODELS = Path.home() / ".local/share/mobarchy/models"
MODEL_NAME = "nb-whisper-small-q5_0.bin"
# the image's (the mobarchy-voice-model package), else one downloaded by `setup voice`
SYSTEM_MODEL = Path("/usr/share/mobarchy/models") / MODEL_NAME
USER_MODEL = MODELS / MODEL_NAME
MODEL_URL = "https://huggingface.co/NbAiLab/nb-whisper-small/resolve/main/ggml-model-q5_0.bin"
MODEL_BYTES = 175_209_680
THREADS = 4  # the fast cores; more threads are slower here
# Whisper makes up text for silence ("Teksting av ..."): below this loudness (RMS of the
# loudest 100 ms, 16-bit samples) nothing was said. Speech measures in the hundreds or more.
SPEECH_RMS = 150


def whisper_bin() -> str | None:
    return shutil.which("whisper-cli") or next(
        (str(p) for p in (Path("/opt/mobarchy/whisper/bin/whisper-cli"),) if p.exists()), None)


def model_file() -> Path:
    return SYSTEM_MODEL if SYSTEM_MODEL.exists() else USER_MODEL


def ready() -> bool:
    return whisper_bin() is not None and model_file().exists()


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
    model = model_file()
    if not model.exists():
        raise RuntimeError("the speech model is missing: run `mobarchy-agent setup voice`")
    with wave.open(str(path)) as w:
        rate = w.getframerate()
        samples = array.array("h", w.readframes(w.getnframes())) if w.getsampwidth() == 2 else None
    seconds = len(samples) / rate if samples is not None else 0
    if seconds < 0.3 or loudest(samples, rate) < SPEECH_RMS:
        return ""
    r = subprocess.run([exe, "-m", str(model), "-f", str(path), "-l", language,
                        "-t", str(THREADS), "-ac", str(audio_ctx(seconds)), "-mc", "0", "-nt", "-np"],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "whisper failed")
    return " ".join(r.stdout.split())


# OpenAI's multilingual Whisper (small): other languages than Norwegian, and detecting the
# language. NB-Whisper writes Norwegian whatever is spoken (it turns English into Norwegian).
# On comet it takes ~10 s for a sentence where NB-Whisper with -l no takes ~2.
MULTI_MODEL_NAME = "ggml-small-q5_1.bin"
MULTI_MODEL_URL = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin"


def multi_model_file() -> Path:
    system = Path("/usr/share/mobarchy/models") / MULTI_MODEL_NAME
    return system if system.exists() else MODELS / MULTI_MODEL_NAME


def transcribe_detect(path: Path, language: str = "auto") -> tuple[str, str]:
    """Speech in any language: (text, the language heard, e.g. "no", "en"). language: a code,
    or "auto" to detect it. Uses the multilingual model."""
    exe = whisper_bin()
    if not exe:
        raise RuntimeError("whisper.cpp (whisper-cli) is not installed")
    model = multi_model_file()
    if not model.exists():
        raise RuntimeError("the multilingual speech model is missing: run `mobarchy-agent setup talk`")
    with wave.open(str(path)) as w:
        rate = w.getframerate()
        samples = array.array("h", w.readframes(w.getnframes()))
    seconds = len(samples) / rate
    if seconds < 0.3 or loudest(samples, rate) < SPEECH_RMS:
        return "", language if language != "auto" else "no"
    # (-mc 0: no text carried from window to window; with it whisper can loop on a phrase)
    r = subprocess.run([exe, "-m", str(model), "-f", str(path), "-l", language,
                        "-t", str(THREADS), "-ac", str(audio_ctx(seconds)), "-mc", "0", "-nt", "-np"],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "whisper failed")
    m = re.search(r"auto-detected language: (\w+)", r.stderr)
    return " ".join(r.stdout.split()), (m.group(1) if m else language if language != "auto" else "no")


def setup() -> None:
    """Download the model (175 MB) if neither the image nor a download has it."""
    if model_file().exists():
        return
    MODELS.mkdir(parents=True, exist_ok=True)
    part = USER_MODEL.with_suffix(".part")
    subprocess.run(["curl", "-fsSL", "-C", "-", "-o", str(part), MODEL_URL], check=True)
    if part.stat().st_size != MODEL_BYTES:
        raise RuntimeError("the download is incomplete")
    part.rename(USER_MODEL)
