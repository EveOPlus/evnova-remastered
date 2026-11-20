# Local Windows remake

This workspace starts from `wraitii/evnova-decomp` revision
`e3dd30407939296bf19e96f94a499cc6bb2cafd1`. Its engine and original resource
loaders supply the initial remake. Local Windows packaging uses SDL's native
application entry point so launching the game does not open a terminal window.

The original installed data is copied into the ignored `EV Nova/` directory.
Ghidra preparation and local validation evidence remain outside the public
source under ignored research and `analysis/` folders.

## Build

Visual Studio with Desktop development with C++ is required. Bootstrap vcpkg
inside `.cache/vcpkg` at revision
`319504a5326aa870edde46438c5455fa76305a56`, matching upstream CI:

```powershell
git init .cache/vcpkg
git -C .cache/vcpkg remote add origin https://github.com/microsoft/vcpkg.git
git -C .cache/vcpkg fetch --depth 1 origin 319504a5326aa870edde46438c5455fa76305a56
git -C .cache/vcpkg checkout --detach FETCH_HEAD
& .\.cache\vcpkg\bootstrap-vcpkg.bat -disableMetrics
& .\tools\build-windows.ps1 -Configuration release
& .\tools\build-windows.ps1 -Configuration debug
```

Both configurations use static dependencies and the static C runtime. The
optional external probe is disabled. The build helper sets the compiler and
dependency environment only in its process.

Dependencies are shared between configurations in
`%LOCALAPPDATA%/EVNovaBuild/dependencies`. This avoids a HarfBuzz/pkg-config
linker bug with spaces in directory names; Windows short paths do not work
because vcpkg expands them. To choose a different location, pass
`-DependencyDirectory` with a writable path without spaces.

## Validate and package

For a public engine-only ZIP, run `python tools/package_release.py`. This includes
the first-run downloader and license notices. The `package-windows.ps1` command
below builds a private local package with game data and must not be uploaded.

Run the full test binary serially with the stock data present. Several tests
use fixed temporary paths, so running multiple instances can cause collisions.

```powershell
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
$env:SDL_RENDER_DRIVER = 'software'
& .\build\release\tests\evnova_tests.exe --reporter junit --out analysis\tests-release.xml
```

Use a fresh shell without those dummy-driver variables when launching the game.

```powershell
& .\tools\package-windows.ps1
```

The package is `dist/EV Nova Remastered/`. Open `evnova.exe` there, or use the
workspace's `Play EV Nova Remake` shortcut. Packaging copies game resources and
license notices; it does not copy pilot saves. The packaged build resolves its
data relative to its own executable, so it also works when launched elsewhere.

## Modern controls and direct testing

The window opens maximized; F11 switches fullscreen. Mouse-wheel zoom expands
the visible world independently of HUD size. Hold left mouse to steer and fly;
release to coast. Hold right mouse for ship, port, or flight
actions and release over a wedge to select it. See [modern_controls.md](modern_controls.md).

Build with `-TestDriver` for the separate localhost-controlled test executable.
It uses an isolated test profile and can run headless. The normal packaged
game has the testing listener compiled out. Usage and scenarios are documented
in [windows_test_driver.md](windows_test_driver.md).

Validation on 17 November 2026: release and debug builds passed for both normal
and driver configurations. The full C++ suite passed with two optional
archived-pilot fixture cases skipped (379,747 assertions). Manual clang-tidy
passed for the affected translation units. Live checks covered mouse steering,
release braking to zero, keyboard override, zoom, radial release selection and
cancellation, quick taps at screen edges, the galaxy map, auto-docking, and
ship/planet targeting. Headed checks verified maximized
2560x1369 and fullscreen 2560x1440 on the local monitor, with zoom expanding the
world view while retaining HUD scale. Reports and renderer captures are under
`analysis/driver/`.

The narration and docking-departure update also passed release/debug builds,
manual clang-tidy, and the full suite (382,757 assertions, two optional fixture
skips). Live checks verified that Leave stays in flight, mission text reads
automatically, and ship hails read both greetings and subsequent responses.
Stop/replay, voice toggling, and cancellation on close passed through the direct
driver. See [narration.md](narration.md) for the local voice setup.

The inherited engine still has known fidelity gaps described in the main
README, including unconverted QuickTime movies and exact simulation/AI timing. Build and
test results alone do not establish that every storyline can be completed.

The ARPIA2, jump-departure and manual-save update passed release/debug builds
and the full suite (382,785 assertions, two optional fixture skips). The
separate ARPIA2 test passed 3,333 assertions. Live driver checks verified J
departure from Snowmelt to Kon, steering cancellation, Save Pilot and S,
reload after a jump, and campaign isolation across restarts. A no-stellar
save regression now checks that the XOR-format discriminator cannot corrupt
pilot fields. Evidence is under `analysis/arpia2` and `analysis/tests-update.xml`.

Manual clang-tidy passed for the changed units after excluding the known
`clang-analyzer-optin.core.EnumCastOutOfRange` false positive in MSVC's
filesystem bitmask implementation for `brgr_archive.cpp` only. The source and
tracker audit passed its hard consistency checks; the live Ghidra service
was unavailable, so name/coverage verification was not performed. Archived
decompilation supplied the format evidence.

The remastered-graphics update passed release/debug builds, static checks on
the changed units, and 382,858 assertions (two optional fixture skips).
Driver checks covered classic/remastered switching and restart persistence,
Open Pilot with an isolated ARPIA2 `sample.plt`, radial release selection and
cancellation, steering/braking, and packaged-data startup. The original sample
was not modified. That save restores full health; a separate regression checks
radial availability with armor below the actual disabled threshold.

The Sol-to-Wolf-359 J departure completed with no route overlay or galaxy map;
the Shuttle's warp-up phase measured 2.344 seconds at ordinary game speed, with
the normal 100-point fuel cost. The controls banner is removed. Original
cockpit artwork and government palettes are retained. Evidence is under
`analysis/remaster/`; Ghidra remained offline, and source/tracker consistency
passed without changing any reconstruction percentages.
