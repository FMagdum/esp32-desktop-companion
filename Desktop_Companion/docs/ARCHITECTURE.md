# Firmware architecture

## Hardware
- Waveshare ESP32-S3 Audio Board; LCD connected through onboard FPC connector
- 3.5-inch IPS ST7796S, 320x480 SPI
- FT6336U capacitive touch, I2C at 0x38
- TCA9555 I2C expander at 0x20; EXIO0 LCD reset, EXIO1 touch reset
- Shared I2C: SDA GPIO11, SCL GPIO10
- SPI LCD pins: CS 3, SCLK 4, MOSI 9, MISO 8, DC 7, BL 5

## Data flow
- `.ino` initializes Wire, expander reset, display, UI and touch in sequence.
- `Touch::read` polls touch and returns raw coordinates.
- `Face::onTouch` triggers a timed happy expression; `Face::update` animates blinking and gaze using a small canvas through `Display::graphics`.
- All hardware pin assignments live only in `BoardPins.h`.

## Future layers (not implemented yet)
- Future face extensions: listening/speaking expressions (idle/blink/happy implemented in Phase 3)
- Future LED extensions: additional effects (idle/blink/happy synchronization implemented in Phase 4)
- `src/audio`: microphone capture and speaker playback
- `src/storage`: microSD file access and assets
- `src/music`: YTMDesktop desktop API bridge over local Wi-Fi
- `src/ai`: cloud assistant integration, credentials from ignored local settings
- `src/alarm`: RTC-backed local alarm state
- `src/network`: Wi-Fi state and reconnect

## Design constraints
- One change per phase; compile and test each module on actual hardware.
- Don't infer previous DevKit pin mapping.
- Avoid `delay()` in future event loops where animation/audio require responsiveness.
- Avoid storing credentials in committed source or Codex cloud tasks.
- Preserve Arduino IDE compilation: recursively compiled `.cpp` sources are inside `src`.


## Phase 4 LED synchronization

The sketch calls LedRing::update with Face::expression() after touch processing.
Face exposes its current state without depending on the LED driver. The LED module
owns its NeoPixel object, pulse timing and color fades; the sketch still coordinates
modules. GPIO38 / seven pixels are in BoardPins.h; byte order and brightness are in
LedConfig.h. Phase 3 was hardware-confirmed; Phase 4 validation is pending.
See ROADMAP.md for the user-approved phase order.


## Phase 5 storage and sound

Storage::begin mounts the separate native SDMMC bus in 1-bit mode; no card data
is modified. Sound::begin configures ES8311 through the existing Wire bus and
starts an I2S playback task. Sound::play sends a bounded queue message; the
sketch triggers it once on a valid new touch press. The playback task exclusively
owns its File object, WAV parsing and I2S writes. Face rendering and LED updates
remain in the main loop. SD removal or bad sound data stops that effect; hot
remount is not implemented. See PHASE5_TEST.md for configuration and tests.

## Phase 6 - desktop music

Music::begin reads the runtime card configuration before the sound task starts.
A separate network task polls YTMDesktop 2.1.0 Companion API v1 and processes a
bounded command queue. The task publishes fixed-size snapshots under a short
critical section; MusicUI renders snapshots on the main loop. All LCD operations
remain serialized on that loop. Face/LED/audio drivers are unchanged.
Credentials are on the physical microSD, never in headers or logs.
See PHASE6_TEST.md for protocol, pairing, limits and the hardware milestone.

## Phase 7 - one audio owner

The Sound task owns both I2S TX/RX and all recording/playback file operations.
Microphone configures the bundled ES7210 library via Wire in setup only.
TX/RX share the existing sample format and clock pins. Recording stores incoming
stereo blocks while TX emits silence; incoming blocks are discarded otherwise.
AudioUI sends bounded commands and renders snapshots on the main loop, using
bands outside the face canvas and music controls. See PHASE7_TEST.md.
