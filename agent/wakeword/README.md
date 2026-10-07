# "Hei Frigg": the wake phrase

Frigg (ADR 0026) can wait for "Hei Frigg" between conversations. The phone runs
openWakeWord's models with onnxruntime (`mobarchy_agent/wake.py`): a melspectrogram,
Google's speech embedding, and our small classifier for the phrase, `hei_frigg.onnx`.
Nothing leaves the phone before the phrase is heard.

This directory trains that classifier:

    agent/wakeword/train <work dir>

It needs a PC with an NVIDIA GPU (a CPU works, slowly), ~40 GB in the work dir, and a few
hours. Then copy `<work dir>/frigg/hei_frigg.onnx` to the phone's
`~/.local/share/mobarchy/models/`.

## The data

Positive clips (the phrase):
- ~12,000 from openWakeWord's generator: Piper's LibriTTS-R model, mixing its 904 English
  speakers, saying "hey frigg" and "hei frigg".
- ~4,400 from Nordic Piper voices (`voices.py`): Norwegian (nvcc's ten speakers,
  talesyntese), Swedish, Danish, Icelandic and German, at different speeds.
- ~3,300 from 32 of ElevenLabs' Norwegian voices (Oslo, Bergen, Trøndelag, the north), with
  an `ELEVENLABS_API_KEY`.

openWakeWord mixes them with noise and music (two AudioSet shards), Norwegian speech, and
room echo (MIT's impulse responses).

Negatives:
- openWakeWord's ~2,000 hours of features (ACAV100M), mostly English.
- Norwegian speech from Google's FLEURS (`features.py`), so everyday Norwegian doesn't wake
  Frigg.
- Near misses: openWakeWord's, made from the phonemes, and those in `hei_frigg.yml`.

Its false wakes are counted on openWakeWord's validation set (~11 h of speech, noise and
music), with an hour of Norwegian added.

## Known gaps in the recipe

- DeepPhonemizer's checkpoint (for the near misses) is no longer at its own address.
  `train` takes NRC Canada's copy on Hugging Face and renames its modules back.
- AudioSet on Hugging Face moved to Parquet shards; `train` converts them.
