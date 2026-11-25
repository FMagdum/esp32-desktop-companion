# ESP32-S3 Desktop Companion

Desktop Companion is a touchscreen desk pod built around the Waveshare ESP32-S3 Audio Board. It combines an animated face, a voice assistant called Sam, a clock with alarms, and controls for YouTube Music on a Windows PC. This release includes printable covers for the screen and speaker.

## What it does

- **Home and settings** offer an animated face, RGB lighting, a clock, and persistent display, audio, and LED settings. The board's three physical keys open Home, Music, and Chat.
- **Sam** responds to an enrolled wake phrase or a tap. The Windows bridge sends activated recordings to OpenAI for transcription and handles spoken replies. Recognized device commands can change the volume, report the time, or set an alarm.
- **Music** shows track details and artwork and sends play, pause, skip, and search commands to the paired YTMDesktop app. Music plays through the PC.
- **Alarms** can be set on the touchscreen or by voice. One-shot alarms are stored on the device and can ring, snooze, or be dismissed while the pod is running with a synchronized clock.

## Build and setup

1. Follow the [setup guide](docs/SETUP.md) to build and flash the Arduino sketch, prepare the microSD card, and configure the Windows bridge and optional music integration.
2. Download the two STL files and follow the [printed cover instructions](docs/SETUP.md#printed-covers).
3. Use the [GitHub publishing guide](docs/GITHUB_UPLOAD.md) when the project is ready to share.

## Project files

| Folder | Contents |
| --- | --- |
| [Desktop_Companion](Desktop_Companion/) | Arduino sketch, C/C++ modules, microSD assets, Windows bridge, and firmware reference notes |
| [arduino-libraries](arduino-libraries/) | ES8311 driver and LVGL configuration used by the sketch |
| [CAD](CAD/) | `ScreenBackCover.STL` and `SpeakerTopCover.STL` |

The firmware compiled with ESP32 Arduino core 3.3.12. Use the first-run checks in the setup guide to confirm the integrated build on your hardware. [Third-party code notices](docs/THIRD_PARTY.md)
