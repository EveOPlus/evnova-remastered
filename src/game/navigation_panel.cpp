#include "navigation_panel.hpp"

#include "hud_overlay.hpp"
#include "preferences_ui.hpp"
#include "starmap_internal.hpp"
#include "starmap_route.hpp"
#include "travel.hpp"
#include "util/geometry.hpp"
#include "util/render_clip_scope.hpp"

#include <algorithm>
#include <cmath>

namespace game {
namespace {
constexpr SDL_FPoint kSize{348, 278};
constexpr SDL_FRect kChart{2, 2, 344, 274};

Placement PanelPlacement(SdlPlatform &platform, GameState &state) {
  const auto window = platform.logical_playfield_size();
  const float scale =
      std::min({platform.ui_scale(), window.x / kSize.x, window.y / kSize.y});
  auto &panel = state.navigation_panel;
  panel.x =
      std::clamp(panel.x, 0.0F, std::max(0.0F, window.x - kSize.x * scale));
  panel.y =
      std::clamp(panel.y, 0.0F, std::max(0.0F, window.y - kSize.y * scale));
  return PlaceCenteredIn(
      SDL_FRect{panel.x, panel.y, kSize.x * scale, kSize.y * scale},
      kSize,
      scale);
}

starmap_detail::MapView View(const GameState &state) {
  const auto &p = state.navigation_panel;
  return {p.zoom, p.pan_x, p.pan_y};
}

void Center(GameState &state) {
  auto &p = state.navigation_panel;
  if (const auto *current = state.scenario.System(
          static_cast<std::int16_t>(state.player.current_system_id + 0x80))) {
    p.pan_x = static_cast<float>(current->pos_x);
    p.pan_y = static_cast<float>(current->pos_y);
    p.centered_system = state.player.current_system_id;
  }
}

bool Marked(const GameState &state,
            std::int16_t id,
            const std::vector<std::int16_t> &missions) {
  return starmap_detail::SystemOnMap(state, id) ||
         id == state.player.current_system_id ||
         id == state.navigation_panel.selected ||
         std::find(missions.begin(), missions.end(), id) != missions.end();
}
} // namespace

void OpenNavigationPanel(GameState &state,
                         float zoom,
                         float pan_x,
                         float pan_y,
                         std::int16_t selected) {
  auto &p = state.navigation_panel;
  p.visible = true;
  p.zoom = std::clamp(zoom, .4F, 2.5F);
  p.pan_x = pan_x;
  p.pan_y = pan_y;
  p.selected = selected;
  p.centered_system = state.player.current_system_id;
  state.route_map.overlay_visible = false;
}

NavigationPanelInput NavigationPanel::Update(SdlPlatform &platform,
                                             GameState &state,
                                             FlightInput &input) {
  NavigationPanelInput result;
  auto &p = state.navigation_panel;
  if (!p.visible) {
    drag_ = Drag::none;
    return result;
  }
  if (p.centered_system != state.player.current_system_id)
    Center(state);
  const auto placement = PanelPlacement(platform, state);
  const SDL_FPoint pointer{input.window_mouse_x, input.window_mouse_y};
  const auto local = placement.ToAuthored(pointer);
  const bool inside =
      evnova::util::Contains(SDL_FRect{0, 0, kSize.x, kSize.y}, local);
  result.consumed_pointer = inside || drag_ != Drag::none;
  if (drag_ != Drag::none) {
    if (!input.primary_held)
      drag_ = Drag::none;
    else if (drag_ == Drag::window) {
      p.x += pointer.x - last_pointer_.x;
      p.y += pointer.y - last_pointer_.y;
    } else {
      p.pan_x -= (pointer.x - last_pointer_.x) / placement.scale * p.zoom;
      p.pan_y -= (pointer.y - last_pointer_.y) / placement.scale * p.zoom;
    }
  }
  last_pointer_ = pointer;
  if (!result.consumed_pointer)
    return result;
  if (std::isfinite(input.scene_zoom_delta) && input.scene_zoom_delta != 0) {
    p.zoom = std::clamp(
        p.zoom *
            std::pow(.75F, std::clamp(input.scene_zoom_delta, -20.0F, 20.0F)),
        .4F,
        2.5F);
    input.scene_zoom_delta = 0;
  }
  const bool busy =
      state.travel.engaging || state.player.hyperspace_jump_timer > 0;
  if (input.secondary_clicked) {
    p.visible = false;
    drag_ = Drag::none;
    platform.ClearProbeUi();
  } else if (input.primary_clicked) {
    using evnova::util::Contains;
    if (Contains(kChart, local)) {
      const auto mapped =
          starmap_detail::BuildMappedSystems(state, View(state), kChart);
      const auto missions = BuildMissionTargetSystems(state);
      std::int16_t hit = -1;
      float nearest = 11;
      for (const auto &node : mapped) {
        const float distance = std::hypot(local.x - node.sx, local.y - node.sy);
        if (distance < nearest && Marked(state, node.zero_based_id, missions)) {
          hit = NovaSystem_ResolveVisibleForTravel(state, node.zero_based_id);
          nearest = distance;
        }
      }
      const bool shift = platform.IsOriginalKeyCodeHeld(0x2a) ||
                         platform.IsOriginalKeyCodeHeld(0x36);
      if (hit < 0)
        drag_ = shift ? Drag::chart : Drag::window;
      else if (busy)
        NovaHud_ShowOverlayMessage(state, "Navigation locked during jump.");
      else {
        std::string notice;
        p.selected = hit;
        if (shift || hit == state.player.current_system_id) {
          result.changed_route = PlotShortestStarmapRoute(state, hit);
          notice = result.changed_route
                       ? "Route ready. J: one jump. Double-J: auto-route."
                       : "No known route to this system.";
        } else {
          const auto previous = state.travel.starmap_destination_system_id;
          if (NovaTravel_PlotStarmapDestination(state, hit)) {
            NovaStarmap_ClearRoute(state);
            (void)NovaTravel_PlotStarmapDestination(state, hit);
            result.changed_route = true;
            notice = "Destination ready. Press J to jump.";
          } else {
            state.travel.starmap_destination_system_id = previous;
            notice = "Shift-click to plot a route here.";
          }
        }
        state.route_map.overlay_visible = false;
        NovaHud_ShowOverlayMessage(state, notice);
      }
    } else
      drag_ = Drag::window;
  }
  input.primary_clicked = false;
  input.primary_held = false;
  input.secondary_clicked = false;
  return result;
}

void NavigationPanel::Draw(SdlPlatform &platform, GameState &state) {
  if (!state.navigation_panel.visible)
    return;
  if (!loaded_) {
    icons_ = NovaStarmap_LoadMarkerIcons(platform);
    loaded_ = true;
  }
  const auto placement = PanelPlacement(platform, state);
  const SdlPlatform::ScopedPlacement scope(platform, placement);
  using namespace preferences_detail;
  auto *renderer = platform.renderer();
  DrawPanel(renderer, {0, 0, kSize.x, kSize.y});
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(renderer, &kChart);
  const auto mapped =
      starmap_detail::BuildMappedSystems(state, View(state), kChart);
  const auto missions = BuildMissionTargetSystems(state);
  starmap_detail::StarmapGeometry geometry{};
  geometry.map = kChart;
  {
    const evnova::util::RenderClipScope clip(renderer, kChart);
    starmap_detail::DrawGalaxy(platform,
                               fonts_,
                               state,
                               mapped,
                               View(state),
                               geometry,
                               state.navigation_panel.selected,
                               missions,
                               icons_);
  }
  std::vector<ProbeNamedRect> controls;
  controls.emplace_back("nav.chart", placement.ToWindowRect(kChart));
  for (const auto &node : mapped) {
    if (Marked(state, node.zero_based_id, missions) &&
        evnova::util::Contains(kChart, node.sx, node.sy))
      controls.emplace_back(
          "nav.system." + std::to_string(node.zero_based_id),
          placement.ToWindowRect({node.sx - 4, node.sy - 4, 8, 8}));
  }
  platform.PublishProbeUiItems("navigation_panel", std::move(controls));
}

} // namespace game
