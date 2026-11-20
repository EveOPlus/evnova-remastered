#pragma once

#include "../sdl_platform.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace game {

// Original decoded pixels, independent of the sprite's world-space geometry.
struct SpriteDetailSource {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

inline constexpr int kSpriteDetailScale = 3;
inline constexpr int kSpriteDetailMaxSize = 512;

// Conservative reconstruction of the existing artwork, not new art or masks.
[[nodiscard]] std::optional<SpriteDetailSource>
EnhanceSpritePixels(const SpriteDetailSource &source);

[[nodiscard]] std::string SpriteDetailKey(const SpriteDetailSource &source);
[[nodiscard]] std::optional<SpriteDetailSource>
LoadSpriteDetailPack(const std::filesystem::path &directory,
                     const SpriteDetailSource &source);

struct EnhancedSpriteTextures {
  std::unique_ptr<SdlTexture> color;
  std::unique_ptr<SdlTexture> silhouette;
};

// Owned by a flight view (and its renderer). Only visible frames are generated.
class SpriteEnhancementCache {
public:
  explicit SpriteEnhancementCache(std::size_t budget_bytes = 96U * 1024U *
                                                             1024U)
      : budget_bytes_(budget_bytes) {}

  [[nodiscard]] const EnhancedSpriteTextures *
  Get(SDL_Renderer *renderer,
      const std::shared_ptr<const SpriteDetailSource> &source);

  [[nodiscard]] std::size_t bytes() const { return bytes_; }

private:
  struct Entry {
    std::shared_ptr<const SpriteDetailSource> source;
    EnhancedSpriteTextures textures;
    std::size_t bytes = 0;
    std::uint64_t last_use = 0;
  };

  std::map<const SpriteDetailSource *, Entry> entries_;
  std::size_t budget_bytes_;
  std::size_t bytes_ = 0;
  std::uint64_t clock_ = 0;
  bool logged_pack_ = false;
};

} // namespace game
