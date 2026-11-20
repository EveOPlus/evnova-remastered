#include "mouse_controls.hpp"

#include "../sdl_platform.hpp"
#include "game_state.hpp"
#include "nova_font.hpp"
#include "ship_ai.hpp"
#include "spaceflight_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace game {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kInner = 43.0F;
constexpr float kOuter = 143.0F;
constexpr float kLabelRadius = 95.0F;

struct Choice {
  RadialAction action;
  const char *name;
  const char *label;
};

std::array<Choice, 4> Choices(MouseTargetKind kind) {
  if (kind == MouseTargetKind::asteroid) {
    return {{{RadialAction::mine, "radial.mine", "AUTO-MINE"},
             {RadialAction::map, "radial.map", "GALAXY MAP"},
             {RadialAction::cancel, "radial.cancel", "CANCEL"},
             {RadialAction::markets, "radial.markets", "SYSTEM MARKETS"}}};
  }
  if (kind == MouseTargetKind::ship) {
    return {{{RadialAction::target, "radial.target", "TARGET"},
             {RadialAction::hail, "radial.hail", "HAIL"},
             {RadialAction::board, "radial.board", "BOARD"},
             {RadialAction::cancel, "radial.cancel", "CANCEL"}}};
  }
  if (kind == MouseTargetKind::stellar) {
    return {{{RadialAction::target, "radial.target", "TARGET"},
             {RadialAction::hail, "radial.hail", "HAIL"},
             {RadialAction::dock, "radial.dock", "AUTO DOCK"},
             {RadialAction::markets, "radial.markets", "SYSTEM MARKETS"}}};
  }
  return {{{RadialAction::map, "radial.map", "GALAXY MAP"},
           {RadialAction::missions, "radial.missions", "MISSIONS"},
           {RadialAction::dock, "radial.dock", "NEAREST PORT"},
           {RadialAction::markets, "radial.markets", "SYSTEM MARKETS"}}};
}

SDL_FPoint Polar(SDL_FPoint center, float radius, float angle) {
  return {center.x + std::sin(angle) * radius,
          center.y - std::cos(angle) * radius};
}

void DrawSector(SDL_Renderer *renderer,
                SDL_FPoint center,
                int sector,
                bool highlighted) {
  constexpr int segments = 24;
  constexpr float gap = 0.035F;
  const float start =
      static_cast<float>(sector) * kPi / 2.0F - kPi / 4.0F + gap;
  const float span = kPi / 2.0F - 2.0F * gap;
  const SDL_FColor fill = highlighted
                              ? SDL_FColor{0.09F, 0.44F, 0.52F, 0.97F}
                              : SDL_FColor{0.035F, 0.075F, 0.11F, 0.97F};
  std::vector<SDL_Vertex> vertices;
  std::vector<int> indices;
  for (int i = 0; i <= segments; ++i) {
    const float angle = start + span * static_cast<float>(i) / segments;
    vertices.push_back({Polar(center, kInner, angle), fill, {}});
    vertices.push_back({Polar(center, kOuter, angle), fill, {}});
    if (i < segments) {
      const int at = i * 2;
      indices.insert(indices.end(),
                     {at, at + 1, at + 2, at + 1, at + 3, at + 2});
    }
  }
  SDL_RenderGeometry(renderer,
                     nullptr,
                     vertices.data(),
                     static_cast<int>(vertices.size()),
                     indices.data(),
                     static_cast<int>(indices.size()));
  SDL_SetRenderDrawColor(renderer,
                         highlighted ? 133 : 60,
                         highlighted ? 246 : 116,
                         highlighted ? 255 : 137,
                         255);
  for (int i = 0; i < segments; ++i) {
    const auto a = vertices[static_cast<std::size_t>(i * 2 + 1)].position;
    const auto b = vertices[static_cast<std::size_t>((i + 1) * 2 + 1)].position;
    SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
  }
}
} // namespace

bool CanOpenMouseRadial(const GameState &state) {
  // A crippled hull must still expose targeting, comms, map and missions.
  return state.player.is_active && !state.travel.engaging &&
         !NovaAiShip_IsDestroyed(state.player) &&
         state.player.hyperspace_jump_timer <= 0.0F;
}

void MouseFlightControl::Update(const FlightInput &input,
                                bool can_start,
                                bool controls_available) {
  if (!controls_available) {
    Cancel();
    return;
  }
  if (input.primary_clicked && can_start && input.primary_held) {
    steering_ = true;
    braking_ = false;
  }
  if (steering_ && !input.primary_held) {
    steering_ = false;
    braking_ = true;
  }
  if (input.turn_left || input.turn_right || input.thrust || input.reverse ||
      input.afterburner || input.face_target || input.travel || input.land)
    braking_ = false;
}

void ApplyMouseBraking(Ship &ship, float deceleration, float elapsed_ticks) {
  const float speed = std::hypot(ship.vel_x, ship.vel_y);
  const float step = deceleration * elapsed_ticks;
  if (!std::isfinite(speed) || !std::isfinite(step) || step <= 0.0F)
    return;
  const float scale = speed > step ? (speed - step) / speed : 0.0F;
  ship.vel_x *= scale;
  ship.vel_y *= scale;
  // Inertialess hulls also maintain scalar speed independently of velocity.
  ship.speed = std::max(0.0F, ship.speed - step);
}

bool ApplyMouseSteering(float heading,
                        SDL_FPoint direction,
                        float turn_step_radians,
                        FlightInput &input) {
  if (input.turn_left || input.turn_right || input.reverse ||
      !std::isfinite(direction.x) || !std::isfinite(direction.y) ||
      !std::isfinite(heading) || !std::isfinite(turn_step_radians)) {
    return false;
  }
  if (std::hypot(direction.x, direction.y) < 24.0F)
    return false;
  const float desired = std::atan2(direction.x, -direction.y);
  const float delta = std::remainder(desired - heading, 2.0F * kPi);
  const float tolerance = std::max(0.015F, std::abs(turn_step_radians));
  input.turn_left = delta < -tolerance;
  input.turn_right = delta > tolerance;
  // Turn before accelerating, so dragging behind the ship does not keep
  // driving it away from the pointer during the turn.
  input.thrust = input.thrust || std::abs(delta) < kPi / 6.0F;
  return true;
}

int RadialSector(SDL_FPoint offset, float inner_radius, float outer_radius) {
  const float distance = std::hypot(offset.x, offset.y);
  if (!std::isfinite(distance) || distance < inner_radius ||
      distance > outer_radius)
    return -1;
  const float angle = std::atan2(offset.x, -offset.y);
  return static_cast<int>(
             std::floor((angle + 2.0F * kPi + kPi / 4.0F) / (kPi / 2.0F))) %
         4;
}

SDL_FPoint
ClampRadialCenter(SDL_FPoint point, SDL_FPoint window, float radius) {
  const auto clamp_axis = [radius](float value, float extent) {
    return extent < radius * 2.0F ? extent * 0.5F
                                  : std::clamp(value, radius, extent - radius);
  };
  return {clamp_axis(point.x, window.x), clamp_axis(point.y, window.y)};
}

MouseTarget PickMouseTarget(SdlPlatform &platform,
                            SpaceflightView &view,
                            const GameState &state,
                            SDL_FPoint window_point) {
  const auto geometry = FlightSceneGeometryFor(platform);
  const auto cursor = geometry.placement.ToAuthored(window_point);
  if (cursor.x < 0 || cursor.y < 0 || cursor.x >= geometry.viewport_w ||
      cursor.y >= geometry.viewport_h)
    return {};
  if (const auto id = view.PickShipAt(platform, state, cursor.x, cursor.y);
      id > 0) {
    const auto &ship = state.ShipAt(static_cast<std::size_t>(id));
    const auto *cls = state.scenario.Ship(
        static_cast<std::int16_t>(ship.ship_class_id + 0x80));
    return {MouseTargetKind::ship,
            id,
            ship.ship_name.empty() ? (cls ? cls->display_name : "SHIP")
                                   : ship.ship_name};
  }
  if (const auto id = view.PickAsteroidAt(platform, state, cursor.x, cursor.y);
      id >= 0) {
    return {MouseTargetKind::asteroid, id, "ASTEROID"};
  }
  if (const auto id = view.PickStellarAt(platform, state, cursor.x, cursor.y);
      id >= 0x80) {
    const auto *stellar = state.scenario.Stellar(id);
    return {MouseTargetKind::stellar, id, stellar ? stellar->name : "PLANET"};
  }
  return {};
}

void SelectMouseTarget(GameState &state, const MouseTarget &target) {
  if (target.kind == MouseTargetKind::ship) {
    state.player.primary_target_ship_slot = target.id;
    state.ship_reticle_pulse = 256.0F;
  } else if (target.kind == MouseTargetKind::stellar) {
    state.player.primary_target_ship_slot = -1;
    state.travel.selected_stellar_id = target.id;
    state.travel.selected_stellar_is_manual = true;
    state.player.travel_transfer_mode = 2;
    state.travel_reticle_pulse = 256.0F;
  }
}

RadialAction RunMouseRadial(SdlPlatform &platform,
                            const MouseTarget &target,
                            SDL_FPoint window_point,
                            const std::function<void()> &draw_background) {
  NovaFontCache fonts;
  ProbeUiAutoClear probe_ui_guard(platform);
  const auto choices = Choices(target.kind);
  while (!platform.quit_requested()) {
    const auto window = platform.logical_playfield_size();
    const float scale =
        std::min({platform.ui_scale(), window.x / 380.0F, window.y / 380.0F});
    const auto placement = PlaceWindow(window, scale);
    const auto center = ClampRadialCenter(placement.ToAuthored(window_point),
                                          placement.authored_size,
                                          kOuter + 42.0F);
    const auto sector_at = [&](SDL_FPoint point) {
      // Near an edge the menu shifts inward. A quick tap at its original
      // anchor still cancels rather than accidentally choosing a shifted wedge.
      if (std::hypot(point.x - window_point.x, point.y - window_point.y) < 8.0F)
        return -1;
      const auto local = placement.ToAuthored(point);
      return RadialSector(
          {local.x - center.x, local.y - center.y}, kInner, kOuter);
    };
    for (std::optional<TextInput> event; (event = platform.PollTextEvent());) {
      if (event->key == TextKey::escape)
        return RadialAction::cancel;
      if (event->key == TextKey::primary ||
          event->key == TextKey::secondary_released) {
        const int sector = sector_at(platform.mouse_window_point());
        return sector < 0 ? RadialAction::cancel
                          : choices[static_cast<std::size_t>(sector)].action;
      }
    }
    // Losing focus or the initiating button must never leave a modal stuck.
    if (!platform.MouseButtonDown(SDL_BUTTON_RIGHT))
      return RadialAction::cancel;
    draw_background();
    const SdlPlatform::ScopedPlacement scoped(platform, placement);
    auto *renderer = platform.renderer();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 3, 8, 115);
    const SDL_FRect shade{
        0, 0, placement.authored_size.x, placement.authored_size.y};
    SDL_RenderFillRect(renderer, &shade);
    const int hovered = sector_at(platform.mouse_window_point());
    std::vector<ProbeNamedRect> rects;
    for (int sector = 0; sector < 4; ++sector) {
      DrawSector(renderer, center, sector, hovered == sector);
      const auto label =
          Polar(center, kLabelRadius, static_cast<float>(sector) * kPi / 2.0F);
      const auto &choice = choices[static_cast<std::size_t>(sector)];
      NovaText_DrawCentered(platform,
                            fonts,
                            NovaFontFamily::kGeneva,
                            12.0F,
                            kNovaFontStyleRegular,
                            {225, 244, 248, 255},
                            label.x - 65.0F,
                            label.x + 65.0F,
                            label.y + 4.0F,
                            choice.label);
      rects.emplace_back(
          choice.name,
          placement.ToWindowRect({label.x - 35, label.y - 12, 70, 24}));
    }
    std::string title = target.label;
    if (title.size() > 38)
      title = title.substr(0, 35) + "...";
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kGeneva,
                          16.0F,
                          kNovaFontStyleRegular,
                          {225, 244, 248, 255},
                          center.x - 190,
                          center.x + 190,
                          center.y - kOuter - 16,
                          title);
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kGeneva,
                          9.0F,
                          kNovaFontStyleRegular,
                          {148, 185, 196, 255},
                          center.x - kInner,
                          center.x + kInner,
                          center.y + 14,
                          "Esc / cancel");
    NovaText_DrawCentered(platform,
                          fonts,
                          NovaFontFamily::kGeneva,
                          10.0F,
                          kNovaFontStyleRegular,
                          {148, 185, 196, 255},
                          center.x - kInner,
                          center.x + kInner,
                          center.y - 4,
                          target.kind == MouseTargetKind::ship      ? "SHIP"
                          : target.kind == MouseTargetKind::stellar ? "PORT"
                          : target.kind == MouseTargetKind::asteroid
                              ? "ROCK"
                              : "FLIGHT");
    rects.emplace_back(
        "cancel",
        placement.ToWindowRect({center.x - 22, center.y - 22, 44, 44}));
    platform.PublishProbeUiItems("flight_radial", std::move(rects));
    platform.Present();
    platform.PaceFrame();
  }
  return RadialAction::cancel;
}
} // namespace game
