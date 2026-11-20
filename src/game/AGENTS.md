# Gameplay and UI instructions

The root and [source instructions](../AGENTS.md) apply. Use the subsystem table
in [src/README.md](../README.md) and the matching research note before editing.

## Simulation and input

- `spaceflight.cpp` owns integration order, input edges, modal transitions and
  simulation timing. Inspect both frame input and simulation-step handling.
- Community automation belongs in `flight_automation.*` and produces existing
  inputs. Preserve normal fuel, range, crew, disabled-ship and mission gates.
  Explicitly hand control over when switching between cruise, mining, boarding,
  docking and route following; stale steering must not fight the new goal.
- Preserve original numeric units and quirks. Do not replace integer-degree
  checks, fixed-width resource fields or tick-based constants speculatively.
- Keep player slot, NPC slot, resource ID and sentinel conversions explicit.
  Test target loss, invalid state and cancellation when changing automation.
- Commands often have held/edge latches. A UI action must produce one command,
  and returning from a modal must not trigger a second landing/jump/board.

## UI and presentation

- Keep original cockpit, ship and planet designs. Scale through existing
  placement helpers and preserve the larger readable docked UI.
- Use `Present()` and normal platform event polling in modal loops. Publish
  stable probe control names for new interactions; preserve existing names.
- The floating map consumes its own clicks, wheel and drag input. Chart drag
  pans, title drag moves, edges resize, and only its X closes it. It must not
  cancel autopilot simply because the player is browsing it.
- Maintain mission markers when current-system and mission symbols overlap.
  System Markets uses shared mission destination and real trading calculations.
- Narration lifetimes follow dialog scopes. New mission/hail text should use
  `MissionNarrationScope`, expanded visible text and the existing toolbar.
- Prefer shared `NovaListControl`, dialog helpers and `preferences_ui.hpp`
  controls over inventing another menu theme or scroll implementation.

## What to verify

For input changes, test activation, release, override, modal boundaries and
disabled/jumping states. For maps, test both full and floating maps. For an
automation change, check actual movement and its completion condition with
normal physics. For economy or missions, check state effects, not just labels.

Keep the relevant [modern controls](../../docs/modern_controls.md) or subsystem
guide current, and apply the root port-tracker rules to affected original
functions. A driver scenario is evidence for that scenario, not a campaign
playthrough or proof of all original-game behavior.
