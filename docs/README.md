# Project documentation map

For a new agent, read [root AGENTS.md](../AGENTS.md),
[src/AGENTS.md](../src/AGENTS.md), [engine architecture](../src/README.md), then
[the handoff](agent_handoff.md). Use the subsystem guide below before changing
its implementation. [Documentation instructions](AGENTS.md) describe evidence
and maintenance standards.

## Directory guides

| Directory | Instructions | Overview |
| --- | --- | --- |
| Repository | [AGENTS.md](../AGENTS.md) | [Project README](../README.md), [credits](../CREDITS.md), [notices](../NOTICE.md) |
| Source | [src/AGENTS.md](../src/AGENTS.md) | [Architecture](../src/README.md) |
| Gameplay/UI | [src/game/AGENTS.md](../src/game/AGENTS.md) | [Subsystem map](../src/README.md) |
| Shared utilities | [src/util/AGENTS.md](../src/util/AGENTS.md) | Existing focused headers |
| Vendored code | [src/third_party/AGENTS.md](../src/third_party/AGENTS.md) | [Provenance](../src/third_party/README.md) |
| Tests | [tests/AGENTS.md](../tests/AGENTS.md) | [Test workflow](../tests/README.md) |
| Tools | [tools/AGENTS.md](../tools/AGENTS.md) | [Tool catalog](../tools/README.md) |
| GitHub automation | [.github/AGENTS.md](../.github/AGENTS.md) | [Release policy](automatic_updates.md) |
| Documentation | [docs/AGENTS.md](AGENTS.md) | This index and [handoff](agent_handoff.md) |

## How to read the research

Many subsystem notes are inherited reverse-engineering records. Original
addresses and resource IDs describe the binary; a note is not automatically a
claim that every behavior is implemented. Check the owning source's `@port`
markers, `decomp-progress.tsv` and relevant tests. Provisional/investigation
notes remain hypotheses until evidence resolves them.

Original Bible, reference images, ResForge and decompile dumps are local,
separately supplied research assets and may be absent from a public checkout.
Do not fabricate them or upload copyrighted assets to fill missing references.

## Build, delivery and community features

- [Local Windows remake](local_windows_build.md)
- [Automatic updates and build versions](automatic_updates.md)
- [Game data setup](game_data_setup.md)
- [ARPIA2 campaign](arpia2.md)
- [Modern controls](modern_controls.md)
- [Local mission and ship narration](narration.md)
- [Remastered graphics](remastered_graphics.md)

## Testing and reverse-engineering tools

- [Direct Windows test driver](windows_test_driver.md)
- [External probe harness (EVN_PROBE=1)](probe_harness.md)
- [Plan for scenario testing](scenario_testing_plan.md)
- [Find symbols matching the pattern 'Game' or 'Init'. Regexes are case-insensitive.](ghidra_api.md)
- [known original bugs](known_original_bugs.md)

## Resources, saves and interface reconstruction

- [Scenario data loading (ships / outfits / weapons / stellars / systems / governments / fleets)](scenario_data_loading.md)
- [The `chär` resource (character template / pilot block)](char_resource_format.md)
- [Pilot save files (.plt): format and load/save flow](pilot_save_file_format.md)
- [Startup Splash, Main Menu, and New-Game Intro Cinematic](intro_and_main_menu.md)
- [DLOG / DITL dialog resources (EV Nova's window format)](dlog_ditl_dialog_format.md)
- [Preferences, Key Settings, and the Input Binding Model](preferences_keybindings.md)
- [The in-flight Player Info window ("special interaction" family)](player_info_window.md)
- [In-space comm dialogs (ship hail + stellar destination interaction)](comm_dialogs.md)

## Flight, navigation, ships and rendering

- [Spaceflight timing and cadence](frame_timing_and_cadence.md)
- [Player hyperspace jump: fast-jump and inertialess parent control flow](player_hyperspace.md)
- [Hyperspace sound duration and Mac display fade](hyperspace_audio_timing.md)
- [Starmap / Galaxy Map (in-flight navigation) — reverse-engineering note](starmap_navigation_map.md)
- [Player target selection (ship target + travel/landing stellar)](player_targeting.md)
- [NPC ship behaviour](npc_ship_behaviour.md)
- [Ship sprite rendering path (metadata notes)](ship_sprite_rendering_path.md)
- [Sprite pixel-mask collision (direct weapon contact)](sprite_pixel_mask_collision.md)
- [System murkiness rendering](system_murk_rendering.md)
- [Outfit-derived player state (`Outfit_RecomputeOutfitDerivedState`, 0x0046d4b0)](outfit_derived_state.md)
- [Player ship swap / escort capture](player_ship_swap.md)
- [x87 numeric behavior: FIST truncation and accepted precision divergences](x87_precision.md)

## Missions, economy, combat and interactions

- [Mission system reference](mission_reimplementation_overview.md)
- [Spaceport Bar & Escort Hiring](escort_hiring_and_bar.md)
- [Boarding / Plunder / Capture](boarding_plunder_capture.md)
- [Trade Center (commodity exchange)](trade_center.md)
- [Asteroid mining](asteroid_mining.md)
- [Faction combat events and ranks](faction_combat_and_ranks.md)
- [Contraband scanning (`Ship_ScanPlayerForContraband` 0x00401800)](contraband_scan.md)
- [Weapon impact particles (SWParticles)](weapon_impact_particles.md)
- [Weapon mode code mapping (EVN `wëap` Guidance)](weapon_mode_code_mapping.md)
- [Auto-abort CompReward + domination/"Avoid" mission investigation](auto_abort_domination_investigation.md)
