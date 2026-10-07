"""The wake phrase, "Hei Frigg" (ADR 0026, phase 2).

openWakeWord's models, run here with onnxruntime alone: a melspectrogram, Google's speech
embedding (one 96-number embedding per 80 ms of audio, from the last 0.76 s), and a small
classifier over the last 16 embeddings (1.28 s) that scores the phrase. Audio goes in as
16 kHz 16-bit samples, 1280 at a time; nothing leaves the phone.
"""

from __future__ import annotations

from collections import deque
from pathlib import Path

import numpy as np

CHUNK = 1280            # 80 ms
CONTEXT = 160 * 3       # samples before a chunk the melspectrogram needs
MEL_WINDOW = 76         # melspectrogram frames an embedding looks at
FEATURES = "melspectrogram.onnx", "embedding_model.onnx"


def _session(path: Path):
    import onnxruntime as ort
    opts = ort.SessionOptions()
    opts.inter_op_num_threads = 1
    opts.intra_op_num_threads = 1
    return ort.InferenceSession(str(path), sess_options=opts, providers=["CPUExecutionProvider"])


class WakeWord:
    """Scores audio for the phrase, chunk by chunk: feed() returns 0..1 per 80 ms."""

    def __init__(self, classifier: Path, models: Path) -> None:
        self.mel = _session(models / FEATURES[0])
        self.embed = _session(models / FEATURES[1])
        self.clf = _session(classifier)
        self.frames = self.clf.get_inputs()[0].shape[1] or 16
        self.reset()

    def reset(self) -> None:
        self.tail = np.zeros(CONTEXT, dtype=np.int16)
        self.pending = np.zeros(0, dtype=np.int16)
        self.mels = np.ones((MEL_WINDOW, 32), dtype=np.float32)
        self.features: deque = deque(maxlen=self.frames)

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
        return float(np.squeeze(self.clf.run(None, {self.clf.get_inputs()[0].name: x})[0]))
