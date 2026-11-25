"""Windows per-session ducking for the Pod bridge."""
import sys
import threading
import time


class Ducking:
    LEASE_SECONDS = 20
    TARGETS = {"msedge.exe", "youtube-music-desktop-app.exe",
               "youtube music desktop app.exe", "youtube music.exe"}

    def __init__(self):
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.stopping = threading.Event()
        self.deadline = 0
        self.thread = None

    def start(self):
        if sys.platform == "win32":
            self.thread = threading.Thread(target=self.run, name="pod-audio-duck", daemon=True)
            self.thread.start()

    def update(self, active):
        with self.lock:
            self.deadline = time.monotonic() + self.LEASE_SECONDS if active else 0
        self.wake.set()

    def close(self):
        self.stopping.set()
        self.wake.set()
        if self.thread:
            self.thread.join(timeout=4)

    def run(self):
        try:
            import comtypes
            from pycaw.pycaw import AudioUtilities
        except ImportError:
            print("Windows audio ducking unavailable. Install pycaw in the bridge environment.", flush=True)
            return
        comtypes.CoInitialize()
        saved = {}
        try:
            while not self.stopping.is_set():
                with self.lock:
                    active = time.monotonic() < self.deadline
                if active:
                    try:
                        self.duck(AudioUtilities.GetAllSessions(), saved)
                    except Exception:
                        pass  # An endpoint can disappear while Windows switches devices.
                else:
                    self.restore(saved)
                self.wake.wait(0.5)
                self.wake.clear()
        finally:
            self.restore(saved)
            comtypes.CoUninitialize()

    @classmethod
    def duck(cls, sessions, saved):
        for session in sessions:
            try:
                process = session.Process
                if process is None or process.name().casefold() not in cls.TARGETS:
                    continue
                identifier = session.InstanceIdentifier
                if not identifier or identifier in saved:
                    continue
                volume = session.SimpleAudioVolume
                original = volume.GetMasterVolume()
                ducked = original * 0.20
                volume.SetMasterVolume(ducked, None)
                saved[identifier] = (volume, original, ducked)
            except Exception:
                continue  # A process or audio session can exit during enumeration.

    @staticmethod
    def restore(saved):
        for volume, original, ducked in saved.values():
            try:
                # Preserve a change the user made in the mixer during the conversation.
                if abs(volume.GetMasterVolume() - ducked) < 0.002:
                    volume.SetMasterVolume(original, None)
            except Exception:
                pass  # The Windows session may have ended.
        saved.clear()

