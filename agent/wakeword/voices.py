"""Positive clips of the wake phrase in Nordic and other European voices, besides the English
generator's (openWakeWord's training makes those). Piper's voices, and with an
ELEVENLABS_API_KEY a few hundred from ElevenLabs' Norwegian voices.

    python voices.py <work dir> [--piper N] [--elevenlabs N]

Writes 16 kHz WAVs into <work dir>/frigg/hei_frigg/positive_train and positive_test (9:1), where
openWakeWord's --generate_clips then tops them up to n_samples.
"""

from __future__ import annotations

import argparse
import io
import json
import os
import random
import time
import urllib.request
import wave
from pathlib import Path

import numpy as np
from scipy.signal import resample_poly

PHRASES = ["Hei Frigg", "Hei, Frigg", "Hei Frigg!", "Hei Frigg?", "Hei, Frigg."]
VOICE_BASE = "https://huggingface.co/rhasspy/piper-voices/resolve/main"
# Voices whose phonemes say "Hei Frigg" close to how a Norwegian does
PIPER = ["no_NO-nvcc-medium", "no_NO-talesyntese-medium", "sv_SE-nst-medium", "sv_SE-alma-medium",
         "sv_SE-lisa-medium", "da_DK-talesyntese-medium", "is_IS-bui-medium", "is_IS-salka-medium",
         "is_IS-steinn-medium", "is_IS-ugla-medium", "de_DE-thorsten-medium"]
# (not de_DE-mls or nl_NL-mls: their many speakers say it badly)
# ElevenLabs' Norwegian voices (the voice library): women, then men, from Oslo, Bergen,
# Trøndelag and the north
ELEVENLABS = [
    "BRY5wALXFkJsxVM6sEOH", "BGEU6wFi2uNm6Kje1Yhk", "MULNGIATG7qUsJ0Sjuhy", "uNsWM1StCcpydKYOjKyu",
    "b3jcIbyC3BSnaRu8avEk", "k5IgYJw2jfo6mO5HhagG", "pbb4nvdETnNfQljwVeaL", "pSuPcYPRhKrCEH0xQhRU",
    "SUVXSJC7hh41K4FtAVuH",
    "2dhHLsmg0MVma2t041qT", "4kCDY3HJwvO7Zp3con83", "580zXINOKYzlp5JwzW2H", "9pRpxWU0T7UFt2oEMH6n",
    "cExJ7mRTtNXe4K57uzVh", "CMbvLbbccSd611KtwxV3", "ei6ah5sRupNv5tIBtewr", "EpYEY8MWJrUGskHBoNMA",
    "GC1txXxPYDcfgO0KfuXm", "gFW8Yrl1htOR6XcdLhWT", "goUjLCRHn3Deb8y2jFuG", "ikwOHnCcqQJgOoz5Wxex",
    "nDDLnlNUK5tNoyGVJSRp", "nhvaqgRyAq6BmFs3WcdX", "Rgn85JVy9km7zS5Dg1qP", "s2xtA7B2CTXPPlJzch1v",
    "SAa140Q9akujSiZCBSNt", "St7X3jjf2NZQB9rcGjjZ", "TdMof89srIrEcmiMQhjW", "vUmLiNBm6MDcy1NUHaVr",
    "X6lMep3MH36hSteAavNu", "xF681s0UeE04gsf0mVsJ", "yVDKAA7DOs7PvBoDszH3"]


def save(path: Path, audio: np.ndarray, rate: int) -> None:
    if rate != 16000:
        audio = resample_poly(audio.astype(np.float32), 16000, rate).clip(-32768, 32767).astype(np.int16)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(audio.tobytes())


def target(out: Path, name: str) -> Path:
    split = "positive_test" if random.random() < 0.1 else "positive_train"
    (out / split).mkdir(parents=True, exist_ok=True)
    return out / split / f"{name}.wav"


def piper_voice(cache: Path, name: str) -> Path:
    family, rest = name.split("-", 1)[0], name.split("-", 1)[1]
    lang, speaker, quality = family, rest.rsplit("-", 1)[0], rest.rsplit("-", 1)[1]
    path = f"{lang.split('_')[0]}/{lang}/{speaker}/{quality}"
    for f in (f"{name}.onnx", f"{name}.onnx.json"):
        if not (cache / f).exists():
            urllib.request.urlretrieve(f"{VOICE_BASE}/{path}/{f}", cache / f)
    return cache / f"{name}.onnx"


def piper_clips(out: Path, cache: Path, n: int) -> None:
    from piper import PiperVoice, SynthesisConfig
    per = n // len(PIPER)
    for name in PIPER:
        voice = PiperVoice.load(str(piper_voice(cache, name)))
        speakers = voice.config.num_speakers
        for i in range(per):
            cfg = SynthesisConfig(speaker_id=random.randrange(speakers) if speakers > 1 else None,
                                  length_scale=random.uniform(0.8, 1.25),
                                  noise_scale=random.uniform(0.4, 0.75), noise_w_scale=random.uniform(0.5, 0.9))
            chunks = list(voice.synthesize(random.choice(PHRASES), syn_config=cfg))
            audio = np.concatenate([c.audio_int16_array for c in chunks])
            save(target(out, f"piper_{name}_{i}"), audio, chunks[0].sample_rate)
        print(name, per, flush=True)


def elevenlabs_clips(out: Path, n: int, start: int = 0) -> None:
    """Three at a time (the Starter plan's concurrency)."""
    from concurrent.futures import ThreadPoolExecutor
    key = os.environ["ELEVENLABS_API_KEY"]
    rng = random.Random(start)
    jobs = []
    for i in range(start, start + n):
        voice = ELEVENLABS[i % len(ELEVENLABS)]
        body = {"text": rng.choice(PHRASES), "model_id": rng.choice(["eleven_flash_v2_5", "eleven_multilingual_v2"]),
                "voice_settings": {"stability": rng.uniform(0.2, 0.8), "similarity_boost": 0.75,
                                   "style": rng.uniform(0, 0.5), "speed": rng.uniform(0.8, 1.15)}}
        if body["model_id"] == "eleven_flash_v2_5":
            body["language_code"] = "no"
        jobs.append((target(out, f"elevenlabs_{voice}_{i}"), voice, body))

    def make(job) -> None:
        path, voice, body = job
        req = urllib.request.Request(
            f"https://api.elevenlabs.io/v1/text-to-speech/{voice}?output_format=pcm_16000",
            data=json.dumps(body).encode(), headers={"xi-api-key": key, "Content-Type": "application/json"})
        for attempt in range(5):
            try:
                with urllib.request.urlopen(req, timeout=60) as r:
                    audio = np.frombuffer(r.read(), dtype=np.int16)
                break
            except OSError:
                if attempt == 4:
                    raise
                time.sleep(10 * (attempt + 1))
        save(path, audio, 16000)

    with ThreadPoolExecutor(3) as pool:
        list(pool.map(make, jobs))
    print("elevenlabs", n, flush=True)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("work", type=Path)
    p.add_argument("--piper", type=int, default=4000)
    p.add_argument("--elevenlabs", type=int, default=0)
    p.add_argument("--start", type=int, default=0, help="the first ElevenLabs clip's number")
    a = p.parse_args()
    out, cache = a.work / "frigg" / "hei_frigg", a.work / "piper-voices"
    cache.mkdir(parents=True, exist_ok=True)
    random.seed(26)
    if a.piper:
        piper_clips(out, cache, a.piper)
    if a.elevenlabs:
        elevenlabs_clips(out, a.elevenlabs, a.start)


if __name__ == "__main__":
    main()
