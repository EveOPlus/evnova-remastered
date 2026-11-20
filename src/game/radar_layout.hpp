#pragma once

#include "gameplay_interface.hpp"
#include "radar_icons.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace game {

// Remake presentation only: enlarge the original upper cockpit section and
// move its remaining panels down, retaining the government-specific artwork.
inline constexpr float kRadarHudExpansion = 56.0F;

inline float RadarHudScale(SDL_FPoint window,
                           float requested,
                           bool remastered,
                           float art_width = kGameplayHudStripWidth,
                           float art_height = kGameplayHudStripHeight) {
  const float extra = remastered ? kRadarHudExpansion : 0;
  return std::min({requested,
                   window.x / (art_width + extra),
                   window.y / (art_height + extra)});
}

struct RadarContact {
  SDL_FPoint anchor{};
  SDL_FPoint position{};
  RadarIcon icon = RadarIcon::ship;
  float radius = 3;
  float heading = 0;
  SDL_Color color{};
  bool selected = false;
  bool placed = false;

  [[nodiscard]] float extent() const { return radius + (selected ? 4 : 2); }

  [[nodiscard]] int priority() const {
    if (icon == RadarIcon::player)
      return 0;
    if (selected)
      return 1;
    if (icon == RadarIcon::planet || icon == RadarIcon::station ||
        icon == RadarIcon::gate)
      return 2;
    return 3;
  }
};

// Stable priority/order and bounded square-ring search keep all symbols apart,
// including their opaque halos and selection brackets. Only on-panel anchors
// are admitted; moving a symbol never reveals an off-screen contact.
inline void LayoutRadarContacts(std::span<RadarContact> contacts,
                                SDL_FRect bounds) {
  std::stable_sort(
      contacts.begin(), contacts.end(), [](const auto &a, const auto &b) {
        return a.priority() < b.priority();
      });
  for (std::size_t i = 0; i < contacts.size(); ++i) {
    auto &contact = contacts[i];
    contact.placed = false;
    const float extent = contact.extent();
    if (contact.anchor.x < bounds.x || contact.anchor.y < bounds.y ||
        contact.anchor.x >= bounds.x + bounds.w ||
        contact.anchor.y >= bounds.y + bounds.h || bounds.w < 2 * extent ||
        bounds.h < 2 * extent)
      continue;
    const SDL_FPoint origin{
        std::clamp(
            contact.anchor.x, bounds.x + extent, bounds.x + bounds.w - extent),
        std::clamp(
            contact.anchor.y, bounds.y + extent, bounds.y + bounds.h - extent)};
    const auto available = [&](SDL_FPoint p) {
      if (p.x - extent < bounds.x || p.y - extent < bounds.y ||
          p.x + extent > bounds.x + bounds.w ||
          p.y + extent > bounds.y + bounds.h)
        return false;
      for (std::size_t j = 0; j < i; ++j) {
        const auto &other = contacts[j];
        if (other.placed &&
            std::abs(p.x - other.position.x) < extent + other.extent() &&
            std::abs(p.y - other.position.y) < extent + other.extent())
          return false;
      }
      return true;
    };
    if (available(origin)) {
      contact.position = origin;
      contact.placed = true;
      continue;
    }
    constexpr float step = 4;
    const int rings =
        static_cast<int>(std::ceil(std::max(bounds.w, bounds.h) / step));
    for (int ring = 1; ring <= rings && !contact.placed; ++ring) {
      float best_distance = 0;
      const auto consider = [&](int dx, int dy) {
        const SDL_FPoint candidate{origin.x + static_cast<float>(dx) * step,
                                   origin.y + static_cast<float>(dy) * step};
        const float distance = static_cast<float>(dx * dx + dy * dy);
        if ((!contact.placed || distance < best_distance) &&
            available(candidate)) {
          contact.position = candidate;
          contact.placed = true;
          best_distance = distance;
        }
      };
      for (int d = -ring; d <= ring; ++d) {
        consider(d, -ring);
        consider(d, ring);
        if (d != -ring && d != ring) {
          consider(-ring, d);
          consider(ring, d);
        }
      }
    }
  }
}

} // namespace game
