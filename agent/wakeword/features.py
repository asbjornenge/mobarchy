"""openWakeWord features of Norwegian speech that doesn't hold the phrase: the owner talks
Norwegian all day, and openWakeWord's own negatives (ACAV100M) are mostly English.

    python features.py <work dir>

From Google's FLEURS (nb_no, CC BY 4.0) in <work dir>/data:
  fleurs_nb_train -> data/negative_nb.npy        windows of 16 embeddings, as ACAV100M's
  fleurs_nb_test  -> data/validation_nb_all.npy  openWakeWord's validation set (~11 h) with
                                                 the Norwegian test speech after it (~1 h)
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from openwakeword.utils import AudioFeatures
from scipy.signal import resample_poly
from tqdm import tqdm

WINDOW, STRIDE = 16, 4


def embeddings(F: AudioFeatures, path: Path) -> np.ndarray:
    audio, rate = sf.read(str(path), dtype="float32", always_2d=True)
    audio = audio[:, 0]
    if rate != 16000:
        audio = resample_poly(audio, 16000, rate)
    if len(audio) < 16000:
        return np.zeros((0, 96), dtype=np.float32)
    return F._get_embeddings((audio * 32767).clip(-32768, 32767).astype(np.int16))


def main() -> None:
    data = Path(sys.argv[1]) / "data"
    F = AudioFeatures(inference_framework="onnx", ncpu=8)

    windows = []
    for path in tqdm(sorted((data / "fleurs_nb_train").rglob("*.wav")), desc="train"):
        e = embeddings(F, path)
        for i in range(0, len(e) - WINDOW + 1, STRIDE):
            windows.append(e[i:i + WINDOW])
    np.save(data / "negative_nb.npy", np.array(windows, dtype=np.float16))
    print("negative_nb", len(windows))

    stream = [np.load(data / "validation_set_features.npy")]
    for path in tqdm(sorted((data / "fleurs_nb_test").rglob("*.wav")), desc="test"):
        stream.append(embeddings(F, path))
    joined = np.concatenate(stream).astype(np.float32)
    np.save(data / "validation_nb_all.npy", joined)
    print("validation", joined.shape, f"{(joined.shape[0] - stream[0].shape[0]) / 12.5 / 3600:.2f} h Norwegian")


if __name__ == "__main__":
    main()
