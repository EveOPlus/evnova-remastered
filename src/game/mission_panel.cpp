#include "mission_panel.hpp"

#include "mission.hpp"
#include "mission_navigation.hpp"
#include "preferences_ui.hpp"
#include "util/geometry.hpp"
#include "util/render_clip_scope.hpp"

#include <algorithm>

namespace game {
namespace {
constexpr float kWidth = 264, kRowHeight = 32;
constexpr int kVisibleRows = 4;

std::vector<std::int16_t> VisibleMissions(GameState &state) {
  const auto rows = MissionJournalRows(state);
  auto &panel = state.mission_panel;
  if (std::find(rows.begin(), rows.end(), panel.selected) == rows.end())
    panel.selected = rows.empty() ? -1 : rows.front();
  panel.scroll =
      std::clamp(panel.scroll,
                 0,
                 std::max(0, static_cast<int>(rows.size()) - kVisibleRows));
  return rows;
}

struct Geometry {
  SDL_FRect frame, title, close, list;
};

Geometry Rects(std::size_t count) {
  const float list_height =
      static_cast<float>(std::clamp(static_cast<int>(count), 1, kVisibleRows)) *
      kRowHeight;
  return {{0, 0, kWidth, list_height + 62},
          {2, 2, kWidth - 4, 20},
          {kWidth - 22, 3, 18, 18},
          {6, 26, kWidth - 12, list_height}};
}

SDL_FRect AutoPilotIcon(const SDL_FRect &row) {
  return {row.x + row.w - 25, row.y + 4, 22, 24};
}

Placement
PanelPlacement(SdlPlatform &platform, GameState &state, const Geometry &rects) {
  const auto window = platform.logical_playfield_size();
  const float scale = std::min(
      {platform.ui_scale(), 1.4F, window.x / kWidth, window.y / rects.frame.h});
  auto &p = state.mission_panel;
  p.x = std::clamp(p.x, 0.0F, std::max(0.0F, window.x - kWidth * scale));
  p.y = std::clamp(p.y, 0.0F, std::max(0.0F, window.y - rects.frame.h * scale));
  return PlaceCenteredIn(
      SDL_FRect{p.x, p.y, kWidth * scale, rects.frame.h * scale},
      SDL_FPoint{kWidth, rects.frame.h},
      scale);
}

bool Busy(const GameState &state) {
  return state.travel.engaging || state.player.hyperspace_jump_timer > 0 ||
         state.player.timed_action_counter > 0;
}
} // namespace

bool MissionPanel::Update(SdlPlatform &platform,
                          GameState &state,
                          FlightInput &input) {
  if (!state.mission_panel.visible) {
    dragging_ = false;
    return false;
  }
  const auto rows = VisibleMissions(state);
  const auto rects = Rects(rows.size());
  const auto placement = PanelPlacement(platform, state, rects);
  const SDL_FPoint pointer{input.window_mouse_x, input.window_mouse_y};
  const auto local = placement.ToAuthored(pointer);
  using evnova::util::Contains;
  const bool consumed = dragging_ || Contains(rects.frame, local);
  auto &p = state.mission_panel;
  if (dragging_) {
    if (!input.primary_held)
      dragging_ = false;
    else {
      p.x += pointer.x - last_pointer_.x;
      p.y += pointer.y - last_pointer_.y;
    }
  }
  last_pointer_ = pointer;
  if (!consumed)
    return false;
  if (input.scene_zoom_delta != 0) {
    p.scroll =
        std::clamp(p.scroll - wheel_.Consume(input.scene_zoom_delta),
                   0,
                   std::max(0, static_cast<int>(rows.size()) - kVisibleRows));
    input.scene_zoom_delta = 0;
  }
  if (input.primary_clicked) {
    if (Contains(rects.close, local)) {
      p.visible = false;
      dragging_ = false;
      platform.ClearProbeUi();
    } else if (Contains(rects.title, local)) {
      dragging_ = true;
    } else if (Contains(rects.list, local)) {
      const auto row = static_cast<std::size_t>(
          p.scroll + static_cast<int>((local.y - rects.list.y) / kRowHeight));
      if (row < rows.size()) {
        p.selected = rows[row];
        notice_.clear();
        const SDL_FRect selected_row{
            rects.list.x,
            rects.list.y +
                static_cast<float>(static_cast<int>(row) - p.scroll) *
                    kRowHeight,
            rects.list.w,
            kRowHeight};
        if (Contains(AutoPilotIcon(selected_row), local)) {
          if (Busy(state))
            notice_ = "Wait until the jump or action finishes.";
          else if (QueueMissionAutopilot(state, p.selected, notice_))
            notice_ = "Auto Pilot started. Steer to cancel.";
        }
      }
    }
  }
  input.primary_clicked = false;
  input.primary_held = false;
  input.secondary_clicked = false;
  return true;
}

void MissionPanel::Draw(SdlPlatform &platform, GameState &state) {
  if (!state.mission_panel.visible)
    return;
  const auto rows = VisibleMissions(state);
  const auto rects = Rects(rows.size());
  const auto placement = PanelPlacement(platform, state, rects);
  const SdlPlatform::ScopedPlacement scope(platform, placement);
  auto *renderer = platform.renderer();
  preferences_detail::DrawPanel(renderer, rects.frame);
  SDL_SetRenderDrawColor(renderer, 32, 45, 63, 255);
  SDL_RenderFillRect(renderer, &rects.title);
  constexpr SDL_Color text{220, 228, 238, 255}, muted{158, 178, 199, 255};
  NovaText_Draw(platform,
                fonts_,
                NovaFontFamily::kHelvetica,
                12,
                kNovaFontStyleBold,
                text,
                9,
                16,
                "Missions (I)");
  SDL_SetRenderDrawColor(renderer, 183, 201, 220, 255);
  SDL_RenderLine(renderer, rects.close.x + 5, 8, rects.close.x + 13, 16);
  SDL_RenderLine(renderer, rects.close.x + 13, 8, rects.close.x + 5, 16);
  std::vector<ProbeNamedRect> controls{
      {"missions.title", placement.ToWindowRect(rects.title)},
      {"missions.close", placement.ToWindowRect(rects.close)},
      {"missions.list", placement.ToWindowRect(rects.list)}};
  bool hover_autopilot = false;
  if (rows.empty()) {
    notice_.clear();
    NovaText_Draw(platform,
                  fonts_,
                  NovaFontFamily::kHelvetica,
                  12,
                  kNovaFontStyleRegular,
                  muted,
                  12,
                  46,
                  "No active missions");
  }
  const auto &panel = state.mission_panel;
  for (int visible = 0; visible < kVisibleRows; ++visible) {
    const auto index = static_cast<std::size_t>(panel.scroll + visible);
    if (index >= rows.size())
      break;
    const auto slot = rows[index];
    const auto lead = FindStoryLead(state, slot);
    const bool failed =
        slot >= 0 &&
        state.active_mission_runtime_flags[static_cast<std::size_t>(slot)]
            .is_failed;
    const SDL_FRect row{rects.list.x,
                        rects.list.y + static_cast<float>(visible) * kRowHeight,
                        rects.list.w,
                        kRowHeight};
    SDL_SetRenderDrawColor(
        renderer, panel.selected == slot ? 76 : 12, 20, 29, 255);
    SDL_RenderFillRect(renderer, &row);
    std::string title = lead ? lead->title : "Mission";
    if (slot >= 0) {
      const auto &mission =
          state.active_missions[static_cast<std::size_t>(slot)];
      if (const auto *definition = state.scenario.Mission(
              static_cast<std::int16_t>(mission.mission_template_id + 0x80)))
        title = Mission_ExpandMissionWildcards(
            state, definition->display_name, false, slot);
    }
    const auto icon = AutoPilotIcon(row);
    const SDL_FRect caption{row.x, row.y, icon.x - row.x - 3, row.h};
    {
      const evnova::util::RenderClipScope clip(renderer, caption);
      NovaText_Draw(platform,
                    fonts_,
                    NovaFontFamily::kHelvetica,
                    12,
                    kNovaFontStyleRegular,
                    text,
                    row.x + 5,
                    row.y + 13,
                    title);
      std::string reason;
      const auto destination = MissionAutopilotDestination(state, slot, reason);
      std::string where = lead     ? lead->summary
                          : failed ? "Failed"
                                   : "No docking destination";
      if (destination && !lead) {
        const auto *stellar = state.scenario.Stellar(destination->stellar_id);
        const auto *system = state.scenario.System(
            static_cast<std::int16_t>(stellar->system_id + 0x80));
        where = stellar->name + " / " +
                (stellar->system_id == state.player.current_system_id
                     ? "This system"
                 : system ? system->name
                          : "Unknown");
      }
      NovaText_Draw(platform,
                    fonts_,
                    NovaFontFamily::kHelvetica,
                    10,
                    kNovaFontStyleRegular,
                    muted,
                    row.x + 5,
                    row.y + 27,
                    where);
    }
    std::string reason;
    const bool enabled =
        !Busy(state) &&
        MissionAutopilotDestination(state, slot, reason).has_value();
    const bool hovered =
        evnova::util::Contains(icon, platform.mouse_position());
    hover_autopilot = hover_autopilot || hovered;
    const SDL_FRect face{icon.x + 3, icon.y + 4, 16, 16};
    SDL_SetRenderDrawColor(renderer, hovered ? 75 : 36, 47, 61, 255);
    SDL_RenderFillRect(renderer, &face);
    SDL_SetRenderDrawColor(renderer,
                           enabled ? 220 : 95,
                           enabled ? 228 : 100,
                           enabled ? 238 : 110,
                           255);
    // Small navigation arrow, retaining a comfortable click target around it.
    SDL_RenderLine(renderer, face.x + 4, face.y + 3, face.x + 12, face.y + 8);
    SDL_RenderLine(renderer, face.x + 12, face.y + 8, face.x + 4, face.y + 13);
    SDL_RenderLine(renderer, face.x + 4, face.y + 13, face.x + 6, face.y + 8);
    SDL_RenderLine(renderer, face.x + 6, face.y + 8, face.x + 4, face.y + 3);
    controls.emplace_back("missions.auto_pilot." + std::to_string(slot),
                          placement.ToWindowRect(icon));
    if (panel.selected == slot)
      controls.emplace_back("missions.auto_pilot",
                            placement.ToWindowRect(icon));
    controls.emplace_back("missions.row." + std::to_string(slot),
                          placement.ToWindowRect(row));
  }
  const std::string hint =
      !notice_.empty()      ? notice_
      : hover_autopilot     ? "Auto Pilot: fly to this mission and dock"
      : panel.selected < -1 ? "Story clue: press I for details"
      : rows.size() > kVisibleRows
          ? "Scroll for more missions"
          : "Use the arrow beside a mission to fly there";
  const SDL_FRect hint_box{8, rects.list.y + rects.list.h + 5, kWidth - 16, 28};
  const evnova::util::RenderClipScope clip(renderer, hint_box);
  // Two short lines keep feedback readable without making the tracker wider.
  const auto split = hint.size() > 40 ? hint.rfind(' ', 40) : std::string::npos;
  NovaText_Draw(platform,
                fonts_,
                NovaFontFamily::kHelvetica,
                10,
                kNovaFontStyleRegular,
                muted,
                hint_box.x,
                hint_box.y + 10,
                hint.substr(0, split));
  if (split != std::string::npos)
    NovaText_Draw(platform,
                  fonts_,
                  NovaFontFamily::kHelvetica,
                  10,
                  kNovaFontStyleRegular,
                  muted,
                  hint_box.x,
                  hint_box.y + 23,
                  hint.substr(split + 1));
  platform.PublishProbeOverlay("missions", std::move(controls));
}
} // namespace game
