# Direct Windows test driver

The test game exposes a loopback HTTP interface. It can run headlessly and
uses the real game controls and simulation. It requires no desktop automation.
The normal player package is built without the interface.

```powershell
& .\tools\build-windows.ps1 -TestDriver -Configuration release
python tools/nova_driver.py start
python tools/nova_driver.py ui
python tools/nova_driver.py state
python tools/nova_driver.py state view
python tools/nova_driver.py click new_pilot
python tools/nova_driver.py type "Driver Test"
python tools/nova_driver.py click ok
python tools/nova_driver.py stop
```

`start` waits for the actual main menu, rejects an occupied port, and cleans up
its own child if startup fails. Add `--headed` to show the game. `--port 8191`
selects another local session. Logs, screenshots, and the default test profile
are under `analysis/driver/`; `start --profile PATH` selects another profile.
The profile override is honored only in a driver-enabled build with `EVN_PROBE`
set. A normal launch does not open a listener.

The Windows transport uses Winsock, exclusive binding on `127.0.0.1`, bounded
socket reads/writes, and joins its thread even if binding fails. The shared
transport now captures state providers by value so a timed-out job cannot use
an expired request-local reference. Injected keys include their scancode, as
required by the game's text-entry path.

## Mouse and display tests

```powershell
python tools/nova_driver.py mouse 650 384 --button left --down
python tools/nova_driver.py mouse 650 384 --button left --up
python tools/nova_driver.py mouse 650 384 --wheel -3
python tools/nova_driver.py mouse 200 200 --button right --down
python tools/nova_driver.py ui
# Move to the center of a radial action from ui.rects, then release right.
python tools/nova_driver.py mouse 200 295 --button right --up
python tools/nova_driver.py key F11
python tools/nova_driver.py screenshot analysis/driver/frame.bmp
```

`POST /probe/mouse` accepts integer window-point `x,y`, optional `button`
(`left` or `right`) with `down`, and optional wheel steps. It queues normal SDL
events; held mouse buttons are merged with physical input just like virtual
held keys. `/probe/state?query=view` reports window size, fullscreen, zoom,
visible world size, HUD scale, and the player's screen center. The `stellars`
query exposes current-system bodies for hit-testing. The radial and galaxy
map publish named controls through `/probe/ui`.
The `view` query also reports `radar_x/y/width/height`, `radar_zoom`, and
`map_zoom`. Wheel events carry their window coordinates, so a driver can
exercise radar/world zoom routing and the galaxy map's +/- equivalence.
Map markers are named `system.<zero-based-id>`; hold `Left Shift` or
`Right Shift` while clicking one to exercise automatic routing. The `travel`
state query includes the ordered route, and `galaxy` exposes available systems,
discovery and resolved directed links for an independent path check.

## Repeatable scenarios

```powershell
python tools/nova_driver.py scenario tests/scenarios/fragments/new_pilot.toml
python -u tools/check_modern_controls.py
```

The second command expects a live pilot in flight and records assertions and
renderer screenshots in `analysis/driver/`. It checks hold steering, release braking,
keyboard override, right-button release selection and cancellation,
zoom, radial placement, pause/dismissal, the real map, automatic docking,
ship targeting, and planet targeting. It uses ordinary game input throughout.
The inherited longer tutorial script is also available under
`tests/scenarios/tutorial_first_leg.toml`; its fixed dialog expectations can
be interrupted by additional scenario offers. Do not count an interrupted
run as a complete storyline pass.

The Python API is `tools.nova_driver.NovaDriver`. Methods include `state`,
`ui`, `click`, `key`, `type`, `mouse`, `hold`, and `command`; raw `get`, `post`,
and `screenshot` are inherited from the scenario runner. See
`docs/probe_harness.md` for pause/step, automation, logs, and scenario syntax.

`start --pilot PATH` supplies the file selected when **Open Pilot** is clicked,
so a saved pilot can be tested without opening a desktop file picker. Use a copy
in an isolated profile and enable the matching campaign there. This replaces
only the picker result; the normal pilot loader, repairs and activation still
run. It sets `EVN_PROBE_PILOT_FILE`, which is compiled out of player builds and
requires a running probe in test builds. It never substitutes Save As results.
The summary exposes `player_disabled`; the `view` query reports
`remastered_graphics`, and Extra Prefs publishes a control with that name.

`mining` reports equipment, selected weapon/readiness/cooldown, firing and fold
animation state, live asteroids and scoopable cargo. `group` identities link a
rock to its fragments/yield without confusing recycled slots. These fields
are read-only and are not pilot-file data. Release on `radial.mine` to start
the regular controller; its `mine` goal appears at `/probe/automation`.

`pop_out` in the full map opens `navigation_panel`, which publishes
`nav.title`, `nav.close`, `nav.chart` and `nav.system.<zero-based-id>`.
`view.navigation_panel` and `navigation_zoom` expose session state. Drag the
slim title bar to move the panel, drag blank chart space to pan, wheel to zoom,
and Shift-click to route. The title-bar X or right-click closes the panel.
J makes one jump; double-J within 450 ms follows the route. Each J press must
include a release; holding does not repeat.

`system_markets` publishes selectable `market.<raw-stellar-id>` headings and
`auto_dock`, which closes the comparison and starts the normal landing
controller for the selected column. `done` closes without starting a flight.
Goods use `commodity.<index>`, `junk.<index>` or `mission.<cargo-type>`; their
values report held quantities. Price cells use `market.<stellar-id>.<good-key>`.
`goods_previous/next` and `markets_previous/next` paginate the two axes.
