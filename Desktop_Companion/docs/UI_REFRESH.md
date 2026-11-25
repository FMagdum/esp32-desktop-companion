# Four-tab interface and lighting plan

User request: Home, Chat, Music and Settings; three visual choices before
theme-dependent firmware development. Candidate designs: Aurora, Orbit, Studio.
They are interactive design previews, not uploaded firmware or live telemetry.

## Shared screen requirements

- Home: animated face with generous eye/mouth separation, one talk action.
- Chat: scrollable You/Companion exchanges and listening/thinking/speaking state.
  Keep complete text available for reference; do not replace it with one status line.
- Music: artwork, title/artist, progress, transport controls, queue and playlist context.
- Settings: screen brightness, Wi-Fi SSID/IP/signal/connectivity, firmware identity,
  total/used/free SD capacity, device speaker volume, microphone input gain and
  LED settings. Desktop playback volume is separate from device speaker volume.
- Settings should persist user changes in device preferences when implemented.
  Coordinate codec writes with the shared Wire bus; changes must not race touch.
- Wi-Fi credentials/API keys must never be shown in the info page.
- Normal controls should have touch targets about 44px. Use subpages for detailed
  settings instead of crowding everything into one screen.

## Visual choices

Aurora: soft geometry, centered face, friendly bubbles, readable music page.
Orbit: circular face/music presentation, technical accents, conversation timeline.
Studio: warm editorial hierarchy, compact artwork, more space for text and queue.

All show the same features and four tabs, so the choice concerns visual design
and interaction hierarchy, not which features are included.

## LED behavior

Default modes: per-screen colors, fixed custom color, off.
Suggested per-screen defaults: Home cyan/teal, Chat violet, Music artwork palette,
Settings subdued blue. Exact shades follow the selected design and user settings.
Recording/error indication overrides the screen color; global brightness applies.

Music color options:
1. Artwork palette: extract a dominant palette from the current track thumbnail.
2. Frequency bands: color from bass/mid/high energy.
3. Fixed custom color.

Brightness should follow actual measured music intensity with attack/release
smoothing, a user-adjustable ceiling, and dimming when paused. This requires new
PC bridge audio telemetry. The YTMDesktop Companion API does not expose live PCM,
loudness or beat measurements; its player volume field is a setting, not intensity.
A Windows WASAPI loopback stream can measure the selected playback endpoint.
Ordinary endpoint loopback may include other apps/notifications; verify per-app
capture support before promising YTMDesktop-only synchronization.

YTMDesktop state includes thumbnails, queue and playlistId; GET /playlists returns
the user's playlist IDs/titles. Show a resolved playlist title where available.
A playlistId can instead refer to radio/album/single-track contexts, so fall back
to Queue/Up next rather than inventing a playlist title.

References:
https://ytmdesktop.github.io/developer/companion-server/reference/v1/state.html
https://ytmdesktop.github.io/developer/companion-server/reference/v1/playlists.html
https://github.com/ytmdesktop/ytmdesktop/wiki/v2-%E2%80%90-Companion-Server-API-v1
https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording

## Face defect corrected

The old mouth mask at x105,y75,w71,h37 overlapped moving eye edges.
Face.cpp now places idle eye centers at y50 (maximum lower edge y90) and the
mouth mask at y100..126. Happy eye shapes moved up 15px; the smile moved down
15px. The largest smile ends at y156 inside the 160px canvas.
No board pins, reset sequencing or dependency versions changed.
Upload verification is pending; Codex compilation still encounters the previously
identified Windows compiler DLL startup failure.

## Next implementation sequence

1. User selects Aurora, Orbit or Studio.
2. Build four-screen navigation, settings model/persistence and conversation view.
3. Verify the selected interface and face fix on the board.
4. Complete OpenAI integration against these screens.
5. Add artwork palette/playlist resolution, then true music-level telemetry.

Phase 8 currently contains partial, unvalidated draft integration. It must not
be described as a completed assistant. This design pass does not complete that
draft or implement live music-reactive lighting.


## Selected direction implemented â€” 2026-11-19

The supplied navy/cyan landscape reference supersedes the three earlier
concepts. AppUI now owns the four screens in firmware. Read LANDSCAPE_UI_TEST.md
for controls, test status and remaining backend work. BoardPins native sizes
stay fixed; logical 480x320 and rotation live in UIConfig. No fake weather,
battery, calendar or music-intensity values are shown. External Arduino Verify
and hardware confirmation are pending.


## Music refinement implemented — 2026-11-19

User approved song artwork and matching accent/LEDs, larger square icon buttons,
press feedback, a compact top-bar connection icon and animated playback dots.
Read MUSIC_UI_TEST.md. Battery support was explicitly deferred because its
sensing resistor is not fitted. True audio metering and playlist retrieval
remain separate future work.
