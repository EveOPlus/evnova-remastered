#include "remastered_graphics.hpp"

#include "../log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace game {
namespace {

// Stateless visual noise: never consumes GameState's random stream.
std::uint32_t Hash(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  return value ^ (value >> 16U);
}

float Random(std::uint32_t value) {
  return static_cast<float>(Hash(value) & 0xffffU) / 65535.0F;
}

float Noise(float x, float y, std::uint32_t seed) {
  const int ix = static_cast<int>(std::floor(x));
  const int iy = static_cast<int>(std::floor(y));
  float fx = x - static_cast<float>(ix);
  float fy = y - static_cast<float>(iy);
  fx = fx * fx * (3.0F - 2.0F * fx);
  fy = fy * fy * (3.0F - 2.0F * fy);
  const auto sample = [seed](int xx, int yy) {
    return Random(static_cast<std::uint32_t>(xx) * 73856093U ^
                  static_cast<std::uint32_t>(yy) * 19349663U ^ seed);
  };
  return std::lerp(std::lerp(sample(ix, iy), sample(ix + 1, iy), fx),
                   std::lerp(sample(ix, iy + 1), sample(ix + 1, iy + 1), fx),
                   fy);
}

float Clouds(float x, float y, std::uint32_t seed) {
  float result = 0.0F;
  float weight = 0.5F;
  for (int octave = 0; octave < 5; ++octave) {
    result += weight * Noise(x, y, seed);
    x = x * 2.03F + 13.1F;
    y = y * 2.03F + 7.7F;
    weight *= 0.5F;
  }
  return result;
}

std::uint8_t Byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 255.0F));
}

void Glow(SDL_Renderer *renderer,
          SDL_Texture *texture,
          float x,
          float y,
          float radius,
          SDL_Color color) {
  SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texture, color.a);
  const SDL_FRect rect{x - radius, y - radius, radius * 2.0F, radius * 2.0F};
  SDL_RenderTexture(renderer, texture, nullptr, &rect);
}

} // namespace

void RemasteredGraphics::EnsureTextures(SDL_Renderer *renderer,
                                        int system_id,
                                        std::uint32_t space_color) {
  if (textures_attempted_ && system_id_ == system_id &&
      space_color_ == space_color) {
    return;
  }
  system_id_ = system_id;
  space_color_ = space_color;
  textures_attempted_ = true;
  if (!glow_) {
    constexpr int size = 64;
    std::vector<std::uint8_t> pixels(size * size * 4);
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const float dx = (static_cast<float>(x) + 0.5F - 32.0F) / 32.0F;
        const float dy = (static_cast<float>(y) + 0.5F - 32.0F) / 32.0F;
        const float falloff = std::max(0.0F, 1.0F - dx * dx - dy * dy);
        const auto offset = static_cast<std::size_t>((y * size + x) * 4);
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = 255;
        pixels[offset + 3] = Byte(255.0F * std::pow(falloff, 5.0F));
      }
    }
    glow_ = SdlTexture::Create(renderer, size, size, pixels);
    if (glow_) {
      SDL_SetTextureBlendMode(glow_->get(), SDL_BLENDMODE_ADD);
      SDL_SetTextureScaleMode(glow_->get(), SDL_SCALEMODE_LINEAR);
    }
  }
  constexpr int width = 512, height = 320;
  const auto seed = Hash(static_cast<std::uint32_t>(system_id) + 61U);
  // Retain the scenario's space palette. Untinted space gets a restrained
  // blue-grey haze, rather than assigning new faction/region art themes.
  std::array<float, 3> tint{24.0F, 42.0F, 62.0F};
  if (space_color != 0) {
    tint = {static_cast<float>((space_color >> 16U) & 255U),
            static_cast<float>((space_color >> 8U) & 255U),
            static_cast<float>(space_color & 255U)};
    const float peak = std::max({tint[0], tint[1], tint[2], 1.0F});
    for (float &channel : tint)
      channel *= 70.0F / peak;
  }
  std::vector<std::uint8_t> pixels(width * height * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(width);
      const float v = static_cast<float>(y) / static_cast<float>(height);
      const float warp = Clouds(u * 4.0F, v * 3.0F, seed + 1U);
      const float cloud = Clouds(u * 6.0F + warp * 2.5F, v * 4.0F, seed);
      const float band =
          std::max(0.0F, 1.0F - std::abs(v - u * 0.45F - 0.25F) * 2.3F);
      const float density =
          std::pow(std::max(0.0F, cloud - 0.28F) * 1.5F, 1.8F) * band;
      const float dust = 1.0F - 0.7F * Clouds(u * 14.0F, v * 9.0F, seed + 7U);
      const auto offset = static_cast<std::size_t>((y * width + x) * 4);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        pixels[offset + channel] =
            Byte((2.0F + tint[channel] * density) * dust);
      }
      pixels[offset + 3] = 255;
    }
  }
  nebula_ = SdlTexture::Create(renderer, width, height, pixels);
  if (nebula_) {
    SDL_SetTextureBlendMode(nebula_->get(), SDL_BLENDMODE_ADD);
    SDL_SetTextureScaleMode(nebula_->get(), SDL_SCALEMODE_LINEAR);
  } else {
    NovaLog::Warn("remastered background texture failed in system {}: {}",
                  system_id,
                  SDL_GetError());
  }
}

void RemasteredGraphics::DrawBackground(SDL_Renderer *renderer,
                                        int system_id,
                                        int murk,
                                        float camera_x,
                                        float camera_y,
                                        int width,
                                        int height,
                                        std::uint32_t space_color) {
  if (murk < 0 || width <= 0 || height <= 0) {
    return;
  }
  EnsureTextures(renderer, system_id, space_color);
  const float haze =
      1.0F - static_cast<float>(std::clamp(murk, 0, 100)) * 0.008F;
  if (nebula_) {
    SDL_SetTextureAlphaMod(nebula_->get(), Byte(255.0F * haze));
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const SDL_FRect rect{-w * (0.12F + 0.035F * std::sin(camera_x * 0.0001F)),
                         -h * (0.12F + 0.035F * std::sin(camera_y * 0.0001F)),
                         w * 1.24F,
                         h * 1.24F};
    SDL_RenderTexture(renderer, nebula_->get(), nullptr, &rect);
  }
  if (!glow_) {
    return;
  }
  float scale_x = 1.0F, scale_y = 1.0F;
  SDL_GetRenderScale(renderer, &scale_x, &scale_y);
  const float scale = std::max(0.1F, std::min(scale_x, scale_y));
  SDL_BlendMode previous_blend;
  SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  constexpr std::array<float, 3> depths{0.025F, 0.075F, 0.17F};
  for (std::size_t layer = 0; layer < depths.size(); ++layer) {
    const float cell = (layer == 0 ? 56.0F : 100.0F) / scale;
    const float ox = camera_x * depths[layer], oy = camera_y * depths[layer];
    const int left = static_cast<int>(std::floor(ox / cell));
    const int top = static_cast<int>(std::floor(oy / cell));
    const int columns = static_cast<int>(static_cast<float>(width) / cell) + 2;
    const int rows = static_cast<int>(static_cast<float>(height) / cell) + 2;
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) {
        const int gx = left + column, gy = top + row;
        const auto seed = Hash(static_cast<std::uint32_t>(gx) * 73856093U ^
                               static_cast<std::uint32_t>(gy) * 19349663U ^
                               static_cast<std::uint32_t>(
                                   system_id * 3 + static_cast<int>(layer)));
        const float x = (static_cast<float>(gx) + Random(seed)) * cell - ox;
        const float y =
            (static_cast<float>(gy) + Random(seed + 1U)) * cell - oy;
        const bool warm = (seed % 7U) == 0;
        const float brightness = (70.0F + Random(seed + 2U) * 140.0F) * haze;
        SDL_Color color{static_cast<Uint8>(warm ? 255 : 170),
                        static_cast<Uint8>(warm ? 211 : 208),
                        static_cast<Uint8>(warm ? 167 : 255),
                        Byte(brightness)};
        const float radius = (layer == 2 ? 3.2F : 1.65F) / scale;
        Glow(renderer, glow_->get(), x, y, radius, color);
        if (layer == 2 && (seed % 11U) == 0) {
          color.a = Byte(brightness * 0.2F);
          Glow(renderer, glow_->get(), x, y, 11.0F / scale, color);
          SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
          SDL_RenderLine(renderer, x - 4.0F / scale, y, x + 4.0F / scale, y);
          SDL_RenderLine(renderer, x, y - 4.0F / scale, x, y + 4.0F / scale);
        }
      }
    }
  }
  SDL_SetRenderDrawBlendMode(renderer, previous_blend);
}

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
                         float strength) {
  SpriteDrawOptions glow = options;
  glow.additive = true;
  glow.linear_scale = true;
  glow.alpha_mod *= strength / 12.0F;
  constexpr std::array<SDL_FPoint, 6> offsets{{{1.0F, 0.0F},
                                               {-1.0F, 0.0F},
                                               {0.5F, 0.866F},
                                               {-0.5F, 0.866F},
                                               {0.5F, -0.866F},
                                               {-0.5F, -0.866F}}};
  const float radius = std::clamp(
      static_cast<float>(std::max(asset.tile_width, asset.tile_height)) * 0.06F,
      2.5F,
      10.0F);
  for (const float spread : {0.5F, 1.0F}) {
    for (const auto offset : offsets) {
      DrawSprite(renderer,
                 asset,
                 frame,
                 x + offset.x * radius * spread,
                 y + offset.y * radius * spread,
                 camera_x,
                 camera_y,
                 width,
                 height,
                 glow);
    }
  }
}

} // namespace game
