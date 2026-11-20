#include "sprite_enhancement.hpp"

#include "../log.hpp"
#include "../nova_paths.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace game {
namespace {
using Pixel = std::array<float, 4>;

bool ValidSource(const SpriteDetailSource &source) {
  return source.width > 0 && source.height > 0 &&
         source.width <= kSpriteDetailMaxSize &&
         source.height <= kSpriteDetailMaxSize &&
         source.rgba.size() == static_cast<std::size_t>(source.width) *
                                   static_cast<std::size_t>(source.height) * 4U;
}

std::size_t PixelIndex(int x, int y, int width) {
  return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x);
}

// Catmull-Rom weights at texel centres. The bounded reconstruction below
// suppresses cubic ringing at transparent silhouettes and hard colour edges.
std::array<float, 4> CubicWeights(float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  return {-0.5F * t + t2 - 0.5F * t3,
          1.0F - 2.5F * t2 + 1.5F * t3,
          0.5F * t + 2.0F * t2 - 1.5F * t3,
          -0.5F * t2 + 0.5F * t3};
}

std::uint8_t Byte(float value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

std::vector<Pixel> SharpenInterior(const SpriteDetailSource &source) {
  std::vector<Pixel> pixels(source.rgba.size() / 4U);
  const auto sample = [&](int x, int y, std::size_t channel) {
    const auto index = PixelIndex(std::clamp(x, 0, source.width - 1),
                                  std::clamp(y, 0, source.height - 1),
                                  source.width);
    return static_cast<float>(source.rgba[index * 4U + channel]);
  };
  constexpr std::array<float, 3> weights{1.0F, 2.0F, 1.0F};
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      Pixel blur{};
      bool opaque = true;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          opaque = opaque && sample(x + dx, y + dy, 3) == 255.0F;
          const float weight = weights[static_cast<std::size_t>(dx + 1)] *
                               weights[static_cast<std::size_t>(dy + 1)] /
                               16.0F;
          for (std::size_t c = 0; c < 3; ++c) {
            blur[c] += weight * sample(x + dx, y + dy, c);
          }
        }
      }
      auto &pixel = pixels[PixelIndex(x, y, source.width)];
      for (std::size_t c = 0; c < 3; ++c) {
        const float original = sample(x, y, c);
        pixel[c] = std::clamp(
            original + (opaque ? 0.25F * (original - blur[c]) : 0.0F),
            0.0F,
            255.0F);
      }
      pixel[3] = sample(x, y, 3);
    }
  }
  return pixels;
}
} // namespace

std::optional<SpriteDetailSource>
EnhanceSpritePixels(const SpriteDetailSource &source) {
  if (!ValidSource(source)) {
    return std::nullopt;
  }
  const auto pixels = SharpenInterior(source);
  const auto sample = [&](int x, int y) -> const Pixel & {
    return pixels[PixelIndex(std::clamp(x, 0, source.width - 1),
                             std::clamp(y, 0, source.height - 1),
                             source.width)];
  };
  SpriteDetailSource result;
  result.width = source.width * kSpriteDetailScale;
  result.height = source.height * kSpriteDetailScale;
  result.rgba.resize(static_cast<std::size_t>(result.width) *
                     static_cast<std::size_t>(result.height) * 4U);
  for (int y = 0; y < result.height; ++y) {
    const float sy = (static_cast<float>(y) + 0.5F) / kSpriteDetailScale - 0.5F;
    const int iy = static_cast<int>(std::floor(sy));
    const auto wy = CubicWeights(sy - static_cast<float>(iy));
    for (int x = 0; x < result.width; ++x) {
      const float sx =
          (static_cast<float>(x) + 0.5F) / kSpriteDetailScale - 0.5F;
      const int ix = static_cast<int>(std::floor(sx));
      const auto wx = CubicWeights(sx - static_cast<float>(ix));
      Pixel sum{};
      Pixel lower{255.0F, 255.0F, 255.0F, 255.0F};
      Pixel upper{};
      bool visible = false;
      for (int dy = 0; dy < 4; ++dy) {
        for (int dx = 0; dx < 4; ++dx) {
          const auto &p = sample(ix + dx - 1, iy + dy - 1);
          const float weight = wx[static_cast<std::size_t>(dx)] *
                               wy[static_cast<std::size_t>(dy)];
          for (std::size_t c = 0; c < 3; ++c) {
            sum[c] += weight * p[c] * p[3];
          }
          sum[3] += weight * p[3];
          if (dx >= 1 && dx <= 2 && dy >= 1 && dy <= 2) {
            lower[3] = std::min(lower[3], p[3]);
            upper[3] = std::max(upper[3], p[3]);
            if (p[3] > 0.0F) {
              visible = true;
              for (std::size_t c = 0; c < 3; ++c) {
                lower[c] = std::min(lower[c], p[c]);
                upper[c] = std::max(upper[c], p[c]);
              }
            }
          }
        }
      }
      const auto out = PixelIndex(x, y, result.width) * 4U;
      const float alpha = std::clamp(sum[3], lower[3], upper[3]);
      for (std::size_t c = 0; c < 3; ++c) {
        // Interpolate premultiplied colour, then return straight RGBA for SDL.
        // Hidden source RGB never contaminates the visible boundary.
        result.rgba[out + c] =
            visible
                ? Byte(std::clamp(sum[3] > 0.001F ? sum[c] / sum[3] : lower[c],
                                  lower[c],
                                  upper[c]))
                : 0;
      }
      result.rgba[out + 3] = Byte(alpha);
    }
  }
  return result;
}

std::string SpriteDetailKey(const SpriteDetailSource &source) {
  // Content-addressed, so a plug-in can never inherit the stock resource's art
  // merely by reusing its ID. Two checksums plus dimensions key this cache;
  // this is an asset lookup, not a security or authentication hash.
  const auto count = static_cast<uInt>(source.rgba.size());
  return fmt::format("{:08x}{:08x}-{}x{}",
                     crc32(0, source.rgba.data(), count),
                     adler32(1, source.rgba.data(), count),
                     source.width,
                     source.height);
}

std::optional<SpriteDetailSource>
LoadSpriteDetailPack(const std::filesystem::path &directory,
                     const SpriteDetailSource &source) {
  if (!ValidSource(source)) {
    return std::nullopt;
  }
  const auto path = directory / (SpriteDetailKey(source) + ".evs");
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  const std::size_t expected = source.rgba.size() * 16U;
  if (!file) {
    return std::nullopt;
  }
  const auto size = static_cast<std::streamoff>(file.tellg());
  if (size < 8 || static_cast<std::uint64_t>(size) >
                      compressBound(static_cast<uLong>(expected)) + 8U) {
    NovaLog::Warn("sprite detail pack: invalid file size for {}",
                  path.string());
    return std::nullopt;
  }
  file.seekg(0);
  std::vector<std::uint8_t> packed(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char *>(packed.data()),
            static_cast<std::streamsize>(size));
  const int width = static_cast<int>(packed[4]) * 256 + packed[5];
  const int height = static_cast<int>(packed[6]) * 256 + packed[7];
  if (!file || packed[0] != 'E' || packed[1] != 'V' || packed[2] != 'S' ||
      packed[3] != '1' || width != source.width * 4 ||
      height != source.height * 4) {
    NovaLog::Warn("sprite detail pack: invalid header for {}", path.string());
    return std::nullopt;
  }
  SpriteDetailSource result{width, height, std::vector<std::uint8_t>(expected)};
  auto length = static_cast<uLongf>(expected);
  if (uncompress(result.rgba.data(),
                 &length,
                 packed.data() + 8,
                 static_cast<uLong>(packed.size() - 8)) != Z_OK ||
      length != expected) {
    NovaLog::Warn("sprite detail pack: damaged pixels in {}", path.string());
    return std::nullopt;
  }
  return result;
}

const EnhancedSpriteTextures *SpriteEnhancementCache::Get(
    SDL_Renderer *renderer,
    const std::shared_ptr<const SpriteDetailSource> &source) {
  if (!renderer || !source || !ValidSource(*source)) {
    return nullptr;
  }
  if (const auto it = entries_.find(source.get()); it != entries_.end()) {
    it->second.last_use = ++clock_;
    return it->second.textures.color ? &it->second.textures : nullptr;
  }
  if (source->rgba.size() * kSpriteDetailScale * kSpriteDetailScale * 2U >
      budget_bytes_) {
    return nullptr;
  }
  std::optional<SpriteDetailSource> enhanced;
  if (const auto root = NovaPaths::InstallRoot()) {
    enhanced = LoadSpriteDetailPack(*root / "Remastered Sprites", *source);
    if (enhanced && !logged_pack_) {
      NovaLog::Info(
          "sprite detail pack: using reconstructed 4x original artwork");
      logged_pack_ = true;
    }
  }
  if (!enhanced) {
    enhanced = EnhanceSpritePixels(*source);
  }
  const std::size_t required = enhanced->rgba.size() * 2U;
  if (required > budget_bytes_) {
    return nullptr;
  }
  while (bytes_ + required > budget_bytes_) {
    const auto oldest = std::min_element(
        entries_.begin(), entries_.end(), [](const auto &a, const auto &b) {
          return a.second.last_use < b.second.last_use;
        });
    bytes_ -= oldest->second.bytes;
    entries_.erase(oldest);
  }
  Entry entry;
  entry.source = source;
  entry.last_use = ++clock_;
  entry.textures.color = SdlTexture::Create(
      renderer, enhanced->width, enhanced->height, enhanced->rgba);
  for (std::size_t p = 0; p < enhanced->rgba.size(); p += 4) {
    enhanced->rgba[p] = 255;
    enhanced->rgba[p + 1] = 255;
    enhanced->rgba[p + 2] = 255;
  }
  entry.textures.silhouette = SdlTexture::Create(
      renderer, enhanced->width, enhanced->height, enhanced->rgba);
  if (!entry.textures.color || !entry.textures.silhouette) {
    NovaLog::Warn("sprite enhancement: {}x{} upload failed: {}",
                  source->width,
                  source->height,
                  SDL_GetError());
    entry.textures = {};
  } else {
    SDL_SetTextureScaleMode(entry.textures.color->get(), SDL_SCALEMODE_LINEAR);
    SDL_SetTextureScaleMode(entry.textures.silhouette->get(),
                            SDL_SCALEMODE_LINEAR);
    entry.bytes = required;
    bytes_ += required;
  }
  auto [it, inserted] = entries_.emplace(source.get(), std::move(entry));
  (void)inserted;
  return it->second.textures.color ? &it->second.textures : nullptr;
}
} // namespace game
