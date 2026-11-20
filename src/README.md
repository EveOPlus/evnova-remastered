# Engine architecture

EV Nova Community Remaster is a C++23/SDL3 reimplementation built on
`wraitii/evnova-decomp`. Original game resources remain external inputs.
The runtime reconstructs the original rules while the community fork adds
mouse controls, automation, higher-resolution presentation and Windows setup.
Start with [source instructions](AGENTS.md) and the
[documentation index](../docs/README.md).

## Startup and ownership

1. `main.cpp` reports embedded version/revision information. `--version` exits
   before launching a window. A packaged Windows build may hand a pending
   update to `game_update.cpp`; ordinary source and probe builds do not update.
2. `nova_app.cpp` initializes the runtime, locates resources, displays startup
   art and owns the main menu/mode dispatch. `NovaRuntime` in `nova_app.hpp`
   owns `SdlPlatform`, audio/music, fonts, scenario and pilot state.
3. New/open pilot flows restore `game::GameState`, then enter spaceflight or
   landed services. Saves use the original pilot encoding, not a full snapshot
   of every current NPC and world object.
4. `game/spaceflight.cpp` coordinates input, simulation cadence, automation,
   travel, modal dialogs, rendering and return to the main menu. Its helpers
   contain movement, player state, visuals and NPC logic.
5. Menus and modal dialogs render through `SdlPlatform::Present()`. The common
   platform input channels also service narration controls and the test driver.

`GameState` in `game/game_state.hpp` groups the player, ship slots, scenario
tables, travel state, missions, inventory and command latches. Scenario records
are loaded by `game/scenario_data.*`; inspect record definitions before changing
field interpretation. Resource IDs, zero-based slots and sentinel values are
distinct and are often intentionally retained from the binary.

## Subsystem map

| Area | Main implementation | Notes |
| --- | --- | --- |
| Host window, input, placement, processes | `sdl_platform.*` | Shared SDL and driver boundary |
| App and main menu | `main.cpp`, `nova_app.*` | [Startup/menu](../docs/intro_and_main_menu.md) |
| Data and profiles | `nova_paths.*`, `brgr_archive.*`, `mac_resource_fork.*` | [Data setup](../docs/game_data_setup.md), [loading](../docs/scenario_data_loading.md) |
| Resource images and sprites | `pict_image.*`, `pixpat_image.*`, `cicn_image.*`, `rle_sprite_sheet.*` | [Sprites](../docs/ship_sprite_rendering_path.md) |
| Flight integration and movement | `game/spaceflight.*`, `spaceflight_movement.*`, `spaceflight_player_state.*` | [Timing](../docs/frame_timing_and_cadence.md) |
| Player input and assistance | `game/command_input.*`, `mouse_controls.*`, `flight_automation.*`, `jump_controls.hpp` | [Modern controls](../docs/modern_controls.md) |
| NPCs, escorts and targeting | `game/ship_ai*`, `ship_spawn.*`, `targeting.*`, `escort_*` | [NPC behavior](../docs/npc_ship_behaviour.md), [targeting](../docs/player_targeting.md) |
| Weapons and collisions | `game/weapon*`, `collision.*`, `impact_effects.*`, `sprite_mask.*` | [Weapons](../docs/weapon_impact_particles.md), [collision](../docs/sprite_pixel_mask_collision.md) |
| Asteroids and mining | `game/asteroid.*`, `freeflight_objects.*`, `flight_automation.*` | [Mining](../docs/asteroid_mining.md) |
| System travel | `game/travel.*`, `world_wrap.*`, flight handlers | [Hyperspace](../docs/player_hyperspace.md) |
| Galaxy and floating maps | `game/starmap*`, `navigation_panel.*`, `route_map.*` | [Map behavior](../docs/starmap_navigation_map.md) |
| HUD, radar and remaster | `game/hud_*`, `radar_*`, `spaceflight_view.*`, `ship_visual.*`, `sprite_world.*`, `remastered_graphics.*`, `sprite_enhancement.*` | [Graphics](../docs/remastered_graphics.md) |
| Landing, stores and trade | `game/landed_*`, `docked_*`, `trade_center.*`, `system_markets.*` | [Trade](../docs/trade_center.md) |
| Bar, hire and gambling | `game/docked_bar_dialog.cpp`, `bar_gambling.*` | [Bar/racing](../docs/escort_hiring_and_bar.md) |
| Boarding and capture | `game/boarding_plunder.*`, `flight_automation.*` | [Boarding](../docs/boarding_plunder_capture.md) |
| Missions and scripts | `game/mission*`, `docked_mission_dialog.cpp` | [Missions](../docs/mission_reimplementation_overview.md) |
| Government, ranks and encounters | `game/government.*`, `rank.*`, `ship_spawn.*` | [Factions](../docs/faction_combat_and_ranks.md) |
| Save/restore and ship exchange | `game/pilot_file.*`, `new_pilot_flow.*`, `outfit.*`, `player_info_window.*` | [Pilot format](../docs/pilot_save_file_format.md), [ship swap](../docs/player_ship_swap.md) |
| Dialogs and text | `game/ui_dialog.*`, `nova_font.*`, `nova_list_control.*`, `selection_text_dialog.*`, `preferences_ui.hpp` | [DLOG/DITL](../docs/dlog_ditl_dialog_format.md) |
| Preferences and compatibility | `game/preferences*`, `compatibility.hpp` | [Bindings](../docs/preferences_keybindings.md), [original bugs](../docs/known_original_bugs.md) |
| Audio, movies and narration | `sdl_audio.*`, `sdl_music.*`, `game/movie_player.*`, `mission_narration.*` | [Narration](../docs/narration.md), [ARPIA2](../docs/arpia2.md) |
| Public updating | `game_update.*`, `tools/update-game.ps1` | [Release policy](../docs/automatic_updates.md) |
| Direct testing | `probe_server*`, `game/probe_state.*`, named UI controls | [Driver](../docs/windows_test_driver.md) |

## Data and presentation boundaries

`NovaPaths` distinguishes shared support data from the selected campaign's
profile. Base and ARPIA2 pilots/preferences are separate; installed narration
runtime files are shared. Changing campaigns requires a restart so loaded
resource tables and a live pilot are not mixed. See [ARPIA2](../docs/arpia2.md).

Game resources include `.rez`, original executable resources, Mac resource
forks, fonts, PICT images and sprite sheets. Loaders preserve override order
and classic resource types. Do not rewrite original archives to implement a
runtime feature. The downloader uses pinned archives and checksums.

World, scene, dialog-authored, logical and physical window coordinates differ.
Use the owning placement/geometry helpers for rendering and hit tests. Scaling
a dialog without scaling its controls, or treating a floating-map click as
flight input, breaks modern controls.

Automation produces normal turn, thrust, braking, weapon, boarding and travel
inputs; it must not teleport ships or bypass gameplay eligibility. Manual
steering takes over. Floating-map gestures preserve active navigation, while
full-map modal behavior follows the documented controls.

## Build products and tests

`src/CMakeLists.txt` builds `evnova_runtime` and the `evnova` application.
`tests/CMakeLists.txt` builds the Catch2 `evnova_tests` executable against the
same runtime. Probe transport is selected at compile time; player releases
use `probe_server_stub.cpp` and `EVNOVA_ENABLE_PROBE=OFF`.

Use [Windows build instructions](../docs/local_windows_build.md),
[testing instructions](../tests/README.md), and the
[tool catalog](../tools/README.md). Generated version headers belong in the
build tree; `VERSION` and the source revision are their inputs.

## Fidelity and extension policy

Original function addresses and `@port` markers connect source to
`decomp-progress.tsv`. `decomp-skipped.tsv` records deliberately replaced or
unneeded original functions. Percentages measure remaining original scope,
not the quality of a community enhancement. Ghidra comments, existing research
notes, binary evidence and source need to agree on any behavioral claim.

The port is substantial but is not a verified complete playthrough of every
stock and ARPIA2 storyline. Preserve that distinction in documentation and
release notes. Keep original art direction and all attribution; do not treat
the source license as permission to redistribute separately licensed assets.
