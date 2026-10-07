# 0026: Frigg: a live voice conversation with the agent

- Status: accepted (phase 1 built; phases 2 and 3 to come)
- Date: 2026-10-07

## Context

The owner wants to talk to the phone the way they talk to the Claude and ChatGPT apps'
voice modes: speak, hear the answer as it is being made, and interrupt it. Later it should
also wake on a phrase. The phone's name is Frigg. Norwegian and English are a must, other
languages are welcome. Local speech comes first; the owner will add a cloud key for
smoothness and speed.

Already there: dictation (whisper.cpp with NB-Whisper on the phone, ADR 0006), and the
agent with the phone's tools, the web and the owner's connectors (ADR 0010).

## Decision

**A live conversation is `mobarchy-agent talk`.** It is a process, like the agent's runs, with
JSON events out (state, heard, said, error) and commands in (stop, interrupt). The shell
starts it and shows it. A turn goes:

1. **Listen.** The microphone comes through PipeWire's echo canceller (WebRTC,
   module-echo-cancel), so Frigg doesn't hear itself and the owner can talk over it.
   Silero VAD (onnxruntime) finds where speech begins and ends: about 0.7 s of silence
   ends a turn.
2. **Understand.** Local: whisper.cpp, in the language the owner speaks: the system's
   (`LANG`), or `MOBARCHY_TALK_LANGUAGE` (e.g. `no`, `en`). English is the default.
   - Norwegian: NB-Whisper with the language set, ~1.5–2 s on comet. OpenAI's multilingual
     model writes poor Norwegian, and NB-Whisper can't do other languages: it writes
     Norwegian whatever is spoken, and with detection it looped on a phrase.
   - Other languages: OpenAI's multilingual small with the language set, ~1.3 s on comet.
   - `auto` detects the language with the multilingual model, but takes ~7 s on comet.
   - Cloud, with a key: OpenAI's transcription, every language and fast.
3. **Think.** Frigg goes on in the Mobarchy chat's Claude Code session (`--resume`), and
   leaves the chat its session afterwards: what is said to one, the other knows. If the chat
   moved on while Frigg waited, Frigg resumes it again before the next turn. The session
   stays open for the whole conversation, on Sonnet
   (`MOBARCHY_TALK_MODEL`). On comet its first words come after ~1–3 s, the default
   model's after 5–7
   (`--input-format stream-json`, `--include-partial-messages`). The first answer comes
   without a cold start for every turn. It has the agent's tools and rules (ADR 0010,
   0024), plus a voice brief: short spoken sentences, no markdown, the owner's language.
4. **Speak** each sentence as soon as it is complete, while the rest is still coming.
   Local: Piper, with a voice for the language heard. A service: ElevenLabs or OpenAI,
   streamed, every language. Playback goes into
   the echo canceller's sink.
5. **Interrupt.** Speech while Frigg talks stops the voice at once, and the owner's words
   become the next turn.

Each turn is also written to the agent's conversation, so it is in the home screen's
history.

**Local first, services as a choice.** `~/.config/mobarchy/agent.env`, each with its key:
- `MOBARCHY_TALK_STT`: `whisper` (default, the phone) or `openai` (`OPENAI_API_KEY`).
- `MOBARCHY_TALK_TTS`: `piper` (default, the phone), `elevenlabs` (`ELEVENLABS_API_KEY`) or
  `openai`. ElevenLabs' Flash streams a sentence's sound as it is made: the first of it
  reaches comet ~0.25–0.35 s after the request, against Piper's 0.4 s for a whole sentence,
  and it sounds human, in Norwegian too.
- `MOBARCHY_TALK_VOICE` (the service's voice: an ElevenLabs voice id, an OpenAI voice name),
  `MOBARCHY_TALK_SPEED`, `MOBARCHY_TALK_LANGUAGE`, `MOBARCHY_TALK_MODEL`.
- Without a key everything works on the phone. It is slower, and the voice sounds less
  natural. A choice without its key falls back to the phone.

**Models by language.** The first conversation in a language downloads what it needs:
whisper's model for it (NB-Whisper for Norwegian, the multilingual one for the rest) and a
Piper voice from Piper's catalogue (ours for Norwegian and English; else one speaker, at
the best quality). The overlay shows the download. English's voice is always there, for a
language Piper has no voice for.
- Without a key everything works on the phone. It is slower, and the voice sounds less
  natural.

**Phases**
1. A live conversation from a button (now).
2. The wake phrase "Hei Frigg": openWakeWord's small ONNX models on the CPU, trained for the
   phrase with synthetic speech. Nothing leaves the phone before it is heard. It has a
   switch (always, while charging, off), because the microphone stays on.
3. Battery, the lock screen (Frigg answers there; what needs the phone unlocked asks for
   it, as today), and more languages.

Measured on comet, one turn spoken as "Hva blir været i Oslo i morgen?": heard after 0.9 s;
the first sentence ("Jeg sjekker værmeldingen for Oslo") after 5.1 s, during the web search;
the forecast after 10.8–12.1 s. Piper takes 0.4 s a sentence, and Silero 0.1 ms a window.

**The wake phrase (phase 2).** `mobarchy-agent talk --wake` waits for "Hei Frigg"
("waiting"), then talks as from the button: the words after the phrase are the first turn.
After 8 s without speech it waits again; Claude Code's session stays open for 10 minutes of
waiting. The shell runs it while the owner's switch says so
(the overlay's "Hei Frigg": always, while charging, off; `~/.config/mobarchy/frigg.ini`). The status bar shows a microphone while it listens.

- Detection is openWakeWord's pipeline, run with onnxruntime alone (`wake.py`; the same
  scores as openWakeWord's own code): ~16 % of one core on comet.
- The classifier is ours, trained by `agent/wakeword/train` on synthetic speech: the English
  generator's 904 speakers, Nordic Piper voices, and ElevenLabs' Norwegian voices; with
  Norwegian speech (FLEURS) among the negatives, since the owner speaks Norwegian all day.
- A soft chime says Frigg is listening.

**A locked phone (phase 3): Voice Match.** "Hei Frigg" works on a locked phone too. Who
speaks decides what Frigg may do:
- The owner's voice: as on an unlocked phone (the chat's session, everything). What needs
  approval (installing, sudo) still needs the phone unlocked, with a fingerprint or the PIN:
  the tool says so, and Frigg asks the owner to unlock and tries again.
- Another voice: a Claude Code of its own (its own configuration with the owner's login only:
  none of their memory, history or connectors; `--strict-mcp-config`), the web, and the phone's
  harmless tools, which mobarchy-mcp enforces (`?voice=guest`: battery, time, music, ...).
  It doesn't see or write the owner's conversation, and starts afresh for the next guest.
- A phone without a PIN keeps no one out anyway: everyone is the owner.

Voice Match is NVIDIA NeMo's TitaNet small speaker embedding (onnxruntime, NeMo's features in
numpy: `speaker.py`), against the mean of 8 sentences the owner reads once ("Learn my voice"
in the overlay; `talk --enroll`). Of four models tried on 22 Norwegian voices (12 ElevenLabs,
10 Piper; `agent/wakeword/voicematch.py`), it separated them best: at a cosine of 0.6, 0.1 %
of other voices' sentences get in, and 1 % of the owner's are refused. CAM++ let in 70 %.
A check takes ~60 ms on comet. It can be fooled by a recording of the owner, so it opens the
owner's data, never approvals.

## Consequences

- Frigg is the agent with a voice, not a separate assistant: the same tools, permissions
  and history.
- Phase 1 needs the Claude agent; the others answer through their one-shot runs, more
  slowly.
- The echo canceller is a PipeWire node of its own. Other sound (music) is not cancelled,
  and Frigg should pause it while listening.
- Local speech costs CPU: whisper's few seconds per turn and Piper's synthesis are the
  latency without a key.
