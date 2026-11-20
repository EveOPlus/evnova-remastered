#include "brgr_archive.hpp"
#include "game/movie_player.hpp"
#include "nova_paths.hpp"
#include "pict_image.hpp"
#include "sdl_platform.hpp"
#include "third_party/pl_mpeg.h"
#include "util/byte_reader.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>

// Explicit opt-in: campaign resources must never enter the stock-data suite's
// process-wide resource registry. Run with the [arpia] test filter.
TEST_CASE("ARPIA2 complete pack loads and decodes its media",
          "[.arpia][arpia]") {
  REQUIRE(NovaPaths::ArpiaCampaignInstalled());
  const auto profile =
      std::filesystem::temp_directory_path() / "evnova-arpia-check";
  std::filesystem::create_directories(profile);
  NovaPaths::SetSupportDirectoryOverride(profile);
  std::ofstream(profile / "ARPIA2 enabled.txt") << 1;
  NovaPaths::InitializeCampaignSelection();
  REQUIRE(NovaPaths::ArpiaCampaignActive());
  REQUIRE(NovaPaths::SupportDirectory() == profile / "ARPIA2");
  for (const auto id : {236, 237}) {
    const auto weapon =
        NovaResource_Load(0x77916170, static_cast<std::uint16_t>(id));
    REQUIRE(weapon);
    // Optional secondary-weapon archives override the core definitions.
    CHECK((evnova::util::ReadBe16(*weapon, 0x1c) & 2U) != 0);
  }
  const auto splash = NovaResource_LoadPictData(131);
  REQUIRE(splash);
  const auto image = Resource_LoadPictAsImage(*splash);
  REQUIRE(image);
  CHECK(image->width > 500);
  CHECK(image->height > 400);
  CHECK(NovaResource_Load(0x6d95736e, 919).has_value());
  CHECK(NovaResource_Load(0x6f9f7466, 457).has_value());
  for (const auto *name : {"gasminer.mov", "credits.mov"}) {
    const auto path = NovaResource_LocateFile(std::string(name) + ".mpg");
    REQUIRE(path);
    std::unique_ptr<plm_t, decltype(&plm_destroy)> video(
        plm_create_with_filename(path->string().c_str()), plm_destroy);
    REQUIRE(video);
    REQUIRE(plm_has_headers(video.get()));
    plm_set_audio_enabled(video.get(), false);
    int frames = 0;
    while (const auto frame = plm_decode_video(video.get())) {
      CHECK(frame->width >= 618);
      CHECK(frame->height >= 480);
      ++frames;
    }
    CHECK(frames >= (std::string_view(name) == "gasminer.mov" ? 1340 : 300));
  }
  SdlPlatform platform;
  REQUIRE(platform.Initialize(true));
  // Exercise the actual modal and its normal Escape cancellation path.
  const auto timer = SDL_AddTimer(
      300,
      [](void *, SDL_TimerID, Uint32) -> Uint32 {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_ESCAPE;
        SDL_PushEvent(&event);
        return 0;
      },
      nullptr);
  REQUIRE(timer != 0);
  CHECK(game::NovaMovie_Play(platform, "gasminer.mov"));
  CHECK(NovaPaths::SelectArpiaCampaign(false));
  CHECK(NovaPaths::ArpiaCampaignActive()); // selection cannot corrupt a live
                                           // pilot
  CHECK_FALSE(NovaPaths::ArpiaCampaignSelected());
}
