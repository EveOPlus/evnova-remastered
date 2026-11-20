#include "brgr_archive.hpp"
#include "game/ship_visual.hpp"
#include "game/sprite_enhancement.hpp"
#include "game/sprite_world.hpp"
#include "nova_paths.hpp"
#include "rle_sprite_sheet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

namespace {
game::SpriteDetailSource Solid(int w, int h, std::array<Uint8, 4> color) {
  game::SpriteDetailSource source{w, h, {}};
  for (int i = 0; i < w * h; ++i) {
    source.rgba.insert(source.rgba.end(), color.begin(), color.end());
  }
  return source;
}

struct Target {
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface;
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer;

  explicit Target(int w = 80, int h = 80)
      : surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32),
                SDL_DestroySurface),
        renderer(surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr,
                 SDL_DestroyRenderer) {}

  void Clear() {
    REQUIRE(renderer);
    REQUIRE(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer.get()));
  }

  std::vector<Uint8> Pixels() {
    REQUIRE(SDL_FlushRenderer(renderer.get()));
    const auto *p = static_cast<const Uint8 *>(surface->pixels);
    return {p, p + surface->pitch * surface->h};
  }
};
} // namespace

TEST_CASE(
    "Sprite enhancement preserves flat colours and rejects invalid frames",
    "[sprite-detail]") {
  for (const Uint8 alpha : {0, 64, 255}) {
    const auto source = Solid(3, 5, {50, 100, 200, alpha});
    const auto enhanced = game::EnhanceSpritePixels(source);
    REQUIRE(enhanced);
    CHECK(enhanced->width == 9);
    CHECK(enhanced->height == 15);
    for (std::size_t i = 0; i < enhanced->rgba.size(); i += 4) {
      CHECK(enhanced->rgba[i + 3] == alpha);
      if (alpha > 0) {
        CHECK(enhanced->rgba[i] == 50);
        CHECK(enhanced->rgba[i + 1] == 100);
        CHECK(enhanced->rgba[i + 2] == 200);
      }
    }
  }
  CHECK_FALSE(game::EnhanceSpritePixels({0, 1, {}}));
  CHECK_FALSE(game::EnhanceSpritePixels({-1, 1, {}}));
  CHECK_FALSE(game::EnhanceSpritePixels({513, 1, {}}));
  CHECK_FALSE(game::EnhanceSpritePixels({2, 2, {1, 2, 3, 4}}));
}

TEST_CASE("Transparent sprite borders cannot leak hidden RGB or develop rings",
          "[sprite-detail]") {
  auto source = Solid(7, 7, {255, 0, 255, 0});
  for (int y = 2; y <= 4; ++y) {
    for (int x = 2; x <= 4; ++x) {
      const auto i = static_cast<std::size_t>(y * 7 + x) * 4;
      source.rgba[i] = 20;
      source.rgba[i + 1] = 150;
      source.rgba[i + 2] = 240;
      source.rgba[i + 3] = 255;
    }
  }
  const auto enhanced = game::EnhanceSpritePixels(source);
  REQUIRE(enhanced);
  CHECK(game::EnhanceSpritePixels(source)->rgba == enhanced->rgba);
  for (int y = 0; y < 21; ++y) {
    for (int x = 0; x < 21; ++x) {
      const auto i = static_cast<std::size_t>(y * 21 + x) * 4;
      if (enhanced->rgba[i + 3] > 0) {
        CHECK(enhanced->rgba[i] == 20);
        CHECK(enhanced->rgba[i + 1] == 150);
        CHECK(enhanced->rgba[i + 2] == 240);
      }
      if (x < 5 || x > 15 || y < 5 || y > 15) {
        CHECK(enhanced->rgba[i + 3] == 0);
      }
    }
  }
}

TEST_CASE(
    "Enhanced sprite textures preserve placement fog and classic switching",
    "[sprite-detail]") {
  Target target;
  target.Clear();
  game::SpriteAsset asset;
  asset.tile_width = 4;
  asset.tile_height = 6;
  asset.frame_count = 1;
  auto source = std::make_shared<game::SpriteDetailSource>(
      Solid(4, 6, {90, 160, 210, 255}));
  game::SpriteFrame frame;
  frame.texture = SdlTexture::Create(target.renderer.get(), 4, 6, source->rgba);
  frame.white_silhouette = SdlTexture::Create(
      target.renderer.get(), 4, 6, Solid(4, 6, {255, 255, 255, 255}).rgba);
  frame.anchor_x = 1;
  frame.anchor_y = 4;
  frame.detail_source = source;
  asset.frames.push_back(std::move(frame));
  game::SpriteEnhancementCache cache;
  game::SpriteDrawOptions options;
  options.scale = 3;
  const auto draw = [&] {
    target.Clear();
    game::DrawSprite(
        target.renderer.get(), asset, 0, 0, 0, 0, 0, 80, 80, options);
    return target.Pixels();
  };
  const auto classic = draw();
  options.detail_cache = &cache;
  CHECK(draw() == classic);
  CHECK(cache.bytes() == source->rgba.size() * 9U * 2U);
  options.fog_amount = 32;
  options.fog_color = 0x00ff00;
  const auto enhanced_fog = draw();
  options.detail_cache = nullptr;
  CHECK(draw() == enhanced_fog);
  options.fog_amount = 0;
  CHECK(draw() == classic);
  options.detail_cache = &cache;
  options.white_silhouette = true;
  const auto enhanced_white = draw();
  options.detail_cache = nullptr;
  CHECK(draw() == enhanced_white);
}

TEST_CASE("Enhanced texture cache evicts frames within its memory budget",
          "[sprite-detail]") {
  Target target;
  target.Clear();
  const auto first = std::make_shared<game::SpriteDetailSource>(
      Solid(4, 4, {20, 60, 80, 255}));
  const auto second = std::make_shared<game::SpriteDetailSource>(
      Solid(4, 4, {60, 80, 20, 255}));
  const auto cost = first->rgba.size() * 9U * 2U;
  game::SpriteEnhancementCache cache(cost);
  const auto *a = cache.Get(target.renderer.get(), first);
  REQUIRE(a);
  CHECK(cache.Get(target.renderer.get(), first) == a);
  REQUIRE(cache.Get(target.renderer.get(), second));
  CHECK(cache.bytes() == cost);
  REQUIRE(cache.Get(target.renderer.get(), first));
  CHECK(cache.bytes() == cost);
  game::SpriteEnhancementCache too_small(cost - 1);
  CHECK_FALSE(too_small.Get(target.renderer.get(), first));
  CHECK(too_small.bytes() == 0);
  CHECK_FALSE(cache.Get(nullptr, first));
}

TEST_CASE("Reconstructed sprite packs match original content and reject "
          "damaged files",
          "[sprite-detail]") {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("evnova-sprite-pack-" + std::to_string(SDL_GetTicksNS()));
  std::filesystem::create_directories(directory);
  const auto source = Solid(1, 1, {10, 20, 30, 255});
  const auto path = directory / (game::SpriteDetailKey(source) + ".evs");
  const std::vector<Uint8> valid{'E', 'V', 'S', '1', 0,   4,   0,   4,
                                 120, 156, 227, 18,  145, 251, 207, 69,
                                 1,   6,   0,   104, 238, 19,  177};
  const auto write = [&](const std::vector<Uint8> &bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
  };
  CHECK_FALSE(game::LoadSpriteDetailPack(directory, source));
  write(valid);
  const auto loaded = game::LoadSpriteDetailPack(directory, source);
  REQUIRE(loaded);
  CHECK(loaded->width == 4);
  CHECK(loaded->height == 4);
  CHECK(loaded->rgba == Solid(4, 4, {10, 20, 30, 255}).rgba);
  auto replacement = source;
  replacement.rgba[0] = 20;
  CHECK_FALSE(game::LoadSpriteDetailPack(directory, replacement));
  auto damaged = valid;
  damaged.back() ^= 0xff;
  write(damaged);
  CHECK_FALSE(game::LoadSpriteDetailPack(directory, source));
  auto wrong_size = valid;
  wrong_size[5] = 8;
  write(wrong_size);
  CHECK_FALSE(game::LoadSpriteDetailPack(directory, source));
  write({'E', 'V', 'S'});
  CHECK_FALSE(game::LoadSpriteDetailPack(directory, source));
  std::filesystem::remove(path);
  std::filesystem::remove(directory);
}

// Optional visual review using the installed stock resources, isolated from
// normal assertions so missing game data does not make the unit suite fail.
TEST_CASE("Export original and enhanced stock sprite comparisons",
          "[.sprite-preview]") {
  Target target(960, 900);
  target.Clear();
  game::SpriteEnhancementCache cache;
  for (int row = 0; row < 3; ++row) {
    auto asset =
        row == 0
            ? game::SpriteAsset::LoadSheet(target.renderer.get(), 1000, true)
            : game::SpriteAsset::LoadSpin(target.renderer.get(),
                                          row == 1 ? 1000 : 1005);
    REQUIRE(asset);
    const float scale =
        std::min(6.0F,
                 250.0F / static_cast<float>(
                              std::max(asset->tile_width, asset->tile_height)));
    for (int column = 0; column < 2; ++column) {
      game::SpriteDrawOptions options;
      options.scale = scale;
      options.linear_scale = true;
      options.detail_cache = column == 1 ? &cache : nullptr;
      game::DrawSprite(target.renderer.get(),
                       *asset,
                       7,
                       static_cast<float>(column * 480 - 240),
                       static_cast<float>(row * 300 - 300),
                       0,
                       0,
                       960,
                       900,
                       options);
    }
  }
  REQUIRE(SDL_FlushRenderer(target.renderer.get()));
  std::filesystem::create_directories("analysis/sprite-detail");
  REQUIRE(SDL_SaveBMP(target.surface.get(),
                      "analysis/sprite-detail/comparison.bmp"));
}

TEST_CASE("Export source frames for offline sprite reconstruction",
          "[.sprite-export]") {
  const bool arpia = SDL_getenv("EVN_SPRITE_EXPORT_ARPIA") != nullptr;
  if (arpia) {
    const auto profile =
        std::filesystem::absolute("analysis/sprite-detail/export-profile");
    std::filesystem::create_directories(profile);
    NovaPaths::SetSupportDirectoryOverride(profile);
    std::ofstream(profile / "ARPIA2 enabled.txt") << 1;
    NovaPaths::InitializeCampaignSelection();
    REQUIRE(NovaPaths::ArpiaCampaignActive());
  }
  const auto directory =
      std::filesystem::path("analysis/sprite-detail/sources");
  std::filesystem::create_directories(directory);
  std::set<std::uint16_t> sheets;
  for (const auto &[type, id] : NovaResource_AllKeys()) {
    if (type == game::kShipVisualResourceType) {
      const auto data = NovaResource_Load(type, id);
      const auto def =
          data ? game::DecodeShipVisualDescriptor(*data) : std::nullopt;
      if (def) {
        sheets.insert(def->base_image_id);
        if (def->alt_image_id > 0) {
          sheets.insert(static_cast<std::uint16_t>(def->alt_image_id));
        }
      }
    } else if (type == kResourceTypeSprites && id >= 1000 && id < 3000) {
      const auto data = NovaResource_Load(type, id);
      const auto def = data ? NovaSpriteDefinition_Parse(*data) : std::nullopt;
      if (def) {
        sheets.insert(def->sprites_resource_id);
      }
    }
  }
  for (const auto id : sheets) {
    const auto data = NovaResource_Load(kResourceTypeRleSheet16, id);
    const auto sheet = data ? RleSpriteSheet_Decode16(*data) : std::nullopt;
    if (!sheet || sheet->width > game::kSpriteDetailMaxSize ||
        sheet->height > game::kSpriteDetailMaxSize) {
      continue;
    }
    for (std::size_t frame = 0; frame < sheet->frames.size(); ++frame) {
      const auto name = std::string(arpia ? "arpia-" : "stock-") +
                        std::to_string(id) + "-" + std::to_string(frame) +
                        ".rgba";
      std::ofstream out(directory / name, std::ios::binary);
      const std::array<std::uint32_t, 2> dimensions{
          static_cast<std::uint32_t>(sheet->width),
          static_cast<std::uint32_t>(sheet->height)};
      out.write(reinterpret_cast<const char *>(dimensions.data()), 8);
      out.write(reinterpret_cast<const char *>(
                    sheet->frames[frame].rgba_pixels.data()),
                static_cast<std::streamsize>(
                    sheet->frames[frame].rgba_pixels.size()));
      REQUIRE(out.good());
    }
  }
}
