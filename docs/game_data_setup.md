# Game data setup

Build from source, or extract the whole Windows engine ZIP
from the repository's releases, and run `evnova.exe`. If game data is missing, choose **Download
and Set Up**. Keep the included `tools` folder beside the executable.

Setup downloads Community Edition and ARPIA2 (about 127 MB total) from
[the EV community archive](https://download.escape-velocity.games/). It checks
each archive against the SHA-256 recorded in `tools/game-downloads.json`, then
installs the game resources and original documentation. The original game
executable is used only as a resource container; setup never runs it.

Data goes into `%APPDATA%/Ambrosia Software/EV Nova/Game Data`; downloaded
archives are cached in the sibling `Downloads` folder. Saves and preferences
remain separate. Existing nonempty files, custom plug-ins and pilots are
preserved. Interrupted downloads can be retried. A changed or damaged archive
is rejected; the manifest must be reviewed and updated for an upstream change.

ARPIA2 is installed with its three supplied extras, but enabled separately by
the single **ARPIA2 expansion (all extras)** switch in Extra Prefs. Restart after
changing that setting. See [ARPIA2](arpia2.md) and [credits](../CREDITS.md).

If FFmpeg is already on PATH, setup also creates MPEG companions for ARPIA2's
two QuickTime movies. Otherwise it retains the originals; those movies need
conversion before the SDL player can show them. FFmpeg, narration voices and
optional enhanced sprite packs are not part of this download. See
[narration](narration.md) and [remastered graphics](remastered_graphics.md).

## Existing installation or offline setup

Choose **Locate EV Nova.exe...** to use an existing Community Edition folder.
This works without network access. macOS and Linux currently use this manual
data-location path; the automatic setup helper requires Windows PowerShell 5.1
or newer.

From a source checkout, run:

```powershell
./tools/setup-game-data.ps1
```

The script defaults to the checkout's ignored `EV Nova` folder. Optional
parameters are `-Destination`, `-CacheDirectory`, `-StatusFile` and `-BaseOnly`.
Place the original ZIPs in the cache for offline installation; checksums are
still verified. Rerunning setup repairs missing or empty resources. To replace
a modified nonempty resource, move that specific file aside first.

## Troubleshooting

Keep the engine and its `tools` folder together. The latest status is recorded
in `game-setup-status.txt` in the support folder. Download failures can mean a
network error, insufficient disk space or a changed archive. Retry or locate
an existing installation. The game log contains the reported setup error.

The source repository and public engine ZIP contain no original game data,
ARPIA2 resources, fonts, pilot saves or voice models. Downloaded material keeps
its own terms and original notices; the engine's MIT license does not apply
to it.

## Current validation

Release and debug builds and static checks passed on Windows. Cached archive
installation, original-document retention, reruns, file preservation, checksum
rejection and archive path checks passed under Windows PowerShell 5.1. The
complete first-run setup-to-menu flow passed through the direct test driver
with verified cached archives and an isolated profile; ARPIA2 was installed
and left disabled by default.

On 20 November 2026, Windows Security reported no threats in the ordinary
player executable (SHA-256
`93ded0c433dbcea577172e5ae4da0a93dc57f2e0db034e0f07d6a2acf120dac3`).
The separate local test-driver build had been quarantined as
`Trojan:Win32/Bearfoos.A!ml`. Its owner authorized an exact-file exclusion for
that local test executable so testing could continue. No exclusion was added
for the ordinary player executable. The driver detection remains unresolved;
no Microsoft submission was made. Public packages contain the ordinary
player build, with the test interface disabled.
