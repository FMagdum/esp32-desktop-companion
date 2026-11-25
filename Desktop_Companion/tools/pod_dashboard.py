"""Local dashboard, device command mailbox and Windows-protected API key."""
import ctypes
from ctypes import wintypes
import ipaddress
import json
import os
from pathlib import Path
import secrets
import threading
import time
import urllib.parse

class ProtectedKey:
    def __init__(self, path):
        self.path = Path(path)

    def _crypt(self, data, decrypt=False):
        if os.name != "nt":
            raise ValueError("Saved API keys require Windows data protection")
        class Blob(ctypes.Structure):
            _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_byte))]
        buffer = ctypes.create_string_buffer(data)
        source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_byte)))
        output = Blob()
        function = ctypes.windll.crypt32.CryptUnprotectData if decrypt else ctypes.windll.crypt32.CryptProtectData
        function.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p,
                             ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
        function.restype = wintypes.BOOL
        if not function(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(output)):
            raise ValueError("Windows could not unlock the saved API key")
        try:
            return ctypes.string_at(output.data, output.size)
        finally:
            free = ctypes.windll.kernel32.LocalFree
            free.argtypes = [ctypes.c_void_p]
            free.restype = ctypes.c_void_p
            free(output.data)

    def read(self):
        return self._crypt(self.path.read_bytes(), True).decode() if self.path.exists() else ""

    def save(self, key):
        encrypted = self._crypt(key.encode())
        temporary = self.path.with_suffix(".tmp")
        temporary.write_bytes(encrypted)
        os.replace(temporary, self.path)

class Dashboard:
    def __init__(self, key_store):
        self.key_store = key_store
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.Lock()
        self.telemetry = {}
        self.last_seen = 0
        self.pending = None
        self.result = ""
        self.session = ""
        self.accepted = secrets.randbelow(1 << 30) + 1

    def device(self, payload):
        status = payload.get("status")
        if not isinstance(status, dict) or len(json.dumps(status)) > 7000:
            raise ValueError("Invalid Pod status")
        with self.lock:
            session = payload.get("session", "")
            if session != self.session:
                self.pending = None
                self.session = session
                self.result = "Pod connected"
            self.telemetry = status
            self.last_seen = time.monotonic()
            if self.pending and payload.get("ack") == self.pending["id"]:
                self.result = str(payload.get("result", "Applied"))[:120]
                self.pending = None
            return {"command": self.pending}

    def state(self, engine):
        with engine.lock:
            ai = {"keyConfigured": bool(engine.provider.key), "error": engine.api_error,
                  "busy": engine.busy}
        with self.lock:
            online = time.monotonic() - self.last_seen < 10
            return {"podOnline": online, "pod": self.telemetry, "ai": ai,
                    "pending": bool(self.pending), "result": self.result}

    def command(self, payload):
        action = payload.get("action")
        if action not in ("settings", "enroll", "test", "sleep"):
            raise ValueError("Unknown Pod action")
        if action == "settings":
            settings = payload.get("settings", {})
            if not isinstance(settings,dict):
                raise ValueError("Invalid settings")
            checks = {"brightness": (10,100), "volume": (0,100), "microphoneDb": (-12,12),
                      "recordSeconds": (10,20), "palette": (0,6)}
            for key, (low,high) in checks.items():
                value = settings.get(key)
                if type(value) is not int or not low <= value <= high:
                    raise ValueError("Invalid " + key)
            if type(settings.get("lightMode")) is not bool:
                raise ValueError("Invalid screen theme")
            phrase = settings.get("wakePhrase", "")
            if not isinstance(phrase,str) or not 2 <= len(phrase) <= 28 or not phrase.isascii() or not all(c.isalnum() or c in " '-" for c in phrase):
                raise ValueError("Use a wake phrase of 2–28 English letters, spaces or numbers")
            preferences = {key: settings[key] for key in (*checks, "lightMode", "wakePhrase")}
            for key in ("wakeEachTurn", "followUpPrompt"):
                if key in settings:
                    if type(settings[key]) is not bool:
                        raise ValueError("Invalid voice preference")
                    preferences[key] = settings[key]
            payload = {"action": action, "settings": preferences}
        else:
            payload = {"action": action}
        with self.lock:
            if time.monotonic()-self.last_seen >= 10:
                raise ValueError("Pod is offline")
            if self.pending:
                raise ValueError("Wait for the current Pod action")
            self.accepted += 1
            self.pending = {"id": self.accepted, **payload}
            self.result = "Waiting for Pod"
        return {"accepted": True}

def serve(handler, server, method):
    """Return True when the local dashboard handled the request."""
    path = urllib.parse.urlsplit(handler.path).path
    if path != "/" and not path.startswith("/ui/"):
        return False
    try:
        local = ipaddress.ip_address(handler.client_address[0]).is_loopback
    except ValueError:
        local = False
    host = handler.headers.get("Host", "")
    expected = {f"127.0.0.1:{server.server_port}", f"localhost:{server.server_port}"}
    if not local or host not in expected:
        handler.respond(403, {"error":"Pod Settings is available on this PC"})
        return True
    dashboard = server.dashboard
    if path == "/" and method == "GET":
        html = Path(__file__).with_name("pod_settings.html").read_bytes()
        handler.respond(200, html, "text/html; charset=utf-8")
        return True
    supplied = handler.headers.get("Authorization","")
    if not secrets.compare_digest(supplied.encode("utf-8"), ("Bearer "+dashboard.token).encode("ascii")):
        handler.respond(401, {"error":"Open Pod Settings from the launcher"})
        return True
    try:
        if method == "GET" and path == "/ui/status":
            handler.respond(200, dashboard.state(server.engine))
        elif method == "POST":
            origin = handler.headers.get("Origin")
            if origin and origin not in {"http://"+item for item in expected}:
                raise ValueError("Invalid settings origin")
            size = int(handler.headers.get("Content-Length","0"))
            if not 1 <= size <= 8192 or handler.headers.get("Transfer-Encoding") or handler.headers.get_content_type()!="application/json":
                raise ValueError("Invalid settings request")
            payload = json.loads(handler.rfile.read(size))
            if not isinstance(payload,dict):
                raise ValueError("Invalid settings request")
            if path == "/ui/key":
                key = payload.get("key","")
                if not isinstance(key,str) or not key.startswith("sk-") or not 20 <= len(key) <= 512 or not key.isascii() or any(c.isspace() for c in key):
                    raise ValueError("Enter a valid OpenAI secret API key")
                key = key.strip()
                with server.engine.lock:
                    if server.engine.busy:
                        raise ValueError("Wait for the current AI reply")
                    dashboard.key_store.save(key)
                    server.engine.provider.key = key
                    server.engine.api_error = ""
                handler.respond(200, {"saved":True})
            elif path == "/ui/command":
                handler.respond(202, dashboard.command(payload))
            else:
                handler.respond(404, {"error":"Unknown settings action"})
        else:
            handler.respond(404, {"error":"Unknown settings page"})
    except (ValueError,KeyError,TypeError):
        handler.respond(400, {"error": "Check the settings and try again"})
    except Exception:
        handler.respond(503, {"error":"Settings operation unavailable; check Pod connection"})
    return True

