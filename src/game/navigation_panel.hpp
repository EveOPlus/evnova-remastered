#pragma once

#include "starmap.hpp"

namespace game {

void OpenNavigationPanel(GameState &state,
                         float zoom,
                         float pan_x,
                         float pan_y,
                         std::int16_t selected);

struct NavigationPanelInput {
  bool consumed_pointer = false;
  bool changed_route = false;
};

// Non-modal remake chart. Shares the galaxy map's projection, discovery gates,
// route planner and rendering; owns only input gestures and font/icon caches.
class NavigationPanel {
public:
  NavigationPanelInput
  Update(SdlPlatform &platform, GameState &state, FlightInput &input);
  void Draw(SdlPlatform &platform, GameState &state);

private:
  NovaFontCache fonts_;
  NovaStarmap_MarkerIcons icons_;
  bool loaded_ = false;
  enum class Drag { none, window, chart, resize } drag_ = Drag::none;
  SDL_FPoint last_pointer_{};
  unsigned resize_edges_ = 0;
};

} // namespace game
