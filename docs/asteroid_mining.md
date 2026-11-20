# Asteroid mining

Ground truth: archived Ghidra decompilations of `Ship_UpdateVisualState`
0x00428340, `Weapon_FirePlayerWeaponBank` 0x00455150,
`Asteroid_SpawnDestructionPackage` 0x00462550 and
`Ship_SpawnFreeflightObjectAtPosition` 0x0041fb50; the Bible's roid YieldQty,
YieldType, fragment fields, and outfit ModType 31. The live Ghidra server was
unavailable during this change.

The stock Asteroid Miner carries Mining Laser outfits (Mining Blaster bank
53, secondary, range 120) and a mining scoop. W cycles to the secondary;
Left Ctrl holds fire. Shooting breaks rocks; flying through yielded boxes
collects cargo while the hold has space. Silicate types 8..11 yield no cargo.

The player firing path requests folding for shan Flags 0x2|0x80 and returns
until `waypoint_arrival_marker_b` reaches zero. In the binary visual updater,
the fold timer is the `else` of Flags 0x2 clear. The remake had incorrectly
nested it under Flags 0x4, so an idle loaded miner stayed on row five forever.
The corrected branch advances the folding timer and releases normal firing.
This fixes a reconstruction error, not an original-game behavior change.
`ship_visual_test.cpp` exercises held secondary fire through the animation
and confirms that carrier-only Flags 0x4 do not advance the fold state.

Auto-mine is a remake input controller. A radial asteroid hit uses its live
sprite extent. The controller cycles to an installed forward mining weapon,
approaches a moving rock, aims the normal projectile intercept and fires
inside range. It prioritizes yielded cargo over remaining fragments and uses
the existing scoop collision path. Session-only `mining_group` identifiers
propagate to fragments and resource boxes; pool reuse gets a fresh identifier.
No RNG rolls, spawn quantities, lifetimes, collision rules or save formats
change. Equipment, ship state, system changes and cargo capacity gate the job;
manual controls cancel it. The arrival helper disables predictive positional
lead here because adding it to velocity matching holds a fixed offset ahead
of a drifting rock instead of converging on it.

The existing renderer wraps asteroids rather than reproducing the original
deactivate/refill cycle. Its opposite-edge placement formerly landed beyond
the tested envelope, causing alternating edge positions every frame. The
wrap now uses the same symmetric envelope for both the test and destination,
retaining overshoot inside it. The original wrap divergence remains recorded.

Driver checks in `analysis/radar-zoom` used a copy of the user's miner pilot
with an isolated profile. Holding W then Left Ctrl produced shots after
folding. The radial controller selected the secondary itself, broke a
mineral-bearing asteroid and its fragments, scooped 24 tons, and completed
with no remaining group rocks or boxes. A separate silicate run completed
with zero cargo, consistent with its resource definitions.
