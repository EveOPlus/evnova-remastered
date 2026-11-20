#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace game {
enum class RadarIcon { ship, player, planet, station, gate, disabled };

inline SDL_Color RadarIconColor(SDL_Color color) {
  const auto peak = std::max({color.r, color.g, color.b});
  if (peak == 0)
    return {150, 150, 150, 255};
  const float gain = std::max(1.0F, 170.0F / static_cast<float>(peak));
  return {static_cast<Uint8>(std::min(255.0F, color.r * gain)),
          static_cast<Uint8>(std::min(255.0F, color.g * gain)),
          static_cast<Uint8>(std::min(255.0F, color.b * gain)),
          255};
}

// Geometry strokes stay visible below 1x HUD scale, where the SDL software
// renderer can round its integer line width down to zero.
inline void
DrawRadarLine(SDL_Renderer *renderer, float x1, float y1, float x2, float y2) {
  const float length = std::hypot(x2 - x1, y2 - y1);
  if (length < .01F)
    return;
  float scale_x = 1;
  float scale_y = 1;
  SDL_GetRenderScale(renderer, &scale_x, &scale_y);
  const float half_width = .75F / std::min({1.0F, scale_x, scale_y});
  const float nx = (y1 - y2) * half_width / length;
  const float ny = (x2 - x1) * half_width / length;
  SDL_FColor color{};
  SDL_GetRenderDrawColorFloat(renderer, &color.r, &color.g, &color.b, &color.a);
  const std::array<SDL_Vertex, 4> vertices{{{{x1 + nx, y1 + ny}, color, {}},
                                            {{x2 + nx, y2 + ny}, color, {}},
                                            {{x2 - nx, y2 - ny}, color, {}},
                                            {{x1 - nx, y1 - ny}, color, {}}}};
  constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer,
                     nullptr,
                     vertices.data(),
                     static_cast<int>(vertices.size()),
                     indices.data(),
                     static_cast<int>(indices.size()));
}

inline void DrawRadarLines(SDL_Renderer *renderer,
                           std::span<const SDL_FPoint> points) {
  for (std::size_t i = 1; i < points.size(); ++i)
    DrawRadarLine(
        renderer, points[i - 1].x, points[i - 1].y, points[i].x, points[i].y);
}

inline void
DrawRadarRing(SDL_Renderer *renderer, float x, float y, float radius) {
  std::array<SDL_FPoint, 33> points{};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const float angle = static_cast<float>(i) * 6.2831853F / 32.0F;
    points[i] = {x + std::cos(angle) * radius, y + std::sin(angle) * radius};
  }
  DrawRadarLines(renderer, points);
}

// Remastered radar symbols retain the original sensor gates and colour meaning.
inline void DrawRadarIcon(SDL_Renderer *renderer,
                          RadarIcon kind,
                          float x,
                          float y,
                          float radius,
                          float heading,
                          SDL_Color color,
                          bool selected) {
  color = RadarIconColor(color);
  SDL_SetRenderDrawColor(renderer, 3, 5, 7, 255);
  const SDL_FRect halo{
      x - radius - 1, y - radius - 1, radius * 2 + 2, radius * 2 + 2};
  SDL_RenderFillRect(renderer, &halo);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
  if (kind == RadarIcon::ship || kind == RadarIcon::player) {
    const float sn = std::sin(heading);
    const float cs = std::cos(heading);
    const auto point = [&](float sx, float sy) {
      return SDL_FPoint{x + cs * sx - sn * sy, y + sn * sx + cs * sy};
    };
    const std::array<SDL_FPoint, 5> shape{{point(0, -radius),
                                           point(radius * .72F, radius * .7F),
                                           point(0, radius * .28F),
                                           point(-radius * .72F, radius * .7F),
                                           point(0, -radius)}};
    DrawRadarLines(renderer, shape);
    if (kind == RadarIcon::player) {
      const auto tip = point(0, -radius * .5F);
      const auto tail = point(0, radius * .25F);
      DrawRadarLine(renderer, tip.x, tip.y, tail.x, tail.y);
    }
  } else if (kind == RadarIcon::planet || kind == RadarIcon::gate) {
    DrawRadarRing(renderer, x, y, radius);
    if (kind == RadarIcon::gate) {
      DrawRadarRing(renderer, x, y, radius * .5F);
    } else {
      DrawRadarLine(renderer, x - radius + 1, y, x + radius - 1, y);
      DrawRadarLine(renderer, x, y - radius + 1, x, y + radius - 1);
    }
  } else if (kind == RadarIcon::station) {
    const std::array<SDL_FPoint, 5> shape{{{x, y - radius},
                                           {x + radius, y},
                                           {x, y + radius},
                                           {x - radius, y},
                                           {x, y - radius}}};
    DrawRadarLines(renderer, shape);
    const SDL_FRect core{x - 1, y - 1, 2, 2};
    SDL_RenderFillRect(renderer, &core);
  } else {
    DrawRadarLine(renderer,
                  x - radius * .6F,
                  y - radius * .6F,
                  x + radius * .6F,
                  y + radius * .6F);
    DrawRadarLine(renderer,
                  x - radius * .6F,
                  y + radius * .6F,
                  x + radius * .6F,
                  y - radius * .6F);
  }
  if (selected) {
    const float r = radius + 3;
    for (const float dx : {-1.0F, 1.0F}) {
      for (const float dy : {-1.0F, 1.0F}) {
        DrawRadarLine(
            renderer, x + dx * r, y + dy * r, x + dx * (r - 2), y + dy * r);
        DrawRadarLine(
            renderer, x + dx * r, y + dy * r, x + dx * r, y + dy * (r - 2));
      }
    }
  }
}
} // namespace game
