# Handoff for the next agent

Start with [root instructions](../AGENTS.md), [source instructions](../src/AGENTS.md),
[architecture](../src/README.md), and the [documentation index](README.md).
This document is a map and a record of constraints, not proof of current
working-tree or release state. Verify the owning source and live state.

## Project identity and scope

- Public repository: <https://github.com/EveOPlus/evnova-remastered>, branch `main`.
- Product name: **EV Nova Community Remaster**. It is unofficial and developed
  with AI tooling; preserve that disclosure and all [credits](../CREDITS.md).
- Foundation: wraitii's C++23/SDL3 `evnova-decomp`, forked at
  `e3dd30407939296bf19e96f94a499cc6bb2cafd1`. Do not claim the inherited engine
  or its research was newly decompiled by this community fork.
- Original gameplay reconstruction coexists with deliberate community controls
  and graphics improvements. Original ship/planet/cockpit designs are retained.
- Publish completed, tested work as regular `major.minor.patch` releases from
  `1.0.0` onward. Old preview installations can migrate; there is no new preview
  stream. Do not equate a regular release label with full original-game parity.

## Local workspace roles

The current Windows development arrangement uses a canonical Git checkout at
`C:\dev\EVNova` and a separate populated build/research workspace at
`%USERPROFILE%\Documents\ChatGPT\EV Nova`. Confirm these still exist before
using them. They may have different Git baselines; the populated workspace
contains ignored assets and substantial historical local changes.

Inspect `git status` in both places. When building in the separate workspace,
copy only reviewed source files to the canonical checkout, verify source
parity, commit there, and pass that full revision to `build-windows.ps1`.
Never run an indiscriminate `git add -A` over the populated research workspace.
Do not reset or delete unrelated user work to obtain a clean status.

The maintainer's playable copy is a public GitHub download, historically under
`%USERPROFILE%\Downloads\evnova-windows-x64\EV Nova Remastered`. Its usual
shortcut is `Play EV Nova Remake.lnk` in the populated workspace. Inspect the
shortcut rather than guessing. Do not replace this copy with a developer build
or point the shortcut back into `build/` or a private `dist/` package. The user
wants the same public updates as everyone else. Leave an active game alone.

## Data, profiles and privacy

Original archives and optional assets are outside Git. Public ZIPs contain
engine files, setup/update helpers and license notices only. See
[data setup](game_data_setup.md), [ARPIA2](arpia2.md) and [narration](narration.md).

Windows support data is under `%APPDATA%\Ambrosia Software\EV Nova`:

| Location | Meaning |
| --- | --- |
| `Pilots/` | Base-game pilot saves |
| `ARPIA2/` | Separate campaign profile and pilots |
| `Game Data/` | Downloaded installation when using the first-run downloader |
| `EV Nova Extra Prefs.ini` | Port settings, including scales and path override |
| `Automatic updates.txt` | Shared updater preference |
| `Narration/` | Profile voice choice, enabled flag and worker state/audio |
| `NarrationRuntime/` | Shared Python/Piper runtime and four installed models |

Use `NovaPaths` for actual resolution and the current campaign. A successful
voice installation does not imply automatic narration is enabled. Check
`Narration/enabled.txt`, selected voice, worker `state.json` and installer logs
before reinstalling. A recent reported narration failure was an Off preference;
re-enabling it required no source patch or replacement executable.

## Community behaviors to preserve

- Hold left mouse to steer; release brakes. Double-click space locks a cruise
  direction at normal full speed. A new hold or keyboard steering takes over.
- Right-button hold opens the contextual radial; release selects or cancels.
  Disabled players retain menu access, subject to normal movement restrictions.
- J once jumps once, flying clear first if necessary without opening the map.
  Double-J follows the route. Jump transitions are faster and dark, with no
  white flash. Automation can use installed afterburners when enabled.
- Double-click/Shift-click a system plots the shortest known route; triple-click
  starts following it. Floating-map chart pan, resize and clicks preserve
  navigation. Its title bar moves it; only X closes it.
- Auto-mine intercepts the asteroid, deploys early, fires in range and collects
  yield. Auto-dock uses normal movement/landing and must not restart on Leave.
- Radial Board approaches, matches target drift and aligns before normal B.
  Manual B explains alignment failure. Original eligibility/loot rules remain.
- Market rows include locally traded and held goods, relative price shading,
  mission destination badges and Auto Dock. Local mission-only stops appear.
- Store and mission/text menus accept wheel scrolling. Bar gambling restores
  Galactic Racing Network wagers, results and payouts using original artwork.
- Save/Open Pilot use file pickers; Settings hosts About Nova. Keep the original
  themed menu artwork and readable larger UI, without the old controls banner.
- Local mission/hail narration has selectable voices, install/repair and
  automatic reading controls. ARPIA2 uses one switch for the campaign/extras.

See [modern_controls.md](modern_controls.md) for exact gestures and boundaries.

## Reverse-engineering evidence

The expected Ghidra API is `http://127.0.0.1:8166`; inspect availability and
program identity before relying on it. An earlier local session had the server
unavailable and used archived decompiles under
`%USERPROFILE%\Games\EV Nova Analysis\reports\decompiled` instead. This is a
fallback location, not a guarantee that those files or a server exist now.

`EV Nova Bible.html`, `docs/reference/`, and `docs/assets/ResForge` are excluded
from public Git and may be absent even though inherited notes refer to them.
Use available source/research and report missing evidence; never invent their
contents. Improve Ghidra when available and supported by evidence.

Run `tools/ref_audit.py` for changed original ports/trackers and inspect
`analysis/ref_audit.txt`. An offline source-only audit cannot validate live
Ghidra names, signatures or full dump coverage. Preserve this limitation in
reports rather than describing it as a full audit.

## Verification already performed and remaining limits

The Windows release that introduced the recent cruise/boarding/map/menu changes
passed Release and Debug builds, affected-file clang-tidy, and 189 focused test
cases with 5,987 assertions. Driver checks covered cruise, route completion,
floating-map interactions, menu scrolling, gambling and narration/model setup.
Boarding approach used real movement integration against stationary and drifting
disabled ships. These are historical checks, not substitutes for a new regression
test after changing those systems.

Public update download and installation were tested in disposable folders,
including preservation of pilot/data sentinels and the older preview updater.
Player executables have the test listener disabled. Earlier Defender scans
reported no threats for those exact binaries; a new hash requires a new check
if reporting that claim. Do not expand security exclusions to ship an update.

Full stock and ARPIA2 storylines, every NPC/mission variant, and cross-platform
feature parity are not verified. Linux/macOS workflow definitions are not proof
that a particular build or notarization succeeded. Apple signing credentials
are a separate requirement. The current Windows release path can use a tested
local build with its exact published revision.

## Finishing a task

1. Reproduce the reported behavior in an isolated profile where possible.
2. Fix the owning subsystem and update relevant tests/research/control docs.
3. Validate at the level the change needs; retain evidence under ignored `analysis/`.
4. Review, synchronize and commit intended files without user assets or secrets.
5. For completed release work, bump the regular version, build that commit,
   package engine-only, upload a draft, verify hashes, then publish as Latest.
6. Verify preview-to-regular migration when touching the updater, and the public
   download with no GitHub authentication. Let the player's normal updater
   deliver it; do not silently install a local build.
7. Report what changed, what was checked, and any material unverified behavior.

Follow the current task's authorization; do not infer that every future source
edit must be published immediately. [automatic_updates.md](automatic_updates.md)
contains the release contract and [tests/README.md](../tests/README.md) the checks.
