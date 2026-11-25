# Landscape UI â€” upload and test

The firmware now follows your reference at **480 Ã— 320**: dark navy surfaces,
a cyan robot outline, white animated eyes and a right-side navigation rail.
Open the sketch from its `Desktop_Companion` folder. For the current firmware use the [setup guide](../../docs/SETUP.md).

## Upload

1. Open Desktop_Companion.ino in Arduino IDE.
2. Keep the tested settings: **ESP32S3 Dev Module**, ESP32 **3.3.12**, flash
   **16 MB**, **OPI PSRAM**, USB CDC on Boot **Enabled**, partition
   **16M Flash (3MB APP/9.9MB FATFS)**, non-SR.
3. Keep Arduino_GFX 1.6.9, Adafruit NeoPixel 1.15.5, ArduinoJson 7.4.3 and the
   installed Waveshare es8311 library. No new library is needed.
4. Verify, then upload to the same board/COM port. Use the same microSD with
   /sounds, /recordings and your private /companion.json. No formatting or
   replacement of existing configuration is needed.
5. Open Serial Monitor at **115200 baud**. Expected messages include:

    Desktop Companion | Landscape UI refresh
    TCA9555 OK
    LCD initialized / face animation ready
    RGB ring initialized: 7 LEDs on GPIO38
    Touchscreen ready! FT6336U at 0x38
    microSD mounted: ... MB
    Microphone ready: ES7210 library, stereo PCM16 at 16000Hz
    Sound ready: ES8311, 16000Hz PCM16
    UI ready: 480x320 landscape / Home Music Chat Settings

Optional hardware/configuration failures are reported separately. The message
"Assistant: /assistant.json not configured; local features continue" is
expected while the cloud bridge is unfinished.

## Device checks

1. **Orientation:** hold the device in landscape. Tap all four rail tiles,
   especially Home at the top and Settings at the bottom. Serial reports
   UI: Home / Music / Chat / Settings. Report any rotated/mirrored targets.
2. **Face:** touch and hold Home. It smiles, then returns to idle about
   1.5 seconds after the last touch. Watch blinking and moving eyes; the mouth
   must not cut them. Other screens must never receive face drawing.
3. **Music:** with YTMDesktop Companion Server running, check real title/artist
   and Prev / Play-Pause / Next. Audio plays on the desktop. Without pairing,
   the screen shows its offline/setup state.
4. **Chat/local audio:** Record, speak for up to five seconds, then Play last.
   Stop ends capture early. The speaker stays silent during recording.
   Navigate away and back during capture; state should remain correct.
   Send requires a configured assistant and an idle saved recording.
5. **Settings:** check every category. Test both slider ends. Brightness has
   a 10% minimum. Start with a low speaker level. Mic adjustment uses the
   ES7210 library's digital gain, -12 to +12 dB; tested 24 dB analog gain stays.
   Adjust mic level while idle. Wi-Fi displays SSID/IP/RSSI, never credentials.
6. **Persistence:** change brightness, speaker/mic levels and LED color.
   Release, wait at least 1.2 seconds for "Settings saved", then restart.
   All four settings should return.
7. **LED:** Per screen uses cyan Home, artwork-colored Music (amber fallback), violet Chat, blue Settings.
   Home's happy mood turns green; recording overrides with red. Fixed colors
   apply across screens. Off stays off during recording. The original 32/255
   channel limit remains. Music breathing is ambient, not track metering.
8. **Storage:** cached total/used/free filesystem space appears in Settings.
   Refresh while audio/AI is idle. Busy audio can reject refresh; files must
   remain intact. Storage reads use the audio worker's acknowledged SD lease.

## Clock and reference-image cards

Time uses NTP after Wi-Fi connects, in India time (UTC+05:30). Until sync,
the header shows --:--. Change TIMEZONE in src/config/UIConfig.h if needed.
This does not implement Phase 9 RTC or alarms.

Home cards show real network and audio/SD readiness. Battery measurement,
weather and calendar connections do not exist yet, so the image's 92%,
weather and meeting are not copied into the live firmware.

## Conversation and remaining work

Chat keeps six recent exchanges in RAM. Swipe vertically or tap the arrows.
Restart clears history. It uses the existing assistant snapshot limits:
255 transcript and 511 reply bytes per turn. Fonts cover printable ASCII;
smart punctuation is normalized and unsupported characters display as ?.

The Chat screen is connected to the existing Phase 8 draft. Its cloud bridge
remains unfinished and has not been validated end to end. Preview PNGs use
simulated data. No OpenAI requests were made for this refresh.
Album-based LED color is now implemented. Measured track intensity and
playlist retrieval remain backend work; the UI does not invent these values.

## Verification completed

- Normal Arduino CLI build attempted with existing settings. It still stops
  before compilation at Windows "Illegal System DLL Relocation". Use Arduino
  IDE Verify or the existing external Build-DesktopCompanion.ps1 script.
- Supplemental Espressif Clang parse/type checks passed for the sketch and
  all 18 C++ modules against installed ESP32 3.3.12 headers.
- Desktop mocks execute actual UI/Face/Touch code: all 153,600 raw touch
  coordinates, tabs, hidden face, happy timeout, controls, swipes, sliders,
  storage lease and frame bounds.
- Actual settings/LED code passes desktop checks for limits, save debounce,
  restore, busy/failure guards, palettes, recording/off priority and ceiling.
- Rendered preview frames were inspected. These checks do not establish
  Arduino GCC code generation, linking, memory usage or hardware behavior.
  External Verify/upload and your device confirmation remain necessary.

## Changed modules

AppUI owns all screens/gestures; DeviceSettings owns validated NVS preferences.
UIConfig holds logical dimensions, palette and timezone. Display/Touch/Face
implement landscape, PWM brightness and the robot. LedRing adds screen colors.
Sound/Microphone expose runtime ES8311 volume and ES7210 library digital gain.
Storage caches filesystem usage. An out-of-scope variable reference in the
existing AI storage-lease helper was corrected.

GPIO definitions, expander reset flow and dependency versions are preserved.
The main sketch stays thin. Legacy portrait MusicUI/AudioUI files remain
unused. Previous sources are saved in the Codex workspace under
work/before-landscape-ui.
