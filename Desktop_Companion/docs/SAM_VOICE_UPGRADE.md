# Sam voice upgrade

## Update the Pod and PC

1. Copy the sketch's `data/assistant/intents.json` to `/assistant/intents.json` on the microSD card. Create the assistant directory if needed. Keep the existing Wi-Fi, paired music, sounds, recordings and assistant configuration files. Reboot after editing the intent file.
2. Keep all files in `tools` together, including `openai_network.py`. Stop the old PC bridge and run `tools/Start-Assistant.ps1` in Windows PowerShell. It uses Python 3.10+ and installs websocket-client, ytmusicapi, pycaw, and Pillow into its private virtual environment. Use the logged-in Windows account that plays the music.
3. Keep the ESP32 and Windows PC on the same trusted LAN. Allow the existing private-network TCP 9870 bridge port. Open Pod Settings from the launcher and use the existing OpenAI key or add one there. The key is saved under Windows DPAPI. The SD card only needs its existing LAN bridge token in `assistant.json`. See [Phase 8 setup](PHASE8_SETUP.md) for initial provisioning and YTMDesktop pairing.
4. In Arduino IDE, verify and upload `Desktop_Companion.ino` as ESP32S3 Dev Module with the original USB CDC, 16 MB flash, 3 MB APP/9.9 MB FATFS and OPI PSRAM settings. Use ESP32 3.3.12, Arduino_GFX 1.6.9, ArduinoJson 7.4.3, NeoPixel 1.15.5, Waveshare es8311 and LVGL 9.6.0. The existing panel and touch driver stay in place. LVGL renders the conversation state in the header.

## Voice and local commands

Say your enrolled phrase and question in one breath, such as “Hey Sam, what time is it?” The screen switches to Listening while the Pod captures audio. An SD recording includes the wake phrase, which the bridge removes from the transcript. You can pause for about one second after the wake phrase. The Music page also supports wake plus command and the Search button. Touch Talk still starts a single turn in standby. Opening Settings ends the hands-free session.

On Home, the top bar shows Standby, Listening, Thinking and Replying. Sam shows the recognized question below its face for a three-second review before submitting it to ChatGPT or executing a local command. Tap Cancel during review to return to standby and restore music volume. After a command starts, Cancel can stop the reply but an already applied action remains in effect. During speech, the question is replaced by short captions paced by the audio playback position. This pacing is approximate.

In Pod Settings on Windows, choose **Quick follow-up** or **Wake each turn**, then select **Apply to Pod**. Quick follow-up listens for the start of a question for eight seconds after each reply and then returns to standby. **Ask if I need anything else** adds one spoken prompt and a four-second response window after playback. The prompt is off by default and only applies to Quick follow-up. In response to that prompt, “no”, “no thanks”, “no thank you” or “that's all” return the Pod to standby as soon as the words are recognized. “Stop listening” and “go to standby” also work during a conversation. Wake each turn restores desktop volume after every reply and retains recent context for up to two minutes between wakes.

The Pod checks for sustained speech-like audio before starting a hands-free follow-up. It waits roughly 0.6 seconds of silence after short speech, one second after medium speech and 1.6 seconds after longer speech. The local check rejects many taps but nearby voices can still qualify as speech during the short window. Use Wake each turn when other people are talking near the Pod.

The bridge transcribes captured speech using `COMPANION_TRANSCRIBE_MODEL` (existing default `gpt-transcribe`). The Pod compares the transcript to SD rules and runs device commands. Unmatched questions use the existing OpenAI Realtime conversation with several recent exchanges in RAM. The existing `COMPANION_VOICE_MODEL` default is `gpt-realtime-2.1-mini` with the Marin voice. The key needs access to both models and API billing. The flow has no search or live news/weather tools. Song lookup still uses ytmusicapi and the existing YTMDesktop integration.

| Try saying | What happens |
| --- | --- |
| What time is it? What is the date? | Reads the Pod clock in the configured firmware timezone (India by default), after NTP sync. |
| Set an alarm in five minutes. Set an alarm for 7:30 AM. | Schedules a one-shot alarm. Up to eight persist in flash. An elapsed alarm rings when the Pod is running and its clock is synced. Deep sleep cannot wake for these alarms. |
| Alarm status. Cancel all alarms. | Reads the next scheduled alarm or clears the schedule. |
| What's the volume? Set volume to forty percent. Volume up. Mute. | Reads or changes the Pod speaker volume. |
| Pause music. Next song. What's playing? Play September by Earth, Wind & Fire. | Sends the corresponding action to the paired desktop. |
| Device status. | Reads the current speaker volume and desktop music connection. |

Each SD rule lists recognized `phrases` and a set of short `responses`. Time/date understand `{value}`, volume understands `{volume}`, and status understands `{volume}` and `{music}`. Each response can be up to 160 bytes before substitution. Sam selects one response for a turn, then uses that same wording for the displayed reply and speech request. The PC keeps up to 256 same-voice WAVs in its private `speech-cache` directory. The exact text, voice, model and audio format determine the cache key. The first use of a phrase can take longer while speech is generated. Speech recognition still needs the PC bridge and OpenAI even for local commands. A scheduled alarm has a Pod tone when the service is unavailable.

## Verify on hardware

- Restart the updated Windows bridge and reload Pod Settings. Open Serial Monitor at 115200 baud after uploading. Look for `Desktop Companion | Sam Home captions`, `Assistant: loaded SD intents` and the 480×320 UI-ready line.
- With Edge or YTMDesktop playing, say “Hey Sam, what time is it?” in one breath. Verify that the first word survives, the display changes to Listening promptly, and desktop audio ducks.
- Ask “What's the volume?” within eight seconds of the end of Sam's reply. After the next reply, stay quiet and verify the display shows Standby and music restores. A light desk tap during the window should not start a new turn or restart the timer.
- Enable the optional prompt. Wait for it and say “No thanks.” Verify that Sam returns to standby without another spoken reply. Repeat and stay quiet to check the four-second deadline after playback.
- Switch to Wake each turn. After a reply, speak without the wake phrase and verify the Pod stays in standby. Wake it again within two minutes to ask a context-dependent follow-up.
- If the Pod disappears while desktop audio is ducked, the bridge restores its mixer sessions when the telemetry lease expires. Edge ducking applies to the browser's Windows audio session, including YouTube Music.

## Slow transcription or missing spoken replies

The PC bridge logs OpenAI transcription and reply times separately. The Pod also takes time to finish recording and shows the Home transcript for its three-second review. The bridge prefers IPv4 for OpenAI connections and retains IPv6 as a fallback. If a Realtime connection fails before a model request is sent, it retries the connection once and can use the HTTPS audio model with the Marin voice. The HTTPS audio model can be overridden with COMPANION_HTTPS_VOICE_MODEL. Authentication and rate-limit errors are reported directly. After updating the PC files, stop the old launcher with Ctrl+C and run Start-Assistant.ps1 again. The saved API key and ESP32 firmware can stay in place.
