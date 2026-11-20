# Testing the community remaster

The C++ suite is Catch2, built as `build/release/tests/evnova_tests.exe` on
Windows and `build/release/tests/evnova_tests` on Unix. It links `evnova_runtime`.
Read [AGENTS.md](AGENTS.md) before adding tests.

## Build and select tests

From the repository root on Windows:

```powershell
& ./tools/build-windows.ps1 -Configuration release
& ./tools/build-windows.ps1 -Configuration debug
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
$env:SDL_RENDER_DRIVER = 'software'
& ./build/release/tests/evnova_tests.exe --filenames-as-tags '[#mouse_controls_test],[#flight_automation_test],[#starmap_route_test]'
```

Use a fresh shell without dummy drivers when launching an audible/visible game.
With configured portable presets, `ctest --preset release -R '<pattern>'`
selects discovered test names. The single Catch2 process is preferable for
tests sharing temporary paths; do not run competing resource/profile tests in
parallel. Build setup and affected-file clang-tidy are documented in the root
instructions and [Windows guide](../docs/local_windows_build.md).

| Change | Useful test files or checks |
| --- | --- |
| Mouse, radial and cruise | `mouse_controls_test.cpp`, `command_input_test.cpp`, direct driver |
| Mining, boarding, docking and routes | `flight_automation_test.cpp`, `movement_test.cpp`, driver completion state |
| Map and mission destinations | `starmap_route_test.cpp`, `starmap_search_test.cpp`, `system_markets_test.cpp` |
| Store, trade and dialogs | `docked_dialog_test.cpp`, `trade_center_test.cpp`, `nova_list_control_test.cpp`, `selection_text_dialog_test.cpp` |
| Gambling | `bar_gambling_test.cpp`, live wager/cancel/payout checks |
| Combat and sprites | `weapon_test.cpp`, `collision_test.cpp`, `ship_visual_test.cpp`, `sprite_mask_test.cpp` |
| Pilots and settings | `pilot_file_test.cpp`, `preferences_test.cpp`, `preferences_extra_test.cpp` |
| Archive/scenario decoding | `resource_fork_test.cpp`, `scenario_data_test.cpp`, image/sprite decoder tests |
| Missions | `mission_test.cpp`, `mission_script_test.cpp`, selected TOML scenario |
| Data setup and releases | PowerShell/Python checks below and a disposable real update install |

## External data and campaign isolation

Run from the repository root so loaders can find local game data. Some cases
require archives, fonts or local fixtures. Check assertions and skip messages;
the hermetic filter in [.github/workflows/ci.yml](../.github/workflows/ci.yml)
is the maintained starting point for machines without game data.

`arpia_integration_test.cpp` is tagged `[.arpia][arpia]`; run `[arpia]` explicitly
in its own process after installing the optional campaign and required movie
companions. It modifies process-wide resource registration and must not be
mixed with the stock-data suite. Do not add copyrighted resources to make CI
pass.

## Direct driver

Build `tools/build-windows.ps1 -TestDriver -Configuration release`, then follow
[windows_test_driver.md](../docs/windows_test_driver.md). Use
`tools/nova_driver.py start --profile <isolated-folder> --pilot <copied-pilot>`
and a free loopback port. Driver authorization is governed by the root
instructions and current task; prior authorization need not be requested again.

The driver injects normal input and observes state/UI/screenshots. It is not a
general state editor. Wait for named UI states rather than assuming a fixed
delay is sufficient. Inspect a failure before retrying a destructive in-game
action. Stop only the test process you started, using its quit command.

`tests/scenarios/` contains reusable TOML gameplay scenarios and fragments.
See [scenario testing](../docs/scenario_testing_plan.md) and
[probe harness](../docs/probe_harness.md). A story offer can interrupt an old
scenario's expected window; record that rather than counting the scenario as
passed. New modal loops need named controls and normal platform polling.

## Helpers, package and update verification

```powershell
powershell.exe -NoProfile -File tools/test_setup_game_data.ps1
powershell.exe -NoProfile -File tools/test_update_game.ps1
python -m unittest discover -s tools -p test_package_release.py
python -m unittest discover -s tools -p test_notarize_macos.py
```

Updater checks cover numeric versions, migration from old previews, release
selection, safe paths, checksums, rollback and preservation of user data. For
a published updater change, also run the actual downloaded helper in a
disposable installation: check the public release, download it, apply without
launching, verify all manifest hashes and unchanged pilot/data sentinels.
Never test replacement or rollback against the player's installation.

## Evidence and limits

Keep logs, isolated profiles, screenshots and exploratory scripts under ignored
`analysis/`. Include the test filter and build revision in the handoff. A model
producing a non-silent WAV proves synthesis; it does not prove the user's
speaker/mixer is audible. Screenshot review proves a particular rendered state,
not every resolution. Unit and driver coverage does not establish a complete
stock or ARPIA2 playthrough.
