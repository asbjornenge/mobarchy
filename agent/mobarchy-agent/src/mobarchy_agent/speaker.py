"""Voice Match: is it the owner speaking? (ADR 0026, phase 3)

A speaker embedding (NVIDIA NeMo's TitaNet small: 192 numbers for a stretch of speech,
whatever the language) of what Frigg heard, against the owner's: the mean of the embeddings
of a few sentences the owner read when enrolling. Run with onnxruntime alone, on NeMo's
features (log mel spectrogram, normalized per mel band), computed here with numpy. Nothing
leaves the phone.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

MODEL = "nemo_en_titanet_small.onnx"
MODEL_URL = ("https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/"
             "nemo_en_titanet_small.onnx")
RATE = 16000
# Cosine similarity to the owner's voice that counts as the owner. Calibrated with
# agent/wakeword/voicematch.py on 22 Norwegian voices: at 0.6, 0.1 % of other voices' sentences
# get in, and 1 % of the owner's are refused (they say it again, or unlock).
THRESHOLD = 0.6
N_FFT, WIN, HOP, MELS = 512, 400, 160, 80


def _slaney_mel(sr: int, n_fft: int, n_mels: int, fmin: float = 0.0, fmax: float | None = None) -> np.ndarray:
    """librosa.filters.mel(..., htk=False, norm="slaney"): (n_mels, n_fft // 2 + 1)."""
    fmax = fmax or sr / 2

    def hz_to_mel(f):
        f = np.asarray(f, dtype=np.float64)
        mel = f / (200.0 / 3)
        log_t = f >= 1000.0
        return np.where(log_t, 15.0 + np.log(np.maximum(f, 1e-10) / 1000.0) / (np.log(6.4) / 27.0), mel)

    def mel_to_hz(m):
        m = np.asarray(m, dtype=np.float64)
        f = m * (200.0 / 3)
        log_t = m >= 15.0
        return np.where(log_t, 1000.0 * np.exp((np.log(6.4) / 27.0) * (m - 15.0)), f)

    fft_freqs = np.linspace(0, sr / 2, n_fft // 2 + 1)
    mel_f = mel_to_hz(np.linspace(hz_to_mel(fmin), hz_to_mel(fmax), n_mels + 2))
    fdiff = np.diff(mel_f)
    ramps = mel_f[:, None] - fft_freqs[None, :]
    lower = -ramps[:-2] / fdiff[:-1, None]
    upper = ramps[2:] / fdiff[1:, None]
    weights = np.maximum(0, np.minimum(lower, upper))
    return weights * (2.0 / (mel_f[2:n_mels + 2] - mel_f[:n_mels]))[:, None]


_MEL = _slaney_mel(RATE, N_FFT, MELS)
_WINDOW = np.pad(np.hanning(WIN), ((N_FFT - WIN) // 2, (N_FFT - WIN + 1) // 2))


def features(samples: np.ndarray) -> np.ndarray:
    """NeMo's: preemphasis 0.97, 25 ms Hann windows every 10 ms (centred), 80 slaney mel
    bands, log, each band normalized over time. samples: floats in -1..1. -> (80, frames)"""
    x = np.concatenate((samples[:1], samples[1:] - 0.97 * samples[:-1])).astype(np.float64)
    x = np.pad(x, N_FFT // 2)
    n = 1 + (len(x) - N_FFT) // HOP
    idx = np.arange(N_FFT)[None, :] + HOP * np.arange(n)[:, None]
    spec = np.abs(np.fft.rfft(x[idx] * _WINDOW, n=N_FFT)) ** 2
    mel = np.log(spec @ _MEL.T + 2.0 ** -24)
    mel = (mel - mel.mean(axis=0)) / (mel.std(axis=0, ddof=1) + 1e-5)
    return mel.T.astype(np.float32)


class VoiceMatch:
    """Embeds speech, and scores it against the owner's voice."""

    def __init__(self, model: Path, profile: Path) -> None:
        import onnxruntime as ort
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 2
        self.session = ort.InferenceSession(str(model), sess_options=opts, providers=["CPUExecutionProvider"])
        self.profile_path = profile
        self.owner = load_profile(profile)

    def embed(self, pcm: bytes) -> np.ndarray:
        feats = features(np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768)
        emb = self.session.run(["embs"], {"audio_signal": feats[None],
                                          "length": np.array([feats.shape[1]], dtype=np.int64)})[0][0]
        return emb / (np.linalg.norm(emb) or 1.0)

    def score(self, pcm: bytes) -> float | None:
        """Cosine similarity to the owner's voice; None without an enrolled voice."""
        if self.owner is None:
            return None
        return float(self.embed(pcm) @ self.owner)

    def enroll(self, clips: list[bytes]) -> None:
        embs = np.array([self.embed(c) for c in clips])
        owner = embs.mean(axis=0)
        self.owner = owner / np.linalg.norm(owner)
        self.profile_path.parent.mkdir(parents=True, exist_ok=True)
        self.profile_path.write_text(json.dumps({"model": MODEL, "clips": len(clips),
                                                 "embedding": self.owner.round(6).tolist()}))
        self.profile_path.chmod(0o600)


def load_profile(path: Path) -> np.ndarray | None:
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    if data.get("model") != MODEL:
        return None
    v = np.array(data["embedding"], dtype=np.float32)
    return v / np.linalg.norm(v)
