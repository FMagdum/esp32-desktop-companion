# Music UI update — device test

Your existing Desktop_Companion project is updated. Battery sensing and its UI
remain deferred, as requested. No GPIO, board settings or dependency versions
changed. No extra Arduino library or SD artwork files are needed.

## New behavior

- The left tile shows the current song's artwork. A representative artwork
  color accents the music interface and the LED ring in Per screen mode.
  Fixed-color and Off settings still take priority; recording remains red.
- Previous, Play/Pause and Next are 64 × 64 px squares with icons and labels.
  Press fills/highlights the button immediately; release sends one command
  and leaves a brief highlight. Sliding outside cancels the action.
- The header has a compact music connection icon: green for the last successful
  YTMDesktop state, gray when unavailable. Tap it to read the detailed status.
  This is separate from the Wi-Fi icon. The old bottom status card is removed.
- Three dots after NOW PLAYING animate during playback and dim while paused
  or offline. They show playback activity, not measured sound intensity.

## Verify and upload

Open the existing sketch in Arduino IDE and Verify/Upload with the tested
settings: ESP32S3 Dev Module, core 3.3.12, 16 MB flash, OPI PSRAM, USB CDC enabled,
16M Flash (3MB APP/9.9MB FATFS). Keep Arduino_GFX 1.6.9, NeoPixel 1.15.5,
ArduinoJson 7.4.3 and the existing es8311 library. Use the same microSD.
The previous Landscape_UI_Test.md has the full startup checklist.

1. Start YTMDesktop 2.1.0 with Companion Server and select a song. Open Music.
   Check real title/artist and artwork. Serial should print "Music artwork ready"
   after a successful image load.
2. Try several songs with different artwork colors. The tile/accent and ring
   should follow the current song, without briefly showing an older download
   over a newer song. The ring uses the artwork color only in Per screen mode.
3. Press each control and hold briefly. It should highlight before release,
   send one action on release, then restore its normal appearance. Slide away
   and release: no command should be sent. Test Play changing to Pause and back.
4. Observe the dots while playing, paused and disconnected. Desktop state
   polling remains approximately every 5.5 seconds, so confirmed icons/status
   can take one poll to catch up; press feedback is immediate.
5. Stop Companion Server. The header music icon should turn gray after the
   next failed poll, while Wi-Fi may remain connected. Tap the music icon to
   see its status. Restart the server and check recovery.
6. Check Settings → Lighting with a fixed color and Off. Songs must not
   override those choices. Switch tabs to confirm their usual per-screen colors.
7. Confirm Home, touch/face reactions, Chat recording/playback and Settings
   continue to work. No battery reading or sensing GPIO was added.

## Artwork availability

Downloads run in a separate background task and cache the current song in RAM.
They do not access the SD card or draw from the worker. Missing/unsupported
artwork shows the music icon, with an amber fallback accent.

The installed ESP32 package supplies the JPEG decoder and built-in TLS trust
bundle. HTTPS certificate verification stays enabled and needs valid time
from the existing NTP clock. Public YouTube image requests never carry the
desktop authorization token. Small baseline JPEG thumbnails on YouTube's
image hosts are supported, capped at 128 KiB and 1024 × 1024 source dimensions.
PNG, WebP, progressive JPEG and embedded data URLs currently use the fallback.
Downloads have time/size limits and at most three attempts for a song request.
A failed cover can be retried by changing tracks or reconnecting.

## Checks completed

Normal Arduino CLI compilation was attempted. Windows still refuses to start
the ESP32 GCC executable with Illegal System DLL Relocation. Full Arduino
Verify/linking and real device behavior remain unconfirmed.

Supplemental Espressif Clang parse/type checks passed for the sketch and all
19 C++ modules against installed ESP32 headers. Desktop checks passed for:

- Actual UI rendering bounds, artwork identity/cache, connection icon,
  playback dots, press/release/cancel and existing navigation/touch behavior.
- Actual thumbnail filtering/selection, a real JPEG header, truncation and
  malformed-header cases, crop coordinates and accent extraction.
- Actual artwork worker request/transfer/cache logic with mocked network,
  RTOS and decoder: stale generations, latest-request replacement, lock
  contention, length/time bounds and decoder input/output contract.
- Actual LED behavior: artwork color in Per screen mode, fixed-color/Off
  priority, recording indication and the original 32/255 channel ceiling.

These checks do not run TLS or the ESP32 JPEG decoder on the board. Preview
images use procedural sample artwork and simulated metadata, not a live track.
No OpenAI requests were made; the unfinished AI backend is unchanged.

Main modules: src/music/Artwork.*, ArtworkMath.h and MusicMetadata.h;
src/music/Music.*; src/ui/AppUI.cpp; src/led/LedRing.*.
Previous files are saved in the Codex workspace under work/before-music-update.
