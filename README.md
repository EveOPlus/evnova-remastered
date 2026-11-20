# EV Nova Remastered

An unofficial community fork of [wraitii/evnova-decomp](https://github.com/wraitii/evnova-decomp),
**Lancelot de Ferrière's C++23/SDL3 reimplementation of Escape Velocity Nova**.
This fork adds modern controls and presentation while retaining the original
ship, planet and interface designs. Upstream history and the original
[MIT license](LICENSE.txt) are preserved.

**[Credits and contributors](CREDITS.md) · [Notices](NOTICE.md) ·
[Controls](docs/modern_controls.md) · [Windows build](docs/local_windows_build.md)**

## Playing on Windows

Build the game using the [Windows guide](docs/local_windows_build.md).
Prebuilt releases are currently withheld while a Windows Security detection
on the local test-driver executable is investigated. The detection has not
been established as a false positive.

Open `evnova.exe`. On a new installation, **Download and Set Up**
fetches Community Edition and ARPIA2 from
[download.escape-velocity.games](https://download.escape-velocity.games/),
checks their pinned SHA-256 hashes, and keeps the original documentation.
You can instead choose an existing installation with **Locate EV Nova.exe**.

The download is approximately 127 MB. Existing saves and plug-ins are not
replaced. ARPIA2 remains an optional switch in **Set Prefs > Extra Prefs**.
See [data setup](docs/game_data_setup.md) for offline setup and movie conversion.

Game data, ARPIA2 resources, fonts, voice models, pilot saves and enhanced game
art are **not bundled in this Git repository or its engine-only releases**.
They have separate terms; see [credits](CREDITS.md). This is not an official
Ambrosia Software, ATMOS or ARPIA2 release.

## Features in this fork

- Hold the mouse to steer; release to brake. Right-click radial actions cover
  hailing, boarding, docking and asteroid mining.
- Modern resolutions, fullscreen, sharper rendering and independently zoomable
  world, radar and maps. Optional locally generated sprite packs keep the
  original designs; see [remastered graphics](docs/remastered_graphics.md).
- Shift-click routes. J jumps once; double-J follows the route. A compact
  floating navigation map stays open in flight.
- System Markets compares every locally traded good and held cargo. Select a
  market column and choose Auto Dock to fly there and land.
- Save/Open Pilot file pickers, larger dialogs and consistent themed controls.
- Optional [local mission and hail narration](docs/narration.md), with selectable
  voices installed separately.
- [ARPIA2 compatibility](docs/arpia2.md), with all three optional extras under
  one setting and separate campaign saves.
- A [direct test driver](docs/windows_test_driver.md) for isolated testing.
  Player releases have its control listener disabled.

## Status and provenance

The original engine implementation and research notes come from the upstream
project; this fork began at `e3dd30407939296bf19e96f94a499cc6bb2cafd1`.
Windows gameplay and the listed additions have been exercised through builds,
unit checks and isolated driver scenarios. A full playthrough of every stock
or ARPIA2 storyline has not been verified. Original timing, AI and other
fidelity gaps remain documented in the source and progress trackers.

Development uses AI assistance, including OpenAI Codex. Claims about testing
apply only to the checks actually performed, not complete behavioral parity.

## Source and data licenses

The original engine's MIT notice remains in [LICENSE.txt](LICENSE.txt).
[Third-party notices](licenses/README.md) and [full credits](CREDITS.md) explain
the separately licensed libraries, game and plug-in content, models and tools.
The source license does not grant rights over downloaded game assets.

## Prerequisites

You need CMake (3.25+), [Ninja](https://ninja-build.org/), a C++23-capable compiler (AppleClang/Clang, GCC 13+, or MSVC 19.3x), and [vcpkg](https://github.com/microsoft/vcpkg). Ninja is used on every platform, including Windows with MSVC.

Clone and bootstrap vcpkg once; this is where CMake fetches the SDL3, fmt and Catch2 dependencies from:

```sh
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
```

Then point `VCPKG_ROOT` at that checkout (add it to your shell profile to keep it across sessions):

```sh
export VCPKG_ROOT="$HOME/vcpkg"
```

Platform notes:

- **macOS:** `brew install cmake ninja` (the Xcode command line tools provide the compiler).
- **Linux (Ubuntu 24.04):** install the compiler, CMake, Ninja, and SDL build dependencies:

  ```sh
  sudo apt-get update
  sudo apt-get install --no-install-recommends -y \
    build-essential cmake ninja-build pkg-config \
    autoconf autoconf-archive automake libtool libltdl-dev \
    libegl1-mesa-dev libibus-1.0-dev libwayland-dev libx11-dev \
    libxext-dev libxft-dev libxkbcommon-dev
  ```

- **Windows:** install Visual Studio 2022 with the *Desktop development with C++* workload plus CMake and Ninja (`winget install Kitware.CMake Ninja-build.Ninja`). Open an **x64 Developer PowerShell for VS 2022** (so the MSVC environment is initialized), then bootstrap vcpkg in that shell:

  ```powershell
  git clone https://github.com/microsoft/vcpkg.git "$env:USERPROFILE\vcpkg"
  & "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
  $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
  ```

  Set `VCPKG_ROOT` after opening the developer shell; Visual Studio environment initialization can overwrite variables set earlier. Run the build from that same shell so Ninja can find `cl.exe`.

## Build and test

Configure, build, and test the Release preset (CMake installs the `vcpkg.json` dependencies automatically the first time):

```sh
cmake --preset release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Run the game with:

```sh
./build/release/src/evnova          # macOS / Linux
build\release\src\evnova.exe        # Windows
```

The optional probe/control harness ([docs/probe_harness.md](docs/probe_harness.md)) supports POSIX sockets and Windows Winsock. It is built by default on macOS/Linux and off in ordinary Windows player builds. Toggle it with `-DEVNOVA_ENABLE_PROBE=ON|OFF` when configuring.
