"""The wake phrase, "Hei Frigg" (ADR 0026, phase 2).

openWakeWord's models, run here with onnxruntime alone: a melspectrogram, Google's speech
embedding (one 96-number embedding per 80 ms of audio, from the last 0.76 s), and a small
classifier over the last 16 embeddings (1.28 s) that scores the phrase. Audio goes in as
16 kHz 16-bit samples, 1280 at a time; nothing leaves the phone.

The classifier is trained on synthetic voices. The owner's own way of saying it is learnt on
the phone, as openWakeWord's custom verifiers do: a logistic regression on the same 16
embeddings, from the few times they said "Hei Frigg" when Frigg learnt their voice, against
their other sentences. The verifier wakes Frigg on its own when it is sure, two 80 ms steps
in a row (the classifier scored the owner's real voice ~0.001: it can't gate it).
"""

from __future__ import annotations

from collections import deque
from pathlib import Path

import numpy as np

CHUNK = 1280            # 80 ms
CONTEXT = 160 * 3       # samples before a chunk the melspectrogram needs
MEL_WINDOW = 76         # melspectrogram frames an embedding looks at
FEATURES = "melspectrogram.onnx", "embedding_model.onnx"
VERIFIED = 0.9  # the verifier's probability, twice in a row, that wakes Frigg


def _session(path: Path):
    import onnxruntime as ort
    opts = ort.SessionOptions()
    opts.inter_op_num_threads = 1
    opts.intra_op_num_threads = 1
    return ort.InferenceSession(str(path), sess_options=opts, providers=["CPUExecutionProvider"])


class WakeWord:
    """Scores audio for the phrase, chunk by chunk: feed() returns 0..1 per 80 ms."""

    def __init__(self, classifier: Path, models: Path, verifier: Verifier | None = None) -> None:
        self.mel = _session(models / FEATURES[0])
        self.embed = _session(models / FEATURES[1])
        self.clf = _session(classifier)
        self.frames = self.clf.get_inputs()[0].shape[1] or 16
        self.verifier = verifier
        self.reset()

    def reset(self) -> None:
        self.tail = np.zeros(CONTEXT, dtype=np.int16)
        self.pending = np.zeros(0, dtype=np.int16)
        self.mels = np.ones((MEL_WINDOW, 32), dtype=np.float32)
        self.features: deque = deque(maxlen=self.frames)
        self.last_verified = 0.0

    def feed(self, pcm: bytes) -> list[float]:
        """Scores for the whole 80 ms chunks in pcm (and what was left from before)."""
        audio = np.concatenate((self.pending, np.frombuffer(pcm, dtype=np.int16)))
        whole = len(audio) - len(audio) % CHUNK
        self.pending = audio[whole:]
        scores = []
        for i in range(0, whole, CHUNK):
            scores.append(self._chunk(audio[i:i + CHUNK]))
        return scores

    def _chunk(self, chunk: np.ndarray) -> float:
        x = np.concatenate((self.tail, chunk)).astype(np.float32)[None]
        self.tail = chunk[-CONTEXT:]
        mel = np.squeeze(self.mel.run(None, {self.mel.get_inputs()[0].name: x})[0]) / 10 + 2
        self.mels = np.vstack((self.mels, mel))[-MEL_WINDOW * 2:]
        window = self.mels[-MEL_WINDOW:][None, :, :, None].astype(np.float32)
        emb = self.embed.run(None, {self.embed.get_inputs()[0].name: window})[0]
        self.features.append(np.squeeze(emb))
        if len(self.features) < self.frames:
            return 0.0
        x = np.array(self.features, dtype=np.float32)[None]
        score = float(np.squeeze(self.clf.run(None, {self.clf.get_inputs()[0].name: x})[0]))
        if self.verifier is not None:
            p, before = self.verifier.prob(x[0]), self.last_verified
            self.last_verified = p
            if p >= VERIFIED and before >= VERIFIED:
                return max(score, p)
        return score

    def windows(self, pcm: bytes) -> np.ndarray:
        """The 16-embedding windows of a clip, one per 80 ms: (n, 16, 96)."""
        self.reset()
        out = []
        audio = np.frombuffer(pcm, dtype=np.int16)
        for i in range(0, len(audio) - len(audio) % CHUNK, CHUNK):
            self._chunk(audio[i:i + CHUNK])
            if len(self.features) == self.frames:
                out.append(np.array(self.features, dtype=np.float32))
        self.reset()
        return np.array(out).reshape(-1, self.frames, 96)


class Verifier:
    """Logistic regression on a 16 x 96 window: is it the owner saying the phrase?"""

    def __init__(self, weights: np.ndarray, bias: float, mean: np.ndarray, scale: np.ndarray) -> None:
        self.w, self.b, self.mean, self.scale = weights, bias, mean, scale

    def prob(self, window: np.ndarray) -> float:
        z = ((window.ravel() - self.mean) / self.scale) @ self.w + self.b
        return float(1 / (1 + np.exp(-z)))

    @classmethod
    def train(cls, positive: np.ndarray, negative: np.ndarray, l2: float = 1.0, steps: int = 500) -> Verifier:
        x = np.concatenate((positive, negative)).reshape(len(positive) + len(negative), -1).astype(np.float64)
        y = np.concatenate((np.ones(len(positive)), np.zeros(len(negative))))
        mean, scale = x.mean(axis=0), x.std(axis=0) + 1e-3
        x = (x - mean) / scale
        # (the few positives weigh as much as the many negatives)
        weight = np.where(y == 1, len(y) / (2 * len(positive)), len(y) / (2 * len(negative)))
        w, b = np.zeros(x.shape[1]), 0.0
        for _ in range(steps):
            p = 1 / (1 + np.exp(-(x @ w + b)))
            g = weight * (p - y)
            w -= 0.1 * (x.T @ g / len(y) + l2 * w / len(y))
            b -= 0.1 * g.mean()
        return cls(w.astype(np.float32), float(b), mean.astype(np.float32), scale.astype(np.float32))

    def to_json(self) -> dict:
        return {"weights": self.w.round(6).tolist(), "bias": self.b,
                "mean": self.mean.round(6).tolist(), "scale": self.scale.round(6).tolist()}

    @classmethod
    def from_json(cls, d: dict) -> Verifier:
        return cls(np.array(d["weights"], dtype=np.float32), float(d["bias"]),
                   np.array(d["mean"], dtype=np.float32), np.array(d["scale"], dtype=np.float32))


def owner_windows(ww: WakeWord, clips: list[bytes]) -> np.ndarray:
    """Windows of the owner saying the phrase: those that end as the phrase ends (VAD's clip,
    with a second of silence before it), from 0.16 s before to 0.24 s after."""
    out = []
    for pcm in clips:
        padded = bytes(16000 * 2) + pcm + bytes(int(16000 * 0.4) * 2)
        w = ww.windows(padded)
        end = (len(padded) - int(16000 * 0.4) * 2) // 2 // CHUNK      # the chunk the phrase ends in
        first = ww.frames - 1                                          # (windows[i] ends at chunk i + first)
        out.extend(w[max(0, end - 2 - first):end + 3 - first])
    return np.array(out)


def other_windows(ww: WakeWord, clips: list[bytes]) -> np.ndarray:
    """Every window of the owner's other speech (and the silence around it)."""
    out = [ww.windows(bytes(16000) + pcm + bytes(16000)) for pcm in clips]
    return np.concatenate([w for w in out if len(w)])
