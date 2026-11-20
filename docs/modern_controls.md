# Modern controls

These are intentional remake features layered over the existing game rules.
Original keyboard bindings still work.

| Control | Action |
| --- | --- |
| Hold left mouse in space | Turn toward the cursor and thrust once aligned |
| Release a left-mouse hold | Automatically brake to a complete stop |
| Double-click empty space | Turn that way and cruise at normal full speed without braking |
| Hold right mouse over a ship | Target, hail, or board it |
| Hold right mouse over a planet/station | Target, hail, or automatically approach and dock |
| Hold right mouse over empty space | Galaxy map, missions, or automatically dock at the nearest port |
| Release right mouse over an action | Select the highlighted action |
| Release right mouse in the center or outside the ring; Escape | Dismiss the radial menu |
| Mouse wheel over space | Zoom the world from 0.5x to 4x; the HUD keeps its own scale |
| Mouse wheel over the minimap | Zoom radar independently from 0.5x to 4x |
| Mouse wheel in the galaxy map | Zoom using the same steps and limits as + and - |
| Mouse wheel in Outfitter, Shipyard or Hire Escort | Page the stock list like its up/down arrows |
| Mouse wheel over mission lists or scrolling dialog text | Scroll the list or text within its normal limits |
| System Markets in the space or port radial menu | Compare all local commodity exchanges |
| Pop out in the galaxy map | Keep a draggable navigation panel open in flight |
| Double-click or Shift-click a system in either map | Plot the shortest known route |
| Triple-click a system in either flight map | Plot the route and start following it |
| Bar > Gamble | Bet on the original racing game; Alt-click a racer for a custom wager |
| Release the asteroid radial menu on Auto-mine | Approach, fire the mining weapon, then scoop the yield |
| F11 | Toggle fullscreen from menus, flight, and dialogs |
| J with a destination selected | If too close to jump, fly clear automatically and then jump |
| Save Pilot / S in the main menu | Choose a folder and filename for a pilot save |

The window opens maximized. Its viewport expands with the window, revealing
more world instead of stretching the old viewport. Wheel zoom and fullscreen
mode are saved on normal exit. Extra Prefs still offers separate UI and mission
text scaling.

Mouse steering has a dead zone around the ship. Keyboard turning and reverse
take priority. A mouse press beginning on the HUD or route map does not start
steering. Jump, disabled-ship, and docking restrictions remain in the normal
flight handlers. Steering cancels automatic docking or boarding.

Two left presses within 500 ms and 12 screen pixels start cruise. The second
press locks the direction relative to the ship; moving the pointer afterwards
does not change it. A fresh hold or keyboard steering takes control again.
Cruise uses normal thrust and top speed, without automatically using afterburner.

Releasing a mouse flight hold applies a braking assist along the velocity vector
without turning the ship. Deceleration follows the ship's effective thrust and
ionization penalty, clamps at zero, and handles inertialess scalar speed too.
A fresh flight hold or keyboard movement cancels braking immediately. HUD clicks
do not arm it; jumps, disabled ships, and automatic docking retain control of
their own movement.

The radial menu pauses the flight simulation while choosing. It uses the same
ship and stellar hit tests as targeting, stays inside the current window, and
sends actions through the existing hail, boarding, navigation, and docking
paths. A planet action clears an old ship target so a hail addresses the planet.
Hull damage no longer prevents the radial menu from opening: disabled ships
retain targeting, hailing, map and mission access through the usual handlers.
Attempting auto-dock while disabled reports that the ship cannot auto-dock;
it does not start a stuck approach. Jump transitions and destroyed ships
still block the modal. The persistent controls banner has been removed.
Release selects the wedge under the pointer; a quick tap without moving cancels,
including where the menu has shifted inward to fit a screen edge.
Radial **Board** approaches a disabled ship, matches its drifting velocity,
and aligns alongside it before issuing the normal boarding command. It reports
an ineligible or lost target instead of leaving a silent request. Manual B keeps
the original distance and speed checks and now explains when alignment is needed.
Loot, capture, mission and crew rules remain in the normal boarding handler.

Automatic docking uses the existing input-producing flight controller: it turns,
thrusts, brakes, and issues the normal land command without teleporting.
Arrival completes the docking goal in normal and test builds. Leaving the port
does not restart that goal; another automatic docking attempt requires a new
request.

Automatic jump departure uses the normal proximity, fuel, ship condition, and
route checks. J closes the small route overlay and automatic destination
selection leaves it closed. It flies out of the system's no-jump area and issues the normal
jump command. The player jump transition and its sound/fades run at twice
the original rate; ordinary flight and combat speed are unchanged. Manual steering or opening the full galaxy
map cancels the assist; floating-map edits retarget it. A missing destination or insufficient fuel still uses
the game's usual denial.

Save Pilot appears once a living pilot is loaded. Escape from flight returns
to the main menu; click Save Pilot or press S there. It uses the original
About Nova button artwork and position; About Nova now lives in Set Prefs.
The original curved menu layout and surrounding artwork are retained.
Settings, Extra Prefs, Narration and key bindings share a dark metal/red palette.
A native Save As dialog
starts in the active campaign's normal Pilots folder, with the pilot's name
suggested. Choose another folder/name if desired; a missing extension becomes
`.plt`. Cancel writes nothing, and replacing a file requires confirmation.
The success message includes the saved path. Open Pilot starts in that same
normal Pilots folder and can browse to any saved `.plt` file.

The file includes missions, credits, equipment and discoveries. It uses the
existing pilot restore behavior, not a snapshot of every ship's position in
space. Automatic saves remain in the active campaign's normal Pilots folder.
Jump departure uses the plotted destination directly and does not open the
route-map overlay to switch display modes.

Mission and ship hail dialogs include automatic local narration, with Read
again (F6), Stop (F8), and Voice on/off (F7) controls below the dialog. See
[narration.md](narration.md).

Implementation: `src/game/mouse_controls.*`, the input adapter in
`src/game/spaceflight.cpp`, and SDL input/window handling in `src/sdl_platform.*`.
`tests/mouse_controls_test.cpp` covers steering, wraparound, override, radial
geometry, and target selection. `tools/check_modern_controls.py` exercises the
running game through its test driver.

One J press makes one jump, including flying clear first when needed. Two
presses within 450 ms follow the entire Shift-click route through normal
movement and jump input. The first press acts immediately; the second can
promote a departure already in progress. Holding J is not a double press.
While following a route, J again, steering, landing/boarding, changing
navigation, or opening the map cancels it. Fuel exhaustion and unavailable
links stop the controller without discarding the remaining route. Double-press
J after refueling to resume it. Jump and hypergate screen transitions always
fade through black, independent of the legacy effects preference.

Remastered radar replaces single-pixel contacts with heading arrows, planet
globes, station diamonds, gate rings and disabled-ship crosses. Selected targets
get brackets and faint range rings help with orientation. Original IFF colours,
equipment gates, range, cloak detection and interference still apply. Turning
Remastered graphics off restores the original radar symbols.

The remastered minimap enlarges the original cockpit's upper section by 56
authored pixels in each dimension; lower panels move down and the complete
strip still fits the window. Radar zoom is saved on normal exit. Crowded
contacts get separate symbol positions with faint leaders to their real
locations. The player stays centered, selected contacts have placement priority,
and off-screen/cloaked contacts are not revealed by the spacing pass. The galaxy
map wheel preserves the current map center and its existing +/- behavior.

System Markets lists each commodity exchange in the current system's nav list.
Goods appear in rows, with markets in columns. Every locally traded standard
or special commodity is included, along with held goods that have no local
market. The Tons column shows how much you carry; mission freight is included
and marked as non-tradable. Goods in your cargo have bold
names and prices: high selling prices are green and low ones red. Unheld
goods use green for low buying prices and gold for high ones. Strong shades
mark the lowest/highest price across local markets; softer shades mark prices
below/above the scenario's normal price. Equal normal prices remain neutral.
Special cargo follows the same rules, comparing matching cargo definitions
across ports. Multiple prices for the same cargo at one port are shown together.
Prices come
from the same Trade Center snapshot used when docking, including reputation,
domination, active disasters and junk availability conditions. Select a market
and click Auto Dock to close the comparison, fly there and land through the
normal docking controller. Steering cancels the approach. A red mission arrow beside a market's name and
**Mission here** label mark its current mission destination; multiple jobs
show a count. The badge follows the outbound/return leg, excludes failed
missions and respects hidden-destination flags. Selecting it still uses the
same market heading and Auto Dock action. A local mission stop without a
commodity exchange also appears, labelled **Mission / No market**, so Auto Dock
can take you there. Browsing or Done
does not change cargo, credits or destination. Ten goods and four markets fit
per page. Wheel/Previous/Next scroll the goods; Previous ports/Next ports show
additional markets. Price extremes always compare all markets, including
those on another page.
The radial menu still cancels in its center, outside the ring or with Escape.

The floating Navigation panel shares the full map's discovered systems and
route planner. A slim title bar moves the panel and has a close button.
Drag empty chart space to pan, and use the wheel to
zoom. Click a neighboring system or Shift-click a destination to plot the
shortest known route. J jumps once; double-J follows the route. The panel stays open and recenters
after each jump. Only the X closes it; right-click leaves it open. Panel gestures do not start
mouse steering or change world/radar zoom. Drag the panel's edges or bottom-right
grip to resize it without changing the chart zoom.
Map clicks, panning, resizing and closing the panel leave autopilots running.
A new destination retargets active jump assistance; a new route retargets
auto-route. Selecting the current system or current next hop preserves the
route. Mining and docking keep their own target while the map is used.

**Extra Prefs > Auto-flight uses afterburner** enables automatic boosting for
mining approaches, jump departures, Auto Dock and Nearest Port. It needs an
installed afterburner and preserves enough fuel for the planned route (or one
jump). The ship releases boost in time to brake for docking or mining.

Auto-mine requires a forward-firing mining weapon, an asteroid scoop and free
cargo space. It uses normal movement, weapon selection, shooting and collision
rules. It tracks the chosen rock's fragments and scoops their cargo before
continuing to break remaining fragments. Steering, manual firing, changing
weapons, jumping, landing, opening the full map or another radial action
cancels it. A disabled ship, full hold or unavailable weapon stops it with a
message. Some rocks (including stock silicates) yield no cargo; loose cargo
retains its normal expiry and can be collected by other miners.

Manual mining: W selects the Mining Blaster secondary, hold Left Ctrl to fire,
then fly over the resource boxes. Its stock range is 120 world pixels. The
miner finishes its short folding animation before the first shot. The remake's
misnested fold-animation branch has been fixed: previously it could block
firing indefinitely even with the secondary selected.


**Active Missions (I) > Auto Pilot** flies to the selected mission's current
planet or station, following the shortest known route and docking normally.
I also opens this menu while docked; Auto Pilot launches the ship automatically.
It is available from the active-mission window inside the Mission BBS too.
Finish an unanswered mission offer before starting an autopilot. Return-leg
missions target their return port after the outbound destination is reached.
Failed, hidden or unavailable destinations and unknown routes produce an
explanation and do not launch. Fuel, disabled-ship and landing permission rules
still apply. Refuel when needed and select the mission's Auto Pilot again.
Steering or another flight action cancels it. Browsing the floating map keeps
the mission target; replacing its route stops it rather than docking somewhere
unrelated. A failed/replaced mission or vanished destination stops the trip.


**Active Missions (I) > Pop Out** opens a compact floating tracker in flight or
at the main spaceport screen. Click the small **Auto Pilot arrow** to the right
of a mission to launch if needed, follow its route and dock. Drag the small title
bar to reposition it; only X closes it. Wheel scrolling stays within the panel,
and clicking it does not steer, zoom the world or cancel an active autopilot.
It updates destinations and removes completed missions automatically, and can
remain open beside the floating map. Hovering an arrow identifies its action.
The panel size follows its visible rows
(up to four); the normal mission window remains available for full briefings.

### Story leads between missions

Press **I** to see current assignments and story leads, including when no formal
mission is active. Leads name the contact's port/service or a broader search
area, explain delayed appearances and blocked docking, and update as the story
advances. Scroll the description for longer clues. **Pop Out** keeps them in the
compact tracker; the small arrow uses normal Auto Pilot for a known accessible
port. A lead itself does not grant docking clearance or force a mission offer.


Pilot saves remember which floating map/mission panels are open and their
positions. The map also keeps its resized dimensions and zoom. Loading the
pilot restores that layout, adjusted to fit the current game window. The
mission tracker keeps its automatic size based on the visible rows.
