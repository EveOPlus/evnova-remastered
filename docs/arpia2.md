# ARPIA2 campaign

The local remake can load the complete installed ARPIA2 resource pack. In
Set Prefs > Extra Prefs, **ARPIA2 expansion (all extras)** selects the seven
core archives plus Variants, Secondary RocketL and Secondary Stingray. Restart
after changing it. All three optional archives follow the core archives so
their overrides take effect.

The switch is stored in `ARPIA2 enabled.txt` in the base support folder.
ARPIA2 uses its own `ARPIA2/Pilots` folder and preferences; the ordinary
campaign keeps its original files. The first ARPIA2 launch copies the display
and narration settings, then offers a new pilot. Save/Open Pilot start in the
currently active campaign's Pilots folder even if a switch is pending for next
launch. Manual saves can also be written to a folder chosen in the file dialog.

## Install local data

The Windows first-run **Download and Set Up** button downloads ARPIA2 alongside
Community Edition. See [automatic data setup](game_data_setup.md).

Run `tools/setup-arpia2.ps1` with the downloaded ARPIA2 folder. The default is
`%USERPROFILE%\Downloads\ARPIA2`. It copies the resources and documentation
into the ignored `EV Nova/Campaigns/ARPIA2` folder, preserves the original
movies, and creates MPEG companions for the SDL player. FFmpeg must be
installed. `tools/package-windows.ps1` includes that folder in the local
playable package.

This is a local import of the user's supplied assets. The bundled ReadMe and
its original credits remain with them; this project does not grant rights to
redistribute those assets.

## Contents and compatibility

The pack adds three story branches, officers and boarding crew, ships,
outfits, weapons, locations, gas mining and spaceball content. Core resource
differences include 208 new mission definitions, 72 ship definitions, 79
outfits, 28 weapons, 76 system definitions and 53 stellars. Definitions include
story variants and internal placeholders, so these counts are not counts of
independently playable locations or purchasable ships.

Its opening Pirate Hunt mission can appear in a bar once the pilot reaches
combat rating Average (100), before committing to another storyline.

Compatibility additions include non-one-based BRGR entry tables (needed by
the two secondary-weapon extras), four-plane PICT images, and movie playback
from mission offers, briefings/debriefings and landing descriptions. Both
`gasminer.mov` and `credits.mov` have prepared MPEG companions. Movie playback
respects the existing movie preference and can be skipped with Escape or a
click. Mission narration starts after the clip.

## Validation

The opt-in test `evnova_tests.exe "[arpia]"` loads the complete pack, verifies
the secondary-weapon overrides, decodes the expansion splash and all video
frames of both clips, and exercises the real movie modal's Escape path.
The stock-data suite must run in a separate process, since the resource
registry is process-wide. Driver checks cover switching, separate pilots,
basic flight, docking and saving. These checks do not establish completion
of every branch; a full campaign playthrough remains unverified.
