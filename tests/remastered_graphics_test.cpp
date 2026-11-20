#include "game/remastered_graphics.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

namespace {
struct Target {
  std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{
      SDL_CreateSurface(128, 96, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface};
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{
      surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr,
      SDL_DestroyRenderer};

  void Clear() {
    REQUIRE(renderer);
    REQUIRE(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer.get()));
  }

  std::vector<Uint8> Pixels() {
    REQUIRE(SDL_FlushRenderer(renderer.get()));
    const auto *first = static_cast<const Uint8 *>(surface->pixels);
    return {first, first + surface->pitch * surface->h};
  }
};
} // namespace

TEST_CASE(
    "Remastered scenery is stable across travel and respects hidden starfields",
    "[remastered]") {
  Target target;
  game::RemasteredGraphics effects;
  target.Clear();
  const auto blank = target.Pixels();
  effects.DrawBackground(target.renderer.get(), 8, -1, 0, 0, 128, 96);
  CHECK(target.Pixels() == blank);

  effects.DrawBackground(target.renderer.get(), 8, 0, -300, 800, 128, 96);
  const auto first = target.Pixels();
  CHECK(first != blank);
  target.Clear();
  effects.DrawBackground(target.renderer.get(), 9, 0, -300, 800, 128, 96);
  CHECK(target.Pixels() != first);
  target.Clear();
  effects.DrawBackground(target.renderer.get(), 8, 0, -300, 800, 128, 96);
  CHECK(target.Pixels() == first);
  target.Clear();
  effects.DrawBackground(target.renderer.get(), 8, 0, 500, -600, 128, 96);
  CHECK(target.Pixels() != first);
}

TEST_CASE("Remastered bloom cannot expose transparent or fully fogged sprites",
          "[remastered]") {
  Target target;
  target.Clear();
  const auto blank = target.Pixels();
  game::SpriteAsset asset;
  asset.tile_width = asset.tile_height = 8;
  asset.frame_count = 1;
  game::SpriteFrame frame;
  const std::vector<Uint8> pixels(8 * 8 * 4, 255);
  frame.texture = SdlTexture::Create(target.renderer.get(), 8, 8, pixels);
  REQUIRE(frame.texture);
  frame.anchor_x = frame.anchor_y = 4;
  asset.frames.push_back(std::move(frame));
  game::SpriteDrawOptions options;
  options.alpha_mod = 0;
  const auto draw = [&] {
    game::DrawRemasteredBloom(
        target.renderer.get(), asset, 0, 0, 0, 0, 0, 128, 96, options, 0.85F);
  };
  draw();
  CHECK(target.Pixels() == blank);
  options.alpha_mod = 1;
  options.fog_amount = 32;
  draw();
  CHECK(target.Pixels() == blank);
  options.fog_amount = 0;
  draw();
  CHECK(target.Pixels() != blank);
}
