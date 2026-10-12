"""Hearing through ElevenLabs' Scribe v2 Realtime (ADR 0026): what the owner says is sent
while they say it, so the text is there as soon as they stop (~150 ms), not after a whole
transcription on the phone (~1.5 s with NB-Whisper small); and it hears Norwegian better.

A turn is one session: opened when speech starts (the connection is made while the owner
speaks), the speech streamed in as it comes, committed when it ends (Frigg's own VAD
decides that, as for the phone's whisper), and the committed text is the turn's. Anything
that goes wrong (no network, a refusal) ends in ScribeError: the caller falls back to the
phone's whisper with the same audio.
"""

from __future__ import annotations

import asyncio
import base64
import json
import os
import urllib.parse

URL = "wss://api.elevenlabs.io/v1/speech-to-text/realtime"
MODEL = "scribe_v2_realtime"
RATE = 16000
# Names it would not know, so it doesn't make other words of them
KEYTERMS = ("Frigg", "Mobarchy", "Bifrost", "Hei Frigg")


class ScribeError(Exception):
    pass


def keyterms() -> list[str]:
    extra = [k.strip() for k in os.environ.get("MOBARCHY_TALK_KEYTERMS", "").split(",") if k.strip()]
    return list(dict.fromkeys([*KEYTERMS, *extra]))[:50]


def url(language: str) -> str:
    params = [("model_id", os.environ.get("MOBARCHY_TALK_STT_MODEL", MODEL)), ("audio_format", f"pcm_{RATE}"),
              ("commit_strategy", "manual")]
    if language in ("no", "nb", "nn"):
        params.append(("language_code", "no"))
    elif language and language != "auto":
        params.append(("language_code", language))
    params += [("keyterms", k) for k in keyterms()]
    return f"{URL}?{urllib.parse.urlencode(params)}"


def chunk(pcm: bytes, commit: bool = False) -> str:
    return json.dumps({"message_type": "input_audio_chunk", "audio_base_64": base64.b64encode(pcm).decode(),
                       "commit": commit, "sample_rate": RATE})


def committed_text(message: dict) -> str | None:
    """The text of a committed transcript, None for other messages; an error raises."""
    kind = message.get("message_type", "")
    if kind in ("committed_transcript", "committed_transcript_with_timestamps"):
        return (message.get("text") or "").strip()
    if kind.endswith("error") or kind in ("quota_exceeded", "rate_limited", "queue_overflow",
                                          "resource_exhausted", "session_time_limit_exceeded",
                                          "insufficient_audio_activity"):
        raise ScribeError(f"{kind}: {message.get('error', '')}".strip(": "))
    return None


class Scribe:
    """One turn's transcription: start(), feed() its audio as it comes, then finish()."""

    def __init__(self, language: str) -> None:
        self.language = language
        self.queue: asyncio.Queue[bytes | None] = asyncio.Queue()
        self.task: asyncio.Task | None = None

    def start(self) -> None:
        self.task = asyncio.create_task(self._run())

    def feed(self, pcm: bytes) -> None:
        self.queue.put_nowait(pcm)

    def cancel(self) -> None:
        if self.task:
            self.task.cancel()

    async def finish(self, timeout: float = 4.0) -> str:
        """The text, once the speech has ended (or ScribeError)."""
        self.queue.put_nowait(None)
        try:
            return await asyncio.wait_for(self.task, timeout)
        except asyncio.TimeoutError as e:
            raise ScribeError("no transcript in time") from e
        except ScribeError:
            raise
        except Exception as e:  # (the network, the service)
            raise ScribeError(str(e) or type(e).__name__) from e

    async def _run(self) -> str:
        import websockets   # (only when this service is chosen)
        key = os.environ.get("ELEVENLABS_API_KEY", "")
        if not key:
            raise ScribeError("no ELEVENLABS_API_KEY")
        async with websockets.connect(url(self.language), additional_headers={"xi-api-key": key},
                                      open_timeout=5, max_size=2**22) as ws:
            committed = asyncio.Event()

            async def send() -> None:
                pending = b""
                while True:
                    pcm = await self.queue.get()
                    if pcm is None:
                        await ws.send(chunk(pending, commit=True))
                        committed.set()
                        return
                    pending += pcm
                    if len(pending) >= RATE // 10 * 2:      # ~100 ms a message
                        await ws.send(chunk(pending))
                        pending = b""

            sender = asyncio.create_task(send())
            texts: list[str] = []
            try:
                async for raw in ws:
                    text = committed_text(json.loads(raw))
                    if text is not None:
                        texts.append(text)
                        if committed.is_set():
                            break
            finally:
                sender.cancel()
            return " ".join(t for t in texts if t)
