#pragma once

#include "../sdl_platform.hpp"
#include "sprite_world.hpp"

#include <cstdint>
#include <memory>

namespace game {
// Optional presentation layer, independent of the original simulation/RNG.
class RemasteredGraphics {
public:
  void DrawBackground(SDL_Renderer *renderer,
                      int system_id,
                      int murk,
                      float camera_x,
                      float camera_y,
                      int width,
                      int height,
                      std::uint32_t space_color = 0);

private:
  void EnsureTextures(SDL_Renderer *renderer,
                      int system_id,
                      std::uint32_t space_color);
  std::unique_ptr<SdlTexture> nebula_;
  std::unique_ptr<SdlTexture> glow_;
  int system_id_ = -1;
  std::uint32_t space_color_ = 0;
  bool textures_attempted_ = false;
};

// Soft additive halo from the actual artwork. Preserves its anchor, tint,
// cloak opacity and fog; used only at already-visible sprite draw sites.
void DrawRemasteredBloom(SDL_Renderer *renderer,
                         const SpriteAsset &asset,
                         int frame,
                         float x,
                         float y,
                         float camera_x,
                         float camera_y,
                         int width,
                         int height,
                         const SpriteDrawOptions &options,
                         float strength);

} // namespace game
