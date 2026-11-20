# Local mission and ship narration

This Windows remake feature reads mission descriptions, offers, mission text
readers, active-mission details, and ship communications using a local Piper
voice. Ship communications read the greeting and each changed response. Escort
management is a control panel, so it does not read the statistics or commands.

Open **Set Prefs > Narration** to choose a voice. Lessac and Ryan use American
English; Alba and Alan use British English. Select a row, then **Preview** to
hear it. **Stop** cancels the preview. **Auto-read** controls automatic mission
and hail reading; previews still work when automatic reading is off. Changes
apply immediately and are saved in the active campaign profile. Closing the
panel stops its preview. Existing profiles keep Lessac as the default.

Narration starts when a supported dialog opens or its selected text changes.
Closing it stops playback; a nested dialog replaces the outer narration and
returns to it on closing. The toolbar reserves space below the dialog:

| Control | Action |
| --- | --- |
| Read again / F6 | Restart the current text |
| Stop / F8 | Stop the current reading |
| Voice: On/Off / F7 | Toggle automatic narration and save the preference |

## Installation

Open **Set Prefs > Narration > Install voices**. The game installs the speech
runtime and all four voices in the background, displays progress, and refreshes
the voice list when ready. Choose a voice and press **Preview**. The button
becomes **Repair voices** once they are installed. If a download fails, retry
from the same button. Closing settings does not cancel the installer.

No separately installed Python is needed. The helper downloads a pinned,
checksum-verified Python runtime and installs `piper-tts==1.8.0`, then fetches
Lessac, Ryan, Alba and Alan with their model cards. It loads each model before
reporting success. Speech runs locally afterward, with no account or API key.

Runtime files live in `%APPDATA%/Ambrosia Software/EV Nova/NarrationRuntime`,
shared by the base game and ARPIA2 and by installed copies on this computer.
Game updates preserve that folder. The previous workspace `.cache` layout is
still supported as a fallback. `tools/setup-narration.ps1` provides the same
installer outside the game; `-Destination` selects an isolated test folder.
`setup-status.txt` and `install-log.txt` inside the runtime folder record progress
and detailed errors. The download is approximately 320 MB.

Piper is a separate GPL-3.0 speech engine; see
[Piper](https://github.com/OHF-Voice/piper1-gpl) and the downloaded voice's
`MODEL_CARD` for its terms. The game engine's license remains in `LICENSE.txt`.

## Integration and testing

`src/game/mission_narration.*` owns dialog lifetimes, the toolbar, and a hidden
helper process. `tools/nova_narrator.py` accepts numbered UTF-8 hex requests on
stdin, with the selected model ID on each request. New requests cancel earlier
speech; empty text stops and EOF exits. The worker loads voices on demand and
keeps one model active. Model changes happen outside the game's rendering
thread. The helper also accepts the older two-field protocol for existing
game builds, using their initial model.
Piper generates sentence audio while the game keeps rendering. Resource text
is expanded by the existing mission/communication code and converted from
MacRoman to UTF-8 at the narrator boundary.

The profile's `Narration` directory holds `enabled.txt`, `voice.txt`, the last
two temporary WAV files, and `state.json`. `voice.txt` stores the selected model
ID; invalid or missing-model selections
fall back to the default voice. The direct driver exposes voice choice and
worker model with `state narration`, plus `narration.replay`, `narration.stop`, and
`narration.mute` controls. Dummy-audio test runs synthesize without playing.

Live verification on 17 November 2026 covered automatic mission reading,
selection replacement, stop, replay, mute/unmute, ship hail reading, and speech
cancellation when dialogs close. Evidence is in
`analysis/driver/narration-results.json`.

The voice selector publishes `narration_settings`, `voice.<model-id>`,
`voice.preview`, `voice.stop`, `voice.enabled` and `ok`. Four distinct synthesized
previews, muted preview, rapid replacement, cancellation, restart persistence,
mission text, ship hails and rendering are checked through the direct driver
under `analysis/voices/`.
