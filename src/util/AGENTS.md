# Shared utility instructions

Follow the root and [source instructions](../AGENTS.md). This directory is for
small cross-cutting helpers in `evnova::util`, independent of game state.

Reuse `byte_reader.hpp`, `geometry.hpp`, `math.hpp`, `format.hpp`, `color.hpp`
and `scroll_wheel.hpp` before adding duplicate parsing, coordinate, formatting
or input helpers. Helpers that require game concepts belong in `src/game/`.

Keep units, bounds, signedness and rectangle-edge semantics explicit. Test
meaningful boundary cases such as truncated input, non-finite wheel deltas or
fractional accumulation when changing those behaviors. Avoid a template-heavy
framework for a small shared operation. Update all affected call sites and
run their focused tests after changing a helper contract.
