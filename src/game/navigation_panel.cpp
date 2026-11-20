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
constexpr SDL_FPoint kMinimumSize{240, 180};

struct PanelRects {
  SDL_FPoint size;
  SDL_FRect title, close, chart, resize;
};

PanelRects Rects(const GameState &state) {
  const auto &p = state.navigation_panel;
  return {{p.width, p.height},
          {2, 2, p.width - 4, 20},
          {p.width - 22, 3, 18, 18},
          {2, 24, p.width - 4, p.height - 26},
          {p.width - 14, p.height - 14, 12, 12}};
}

unsigned ResizeEdges(const PanelRects &r, SDL_FPoint point) {
  if (evnova::util::Contains(r.resize, point))
    return 2U | 8U;
  unsigned edges = 0;
  if (point.x < 6)
    edges |= 1U;
  if (point.x > r.size.x - 6)
    edges |= 2U;
  if (point.y < 3)
    edges |= 4U;
  if (point.y > r.size.y - 6)
    edges |= 8U;
  return edges;
}

Placement PanelPlacement(SdlPlatform &platform, GameState &state) {
  const auto window = platform.logical_playfield_size();
  const float scale = std::min({platform.ui_scale(),
                                window.x / kMinimumSize.x,
                                window.y / kMinimumSize.y});
  auto &panel = state.navigation_panel;
  panel.width = std::clamp(panel.width, kMinimumSize.x, window.x / scale);
  panel.height = std::clamp(panel.height, kMinimumSize.y, window.y / scale);
  panel.x =
      std::clamp(panel.x, 0.0F, std::max(0.0F, window.x - panel.width * scale));
  panel.y = std::clamp(
      panel.y, 0.0F, std::max(0.0F, window.y - panel.height * scale));
  return PlaceCenteredIn(
      SDL_FRect{panel.x, panel.y, panel.width * scale, panel.height * scale},
      SDL_FPoint{panel.width, panel.height},
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
    clicks_.Reset();
    return result;
  }
  if (p.centered_system != state.player.current_system_id) {
    Center(state);
    clicks_.Reset();
  }
  const auto placement = PanelPlacement(platform, state);
  const auto rects = Rects(state);
  const SDL_FPoint pointer{input.window_mouse_x, input.window_mouse_y};
  const auto local = placement.ToAuthored(pointer);
  const bool inside = evnova::util::Contains(
      SDL_FRect{0, 0, rects.size.x, rects.size.y}, local);
  result.consumed_pointer = inside || drag_ != Drag::none;
  if (drag_ != Drag::none) {
    if (!input.primary_held)
      drag_ = Drag::none;
    else if (drag_ == Drag::window) {
      p.x += pointer.x - last_pointer_.x;
      p.y += pointer.y - last_pointer_.y;
    } else if (drag_ == Drag::resize) {
      const auto window = platform.logical_playfield_size();
      const float dx = (pointer.x - last_pointer_.x) / placement.scale;
      const float dy = (pointer.y - last_pointer_.y) / placement.scale;
      if ((resize_edges_ & 1U) != 0) {
        const float width = std::clamp(
            p.width - dx, kMinimumSize.x, p.width + p.x / placement.scale);
        p.x -= (width - p.width) * placement.scale;
        p.width = width;
      }
      if ((resize_edges_ & 2U) != 0)
        p.width = std::clamp(
            p.width + dx, kMinimumSize.x, (window.x - p.x) / placement.scale);
      if ((resize_edges_ & 4U) != 0) {
        const float height = std::clamp(
            p.height - dy, kMinimumSize.y, p.height + p.y / placement.scale);
        p.y -= (height - p.height) * placement.scale;
        p.height = height;
      }
      if ((resize_edges_ & 8U) != 0)
        p.height = std::clamp(
            p.height + dy, kMinimumSize.y, (window.y - p.y) / placement.scale);
    } else {
      p.pan_x -= (pointer.x - last_pointer_.x) / placement.scale * p.zoom;
      p.pan_y -= (pointer.y - last_pointer_.y) / placement.scale * p.zoom;
    }
  }
  last_pointer_ = pointer;
  if (!result.consumed_pointer) {
    if (input.primary_clicked || input.secondary_clicked)
      clicks_.Reset();
    return result;
  }
  if (input.secondary_clicked)
    clicks_.Reset();
  if (std::isfinite(input.scene_zoom_delta) && input.scene_zoom_delta != 0) {
    clicks_.Reset();
    p.zoom = std::clamp(
        p.zoom *
            std::pow(.75F, std::clamp(input.scene_zoom_delta, -20.0F, 20.0F)),
        .4F,
        2.5F);
    input.scene_zoom_delta = 0;
  }
  const bool busy =
      state.travel.engaging || state.player.hyperspace_jump_timer > 0;
  if (input.primary_clicked) {
    using evnova::util::Contains;
    if (!Contains(rects.chart, local) || ResizeEdges(rects, local) != 0)
      clicks_.Reset();
    if (Contains(rects.close, local)) {
      p.visible = false;
      drag_ = Drag::none;
      platform.ClearProbeUi();
    } else if (const unsigned edges = ResizeEdges(rects, local); edges != 0) {
      drag_ = Drag::resize;
      resize_edges_ = edges;
    } else if (Contains(rects.title, local)) {
      drag_ = Drag::window;
    } else if (Contains(rects.chart, local)) {
      const auto mapped =
          starmap_detail::BuildMappedSystems(state, View(state), rects.chart);
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
      const unsigned presses =
          clicks_.Press(busy ? -1 : hit, platform.wall_ticks_ms());
      const bool plot_route = shift || presses >= 2;
      if (hit < 0)
        drag_ = Drag::chart;
      else if (busy)
        NovaHud_ShowOverlayMessage(state, "Navigation locked during jump.");
      else {
        std::string notice;
        p.selected = hit;
        if (hit == state.player.current_system_id) {
          notice = "Current system.";
        } else if (!plot_route &&
                   hit == state.travel.starmap_destination_system_id) {
          notice = "Current jump destination.";
        } else if (plot_route) {
          result.changed_route = PlotShortestStarmapRoute(state, hit);
          result.follow_route = result.changed_route && presses == 3;
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
            notice = "Double-click or Shift-click to plot a route here.";
          }
        }
        state.route_map.overlay_visible = false;
        NovaHud_ShowOverlayMessage(state, notice);
      }
    }
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
  const auto rects = Rects(state);
  const SdlPlatform::ScopedPlacement scope(platform, placement);
  using namespace preferences_detail;
  auto *renderer = platform.renderer();
  DrawPanel(renderer, {0, 0, rects.size.x, rects.size.y});
  SDL_SetRenderDrawColor(renderer, 32, 45, 63, 255);
  SDL_RenderFillRect(renderer, &rects.title);
  NovaText_DrawCentered(platform,
                        fonts_,
                        NovaFontFamily::kHelvetica,
                        12,
                        kNovaFontStyleRegular,
                        SDL_Color{183, 201, 220, 255},
                        4,
                        rects.close.x - 4,
                        16,
                        "Navigation");
  const bool close_hovered =
      evnova::util::Contains(rects.close, platform.mouse_position());
  SDL_SetRenderDrawColor(renderer,
                         close_hovered ? 255 : 180,
                         close_hovered ? 185 : 198,
                         close_hovered ? 185 : 218,
                         255);
  SDL_RenderLine(renderer,
                 rects.close.x + 5,
                 rects.close.y + 5,
                 rects.close.x + 13,
                 rects.close.y + 13);
  SDL_RenderLine(renderer,
                 rects.close.x + 13,
                 rects.close.y + 5,
                 rects.close.x + 5,
                 rects.close.y + 13);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderFillRect(renderer, &rects.chart);
  const auto mapped =
      starmap_detail::BuildMappedSystems(state, View(state), rects.chart);
  const auto missions = BuildMissionTargetSystems(state);
  starmap_detail::StarmapGeometry geometry{};
  geometry.map = rects.chart;
  {
    const evnova::util::RenderClipScope clip(renderer, rects.chart);
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
  SDL_SetRenderDrawColor(renderer, 132, 157, 181, 255);
  for (const float offset : {4.0F, 8.0F})
    SDL_RenderLine(renderer,
                   rects.size.x - 4 - offset,
                   rects.size.y - 4,
                   rects.size.x - 4,
                   rects.size.y - 4 - offset);
  std::vector<ProbeNamedRect> controls;
  controls.emplace_back("nav.resize", placement.ToWindowRect(rects.resize));
  controls.emplace_back("nav.chart", placement.ToWindowRect(rects.chart));
  controls.emplace_back("nav.title", placement.ToWindowRect(rects.title));
  controls.emplace_back("nav.close", placement.ToWindowRect(rects.close));
  for (const auto &node : mapped) {
    if (Marked(state, node.zero_based_id, missions) &&
        evnova::util::Contains(rects.chart, node.sx, node.sy))
      controls.emplace_back(
          "nav.system." + std::to_string(node.zero_based_id),
          placement.ToWindowRect({node.sx - 4, node.sy - 4, 8, 8}));
  }
  platform.PublishProbeUiItems("navigation_panel", std::move(controls));
}

} // namespace game
