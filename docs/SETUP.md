# Set up the Desktop Companion

## 1. Connect the hardware

Use the Waveshare ESP32-S3 Audio Board with its factory speaker and microphones, the Waveshare 3.5-inch capacitive touch LCD (ST7796S and FT6336U), a compatible FPC cable, a USB-C data cable, and a FAT32 microSD card. With power disconnected, connect the LCD to the board's FPC port. Check the connector orientation and cable against your hardware revision. Test the board and display on the bench before fitting the covers.

[Waveshare audio board documentation](https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board) · [Waveshare display specifications](https://www.waveshare.com/product/3.5inch-capacitive-touch-lcd.htm)

## 2. Install the Arduino dependencies

In Arduino IDE 2, add Espressif's stable Boards Manager URL under **File → Preferences → Additional Boards Manager URLs**. Install **esp32 by Espressif Systems 3.3.12** in Boards Manager. [Espressif installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Install these versions in Library Manager.

| Library | Version |
| --- | --- |
| GFX Library for Arduino (Arduino_GFX) | 1.6.9 |
| Adafruit NeoPixel | 1.15.5 |
| ArduinoJson | 7.4.3 |
| lvgl | 9.6.0 |

Find your **Sketchbook location** in Arduino IDE Preferences. Copy the [es8311 folder](../arduino-libraries/es8311/) and [lv_conf.h](../arduino-libraries/lv_conf.h) into the sketchbook's `libraries` directory. The config should sit next to the `lvgl` folder. If you already use LVGL on other projects, keep a copy of your existing config before replacing it. The ES7210 driver is already part of the sketch.

## 3. Prepare the microSD card

With the board off, put the microSD card in the PC. Copy the `sounds` and `assistant` folders from the [sketch's data directory](../Desktop_Companion/data/) to the **root of the card**. The shipped files should be at these paths.

```text
/sounds/startup.wav
/sounds/touch.wav
/assistant/intents.json
```

Keep any existing card configuration and recordings you want to retain. To enable Wi-Fi and clock sync, put a private `companion.json` at the card root. For Wi-Fi only, use the format below and replace both sample values. Save it as UTF-8 without a byte-order mark. If pairing music now, the pairing script in step 5 creates the complete file for you.

```json
{
  "ssid": "YOUR_2_4_GHZ_WIFI",
  "password": "YOUR_WIFI_PASSWORD"
}
```

## 4. Verify and flash the sketch

Open [Desktop_Companion.ino](../Desktop_Companion/Desktop_Companion.ino) from its `Desktop_Companion` folder. Select **ESP32S3 Dev Module**, choose the board's USB port, and apply these settings in the **Tools** menu. Leave the remaining options at their defaults.

| Arduino IDE option | Setting |
| --- | --- |
| USB Mode | Hardware CDC and JTAG |
| USB CDC On Boot | Enabled |
| Upload Mode | UART0 / Hardware CDC |
| CPU Frequency | 240 MHz |
| Flash Mode | QIO 120MHz |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

Click **Verify**, then **Upload**. Power the board off before inserting the prepared card. Start the board and open Serial Monitor at **115200 baud**. Look for the Desktop Companion banner, `TCA9555 OK`, LCD initialization, and touchscreen detection. Touch the Home screen and check that the startup and touch sounds play.

If Upload cannot connect, check the USB data cable and selected port. Use the BOOT/RESET procedure in the Waveshare board guide if the board needs to enter download mode manually.

## 5. Pair YouTube Music on Windows

This step is optional. With the microSD card mounted on the PC, run YTMDesktop 2.1.0 and enable **Companion Server** and companion authorization in **Settings → Integrations**. Run the following from the repository root in PowerShell. Enter the card drive, the PC's LAN IPv4 address, and your 2.4 GHz Wi-Fi details. Approve the matching code shown in YTMDesktop within 30 seconds. The script writes `companion.json` to the card. [YTMDesktop 2.1.0 release](https://github.com/ytmdesktop/ytmdesktop/releases/tag/v2.1.0)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Desktop_Companion\tools\Pair-YTMDesktop.ps1
```

If prompted by Windows Firewall, allow the app on the private network (TCP **9863** by default). Both devices must be on the same reachable LAN. After reinserting the card and restarting the board, open Music and check the track display and playback controls.

## 6. Start Sam and Pod Settings on Windows

Install Python **3.10 or later** so `python` is available from PowerShell. Keep the bridge files together. With the microSD card mounted and `companion.json` already on it, run these commands from the repository root.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Desktop_Companion\tools\Configure-Assistant.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\Desktop_Companion\tools\Start-Assistant.ps1
```

The first script writes `assistant.json` to the card and stores the matching bridge token on the PC. The launcher creates a private Python environment on first use and downloads its dependencies. Allow the bridge on the private network (TCP **9870** by default) if prompted. Save your OpenAI API key in the Pod Settings page that opens and leave the launcher running. The key needs API billing and access to the configured transcription and voice models. Windows stores it for your account.

Keep the board and PC on a trusted LAN. The board-to-PC bridge uses local HTTP. Activated voice recordings are sent to the bridge and then to OpenAI. The microSD card retains recent audio files. Keep live card configs, keys, tokens, and recordings private.

## First-run checks

1. Open Chat, choose **Teach phrase**, say “Hey Sam” once, and wait for confirmation. Use **Talk** to verify that a question produces a transcript and spoken reply. Try the enrolled phrase from Home. Re-enroll in a quiet room if detection is unreliable.
2. Once Wi-Fi and the clock have synchronized, schedule a near-term alarm and check that it rings, snoozes, and dismisses. The firmware uses India time by default. The alarm test needs the pod powered and running.
3. If Music or Sam reports a connection problem, check that the PC service is running, the card has the current PC IPv4 address, and both devices can reach one another. Re-run the corresponding provisioning script if the address changes.

See the [Sam voice guide](../Desktop_Companion/docs/SAM_VOICE_UPGRADE.md) for supported phrases, follow-up modes, and troubleshooting.

## Printed covers

Import the STL files into your slicer in millimeters at 100% scale. Check the model dimensions against the table before choosing orientation and supports.

| Part | Approximate imported dimensions (X × Y × Z) |
| --- | --- |
| [ScreenBackCover.STL](../CAD/ScreenBackCover.STL) | 95.45 × 64.01 × 33.20 mm |
| [SpeakerTopCover.STL](../CAD/SpeakerTopCover.STL) | 61.00 × 45.00 × 61.00 mm |

With power disconnected, test-fit both prints on your hardware. Check the LCD ribbon and connector clearance at the screen cover. Check access to the speaker board's ports and controls before securing the speaker cover.
