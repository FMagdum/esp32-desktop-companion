# Phase 6 - YTMDesktop 2.1.0

Status: implemented; external Arduino compilation and live pairing/hardware
testing remain pending. Phase 5 was user-confirmed working on 2026-11-17.

## What it does

The ESP32 connects over 2.4 GHz Wi-Fi to YTMDesktop's Companion Server API v1.
Title, artist and playback state appear above/below the existing face.
PREV, PLAY/PAUSE and NEXT buttons appear near the bottom of the touchscreen.
Music continues playing through your COMPUTER, not the board's speaker.
The board retains its startup/touch sound effects and synchronized LEDs.

## 1. Install the JSON dependency

In Arduino IDE: Sketch > Include Library > Add .ZIP Library, select the supplied
ArduinoJson-7.4.3.zip. Alternatively install ArduinoJson 7.4.3 through Library Manager.
Existing ESP32, Arduino_GFX, NeoPixel and es8311 versions remain unchanged.
The archive was downloaded from Arduino's library index; automatic installation
could not write its temporary directory under this session's folder permissions.

## 2. Prepare YTMDesktop

- Use YTMDesktop 2.1.0 (ytmdesktop/ytmdesktop).
- Open Settings > Integrations and enable Companion Server and companion
  authorization. Keep authorization enabled.
- Start playing a track in YTMDesktop.
- The server uses TCP 9863. Version 2.1.0 listens on all network interfaces.
  The board and PC must share a reachable LAN; avoid guest/client-isolated Wi-Fi.
- Find your PC's active Wi-Fi/Ethernet IPv4 address with ipconfig, for example
  192.168.1.20. Do not use localhost/127.0.0.1 for the board.
- Allow the app's inbound TCP 9863 connection on your trusted/private Windows
  network if Windows Firewall prompts. Do not expose this port to the internet.

## 3. Pair and prepare the microSD card

Power off the board. Insert its microSD card into the PC. Preserve sounds/.

Run Pair-YTMDesktop.ps1 in a normal PowerShell window on the SAME PC as
YTMDesktop. The script is supplied alongside this guide and also in project tools/.
It prompts for:

- the mounted microSD drive root, for example E:\
- your desktop LAN IPv4 address
- the 2.4 GHz Wi-Fi SSID
- the Wi-Fi password (hidden during entry)

The script requests a pairing code, displays it and then waits for the desktop
approval prompt. Compare the codes and approve Desktop Companion ESP32 within
30 seconds. The script validates its token and writes companion.json directly to
the card root. It does not print the password/token or put them into source files.
Re-pairing with this same app ID replaces its previous token.

The board needs this schema (the script fills actual values):
{
  "ssid": "<2.4 GHz Wi-Fi name>",
  "password": "<Wi-Fi password>",
  "host": "<desktop LAN IPv4>",
  "port": 9863,
  "token": "<paired token>"
}

Treat the card as containing credentials. Do not upload companion.json into a
repository or paste its contents into chat. The local Companion API uses HTTP,
so use it only on a trusted LAN. Reserve the desktop address in your router if
it changes frequently. Wi-Fi enterprise/captive-portal provisioning is not included.

## 4. Verify and upload

Eject the card, insert it while the board is off, then power on.
Verify/upload Desktop_Companion.ino using the existing Phase 5 board settings.
Open Serial Monitor at 115200 baud.

Expected new behavior:

- Phase 6 - YTMDesktop startup banner
- Music: Connecting Wi-Fi while connecting
- Screen status changes to YTMDesktop connected, with title and artist
- Three music controls appear below the face
- Existing face, touch, RGB ring and local WAV sounds still operate

Successful state responses are displayed rather than printed repeatedly to Serial.
A missing/invalid card configuration shows Setup /companion.json and keeps other
functions working. Any card edits require a reboot.

## 5. Hardware milestone

1. Confirm the current title/artist and Playing/Paused indication.
2. Tap PLAY/PAUSE once; verify the desktop responds once. Hold the button: it
   must not send repeated commands.
3. Tap NEXT/PREV; verify the desktop changes track or restarts according to the
   player's behavior. The title/status may take up to about 5.5 seconds to refresh.
4. Check the face, LEDs and touch sounds remain responsive while controlling music.
5. Close YTMDesktop: status should become Desktop unavailable and controls dim.
   Restart it: connection should recover automatically.
6. Interrupt/reconnect Wi-Fi: animations continue and music reconnects.
7. Revoke the companion in YTMDesktop: expect Pairing expired - re-pair.
   Re-run the script with the card in the PC, then reboot the board.
8. Report any compile errors, memory problems, screen artifacts or freezes.
9. Confirm this milestone before Phase 7.

## Recovery notes

- Pairing script cannot connect: check the app and Companion Server, default port
  and desktop approval timing. Retry only after inspecting the app; pairing routes
  are rate limited.
- Board says Desktop unavailable: verify desktop IPv4/port, private-network
  firewall rule, same LAN and PC awake.
- Connecting Wi-Fi persists: verify the card's SSID/password and 2.4 GHz network.
- Unsupported music response: verify the exact app is ytmdesktop/ytmdesktop 2.1.0.
- Music rate limited: firmware observes Retry-After, then retries state fetching.
- Source compilation still cannot run inside Codex due to the known compiler
  DLL startup error; use Arduino IDE or the external PowerShell build script.

## Design and limits

- Music::begin reads /companion.json before Sound::begin starts the audio task.
  The network worker never touches the SD card after that read.
- Core WiFi/HTTPClient handle networking; ArduinoJson 7.4.3 parses settings and
  filtered metadata. Worker tasks never draw on the display.
- Snapshots cross a short critical section; the main loop renders MusicUI outside
  the face canvas. JSON queues and thumbnail arrays are discarded by the filter.
- Version 2.1.0 GET /state limit is 1 per 5s: firmware polls every 5.5s.
  POST /command limit is 2 per second: sends are spaced by at least 600ms.
  UI debounce is 500ms with a single-item command queue; extra taps may be dropped.
- HTTP connect/read timeouts are 1.5/2 seconds; failures disable controls until a
  successful poll. Commands are never automatically retried after uncertainty.
- HTTP 401/403 stops requests until re-pair/reboot. HTTP 429 backs off according
  to Retry-After (or 60s fallback).
- Titles are truncated to two lines and artists to one. Built-in ASCII font maps
  unsupported Unicode characters to '?'. Cover art/volume/seeking are not included.
- Play/pause state reflects the next server poll rather than an optimistic toggle.
- No microphone capture, AI calls, RTC or Phase 7 functionality is introduced.

## Verification performed

- Pairing script parsed successfully as PowerShell.
- Mocked pairing tested /metadata -> requestcode -> request -> /state,
  matching app IDs/codes, raw-token Authorization and unsupported API rejection.
- Versioned official source checked for endpoints, server binding and rate limits.
- Arduino CLI compilation attempted: blocked at compiler startup by the known
  Windows DLL relocation error. Firmware source compilation remains unverified.
- Live authentication, LAN reachability and hardware behavior require the above test.

## Sources

https://ytmdesktop.github.io/developer/companion-server/getting-started.html
https://ytmdesktop.github.io/developer/companion-server/reference/v1/state.html
https://ytmdesktop.github.io/developer/companion-server/reference/v1/command.html
https://ytmdesktop.github.io/developer/companion-server/reference/v1/auth-requestcode.html
https://ytmdesktop.github.io/developer/companion-server/reference/v1/auth-request.html
https://github.com/ytmdesktop/ytmdesktop/blob/v2.1.0/src/main/integrations/companion-server/index.ts
https://github.com/ytmdesktop/ytmdesktop/blob/v2.1.0/src/main/integrations/companion-server/api/v1/index.ts

## Token-size correction (2026-11-18)

YTMDesktop 2.1.0 returns 256 random bytes encoded as 512 hexadecimal characters.
The pairing script accepts up to 512 printable characters; firmware storage has
513 bytes including the null terminator. Both previously used a 256-character
limit. Re-run the updated pairing script, then recompile and upload the updated
firmware before using the new card configuration.
Reference: api-shared/auth.ts, createAuthTokenAsync, v2.1.0.
