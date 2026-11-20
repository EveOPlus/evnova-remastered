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
manual controls cancel it.

Long approaches solve for the future meeting point using the target's drift,
the ship's effective speed and coasting during its turn. The controller steers
onto that world-space velocity, then latches a braking/velocity-matching phase
when turnaround drift plus stopping distance reaches the target. This avoids
both chasing the rock's old location and counting its velocity twice. Cargo
uses the same interception approach. Pool-target changes and distant wraps
replan the approach.

The shared arrival controller distinguishes ordinary and inertialess movement.
Vell-os hulls and ships with an inertial dampener use reverse to reduce scalar
speed, steer toward the planned world-space velocity, and slow before turning.
This applies to the intercept, close approach, fragment reposition and final
stop. Ordinary ships keep velocity-error thrust control. Equipment and firing
gates are unchanged.

Mining starts only inside the close-range envelope with relative speed at or
below 0.30 pixels/tick. Unguided shots solve their own relative-motion intercept,
must arrive before expiry, and check the actual degree-rounded firing line
against an inner asteroid footprint, including the pending turn step. Random
weapon spread and collision masks remain unchanged. Overlapping fragments
cause a short reposition so the ship can aim. The original weapon AI and firing
rules are unchanged; this is input automation rather than a binary bug fix.

Regression checks use the real movement integrator at 0.63, 1 and 2 ticks per
frame. A crossing-target approach reaches a valid firing position in under
650 ticks with travelled distance below 115% of its straight-line displacement.
Checks also cover high closing speed, short-lived shots, large turn steps and
overlapping fragments. A direct-driver run mined all fragments, collected
33 tons and finished with no group asteroids or cargo remaining.

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

## Automatic afterburner and early deployment

Extra Prefs includes **Auto-flight uses afterburner**, enabled by default.
Mining approaches, automatic docking (including Nearest Port), and jump-range departures boost only with an installed
working afterburner and spare fuel beyond the remaining planned route (or one
jump when no route is plotted). Boosting requires forward alignment. Mining
uses a conservative braking envelope based on boosted speed and releases the
burner before the final approach, aiming or scooping at close range.

Auto-mining also requests the mining equipment's normal preparation animation
during the approach, before it is in firing range. That request only advances
the existing ship animation fields; it does not create shots, spend ammunition
or bypass the normal animation cadence. The firing gates still require range,
settled motion and an intersecting projectile trajectory. Steering cancels
mining and its preparation request together.
