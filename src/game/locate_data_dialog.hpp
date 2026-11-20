#pragma once

// Startup screen shown when no EV Nova install root can be located. It is a
// port-only addition: the original always had its data beside the executable,
// while the SDL port can be run from anywhere and lets the player point it at
// an existing Community Edition install, or download data on Windows. The
// chosen folder is persisted by the caller in the extra prefs (see
// preferences_extra.hpp).

#include <filesystem>
#include <optional>

class SdlPlatform;

namespace game {

// Runs the locate-data screen in its own modal loop. Returns the resolved
// install root once the player picks a valid location, or nullopt when the
// player chose Quit (including closing the window).
[[nodiscard]] std::optional<std::filesystem::path>
NovaUi_RunLocateDataDialog(SdlPlatform &platform);

// Public Windows downloads install the shared enhanced artwork before play.
// Continuing without it is allowed offline; false means the window was closed.
[[nodiscard]] bool NovaUi_EnsureRemasteredGraphics(SdlPlatform &platform);

} // namespace game
