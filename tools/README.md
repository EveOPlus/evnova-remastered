# Tool catalog

Run tools from the repository root unless their help states otherwise. Read
[AGENTS.md](AGENTS.md), then the linked guide before a mutating operation.

| Purpose | Tool | Guide / limits |
| --- | --- | --- |
| Windows build | `build-windows.ps1` | [Build guide](../docs/local_windows_build.md); Release, Debug, separate `-TestDriver` |
| Public Windows ZIP | `package_release.py` | [Releases](../docs/automatic_updates.md); explicit engine-only allowlist |
| Private local playable package | `package-windows.ps1` | Copies local data and changes a local shortcut; never upload this directory |
| Windows updater | `update-game.ps1` | `-Mode Check` or `Apply`; use `-NoLaunch` in disposable tests |
| Game-data download | `setup-game-data.ps1`, `game-downloads.json` | [Setup](../docs/game_data_setup.md); pinned archive checksums |
| ARPIA2 local preparation | `setup-arpia2.ps1` | [Campaign](../docs/arpia2.md); preserve original notices |
| Voice installation | `setup-narration.ps1`, `narration-downloads.json` | [Narration](../docs/narration.md); `-Destination` isolates tests |
| Speech worker | `nova_narrator.py` | Numbered stdin requests, local Piper models, cancellable sentence playback |
| Enhanced sprite release | `package_graphics.py` | Validates and packages all frames, provenance and original-art credits |
| Enhanced sprite preparation | `build_sprite_pack.py` | [Graphics](../docs/remastered_graphics.md); source frames and models stay local; the generated pack is a companion release asset |
| Direct test interface | `nova_driver.py` | [Driver](../docs/windows_test_driver.md); input, UI, state, screenshots, isolated profiles |
| Scenario execution | `scenario_runner.py`, `check_modern_controls.py` | [Scenarios](../docs/scenario_testing_plan.md), [probe](../docs/probe_harness.md) |
| Port audit | `ref_audit.py` | Root instructions; `--gen` updates marked tracker rows |
| Ghidra access | `ghidra_api` | [API reference](../docs/ghidra_api.md); localhost server must be available |
| Resource inspection | `rez_extract.py`, `rez_strings_map.py`, `pict_decode.py`, `ship_spin_extract.py` | Inspect `--help`/source; original files stay external |
| Pilot analysis/conversion | `convert_mac_pilot.py`, `evn_scan_pilot_strings.rb` | [Pilot format](../docs/pilot_save_file_format.md); work on copies |
| Mission analysis | `mission_data.py` | [Mission overview](../docs/mission_reimplementation_overview.md) |
| macOS packaging | `package_macos.py`, `notarize_macos.py` | Existing workflows; signing requires independently configured credentials |
| Function research | `asm_match.py`, `cpp_symbols.py`, clustering/classification/similarity scripts | Exploratory reverse engineering; inspect assumptions and side effects first |
| Original-code microexecution | `microexec_evnova/` | [Existing README](microexec_evnova/README.md); optional, not normal player startup |

## Typical development loop

1. Inspect status in the canonical checkout and the relevant subsystem note.
2. Make a focused source change and its meaningful regression coverage.
3. Build both configurations and run affected tests; use the driver for the
   actual interaction where needed.
4. Update affected port rows and audit when reconstructing original behavior.
5. Review and commit only intended source/documentation files.
6. For a completed release, follow [automatic_updates.md](../docs/automatic_updates.md).

Use `--help` or read a script's parameter block rather than guessing its flags.
Some research scripts inherit Unix-specific assumptions or optional local
dependencies; their presence does not mean they are safe batch operations.

## Test commands

See [tests/README.md](../tests/README.md) for helper unit checks and actual
package/update tests. Windows `Apply` must run only against a disposable test
installation during development. Neither helpers nor a successful archive
build demonstrate that every original campaign has been played through.
