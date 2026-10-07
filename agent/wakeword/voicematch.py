"""Calibrates Voice Match (mobarchy_agent/speaker.py): how alike CAM++ finds the same voice
and different voices, enrolled as the phone enrolls the owner (8 sentences) and tested with
other sentences.

    python voicematch.py <work dir> [model.onnx]

Voices: Piper's Norwegian nvcc (10 speakers), and with an ELEVENLABS_API_KEY 12 of
ElevenLabs' Norwegian voices. Prints the scores and the error rates at a few thresholds.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import urllib.request
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mobarchy-agent" / "src"))
from mobarchy_agent import speaker  # noqa: E402

ENROLL = [
    "Hei Frigg, hva blir været i morgen?",
    "Kan du sette på litt musikk mens jeg lager middag?",
    "Les opp den siste e-posten fra jobben.",
    "Minn meg på å ringe mamma klokka seks.",
    "Hvor lang tid tar det å kjøre til Stavanger nå?",
    "Skru ned lyden og demp skjermen litt.",
    "Hva står i kalenderen min på fredag?",
    "Send en melding til Kari om at jeg er sen.",
]
TEST = [
    "Hei Frigg, hvor mye batteri har jeg igjen?",
    "Hva er klokka nå?",
    "Finn en god oppskrift på fiskesuppe.",
    "Har jeg fått noen nye meldinger i dag?",
    "Sett en tidtaker på ti minutter.",
]
ELEVENLABS = ["BRY5wALXFkJsxVM6sEOH", "BGEU6wFi2uNm6Kje1Yhk", "MULNGIATG7qUsJ0Sjuhy", "uNsWM1StCcpydKYOjKyu",
              "b3jcIbyC3BSnaRu8avEk", "SUVXSJC7hh41K4FtAVuH", "4kCDY3HJwvO7Zp3con83", "9pRpxWU0T7UFt2oEMH6n",
              "CMbvLbbccSd611KtwxV3", "EpYEY8MWJrUGskHBoNMA", "nDDLnlNUK5tNoyGVJSRp", "goUjLCRHn3Deb8y2jFuG"]


def piper(cache: Path, sid: int, text: str) -> bytes:
    from piper import PiperVoice, SynthesisConfig
    from scipy.signal import resample_poly
    global _nvcc
    if "_nvcc" not in globals():
        _nvcc = PiperVoice.load(str(cache / "no_NO-nvcc-medium.onnx"))
    chunks = list(_nvcc.synthesize(text, syn_config=SynthesisConfig(speaker_id=sid)))
    audio = np.concatenate([c.audio_int16_array for c in chunks]).astype(np.float32)
    return resample_poly(audio, 16000, chunks[0].sample_rate).astype(np.int16).tobytes()


def elevenlabs(cache: Path, voice: str, text: str) -> bytes:
    path = cache / f"{voice}-{hashlib.md5(text.encode()).hexdigest()[:10]}.pcm"
    if not path.exists():
        req = urllib.request.Request(
            f"https://api.elevenlabs.io/v1/text-to-speech/{voice}?output_format=pcm_16000",
            data=json.dumps({"text": text, "model_id": "eleven_multilingual_v2"}).encode(),
            headers={"xi-api-key": os.environ["ELEVENLABS_API_KEY"], "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=60) as r:
            path.write_bytes(r.read())
    return path.read_bytes()


def main() -> None:
    work = Path(sys.argv[1])
    cache = work / "voicematch"
    cache.mkdir(exist_ok=True)
    model = Path(sys.argv[2]) if len(sys.argv) > 2 else work / "speaker" / speaker.MODEL
    vm = speaker.VoiceMatch(model, cache / "profile.json")
    if os.environ.get("SHERPA"):    # (the reference: sherpa-onnx's own features)
        import sherpa_onnx
        ex = sherpa_onnx.SpeakerEmbeddingExtractor(sherpa_onnx.SpeakerEmbeddingExtractorConfig(model=str(model)))

        def embed(pcm: bytes) -> np.ndarray:
            st = ex.create_stream()
            st.accept_waveform(16000, np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768)
            st.input_finished()
            e = np.array(ex.compute(st))
            return e / np.linalg.norm(e)
        vm.embed = embed
    voices = {f"nvcc{s}": (lambda t, s=s: piper(work / "piper-voices", s, t)) for s in range(10)}
    if os.environ.get("ELEVENLABS_API_KEY"):
        voices |= {f"el_{v[:6]}": (lambda t, v=v: elevenlabs(cache, v, t)) for v in ELEVENLABS}
    profiles, tests = {}, {}
    for name, say in voices.items():
        embs = np.array([vm.embed(say(t)) for t in ENROLL])
        p = embs.mean(axis=0)
        profiles[name] = p / np.linalg.norm(p)
        tests[name] = [vm.embed(say(t)) for t in TEST]
        print(name, flush=True)
    same = [float(e @ profiles[n]) for n in voices for e in tests[n]]
    other = [float(e @ profiles[m]) for n in voices for e in tests[n] for m in voices if m != n]
    print(f"same voice:  min {min(same):.3f}  mean {np.mean(same):.3f}")
    print(f"other voice: max {max(other):.3f}  mean {np.mean(other):.3f}  (99.9th pct {np.percentile(other, 99.9):.3f})")
    for th in (0.3, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7):
        frr = np.mean(np.array(same) < th)
        far = np.mean(np.array(other) >= th)
        print(f"threshold {th:.2f}: owner refused {frr:6.1%}   others let in {far:7.2%}")


if __name__ == "__main__":
    main()
