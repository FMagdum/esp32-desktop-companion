# Phase 5 - microSD and sound test

## Prepare the card and speaker

1. Use an existing FAT32 microSD card with room for the test assets. Back up any
   card data before changing its filesystem; the firmware never formats it.
2. Copy the sounds folder from the supplied Phase5_SD_Card.zip to the card root:
   /sounds/startup.wav and /sounds/touch.wav.
   The same files are in this project's data/sounds directory.
   Do not copy a containing data or Phase5_SD_Card folder instead.
3. Power the board off, insert the card in its TF slot and connect the board's
   speaker to the designated speaker connector. Power on again.
   Live card insertion/removal is not supported in this phase.
4. Verify and upload Desktop_Companion.ino using the README board settings.
   This phase uses the existing Waveshare es8311 library and core ESP_I2S;
   do not install or change the ESP32-audioI2S library for this test.

## Expected Serial output (115200 baud)

Existing display/touch/RGB initialization messages should still appear.
Then look for:

    Desktop Companion | Phase 5 - microSD + Sound
    microSD mounted: <capacity> MB
    Sound ready: ES8311, 16000Hz PCM16
    Sound playing: /sounds/startup.wav
    Sound stopped

Task messages may interleave with face/LED messages.
"Sound stopped" means streaming has ended; the I2S DMA tail may finish shortly
afterward. Initialization messages alone do not prove audible hardware output.

## Verify on hardware

- Hear a short three-note startup chime.
- Touch the screen: hear a two-note effect while the happy face and green LEDs
  appear. Hold a finger down: sound must not retrigger continuously.
- Release for at least 1.5 seconds: face and ring return to idle.
- Tap again: sound plays again. Rapid presses may interrupt the previous effect;
  there should be no delayed queue of old sounds.
- Check blinking, gaze movement and LED pulse remain responsive during playback.
- Leave running for several minutes; watch for resets, I2C errors or audio dropouts.
- Power off, remove the card, reboot: expect a microSD mount error, with face,
  touch and LEDs continuing. Power off again before replacing the card.
- Optionally rename touch.wav while the card is in your PC, reinsert and reboot:
  a new press should log Sound missing: /sounds/touch.wav, without a reset.
- Confirm these results before Phase 6.

## Sound asset format

Supported: RIFF/WAVE, uncompressed PCM (format 1), 16000 Hz, signed 16-bit,
mono or stereo, maximum 10 seconds per effect. Metadata chunks and even-byte
padding are handled. MP3, compressed/float WAV and other sample rates are rejected.
File RIFF/chunk lengths, block alignment and byte rate are checked before playback.
The included files are original synthesized chimes; WAV format and ZIP integrity
were checked locally. Arduino does not automatically copy data/ onto microSD.

## Implementation and hardware references

Waveshare's board pin tables:
https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board
Schematic:
https://files.waveshare.com/wiki/ESP32-S3-AUDIO-Board/ESP32-S3-AUDIO-Board_1.1.pdf

Native 1-bit SDMMC: CLK=40, CMD=42, D0=41; EXIO3/D3 held high.
Speaker: MCLK=12, BCLK=13, LRCK=14, DOUT=16, ES8311 at 0x18.
EXIO8 (TCA9555 port 1 bit 0) controls the speaker amplifier.
Only the required expander bits are modified. USB mux bits remain untouched.

The installed es8311 driver uses the existing Wire bus. Codec and amplifier
configuration happen in setup before touch polling; the playback task does not
access Wire. This avoids concurrent multitransaction codec/touch I2C operations.
I2S uses 256x MCLK and stereo 16-bit slots at 16 kHz. Mono samples are duplicated
to both slots. One task owns SD file reads and blocking I2S writes, with an 8 KB
stack and small block buffers. No full audio file is allocated in RAM.
Idle blocks are silence so clocks stay active without repeating old samples.

Volume is in src/config/SoundConfig.h (CODEC_VOLUME and PCM_GAIN_PERCENT).
Initial levels are modest; change gradually if needed after confirming playback.
If silent, report the full initialization/playing logs, speaker connection and
whether either chime is audible. Do not alter pins based on another ESP32 board.

## Validation status

Phase 5 compilation attempted in Codex: blocked at compiler startup by the known
Windows DLL relocation error. No claim of source compilation or hardware success.
The supplied test WAVs and ZIP archive passed local format/integrity checks.
