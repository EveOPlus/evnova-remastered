# Source instructions

Read the root [AGENTS.md](../AGENTS.md), then [README.md](README.md) for the
architecture and [documentation index](../docs/README.md) for the subsystem
you will change. Do not infer binary behavior from names alone.

## Ownership and boundaries

- `main.cpp` handles version reporting and the optional release updater before
  entering `nova_app.cpp`. `NovaRuntime` owns platform, audio and game state.
- `sdl_platform.*` is the window, input, placement, process and probe boundary.
  New loops use `Present()` and existing event channels. Direct SDL presentation
  or a separate input pump can break the test driver and narration controls.
- `nova_paths.*` owns data/profile/campaign paths. Resolve locations through it;
  do not hardcode a developer installation in gameplay or UI code.
- `game/` owns simulation and game UI; follow [its instructions](game/AGENTS.md).
- `util/` contains small shared, non-game helpers; follow
  [its instructions](util/AGENTS.md).
- `third_party/` is vendored code. Preserve attribution and isolate intentional
  local changes; follow [its instructions](third_party/AGENTS.md).

## Changes and validation

Keep C++23 value ownership explicit. Use existing RAII wrappers for SDL handles,
and keep simulation calculations independently testable where practical.
Preserve ID conventions, coordinate spaces, timing units and original numeric
rules at interfaces; check their declarations and subsystem notes first.

For ports, consult Ghidra or the available archived decompile, update the
primary address citation and `@port` marker, and update affected tracker rows.
Community UI/input additions are deliberate divergences, not evidence that
unported original functionality is complete. Follow the root compatibility
policy for confirmed original bugs.

Format changed C++, build Release and Debug, run relevant tests, and run
clang-tidy for changed translation units with the platform's supported setup.
See [tests/README.md](../tests/README.md) and
[Windows build instructions](../docs/local_windows_build.md). Source-only
builds must not acquire release metadata that enables self-updating.

After changing ownership or entry points, update this guide or the architecture
overview. Document behavior that the next agent cannot safely infer from a
function name; avoid duplicating the implementation line by line.
