"""Frigg: a live voice conversation with the agent (ADR 0026).

`mobarchy-agent talk --json`: listens, understands, lets the agent answer, speaks the answer
sentence by sentence as it comes, and listens again. Talking over Frigg interrupts it.

Events out, one JSON object a line: {"event": "state", "state": "listening" | "thinking" |
"speaking" | "idle"}, {"event": "heard", "text"}, {"event": "said", "text"} (a sentence as it
is spoken), {"event": "tool", "text"}, {"event": "error", "text"}.
Commands in, a line each: "stop" (end the conversation), "interrupt" (stop speaking, listen).

Speech is the phone's (whisper.cpp, Piper) unless ~/.config/mobarchy/agent.env chooses a
service, with its key: MOBARCHY_TALK_STT=openai (OPENAI_API_KEY) for understanding,
MOBARCHY_TALK_TTS=elevenlabs (ELEVENLABS_API_KEY) or openai for the voice; MOBARCHY_TALK_VOICE
is the service's voice. On the phone, the language is the system's unless
MOBARCHY_TALK_LANGUAGE sets it (e.g. no, en, or auto to detect it, which is slow on comet).
"""

from __future__ import annotations

import asyncio
import json
import os
import re
import shutil
import tempfile
import time
import urllib.request
import wave
from collections.abc import AsyncIterator, Callable, Iterator
from pathlib import Path

from . import agents, voice
from .config import Config
from .runtime import SYSTEM_PROMPT

RATE = 16000                 # the microphone, for the VAD and whisper
WINDOW = 512                 # Silero VAD's window at 16 kHz (32 ms)
START_PROB, END_PROB = 0.5, 0.35
START_WINDOWS = 3            # ~100 ms of speech starts a turn
END_SILENCE_S = 0.7          # this much silence ends it
BARGE_IN_WINDOWS = 8         # ~250 ms of speech while Frigg talks interrupts it
PRE_ROLL_WINDOWS = 10        # kept from before the start, so the first syllable isn't lost
MAX_TURN_S = 30
# With the wake phrase: the conversation goes back to waiting for it after this long without
# speech, and Claude Code's session is closed after this long waiting.
FOLLOW_UP_S = 8
SESSION_IDLE_S = 600

MODELS = Path("/usr/share/mobarchy/models")
USER_MODELS = Path.home() / ".local/share/mobarchy/models"
VAD_MODEL = "silero_vad.onnx"
# Piper voices: these where a language has several; for others, one from Piper's catalogue,
# downloaded when the language is chosen (whisper's codes: "no", "en", "de", ...).
VOICES = {"en": "en_US-lessac-medium", "no": "no_NO-talesyntese-medium"}
FAMILY = {"nb": "no", "nn": "no"}
DEFAULT_LANGUAGE = "en"

VOICE_PROMPT = """
You are Frigg, the voice of this phone. This is a spoken conversation: what you write is \
read aloud. Answer in the language the owner speaks, in short plain sentences, as you would \
talk: no markdown, no lists, no emoji, no URLs read out. Keep answers brief unless asked for \
more, and ask back when something is unclear. You still have all your tools; say in a few \
words what you are doing before something that takes time."""


def model_path(name: str) -> Path:
    for d in (MODELS, USER_MODELS):
        if (d / name).exists():
            return d / name
    return USER_MODELS / name


def emit(kind: str, **fields) -> None:
    print(json.dumps({"event": kind, **fields}, ensure_ascii=False), flush=True)


# --- sentences --------------------------------------------------------------------------

SENTENCE_END = re.compile(r"([.!?…:;])(\s+)")


class Sentences:
    """Text as it streams in -> whole sentences to speak, as soon as each is complete."""

    def __init__(self) -> None:
        self.buf = ""

    def feed(self, text: str) -> list[str]:
        self.buf += text
        out = []
        while True:
            m = SENTENCE_END.search(self.buf)
            if not m:
                break
            out.append(self.buf[:m.end(1)].strip())
            self.buf = self.buf[m.end():]
        # a long run without an end: break at a comma, so speech can start
        if len(self.buf) > 220 and "," in self.buf[100:]:
            cut = self.buf.index(",", 100) + 1
            out.append(self.buf[:cut].strip())
            self.buf = self.buf[cut:]
        return [s for s in (clean(x) for x in out) if s]

    def flush(self) -> list[str]:
        rest, self.buf = clean(self.buf), ""
        return [rest] if rest else []


def clean(text: str) -> str:
    """What reads aloud badly goes: markdown marks, links, code."""
    text = re.sub(r"```.*?```", "", text, flags=re.S)
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = re.sub(r"https?://\S+", "", text)
    text = re.sub(r"[*_`#>|]+", "", text)
    return " ".join(text.split())


# --- turns: where speech starts and ends ------------------------------------------------

class Endpointer:
    """VAD probabilities, window by window -> the start and the end of a turn."""

    def __init__(self) -> None:
        self.speaking = False
        self.run = 0          # consecutive windows above START_PROB
        self.silence = 0      # consecutive windows below END_PROB, once speaking
        self.windows = 0

    def step(self, prob: float) -> str | None:
        """"start", "end" or None."""
        if not self.speaking:
            self.run = self.run + 1 if prob >= START_PROB else 0
            if self.run >= START_WINDOWS:
                self.speaking, self.silence, self.windows = True, 0, 0
                return "start"
            return None
        self.windows += 1
        self.silence = self.silence + 1 if prob < END_PROB else 0
        if self.silence * WINDOW / RATE >= END_SILENCE_S or self.windows * WINDOW / RATE >= MAX_TURN_S:
            self.speaking, self.run = False, 0
            return "end"
        return None


class Vad:
    """Silero VAD (ONNX) on 512-sample windows of 16 kHz audio."""

    def __init__(self) -> None:
        import numpy as np
        import onnxruntime as ort
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 1
        opts.inter_op_num_threads = 1
        self.np = np
        self.session = ort.InferenceSession(str(model_path(VAD_MODEL)), opts, providers=["CPUExecutionProvider"])
        self.reset()

    def reset(self) -> None:
        self.state = self.np.zeros((2, 1, 128), dtype=self.np.float32)
        self.context = self.np.zeros(64, dtype=self.np.float32)

    def prob(self, window: bytes) -> float:
        np = self.np
        x = np.frombuffer(window, dtype=np.int16).astype(np.float32) / 32768.0
        inp = np.concatenate([self.context, x])[None, :]
        out, self.state = self.session.run(None, {"input": inp, "state": self.state,
                                                  "sr": np.array(RATE, dtype=np.int64)})
        self.context = x[-64:]
        return float(out[0][0])


# --- audio in and out -------------------------------------------------------------------

def node_exists(name: str) -> bool:
    out = os.popen("pw-cli ls Node 2>/dev/null").read()
    return f'node.name = "{name}"' in out


async def microphone() -> AsyncIterator[bytes]:
    """512-sample windows from the microphone, through the echo canceller if it is there."""
    cmd = ["pw-record", "--rate", str(RATE), "--channels", "1", "--format", "s16", "--latency", "32ms"]
    if node_exists("mobarchy-echo-cancel-source"):
        cmd += ["--target", "mobarchy-echo-cancel-source"]
    cmd.append("-")
    proc = await asyncio.create_subprocess_exec(*cmd, stdout=asyncio.subprocess.PIPE,
                                                stderr=asyncio.subprocess.DEVNULL)
    assert proc.stdout is not None
    try:
        while True:
            chunk = await proc.stdout.readexactly(WINDOW * 2)
            yield chunk
    except asyncio.IncompleteReadError:
        return
    finally:
        if proc.returncode is None:
            proc.kill()


class Speaker:
    """Plays 16-bit mono audio, sentence after sentence; stop() silences it at once."""

    def __init__(self) -> None:
        self.proc: asyncio.subprocess.Process | None = None
        self.rate = 0

    async def play(self, audio: bytes, rate: int) -> None:
        if self.proc is None or self.rate != rate or self.proc.returncode is not None:
            await self.stop()
            cmd = ["pw-play", "--rate", str(rate), "--channels", "1", "--format", "s16"]
            if node_exists("mobarchy-echo-cancel-sink"):
                cmd += ["--target", "mobarchy-echo-cancel-sink"]
            cmd.append("-")
            self.proc = await asyncio.create_subprocess_exec(*cmd, stdin=asyncio.subprocess.PIPE,
                                                             stderr=asyncio.subprocess.DEVNULL)
            self.rate = rate
        assert self.proc.stdin is not None
        self.proc.stdin.write(audio)
        await self.proc.stdin.drain()

    async def drain(self) -> None:
        """Wait until what was given has been played (pw-play then exits)."""
        if self.proc and self.proc.stdin:
            self.proc.stdin.close()
            await self.proc.wait()
        self.proc = None

    async def stop(self) -> None:
        if self.proc and self.proc.returncode is None:
            self.proc.kill()
            await self.proc.wait()
        self.proc = None


# --- speech: the phone's, or a service's -----------------------------------------------

STT_KEYS = {"openai": "OPENAI_API_KEY"}
TTS_KEYS = {"openai": "OPENAI_API_KEY", "elevenlabs": "ELEVENLABS_API_KEY"}


def _service(setting: str, keys: dict[str, str], local: str) -> str:
    chosen = os.environ.get(setting, local)
    return chosen if chosen in keys and os.environ.get(keys[chosen]) else local


def stt() -> str:
    """Who understands: "whisper" (the phone) or "openai"."""
    return _service("MOBARCHY_TALK_STT", STT_KEYS, "whisper")


def tts() -> str:
    """Who speaks: "piper" (the phone), "elevenlabs" or "openai"."""
    return _service("MOBARCHY_TALK_TTS", TTS_KEYS, "piper")


def write_wav(path: Path, pcm: bytes) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm)


def talk_language() -> str:
    """The language spoken to Frigg on the phone: MOBARCHY_TALK_LANGUAGE (a code, or "auto"),
    else the system's (LANG), else English."""
    if os.environ.get("MOBARCHY_TALK_LANGUAGE"):
        return os.environ["MOBARCHY_TALK_LANGUAGE"]
    code = os.environ.get("LANG", "").split("_")[0].split(".")[0].lower()
    return code if re.fullmatch(r"[a-z]{2}", code) else "en"


def transcribe(pcm: bytes) -> tuple[str, str]:
    """(text, language)."""
    with tempfile.TemporaryDirectory() as d:
        wav = Path(d) / "turn.wav"
        write_wav(wav, pcm)
        if stt() == "openai":
            return _openai_transcribe(wav)
        # On the phone: Norwegian with NB-Whisper; another language with the multilingual
        # model. Both take ~1.5 s on comet. "auto" detects the language, but takes ~7.
        language = talk_language()
        if language in ("no", "nb", "nn"):
            return voice.transcribe(wav, "no"), "no"
        return voice.transcribe_detect(wav, language)


def _openai_transcribe(wav: Path) -> tuple[str, str]:
    boundary = "----mobarchy"
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\nverbose_json\r\n"
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"turn.wav\"\r\n"
            f"Content-Type: audio/wav\r\n\r\n").encode() + wav.read_bytes() + f"\r\n--{boundary}--\r\n".encode()
    req = urllib.request.Request("https://api.openai.com/v1/audio/transcriptions", data=body, headers={
        "Authorization": f"Bearer {os.environ['OPENAI_API_KEY']}",
        "Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=60) as r:
        result = json.load(r)
    return result.get("text", "").strip(), LANGUAGES.get(result.get("language", ""), DEFAULT_LANGUAGE)


# whisper-1's verbose_json gives language names
LANGUAGES = {"norwegian": "no", "nynorsk": "nn", "english": "en", "swedish": "sv", "danish": "da",
             "german": "de", "french": "fr", "spanish": "es"}


class Voices:
    """Piper voices by language, loaded once."""

    def __init__(self) -> None:
        self.loaded: dict[str, object] = {}

    def synthesize(self, text: str, language: str) -> tuple[bytes, int]:
        from piper import PiperVoice
        name = installed_voice(language) or installed_voice(DEFAULT_LANGUAGE)
        if not name:
            raise RuntimeError("no voice: run `mobarchy-agent setup talk`")
        if name not in self.loaded:
            self.loaded[name] = PiperVoice.load(str(model_path(f"{name}.onnx")))
        audio, rate = b"", 22050
        for chunk in self.loaded[name].synthesize(text):
            audio += chunk.audio_int16_bytes
            rate = chunk.sample_rate
        return audio, rate


def openai_speech(text: str, language: str, previous: str = "") -> Iterator[tuple[bytes, int]]:
    req = urllib.request.Request("https://api.openai.com/v1/audio/speech", data=json.dumps({
        "model": "gpt-4o-mini-tts", "voice": os.environ.get("MOBARCHY_TALK_VOICE", "nova"),
        "input": text, "response_format": "pcm"}).encode(), headers={
        "Authorization": f"Bearer {os.environ['OPENAI_API_KEY']}", "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        yield from pcm_chunks(r, 24000)


# ElevenLabs: Flash (~75 ms to the first sound), streamed. A voice of the owner's choice
# (MOBARCHY_TALK_VOICE, a voice id), else Sarah, one of ElevenLabs' own.
ELEVENLABS_VOICE = "EXAVITQu4vr4xnSDxMaL"
ELEVENLABS_RATE = 22050


def elevenlabs_speech(text: str, language: str, previous: str = "") -> Iterator[tuple[bytes, int]]:
    body = {"text": text, "model_id": os.environ.get("MOBARCHY_TALK_TTS_MODEL", "eleven_flash_v2_5"),
            "voice_settings": {"speed": float(os.environ.get("MOBARCHY_TALK_SPEED", "1.0"))}}
    code = FAMILY.get(language, language)
    if re.fullmatch(r"[a-z]{2}", code):
        body["language_code"] = code
    if previous:
        body["previous_text"] = previous   # (the sentences flow on, not each from scratch)
    voice_id = os.environ.get("MOBARCHY_TALK_VOICE", ELEVENLABS_VOICE)
    req = urllib.request.Request(
        f"https://api.elevenlabs.io/v1/text-to-speech/{voice_id}/stream?output_format=pcm_{ELEVENLABS_RATE}",
        data=json.dumps(body).encode(),
        headers={"xi-api-key": os.environ["ELEVENLABS_API_KEY"], "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        yield from pcm_chunks(r, ELEVENLABS_RATE)


def pcm_chunks(stream, rate: int, size: int = 4096) -> Iterator[tuple[bytes, int]]:
    """16-bit audio from a response as it comes, whole samples at a time."""
    rest = b""
    while chunk := stream.read(size):
        chunk = rest + chunk
        cut = len(chunk) - len(chunk) % 2
        rest = chunk[cut:]
        if cut:
            yield chunk[:cut], rate


# --- the agent: the Mobarchy chat's Claude Code session ----------------------------------

class ClaudeSession:
    """Claude Code with stream-json input: it stays up between turns. It goes on with a
    session (the chat's, resumed), and knows the id each turn leaves it at."""

    def __init__(self, cmd: list[str]) -> None:
        self.cmd = cmd
        self.proc: asyncio.subprocess.Process | None = None
        self.session_id: str | None = None

    @property
    def running(self) -> bool:
        return self.proc is not None and self.proc.returncode is None

    async def start(self, resume: str | None = None) -> None:
        env = {**os.environ, "MCP_TOOL_TIMEOUT": "600000"}
        agents.WORKDIR.mkdir(parents=True, exist_ok=True)
        self.session_id = resume
        self.proc = await asyncio.create_subprocess_exec(
            *self.cmd, *(["--resume", resume] if resume else []),
            "-p", "--input-format", "stream-json", "--include-partial-messages",
            cwd=agents.WORKDIR, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL, env=env, limit=16 * 1024 * 1024)

    async def ask(self, text: str) -> AsyncIterator[tuple[str, str]]:
        """("text", delta) as the answer streams, ("tool", what) for tool calls; ends with the turn."""
        assert self.proc and self.proc.stdin and self.proc.stdout
        msg = {"type": "user", "message": {"role": "user", "content": [{"type": "text", "text": text}]}}
        self.proc.stdin.write((json.dumps(msg) + "\n").encode())
        await self.proc.stdin.drain()
        async for raw in self.proc.stdout:
            if b'"type":"result"' in raw or b'"subtype":"init"' in raw:
                self.session_id = json.loads(raw).get("session_id") or self.session_id
            for kind, value in parse_stream_line(raw.decode(errors="replace")):
                if kind == "end":
                    return
                yield kind, value
        raise RuntimeError("Claude Code stopped")

    async def close(self) -> None:
        self.session_id = None
        if self.proc and self.proc.returncode is None:
            if self.proc.stdin:
                self.proc.stdin.close()
            try:
                await asyncio.wait_for(self.proc.wait(), 5)
            except TimeoutError:
                self.proc.kill()


def parse_stream_line(line: str) -> list[tuple[str, str]]:
    """One stream-json line (with partial messages) -> ("text", delta), ("tool", name),
    ("error", text) or ("end", "")."""
    try:
        msg = json.loads(line)
    except ValueError:
        return []
    kind = msg.get("type")
    if kind == "stream_event":
        ev = msg.get("event", {})
        if ev.get("type") == "content_block_delta" and ev.get("delta", {}).get("type") == "text_delta":
            return [("text", ev["delta"].get("text", ""))]
        if ev.get("type") in ("content_block_stop", "message_stop"):
            # (a block's or a message's text ends here: the next one isn't glued to it)
            return [("text", "\n")]
        return []
    if kind == "assistant":
        return [("tool", b.get("name", "").removeprefix("mcp__mobarchy__"))
                for b in msg.get("message", {}).get("content", []) if b.get("type") == "tool_use"]
    if kind == "result":
        if msg.get("is_error"):
            return [("error", (msg.get("result") or "Claude stopped").strip()), ("end", "")]
        return [("end", "")]
    return []


# --- the conversation -------------------------------------------------------------------

def chime() -> bytes:
    """A soft rising two-note sound for "I'm listening" (16 kHz, 0.25 s)."""
    import numpy as np
    out = []
    for freq, dur in ((660, 0.11), (990, 0.14)):
        t = np.arange(int(16000 * dur)) / 16000
        out.append(np.sin(2 * np.pi * freq * t) * np.sin(np.pi * t / dur) ** 2)
    return (np.concatenate(out) * 0.25 * 32767).astype(np.int16).tobytes()


CHIME = chime()


class Talk:
    """A conversation. With a wake word it waits for the phrase ("waiting"), talks, and after
    FOLLOW_UP_S without speech waits again; without one it talks until stopped."""

    def __init__(self, session: ClaudeSession, out: Callable[..., None] = emit, wake=None) -> None:
        self.session = session
        self.out = out
        self.wake = wake
        self.threshold = float(os.environ.get("MOBARCHY_WAKE_THRESHOLD", "0.5"))
        self.last_active = time.monotonic()
        self.vad = Vad()
        self.speaker = Speaker()
        self.voices = Voices()
        self.state = "idle"
        self.stopping = False
        self.speaking_task: asyncio.Task | None = None
        self.interrupted = asyncio.Event()

    async def join_chat(self) -> agents.Conversation:
        """The Mobarchy chat's conversation, as it is now, and its Claude session for this
        turn: Frigg goes on where the chat is (and the chat where Frigg left off). The
        session is started again when the chat has moved on since (a turn, a new chat)."""
        conv = agents.current_conversation(False)
        chat = conv.sessions.get("claude")
        if not self.session.running or self.session.session_id != chat:
            await self.session.close()
            await self.session.start(resume=chat)
        return conv

    def set_state(self, state: str) -> None:
        if state != self.state:
            self.state = state
            self.out("state", state=state)

    async def run(self) -> None:
        if tts() == "piper":
            # (a voice loads in ~1 s: not on the first sentence)
            for language in {talk_language(), DEFAULT_LANGUAGE}:
                asyncio.get_running_loop().run_in_executor(None, self.voices.synthesize, "Hei.", language)
        commands = asyncio.create_task(self.read_commands())
        try:
            await self.listen()
        finally:
            commands.cancel()
            await self.speaker.stop()
            await self.session.close()
            self.set_state("idle")

    async def read_commands(self) -> None:
        reader = asyncio.StreamReader()
        loop = asyncio.get_running_loop()
        await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), os.fdopen(0, "rb", 0))
        async for line in reader:
            cmd = line.decode().strip()
            if cmd == "stop":
                self.stopping = True
                await self.cut_in()
            elif cmd == "interrupt":
                await self.cut_in()
            elif cmd == "sleep" and self.wake:       # end this conversation, wait for the phrase
                await self.cut_in()
                self.set_state("waiting")
            elif cmd == "wake" and self.state == "waiting":     # as if the phrase was heard
                await self.woken()

    async def woken(self) -> None:
        self.wake.reset()
        self.last_active = time.monotonic()
        self.set_state("listening")
        await self.speaker.play(CHIME, 16000)
        await self.speaker.drain()

    async def cut_in(self) -> None:
        """Stop speaking (and the rest of this answer), and listen."""
        self.interrupted.set()
        await self.speaker.stop()

    async def listen(self) -> None:
        ep = Endpointer()
        turn = bytearray()
        pre: list[bytes] = []
        barge = 0
        self.set_state("waiting" if self.wake else "listening")
        async for window in microphone():
            if self.stopping:
                return
            now = time.monotonic()
            if self.state == "waiting":
                scores = self.wake.feed(window)
                if scores and max(scores) >= self.threshold:
                    await self.woken()
                    ep, turn, pre = Endpointer(), bytearray(), []
                elif self.session.running and now - self.last_active > SESSION_IDLE_S:
                    await self.session.close()
                continue
            if self.wake and self.state == "listening" and not ep.speaking \
                    and now - self.last_active > FOLLOW_UP_S:
                self.set_state("waiting")
                continue
            prob = self.vad.prob(window)
            if self.state == "speaking" or self.state == "thinking":
                # talking over Frigg: stop it, and this is the next turn
                barge = barge + 1 if prob >= START_PROB else 0
                pre = (pre + [window])[-PRE_ROLL_WINDOWS:]
                if barge >= BARGE_IN_WINDOWS and self.state == "speaking":
                    await self.cut_in()
                    ep = Endpointer()
                    ep.speaking, turn = True, bytearray(b"".join(pre))
                    self.set_state("listening")
                continue
            pre = (pre + [window])[-PRE_ROLL_WINDOWS:]
            what = ep.step(prob)
            if ep.speaking:
                self.last_active = now
            if what == "start":
                turn = bytearray(b"".join(pre))
            elif ep.speaking:
                turn += window
            if what == "end":
                pcm, turn = bytes(turn), bytearray()
                self.speaking_task = asyncio.create_task(self.answer(pcm))

    async def answer(self, pcm: bytes) -> None:
        self.set_state("thinking")
        self.interrupted.clear()
        try:
            text, language = await asyncio.to_thread(transcribe, pcm)
        except Exception as e:
            self.out("error", text=f"could not understand: {e}")
            self.set_state("listening")
            return
        if not text:
            self.set_state("listening")
            return
        self.out("heard", text=text, language=language)
        conv = await self.join_chat()
        conv.add("user", text, "claude")
        conv.save()
        prompt = conv.bridged_prompt("claude", text)
        sentences = Sentences()
        spoken = []
        queue: asyncio.Queue[str | None] = asyncio.Queue()
        speak = asyncio.create_task(self.speak(queue, language))
        try:
            for attempt in (1, 2):
                answered = False
                try:
                    async for kind, value in self.session.ask(prompt):
                        answered = True
                        if self.interrupted.is_set():
                            continue    # (the rest of the answer goes unspoken)
                        if kind == "text":
                            for s in sentences.feed(value):
                                spoken.append(s)
                                await queue.put(s)
                        elif kind == "tool":
                            self.out("tool", text=value)
                        elif kind == "error":
                            self.out("error", text=value)
                    break
                except RuntimeError:
                    # (the chat's session could not be resumed: a new one, once)
                    if answered or attempt == 2:
                        raise
                    await self.session.close()
                    await self.session.start()
            if not self.interrupted.is_set():
                for s in sentences.flush():
                    spoken.append(s)
                    await queue.put(s)
        except Exception as e:
            self.out("error", text=f"Claude: {e}")
        finally:
            await queue.put(None)
            await speak
            if spoken:
                conv.add("assistant", " ".join(spoken), "claude")
            if self.session.session_id:
                conv.sessions["claude"] = self.session.session_id
            conv.seen["claude"] = len(conv.turns)
            conv.save()
            if not self.stopping and self.state != "waiting":
                self.last_active = time.monotonic()
                self.set_state("listening")

    async def speech(self, sentence: str, language: str, previous: str) -> AsyncIterator[tuple[bytes, int]]:
        """The sentence's audio as it is made (a service streams it; Piper makes it whole)."""
        make = {"elevenlabs": elevenlabs_speech, "openai": openai_speech}.get(tts())
        if make is None:
            yield await asyncio.to_thread(self.voices.synthesize, sentence, language)
            return
        loop = asyncio.get_running_loop()
        queue: asyncio.Queue = asyncio.Queue()

        def produce() -> None:
            try:
                for item in make(sentence, language, previous):
                    loop.call_soon_threadsafe(queue.put_nowait, item)
            except Exception as e:
                loop.call_soon_threadsafe(queue.put_nowait, e)
            loop.call_soon_threadsafe(queue.put_nowait, None)

        loop.run_in_executor(None, produce)
        while (item := await queue.get()) is not None:
            if isinstance(item, Exception):
                raise item
            yield item

    async def speak(self, queue: asyncio.Queue, language: str) -> None:
        previous = ""
        while (sentence := await queue.get()) is not None:
            if self.interrupted.is_set():
                continue
            first = True
            try:
                async for audio, rate in self.speech(sentence, language, previous):
                    if self.interrupted.is_set():
                        break
                    if first:
                        self.set_state("speaking")
                        self.out("said", text=sentence)
                        first = False
                    await self.speaker.play(audio, rate)
            except Exception as e:
                self.out("error", text=f"could not speak: {e}")
            previous = sentence
        if not self.interrupted.is_set():
            await self.speaker.drain()


async def main(config: Config | None = None, wake: bool = False) -> int:
    config = config or Config.load()
    exe = agents.claude_bin()
    if not exe:
        emit("error", text="Frigg needs the Claude agent (Claude Code) for now.")
        return 1
    for need in ("pw-record", "pw-play"):
        if not shutil.which(need):
            emit("error", text=f"{need} is missing")
            return 1
    # The models for the language, the first time it is spoken (a few hundred MB)
    language = talk_language()
    need = missing(language, wake)
    if need:
        emit("state", state="starting")
        emit("tool", text=f"Downloading speech models ({language})")
        try:
            await asyncio.to_thread(download, need)
        except Exception as e:
            emit("error", text=f"could not download the speech models: {e}")
            return 1
    config = agents.replace(config, mcp_url=agents.task_url(config.mcp_url))
    cmd = await agents.claude_command(exe, config, SYSTEM_PROMPT + VOICE_PROMPT)
    # A quick model for a spoken conversation: on comet, Sonnet's first words come in ~1-3 s,
    # the default's in 5-7 (MOBARCHY_TALK_MODEL changes it; "" for Claude Code's default).
    model = os.environ.get("MOBARCHY_TALK_MODEL", "sonnet")
    if model:
        cmd += ["--model", model]
    detector = None
    if wake:
        from .wake import WakeWord
        if not model_path(WAKE_MODEL).exists():
            emit("error", text=f"the wake phrase's model ({WAKE_MODEL}) is missing")
            return 1
        detector = WakeWord(model_path(WAKE_MODEL), model_path(WAKE_FEATURES[0]).parent)
    await Talk(ClaudeSession(cmd), wake=detector).run()
    return 0


# --- the models: the VAD, whisper's and the Piper voices -------------------------------

VOICE_BASE = "https://huggingface.co/rhasspy/piper-voices/resolve/main"
VAD_URL = "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx"
QUALITY = ("medium", "high", "low", "x_low")
# The wake phrase: openWakeWord's feature models, and our classifier for "Hei Frigg"
# (agent/wakeword trains it)
WAKE_MODEL = "hei_frigg.onnx"
WAKE_FEATURES = ("melspectrogram.onnx", "embedding_model.onnx")
WAKE_URL = "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1"


def installed_voice(language: str) -> str | None:
    """The Piper voice on the phone for a language, if there is one."""
    family = FAMILY.get(language, language)
    if family in VOICES and model_path(f"{VOICES[family]}.onnx").exists():
        return VOICES[family]
    found = sorted(p.stem for d in (MODELS, USER_MODELS) for p in d.glob(f"{family}_*.onnx"))
    return found[0] if found else None


def choose_voice(catalogue: dict, language: str) -> tuple[str, str] | None:
    """(name, path in the catalogue) of a voice for the language: ours, else one speaker at
    the best quality."""
    family = FAMILY.get(language, language)
    candidates = [v for v in catalogue.values() if v["language"]["family"] == family]
    if family in VOICES:
        candidates = [v for v in candidates if v["key"] == VOICES[family]]
    candidates.sort(key=lambda v: (v.get("num_speakers", 1) != 1,
                                   QUALITY.index(v["quality"]) if v["quality"] in QUALITY else 9, v["key"]))
    if not candidates:
        return None
    onnx = next(f for f in candidates[0]["files"] if f.endswith(".onnx"))
    return candidates[0]["key"], onnx.rsplit("/", 1)[0]


def catalogue() -> dict:
    with urllib.request.urlopen(f"{VOICE_BASE}/voices.json", timeout=60) as r:
        return json.load(r)


def missing(language: str, wake: bool = False) -> dict[str, str]:
    """{file: url} of the models the language needs that aren't on the phone: the VAD, and
    whisper's model and a Piper voice unless a service does their job."""
    want = {} if model_path(VAD_MODEL).exists() else {VAD_MODEL: VAD_URL}
    if wake:
        want |= {f: f"{WAKE_URL}/{f}" for f in WAKE_FEATURES if not model_path(f).exists()}
    if stt() == "whisper":
        if FAMILY.get(language, language) == "no":
            if not voice.model_file().exists():
                want[voice.MODEL_NAME] = voice.MODEL_URL
        elif not voice.multi_model_file().exists():
            want[voice.MULTI_MODEL_NAME] = voice.MULTI_MODEL_URL
    if tts() != "piper":
        return want
    voices = [DEFAULT_LANGUAGE] + ([language] if language not in ("auto", DEFAULT_LANGUAGE) else [])
    absent = [lang for lang in voices if not installed_voice(lang)]
    if absent:
        cat = catalogue()
        for lang in absent:
            chosen = choose_voice(cat, lang)
            if not chosen:
                continue   # (no Piper voice for it: Frigg answers with the default's)
            name, path = chosen
            for f in (f"{name}.onnx", f"{name}.onnx.json"):
                want[f] = f"{VOICE_BASE}/{path}/{f}"
    return want


def download(files: dict[str, str]) -> None:
    import subprocess
    USER_MODELS.mkdir(parents=True, exist_ok=True)
    for name, url in files.items():
        part = USER_MODELS / f"{name}.part"
        subprocess.run(["curl", "-fsSL", "-o", str(part), url], check=True, timeout=1800)
        part.rename(USER_MODELS / name)


def setup() -> None:
    """Download what the language Frigg listens in needs (MOBARCHY_TALK_LANGUAGE, else the
    system's): the VAD, whisper's model and the Piper voices."""
    Config.load()   # (agent.env's settings)
    download(missing(talk_language()))
