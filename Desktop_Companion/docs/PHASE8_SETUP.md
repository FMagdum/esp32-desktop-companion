# Phase 8 — Desktop Companion bridge provisioning

For the current wake behavior, commands and timed conversation test, use [Sam voice upgrade](SAM_VOICE_UPGRADE.md). The original Phase 8 interaction below is retained as a test record.

The device uses its working microphone, speaker, SD card and audio task.
The Windows PC runs a small bridge between the board and OpenAI. The voice
you hear is AI-generated.

## Before starting

- Keep the same microSD card and its existing /sounds, /recordings and
  /companion.json files. The latter provides Wi-Fi settings.
- Have an OpenAI API key with access to the configured transcription and
  Realtime models. API use can incur charges. Obtain the key from your own
  OpenAI account; do not send it through chat or save it on the microSD.
- Connect the PC and board to the same trusted local network. The board-to-PC
  protocol uses plaintext HTTP, carrying the bridge token, question audio and
  conversation text; use it only on a trusted LAN. The OpenAI key stays on the
  PC, where the bridge talks to OpenAI over HTTPS/WSS.
- Python 3.10 or newer must be available as python on the PC. The first bridge
  launch creates a private Python environment under
  %LOCALAPPDATA%\DesktopCompanion\venv and installs websocket-client and ytmusicapi there.
  This needs internet access; no global Python package installation is needed.

## Configure the card once

Insert the same microSD card into the PC. Open PowerShell in this firmware docs folder and run:

    powershell -NoProfile -ExecutionPolicy Bypass -File "..\tools\Configure-Assistant.ps1"

Enter the card's drive letter and this PC's Wi-Fi IPv4 address when prompted.
The script displays available PC IPv4 addresses. It writes assistant.json to
the card root and saves a matching random bridge token under
%LOCALAPPDATA%\DesktopCompanion\assistant-token.txt. The existing music
configuration, audio files and Wi-Fi password are not changed. If an assistant
configuration already exists, the script asks before replacing it.

If the PC's IP address changes, rerun the configuration with the new address.
Keep assistant.json and the PC token file private.

## Start the bridge

With assistant_bridge.py, pod_dashboard.py, pod_settings.html,
Configure-Assistant.ps1 and Start-Assistant.ps1 in
the sketch tools folder, run:

    powershell -NoProfile -ExecutionPolicy Bypass -File "..\tools\Start-Assistant.ps1"

Pod Settings opens automatically in your browser. Add your OpenAI API key
there and press Save API key. Windows encrypts it for this account in
%LOCALAPPDATA%\DesktopCompanion\openai-key.dpapi; you do not need to enter it
again after restarting the PC. A saved key takes priority over OPENAI_API_KEY.
The page does not display the saved secret. Keep the bridge window open.
A successful start says:

    Desktop Companion bridge listening on TCP 9870; voice is AI-generated.

Windows Firewall may ask about incoming Python connections. Allow access on
your private network so the board can reach the bridge. The default bridge port
is 9870; if choosing another port, specify the same -Port to both scripts.

## Verify firmware and try Chat

Verify and upload Desktop_Companion.ino in Arduino IDE with your existing board
settings. Open Serial Monitor at 115200 baud, reinsert the card and restart the
board. This update has not yet been verified on hardware.

1. Stop any old bridge window and start the updated script above. In Chat, tap
   **Teach phrase**, say just “Hey Sam” once in your normal voice, then pause.
   Look for “Wake phrase learned”. The board saves an acoustic template in flash.
   Re-teach in a quiet room if recognition is unreliable.
2. Go to Home and say “Hey Sam”. A short higher beep means recording has
   started. Home shows Listening and Thinking, then Moving to Chat before
   the ready reply opens Chat. Ask your question immediately after the wake.
   The Pod Settings webpage sets the maximum recording length. Recording ends
   early after two seconds of detected silence. Silent captures are discarded.
3. A lower beep means the desktop bridge accepted the completed recording
   for processing. Silence or a rejected upload produces no processing cue.
   Chat shows the transcript and reply and plays the AI voice. Talk provides
   touch activation. Finish ends the current recording early.
4. On Music, say “Hey Sam, next song” or tap Search and say “play [song] by
   [artist]”. The bridge transcribes music requests, and song searches use
   ytmusicapi to resolve a video ID. The paired desktop handles playback.
   Settings disables new voice activations. An existing turn keeps its original
   destination when navigating. Restart and repeat the wake test to check
   enrollment persistence. Board volume controls both beeps.
5. Ask a follow-up that refers to the first question. The bridge retains up to
   six recent exchanges as conversation context in RAM. The screen keeps its
   session history in RAM; scroll upward to inspect older messages and use
   Jump to latest to resume following new messages.
6. Restarting the board starts a new conversation session; the next accepted
   request clears the prior bridge context. Restarting the bridge also clears
   its context. No chat text history is saved to the card.

The assistant is prompted to give one or two short spoken sentences. The board
receives up to 1023 UTF-8 bytes each of the question and answer; if a response
exceeds that limit, the displayed text may be shorter than the spoken response.
The current embedded fonts cover printable ASCII; unsupported characters can
appear as question marks on screen.

Text history is RAM-only. The existing audio workflow retains the latest
recording at /recordings/last.wav, may retain the previous recording at
/recordings/previous.wav, and saves the latest reply at /assistant/reply.wav on
the microSD card. These files persist after restart until replaced or removed.

## Troubleshooting

- **Assistant not configured:** make sure assistant.json is at the card root,
  then restart the board.
- **Bridge unavailable or connection lost:** confirm the bridge window remains
  open, both devices share the LAN, assistant.json has the current PC IPv4
  address, and Windows Firewall allows TCP 9870 on the private network.
- **Bridge token mismatch:** rerun Configure-Assistant.ps1 on the same PC/card
  and restart both. Do not share the token.
- **OpenAI rejected key/model or quota:** check the key, API billing and model
  access in your OpenAI account. The bridge never prints the key.
- **Too quiet/no recognized speech:** test the Talk button, speak closer and
  adjust microphone level while idle. Playback and recording continue to use
  the existing audio route.
- **Audio busy:** wait for recording, playback or the current AI reply to finish.
  The firmware owns SD audio access through an acknowledged lease.

## Design and verification

The KMX415 Waveshare reference informed the voice-oriented conversation
approach. The board runs personal wake-phrase matching on the ES7210 audio
worker and uses the ES8311 for cues and replies. Only an activated recording
is submitted to the bridge. Music controls and default phrases reside in
firmware flash. Recognition accuracy and acoustic echo depend on the room.


The bridge uses OpenAI file transcription followed by Realtime voice output,
with a 16 kHz PCM WAV returned to the existing speaker route. Tests use fake
OpenAI transports and exercise WAV validation, channel selection, Realtime
event shapes, resampling, authorization, job handling, session reset and text
bounds. They make no paid API calls and do not verify real network/board audio.
Arduino IDE Verify/upload and an end-to-end device test remain necessary.

Official references:
- https://github.com/KMX415/waveshare-ai-speaker-assistant
- https://developers.openai.com/api/docs/guides/speech-to-text
- https://developers.openai.com/api/docs/guides/realtime-conversations
