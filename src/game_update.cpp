#include "game_update.hpp"

#include "game/preferences_extra.hpp"
#include "log.hpp"
#include "nova_paths.hpp"

#include <SDL3/SDL.h>
#include <filesystem>
#include <string>

#if defined(_WIN32) && !EVNOVA_ENABLE_PROBE
#include <windows.h>
#endif

bool NovaUpdate_OnLaunch() {
#if defined(_WIN32) && !EVNOVA_ENABLE_PROBE
  try {
    game::NovaExtraPrefs prefs;
    (void)game::NovaExtraPrefs_LoadFromSystemStore(prefs);
    if (!prefs.automatic_updates)
      return false;
    const auto shared = NovaPaths::SharedSupportDirectory();
    if (!shared)
      return false;
    const auto preference_arg = (*shared / "Automatic updates.txt").string();
    const char *base = SDL_GetBasePath();
    if (!base)
      return false;
    const auto root = std::filesystem::path{base};
    const auto script = root / "tools/update-game.ps1";
    // Source builds never self-update: only explicit release packages opt in.
    if (!std::filesystem::is_regular_file(root / "release.json") ||
        !std::filesystem::is_regular_file(script))
      return false;
    std::wstring module(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size())
      return false;
    module.resize(length);
    const auto executable = std::filesystem::path{module}.filename().string();
    const auto script_arg = script.string();
    const auto root_arg = root.string();
    const auto pid = std::to_string(GetCurrentProcessId());
    const bool pending =
        std::filesystem::is_regular_file(root / ".evnova-update/pending.json");
    const char *args[] = {"powershell.exe",
                          "-NoProfile",
                          "-NonInteractive",
                          "-WindowStyle",
                          "Hidden",
                          "-ExecutionPolicy",
                          "Bypass",
                          "-File",
                          script_arg.c_str(),
                          "-InstallDirectory",
                          root_arg.c_str(),
                          "-ExecutableName",
                          executable.c_str(),
                          "-ParentProcessId",
                          pid.c_str(),
                          "-PreferenceFile",
                          preference_arg.c_str(),
                          "-Mode",
                          pending ? "Apply" : "Check",
                          nullptr};
    const auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetBooleanProperty(
        props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    auto *process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
      NovaLog::Warn("update helper could not start: {}", SDL_GetError());
      return false;
    }
    SDL_DestroyProcess(process); // Detach; it may finish after the game exits.
    return pending;
  } catch (const std::exception &e) {
    NovaLog::Warn("automatic update unavailable: {}", e.what());
  }
#endif
  return false;
}
