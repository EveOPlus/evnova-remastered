#include "build_version.hpp"
#include "game_update.hpp"
#include "log.hpp"
#include "nova_app.hpp"

#include <SDL3/SDL_main.h>
#include <string_view>

int main(int argc, char **argv) {
  NovaLog::Info("EV Nova {} | revision {} | {} | test driver {}",
                evnova::BuildLabel(),
                evnova::kBuildRevision,
                evnova::kBuildType,
                EVNOVA_ENABLE_PROBE ? "on" : "off");
  bool skip_update = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    if (argument == "--version")
      return 0;
    skip_update = skip_update || argument == "--skip-update";
  }
  if (!skip_update && NovaUpdate_OnLaunch())
    return 0;
  return NovaProgramEntry();
}
