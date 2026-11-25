"""Desktop Companion LAN bridge. API credentials stay in the PC environment."""
from __future__ import annotations

import argparse
import array
import base64
import hashlib
import hmac
import io
import json
import os
import re
import secrets
import struct
import ssl
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import wave
import webbrowser
from audio_ducking import Ducking
from bridge_artwork import artwork
from openai_network import openai_urlopen, openai_websocket
from pod_dashboard import Dashboard, ProtectedKey, serve
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

MAX_UPLOAD = 1_280_100
MAX_REPLY_SECONDS = 30
DISPLAY_BYTES = 1023


def display_text(value: str, limit: int = DISPLAY_BYTES) -> str:
    # Protect the board's bounded JSON response and preserve UTF-8 boundaries.
    value = "".join(c if ord(c) >= 32 or c in "\n\t" else " " for c in value)
    return value.encode("utf-8", "replace")[:limit].decode("utf-8", "ignore")



def strip_wake_phrase(transcript: str, phrase: str) -> str:
    pattern = r"[\s,]+".join(re.escape(word) for word in phrase.split())
    if pattern:
        transcript = re.sub(r"^" + pattern + r"(?=$|[^\w])[,.:!?\s]*", "",
                            transcript.strip(), count=1, flags=re.IGNORECASE)
    return transcript.strip() if transcript.strip(" ,.:!?\t\r\n") else ""


class BridgeError(Exception):
    """Contains only a safe, user-facing message, never upstream response bodies."""


class VoiceTransportError(BridgeError):
    """Realtime connection failed before submitting a generation request."""


def voice_http_error(code):
    if code in (401, 403):
        return BridgeError("OpenAI voice key or model access denied")
    if code == 429:
        return BridgeError("OpenAI voice quota or rate limit reached")
    return BridgeError(f"OpenAI voice request rejected (HTTP {code})")


def samples_from_bytes(raw: bytes) -> array.array:
    if len(raw) % 2:
        raise BridgeError("Malformed PCM audio")
    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def pcm_bytes(samples) -> bytes:
    result = array.array("h", samples)
    if sys.byteorder != "little":
        result.byteswap()
    return result.tobytes()


def make_wav(raw: bytes, rate: int = 16000) -> bytes:
    stream = io.BytesIO()
    with wave.open(stream, "wb") as wav:
        wav.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        wav.writeframes(raw)
    return stream.getvalue()


def question_audio(data: bytes) -> bytes:
    """Validate the board WAV and select the louder of its two microphone slots."""
    if not 44 <= len(data) <= MAX_UPLOAD:
        raise BridgeError("Recording size is invalid")
    try:
        with wave.open(io.BytesIO(data), "rb") as wav:
            channels, width, rate, frames, compression, _ = wav.getparams()
            if channels not in (1, 2) or width != 2 or rate != 16000 or compression != "NONE":
                raise BridgeError("Need a 16kHz PCM16 mono or stereo recording")
            if not 4000 <= frames <= 320000:
                raise BridgeError("Record between 0.25 and 20 seconds")
            raw = wav.readframes(frames)
            if len(raw) != frames * channels * width:
                raise BridgeError("Recording is incomplete")
    except (wave.Error, EOFError, struct.error) as error:
        raise BridgeError("Recording is not a valid WAV") from error
    samples = samples_from_bytes(raw)
    if channels == 2:
        left, right = samples[0::2], samples[1::2]
        samples = left if sum(x*x for x in left) >= sum(x*x for x in right) else right
    if max((abs(x) for x in samples), default=0) < 40:
        raise BridgeError("Recording is too quiet; speak closer to the microphone")
    return make_wav(pcm_bytes(samples))


def reply_wav(raw_24k: bytes) -> bytes:
    """Convert bounded mono PCM16 Realtime output to the board's 16kHz format."""
    samples = samples_from_bytes(raw_24k)
    if not samples or len(samples) > 24000 * MAX_REPLY_SECONDS:
        raise BridgeError("Spoken reply was empty or too long; ask for a shorter answer")
    # Linear interpolation keeps this small bridge independent of scipy/ffmpeg.
    count = len(samples) * 2 // 3
    output = []
    for n in range(count):
        position = n * 3 / 2
        low = int(position)
        fraction = position - low
        high = min(low + 1, len(samples) - 1)
        output.append(round(samples[low] * (1-fraction) + samples[high] * fraction))
    return make_wav(pcm_bytes(output))


class OpenAIProvider:
    def __init__(self, key: str):
        self.key = key
        self.intent_model = os.getenv("COMPANION_INTENT_MODEL", "gpt-5-mini")
        self.transcription_model = os.getenv("COMPANION_TRANSCRIBE_MODEL", "gpt-transcribe")
        self.voice_model = os.getenv("COMPANION_VOICE_MODEL", "gpt-realtime-2.1-mini")
        self.https_voice_model = os.getenv("COMPANION_HTTPS_VOICE_MODEL", "gpt-audio-1.5")
        self.cache_dir = None

    def interpret(self, transcript):
        if not self.key:
            raise BridgeError("Add the OpenAI API key in Pod Settings")
        properties = {
            "kind": {"type": "string", "enum": ["none", "clock", "alarm"]},
            "day": {"type": "string", "enum": ["next", "today", "tomorrow"]},
            "seconds": {"type": ["integer", "null"]},
            "hour": {"type": ["integer", "null"]},
            "minute": {"type": ["integer", "null"]},
            "ambiguous": {"type": "boolean"},
        }
        schema = {"type": "object", "properties": properties,
                  "required": list(properties), "additionalProperties": False}
        instructions = (
            "Classify this voice transcript for a desk clock. Use clock only for a request "
            "for the current local time. Use alarm only for an explicit request to create "
            "one alarm, such as asking to wake the user. Use none for other questions, "
            "examples, quoted requests, recurring alarms, unsupported dates, or requests "
            "to change or cancel an alarm. The supported days are today, tomorrow, and next "
            "occurrence. For a relative delay extract seconds (max 86400), use day next, "
            "and leave hour/minute null. For a clock time, use a 24-hour hour when clear. "
            "When the hour is 1 through 12 and AM/PM is missing, preserve that hour and set "
            "ambiguous=true. Seven past five is 5:07. If the time is missing, leave hour "
            "and minute null and ambiguous=false. Never invent missing information."
        )
        request = urllib.request.Request(
            "https://api.openai.com/v1/chat/completions",
            data=json.dumps({
                "model": self.intent_model, "store": False, "max_completion_tokens": 256,
                **({"reasoning_effort": "minimal"} if self.intent_model.startswith("gpt-5-mini") else {}),
                "response_format": {"type": "json_schema", "json_schema": {
                    "name": "pod_intent", "schema": schema, "strict": True}},
                "messages": [{"role": "system", "content": instructions},
                             {"role": "user", "content": transcript}],
            }).encode(),
            headers={"Authorization": "Bearer " + self.key, "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with openai_urlopen(request, timeout=8) as response:
                body = json.loads(response.read(8193))
            result = json.loads(body["choices"][0]["message"]["content"])
            if not isinstance(result, dict) or result.keys() != properties.keys():
                raise ValueError()
            return result
        except (OSError, ValueError, KeyError, IndexError, TypeError):
            raise BridgeError("Intent interpretation unavailable") from None

    def say(self, text):
        key = hashlib.sha256(json.dumps(["sam-pcm16k-v1", self.voice_model, "marin", text],
                                        ensure_ascii=False).encode()).hexdigest()
        path = self.cache_dir / (key + ".wav") if self.cache_dir else None
        if path:
            try:
                with wave.open(str(path), "rb") as cached:
                    if (cached.getnchannels(), cached.getsampwidth(), cached.getframerate()) == (1, 2, 16000):
                        frames = cached.getnframes()
                        if 0 < frames <= 16000 * MAX_REPLY_SECONDS:
                            pcm = cached.readframes(frames)
                            if len(pcm) == frames * 2:
                                return make_wav(pcm)
            except (OSError, EOFError, wave.Error):
                pass
        if not self.key:
            raise BridgeError("Add the OpenAI API key in Pod Settings")
        _, audio = self.speak(text, [], exact=True)
        if path:
            try:
                path.parent.mkdir(parents=True, exist_ok=True)
                temporary = path.with_suffix(".tmp")
                temporary.write_bytes(audio)
                temporary.replace(path)
                entries = sorted(path.parent.glob("*.wav"), key=lambda item: item.stat().st_mtime)
                for expired in entries[:-256]:
                    expired.unlink()
            except OSError:
                print("Could not update the speech cache", flush=True)
        return audio

    def transcribe(self, wav: bytes) -> str:
        if not self.key:
            raise BridgeError("Add the OpenAI API key in Pod Settings")
        boundary = "companion" + secrets.token_hex(16)
        body = (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"
            f"{self.transcription_model}\r\n"
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"question.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n"
        ).encode() + wav + f"\r\n--{boundary}--\r\n".encode()
        request = urllib.request.Request(
            "https://api.openai.com/v1/audio/transcriptions", data=body,
            headers={"Authorization": "Bearer " + self.key,
                     "Content-Type": "multipart/form-data; boundary=" + boundary},
            method="POST",
        )
        try:
            with openai_urlopen(request, timeout=30) as response:
                payload = response.read(65537)
            if len(payload) > 65536:
                raise BridgeError("Transcription response is too large")
            text = json.loads(payload).get("text", "").strip()
        except urllib.error.HTTPError as error:
            if error.code in (401, 403):
                raise BridgeError("OpenAI rejected the PC API key or model access") from None
            if error.code == 429:
                raise BridgeError("OpenAI quota or rate limit reached") from None
            raise BridgeError("OpenAI transcription failed") from None
        except (OSError, ValueError) as error:
            raise BridgeError("Cannot complete OpenAI transcription") from error
        if not text or len(text) > 2000:
            raise BridgeError("No usable speech recognized; record again")
        return text

    def connect_voice(self, websocket):
        url = "wss://api.openai.com/v1/realtime?" + urllib.parse.urlencode({"model": self.voice_model})
        failure = "connection interrupted"
        for attempt in range(1, 3):
            try:
                return openai_websocket(websocket, url, self.key, timeout=5)
            except websocket.WebSocketBadStatusException as error:
                if error.status_code not in (408, 500, 502, 503, 504):
                    raise voice_http_error(error.status_code) from None
                failure = f"HTTP {error.status_code}"
            except ssl.SSLCertVerificationError:
                raise BridgeError("OpenAI voice TLS certificate verification failed") from None
            except (TimeoutError, websocket.WebSocketTimeoutException):
                failure = "connection timed out"
            except (OSError, websocket.WebSocketException):
                failure = "connection interrupted"
            print(f"OpenAI Realtime attempt {attempt}: {failure}", flush=True)
        raise VoiceTransportError(f"OpenAI Realtime {failure}")

    def speak_https(self, instructions, transcript, history):
        messages = [{"role": "system", "content": instructions}]
        for turn in history[-12:]:
            if turn.get("role") in ("user", "assistant") and isinstance(turn.get("text"), str):
                messages.append({"role": turn["role"], "content": turn["text"]})
        messages.append({"role": "user", "content": transcript})
        body = json.dumps({
            "model": self.https_voice_model,
            "modalities": ["text", "audio"],
            "audio": {"voice": "marin", "format": "wav"},
            "messages": messages,
            "max_completion_tokens": 2048,
            "store": False,
        }).encode()
        request = urllib.request.Request(
            "https://api.openai.com/v1/chat/completions", data=body,
            headers={"Authorization": "Bearer " + self.key, "Content-Type": "application/json"},
            method="POST",
        )
        try:
            with openai_urlopen(request, timeout=25) as response:
                payload = response.read(2100001)
        except urllib.error.HTTPError as error:
            raise voice_http_error(error.code) from None
        except (OSError, ValueError):
            raise BridgeError("OpenAI HTTPS voice connection failed") from None
        try:
            if len(payload) > 2100000:
                raise ValueError()
            message = json.loads(payload)["choices"][0]["message"]
            audio = message["audio"]
            reply = (audio.get("transcript") or message.get("content") or "").strip()
            wav = base64.b64decode(audio["data"], validate=True)
            max_frames = 24000 * MAX_REPLY_SECONDS
            with wave.open(io.BytesIO(wav), "rb") as source:
                if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, 24000):
                    raise ValueError()
                pcm = source.readframes(max_frames + 1)
                # The API can send a streaming WAV header with an unspecified size.
                frames = source.getnframes()
                if frames != 0x7fffffff and len(pcm) != frames * 2:
                    raise ValueError()
            if not reply or not 0 < len(pcm) <= max_frames * 2 or len(pcm) % 2:
                raise ValueError()
            return reply[:2000], reply_wav(pcm)
        except (ValueError, TypeError, KeyError, IndexError, AttributeError, wave.Error, EOFError):
            raise BridgeError("OpenAI HTTPS voice returned unusable audio") from None

    def speak(self, transcript: str, history: list[dict], user_name: str = "", exact=False) -> tuple[str, bytes]:
        if not self.key:
            raise BridgeError("Add the OpenAI API key in Pod Settings")
        import websocket

        instructions = (
            "You are Sam, a friendly desktop companion. When asked your name, say My name is Sam. Reply in the user's language. "
            "Answer in one or two short spoken sentences, at most 35 words. "
            "Do not use markdown. You cannot operate devices, browse the web, or provide live news or weather. "
            "Do not claim to have performed an action."
        )
        if exact:
            instructions = "Read the user message verbatim as Sam. Speak only those words, with no introduction or additions."
        if user_name:
            instructions += " The user's stated name is " + json.dumps(user_name) + ". Remember it during this conversation."

        socket = None
        try:
            try:
                socket = self.connect_voice(websocket)
            except VoiceTransportError:
                print("OpenAI Realtime unavailable; using HTTPS voice", flush=True)
                return self.speak_https(instructions, transcript, history)
            deadline = time.monotonic() + 60

            def receive():
                if time.monotonic() >= deadline:
                    raise BridgeError("OpenAI voice response timed out")
                socket.settimeout(max(0.1, min(15, deadline-time.monotonic())))
                event = json.loads(socket.recv())
                if event.get("type") == "error":
                    raise BridgeError("OpenAI voice request failed; check model access and quota")
                return event

            def wait_for(kind):
                while receive().get("type") != kind:
                    pass

            wait_for("session.created")
            socket.send(json.dumps({"type": "session.update", "session": {
                "type": "realtime", "output_modalities": ["audio"],
                "instructions": instructions,
                "audio": {
                    "input": {"turn_detection": None},
                    "output": {"format": {"type": "audio/pcm", "rate": 24000},
                               "voice": "marin"},
                },
            }}))
            wait_for("session.updated")
            for turn in history[-12:]:
                role = turn.get("role")
                value = turn.get("text")
                if role not in ("user", "assistant") or not isinstance(value, str):
                    continue
                socket.send(json.dumps({"type": "conversation.item.create", "item": {
                    "type": "message", "role": role,
                    "content": [{"type": "input_text" if role == "user" else "output_text",
                                 "text": value}],
                }}))
            socket.send(json.dumps({"type": "conversation.item.create", "item": {
                "type": "message", "role": "user",
                "content": [{"type": "input_text", "text": transcript}],
            }}))
            socket.send(json.dumps({"type": "response.create",
                                    "response": {"max_output_tokens": 2048}}))
            audio = bytearray()
            words = []
            while True:
                event = receive()
                kind = event.get("type")
                if kind == "response.output_audio.delta":
                    audio.extend(base64.b64decode(event["delta"], validate=True))
                    if len(audio) > 24000 * 2 * MAX_REPLY_SECONDS:
                        raise BridgeError("Reply exceeded 30 seconds; ask a shorter question")
                elif kind == "response.output_audio_transcript.delta":
                    words.append(event.get("delta", ""))
                elif kind == "response.output_audio_transcript.done" and not words:
                    words.append(event.get("transcript", ""))
                elif kind == "response.done":
                    if event.get("response", {}).get("status") != "completed":
                        raise BridgeError("OpenAI did not complete the voice response")
                    break
            reply = "".join(words).strip()
            if not reply:
                raise BridgeError("Voice response had no transcript")
            return reply[:2000], reply_wav(bytes(audio))
        except BridgeError:
            raise
        except (TimeoutError, websocket.WebSocketTimeoutException):
            raise BridgeError("OpenAI voice response timed out after connecting") from None
        except (OSError, websocket.WebSocketException):
            raise BridgeError("OpenAI voice connection dropped after connecting") from None
        except Exception:
            # Never echo exception payloads/headers, which may contain credentials.
            raise BridgeError("OpenAI voice returned an unexpected response") from None
        finally:
            if socket is not None:
                try:
                    socket.close()
                except Exception:
                    pass


class Engine:
    def __init__(self, provider):
        self.provider = provider
        self.lock = threading.Lock()
        self.job = None
        self.busy = False
        self.history = []
        self.session_id = None
        self.user_name = ""
        self.api_error = "" if provider.key else "Add the OpenAI API key in Pod Settings"
        self.wake_phrase = "Hey Sam"

    def submit(self, data, session_id: str, music=False, mode="ask") -> str:
        source = question_audio(data) if mode in ("ask", "transcribe") else data
        with self.lock:
            if self.busy:
                raise BridgeError("Assistant is already processing a question")
            if self.session_id != session_id:
                self.history.clear()
                self.user_name = ""
                self.session_id = session_id
            self.busy = True
            job_id = secrets.token_hex(16)
            self.job = {"id": job_id, "status": "pending", "stage": "Transcribing",
                        "transcript": "", "reply": "", "error": "", "audio": b"",
                        "action": "", "created": time.monotonic()}
        thread = threading.Thread(target=self._run, args=(job_id, source, music, mode), daemon=True)
        try:
            thread.start()
        except RuntimeError:
            with self.lock:
                self.busy = False
                self.job = None
            raise BridgeError("Cannot start assistant worker") from None
        return job_id

    def _run(self, job_id, source, music=False, mode="ask"):
        try:
            if mode == "say":
                audio = self.provider.say(source["reply"])
                with self.lock:
                    self.api_error = ""
                    transcript = source.get("transcript", "")
                    if transcript:
                        self.history.extend([{"role": "user", "text": transcript},
                                             {"role": "assistant", "text": source["reply"]}])
                        self.history = self.history[-12:]
                    self.job.update(status="complete", stage="Ready", transcript=transcript,
                                    reply=source["reply"], audio=audio)
                return
            if mode == "respond":
                transcript = source
            else:
                started = time.monotonic()
                transcript = self.provider.transcribe(source)
                print(f"OpenAI transcription: {time.monotonic() - started:.1f}s", flush=True)
            with self.lock:
                self.api_error = ""
            if music or mode == "transcribe":
                transcript = strip_wake_phrase(transcript, self.wake_phrase)
                with self.lock:
                    self.job.update(transcript=transcript, status="complete", stage="Recognized")
                return
            command = strip_wake_phrase(transcript, self.wake_phrase).strip(" .!?").lower()
            if command in ("go to music", "open music", "switch to music", "go to the music page"):
                with self.lock:
                    self.job.update(transcript=transcript, action="music", status="complete", stage="Opening Music")
                return
            if command in ("go to chat", "open chat", "switch to chat", "go to the chat page"):
                with self.lock:
                    self.job.update(transcript=transcript, action="chat", status="complete", stage="Opening Chat")
                return
            match = re.search(r"\b(?:my name is|call me)\s+([^\W\d_][\w'-]*(?:[ -][^\W\d_][\w'-]*){0,2})",
                              transcript, flags=re.IGNORECASE)
            if match:
                name = re.split(r"\s+(?:and|but|please|can|what|how)\b", match.group(1), flags=re.IGNORECASE)[0]
                self.user_name = name[:60]
            with self.lock:
                self.job.update(transcript=transcript, stage="Thinking")
            started = time.monotonic()
            if self.user_name:
                reply, audio = self.provider.speak(transcript, list(self.history), user_name=self.user_name)
            else:
                reply, audio = self.provider.speak(transcript, list(self.history))
            with self.lock:
                self.history.extend([{"role": "user", "text": transcript},
                                     {"role": "assistant", "text": reply}])
                self.history = self.history[-12:]
                self.job.update(status="complete", stage="Ready", reply=reply, audio=audio)
            print(f"OpenAI reply audio: {time.monotonic() - started:.1f}s", flush=True)
        except BridgeError as error:
            with self.lock:
                self.api_error = str(error)
                self.job.update(status="error", stage="Error", error=str(error))
            print("Assistant job failed; see board status", flush=True)
        except Exception:
            with self.lock:
                self.api_error = "Bridge processing failed"
                self.job.update(status="error", stage="Error", error="Bridge processing failed")
            print("Assistant job failed", flush=True)
        finally:
            with self.lock:
                self.busy = False

    def result(self, job_id):
        with self.lock:
            if not self.job or self.job["id"] != job_id or time.monotonic()-self.job["created"] > 600:
                return None
            result = {key: value for key, value in self.job.items()
                      if key not in ("audio", "created")}
            # Do not expose completion until the worker releases the busy slot.
            if self.busy and result["status"] != "pending":
                result["status"], result["stage"] = "pending", "Finishing"
            result["transcript"] = display_text(result["transcript"])
            result["reply"] = display_text(result["reply"])
            return result

    def audio(self, job_id):
        with self.lock:
            if self.busy or not self.job or self.job["id"] != job_id or self.job["status"] != "complete":
                return None
            if time.monotonic()-self.job["created"] > 600:
                return None
            return self.job["audio"]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *_):
        pass

    def respond(self, code, payload, content_type="application/json"):
        body = json.dumps(payload, ensure_ascii=False).encode() if isinstance(payload, dict) else payload
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; frame-ancestors 'none'; connect-src 'self'")
        self.end_headers()
        self.wfile.write(body)

    def authorized(self):
        expected = "Bearer " + self.server.bridge_token
        supplied = self.headers.get("Authorization", "").encode("utf-8")
        if not hmac.compare_digest(supplied, expected.encode("ascii")):
            self.respond(401, {"error": "Bridge authorization required"})
            return False
        return True

    def do_GET(self):
        if serve(self, self.server, "GET"):
            return
        if not self.authorized():
            return
        if self.path == "/health":
            with self.server.engine.lock:
                error = self.server.engine.api_error
            self.respond(200, {"ready": True, "protocol": 1, "api_error": error})
        elif self.path.startswith("/jobs/"):
            result = self.server.engine.result(self.path.removeprefix("/jobs/"))
            self.respond(200 if result else 404, result or {"error": "Job expired"})
        elif self.path.startswith("/audio/"):
            audio = self.server.engine.audio(self.path.removeprefix("/audio/"))
            if audio:
                self.respond(200, audio, "audio/wav")
            else:
                self.respond(404, {"error": "Audio unavailable"})
        else:
            self.respond(404, {"error": "Unknown endpoint"})

    def do_POST(self):
        if serve(self, self.server, "POST"):
            return
        if not self.authorized():
            return
        if self.path == "/device":
            try:
                size=int(self.headers.get("Content-Length", "0"))
                if not 1 <= size <= 8192 or self.headers.get("Transfer-Encoding"):
                    raise ValueError()
                payload=json.loads(self.rfile.read(size))
                if not isinstance(payload,dict):
                    raise ValueError()
                response=self.server.dashboard.device(payload)
                ducking = getattr(self.server, "ducking", None)
                if ducking:
                    ducking.update(payload.get("conversation") is True)
                phrase=payload["status"].get("wakePhrase","Hey Sam")
                with self.server.engine.lock:
                    self.server.engine.wake_phrase=str(phrase)[:28]
                self.respond(200,response)
            except (ValueError,KeyError,TypeError):
                self.respond(400,{"error":"Invalid device status"})
            return
        if self.path in ("/intent", "/artwork"):
            self.auxiliary_request()
            return
        if self.path == "/music/search":
            self.search_music()
            return
        if self.path not in ("/ask", "/music/voice", "/transcribe", "/respond", "/say"):
            self.respond(404, {"error": "Unknown endpoint"})
            return
        session_id = self.headers.get("X-Companion-Session", "")
        if len(session_id) != 32 or any(c not in "0123456789abcdef" for c in session_id):
            self.respond(400, {"error": "Invalid companion session"})
            return
        if self.path in ("/respond", "/say"):
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 1 <= size <= 8192 or self.headers.get("Transfer-Encoding"):
                    raise ValueError()
                payload = json.loads(self.rfile.read(size))
                if not isinstance(payload, dict):
                    raise ValueError()
                transcript = payload.get("transcript", "")
                reply = payload.get("reply", "")
                if not isinstance(transcript, str) or len(transcript) > 1024:
                    raise ValueError()
                if not isinstance(reply, str) or len(reply) > 350:
                    raise ValueError()
                if not (reply if self.path == "/say" else transcript).strip():
                    raise ValueError()
                job = self.server.engine.submit(
                    {"transcript": transcript, "reply": reply} if self.path == "/say" else transcript,
                    session_id, mode=self.path[1:])
                self.respond(202, {"job_id": job})
            except (ValueError, BridgeError):
                self.respond(400, {"error": "Invalid text request or assistant busy"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0
        if not 44 <= size <= MAX_UPLOAD or self.headers.get("Transfer-Encoding"):
            self.respond(413, {"error": "Invalid recording length"})
            return
        if self.headers.get_content_type() != "audio/wav":
            self.respond(415, {"error": "Expected audio/wav"})
            return
        with self.server.engine.lock:
            busy = self.server.engine.busy
        if busy:
            self.respond(409, {"error": "Assistant busy"})
            return
        data = self.rfile.read(size)
        if len(data) != size:
            self.respond(400, {"error": "Incomplete recording"})
            return
        try:
            job = self.server.engine.submit(data, session_id, self.path == "/music/voice",
                                            "transcribe" if self.path == "/transcribe" else "ask")
        except BridgeError as error:
            self.respond(400, {"error": str(error)})
            return
        self.respond(202, {"job_id": job})

    def auxiliary_request(self):
        is_artwork = self.path == "/artwork"
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 1 <= length <= 6144 or self.headers.get("Transfer-Encoding"):
                raise ValueError()
            payload = json.loads(self.rfile.read(length))
            if not isinstance(payload, dict):
                raise ValueError()
            if is_artwork:
                video_id = payload.get("video_id")
                source = payload.get("source", "")
                if (not isinstance(video_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id)
                        or not isinstance(source, str) or len(source) > 767):
                    raise ValueError()
            else:
                transcript = payload.get("transcript")
                if not isinstance(transcript, str) or not 1 <= len(transcript.strip()) <= 1023:
                    raise ValueError()
        except (ValueError, TypeError):
            self.respond(400, {"error": "Invalid bridge request"})
            return
        if is_artwork:
            try:
                jpeg = artwork(video_id, source)
            except ImportError:
                self.respond(503, {"error": "Pillow is required for artwork conversion"})
            except ValueError:
                self.respond(404, {"error": "Artwork unavailable"})
            else:
                self.respond(200, jpeg, "image/jpeg")
        else:
            try:
                result = self.server.engine.provider.interpret(transcript)
            except BridgeError:
                self.respond(503, {"error": "Intent interpretation unavailable"})
            else:
                self.respond(200, result)

    def search_music(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 1 <= length <= 4096 or self.headers.get("Transfer-Encoding"):
                raise ValueError
            query = json.loads(self.rfile.read(length))["query"]
            if not isinstance(query, str) or not 1 <= len(query.strip()) <= 1023:
                raise ValueError
        except (KeyError, TypeError, ValueError):
            self.respond(400, {"error": "Invalid music search"})
            return
        query = re.sub("^" + re.escape(self.server.engine.wake_phrase) + r"[,.]?\s*", "", query, flags=re.IGNORECASE)
        query = re.sub(r"^(?:please\s+)?(?:search for|play|find)\s+", "", query,
                       flags=re.IGNORECASE).strip(" .!?")
        try:
            from ytmusicapi import YTMusic
            results = YTMusic().search(query, filter="songs", limit=3)
            for song in results:
                video_id = song.get("videoId") or ""
                if re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
                    self.respond(200, {"videoId": video_id})
                    return
            self.respond(404, {"error": "No matching song"})
        except Exception:
            self.respond(503, {"error": "Music search unavailable"})


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(15)
        return connection, address


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9870)
    parser.add_argument("--token-file", type=Path, required=True)
    args = parser.parse_args()
    token = args.token_file.read_text().strip()
    if len(token) != 64 or any(c not in "0123456789abcdef" for c in token):
        raise SystemExit("Invalid bridge token file; rerun Configure-Assistant.ps1")
    try:
        import websocket
        if not callable(getattr(websocket, "create_connection", None)):
            raise ImportError("websocket-client module required")
    except ImportError:
        raise SystemExit("Install websocket-client before starting the bridge")
    key_store = ProtectedKey(args.token_file.parent / "openai-key.dpapi")
    try:
        key = key_store.read()
    except (ValueError,OSError,UnicodeError):
        key = ""
        print("Saved API key unavailable. Add it in Pod Settings.", flush=True)
    key = key or os.getenv("OPENAI_API_KEY", "")
    provider = OpenAIProvider(key)
    provider.cache_dir = args.token_file.parent / "speech-cache"
    server = Server((args.host, args.port), Handler)
    server.bridge_token = token
    server.engine = Engine(provider)
    server.dashboard = Dashboard(key_store)
    server.ducking = Ducking()
    server.ducking.start()
    url = f"http://127.0.0.1:{args.port}/#" + server.dashboard.token
    threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    print(f"Pod Settings: http://127.0.0.1:{args.port}/", flush=True)
    print(f"Desktop Companion bridge listening on TCP {args.port}; voice is AI-generated.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.ducking.close()
        server.server_close()


if __name__ == "__main__":
    main()

