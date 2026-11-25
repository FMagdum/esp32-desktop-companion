# Phase 7 - ES7210 recording, playback and audio routing

Status: implementation ready for external compilation and hardware testing.
The user requested Phase 7 after confirming a successful Phase 6 build.
Phase 6 live pairing/control behavior is not recorded as confirmed.

## Library approach

The ES7210 library supplied with the Waveshare board is bundled under
src/drivers/es7210. It is the same C driver already installed in your Arduino
libraries folder. Its codec configuration, clock table and public headers are
unchanged. Only the low-level I2C register write function uses WireTransport.cpp.

This avoids calling legacy i2c_master_* functions alongside the existing Wire
driver, and leaves your globally installed library untouched. Do not add another
ES7210 library or replace its files for this test. Original copyright/SPDX and a
NOTICE are retained with the project copy.

Microphone.cpp calls es7210_new_codec, es7210_config_codec and
es7210_config_volume. There is no handwritten microphone register sequence.

## Controls

Two new buttons appear just below the face and above the music controls:

- RECORD (max 5 sec): tap once and speak. The button becomes STOP with elapsed time.
- STOP: tap again to finish early, or allow recording to finish automatically.
- PLAY LAST: play the most recently saved recording through the onboard speaker.

Holding a button does not repeat presses. Buttons have a 500ms debounce.
A short 200ms settling interval is discarded before storing samples.
Speak for at least a second when testing; stopping before samples are stored
keeps the previous clip rather than producing an empty WAV.

The two thin bars above the face show cumulative peak levels for the left and
right captured I2S slots. They reset when a recording starts; red indicates a
peak near clipping. These are diagnostics, not calibrated sound-pressure meters.

## Routing and files

- I2S uses one full-duplex peripheral with the existing MCLK/BCLK/LRCK and new
  microphone input GPIO15. Speaker output remains GPIO16.
- MCLK=12, BCLK=13, LRCK=14, sample rate=16000, 16-bit stereo, MCLK ratio=256.
- The ES7210 is at I2C address 0x40; ES8311 remains the speaker DAC.
- No microphone signal is fed directly to the speaker.
- During recording, TX sends zeros and local touch/startup chimes are suppressed.
  The amplifier remains powered; this is digital silence, not hardware shutdown.
- Incoming microphone blocks outside a recording are drained and discarded.
  No continuous recording file is created.
- Playback of the saved recording also suppresses touch chimes.
- Recording and playback/file operations are owned by the same audio task.
  Codec configuration happens only in setup before touch polling starts.
- Desktop music still plays on the computer. It is not automatically paused;
  pause it yourself if nearby music would interfere with a voice test.
- No speech recognition, cloud requests, AI responses or echo cancellation are added.

Recordings on the SAME microSD card:

    companion.json
    sounds/startup.wav
    sounds/touch.wav
    recordings/last.wav
    recordings/previous.wav
    recordings/pending.wav  (temporary, only while saving)

No changes to sounds/ or companion.json are required.

Files are stereo PCM16 WAV at 16000 Hz. A full five-second recording contains
320000 bytes of PCM and is normally 320044 bytes including its WAV header.
You can copy last.wav to the PC and listen to it with an ordinary media player.

A new clip is written to pending.wav, then finalized. The old last.wav moves to
previous.wav before the new file is renamed last.wav. One older previous.wav is
replaced on each successful rollover. A failed SD write does not replace last.wav.
If a rename/recovery fails, the Serial log identifies the backup location.
Do not remove power/card during a recording or save; interrupted pending files
are not promoted automatically. Do not store unrelated files at these reserved
recording paths.

## Arduino IDE test

1. Retain ESP32 core 3.3.12, Arduino_GFX 1.6.9, NeoPixel 1.15.5 and ArduinoJson
   7.4.3, plus the existing es8311 library. No new global library install is needed.
2. Keep your current working board settings: ESP32S3 Dev Module, 16MB Flash,
   OPI PSRAM, USB CDC enabled, 16M Flash (3MB APP/9.9MB FATFS), non-SR.
3. Verify and upload Desktop_Companion.ino. Open Serial Monitor at 115200.
4. Expect the Phase 7 banner and:
       Microphone ready: ES7210 library, stereo PCM16 at 16000Hz
       Sound ready: ES8311, 16000Hz PCM16
   Existing display/touch/RGB/SD/music initialization also remains.
5. Check the startup chime. Tap RECORD and speak clearly at normal volume:
       Audio: Recording (speaker silent)
6. After five seconds of captured samples, expect:
       Audio capture: 5000 ms, peak L=<value> R=<value>
       Audio: Recording saved
7. Tap PLAY LAST:
       Audio: Playing last recording
       Audio: Playback finished
   Hear your voice. Final DMA output can finish shortly after the log.
8. Record again and tap STOP after about two seconds. Play it back; verify it
   replaces last.wav and the older clip remains as previous.wav.
9. While recording/playback runs, check face/LED responsiveness, touch input,
   and music controls. Touch chimes should be absent during capture/replay.
10. Power off, move the card to the PC and check last.wav in a media player.
    Verify 16kHz, 16-bit stereo and the expected duration.
11. Reinsert while powered off. Reboot and verify PLAY LAST still works.
12. Confirm this milestone before Phase 8.

## Troubleshooting

- MIC UNAVAILABLE / ES7210 initialization error: send the Serial log. Speaker
  playback remains available if its initialization succeeds.
- Both peaks zero or playback silent: send the peak numbers and Serial output.
  We need to verify microphone slot mapping/gain and audio clocks on the board.
- One channel is silent: report which peak; stereo is retained so the two input
  slots can be inspected independently.
- Red peak bars/distorted voice: lower MIC_GAIN_CODE in SoundConfig.h. Default
  code 8 is 24dB according to the library gain enum. Make small changes.
- Full-duplex I2S failure: firmware attempts the previously working TX-only mode;
  recording is unavailable, but existing speaker effects can continue.
- Recording failed: check card write protection/free space and power stability.
  Existing last.wav is preserved on ordinary open/write errors.
- No card: audio/recording is unavailable; display and LEDs continue.

## Source changes and validation

- Added ES7210 library copy/adapter, Microphone, PcmWav and AudioUI.
- Extended Sound with full-duplex I2S, recording commands, WAV finalization,
  bounded file rollover, snapshots and speaker-only initialization fallback.
- Main sketch coordinates the new controls. MusicUI reserves space for AudioUI.
- Pin map only adds microphone DIN/address; existing assignments are unchanged.
- Face, LED, LCD, touch, reset and music network driver implementations unchanged.
- Verified the ES7210 source differs only in its I2C transport; both headers match
  the installed library byte for byte. Checked source string/comment integrity.
- Arduino CLI build attempted: the known Codex-only Illegal System DLL Relocation
  error stops compiler startup. No Phase 7 compilation or hardware success claimed.

Waveshare microphone pin reference:
https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board
Library source: the installed Waveshare es7210 C driver, copyright Espressif 2022,
Apache-2.0. See src/drivers/es7210/NOTICE.txt.
