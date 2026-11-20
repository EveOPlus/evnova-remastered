#pragma once

#include "game_state.hpp"
#include "nova_font.hpp"
#include "util/scroll_wheel.hpp"

namespace game {
// Small, non-modal mission tracker shared by flight and the main dock screen.
// It queues the same checked command as the full active-mission window.
class MissionPanel {
public:
  // Consumes pointer gestures over the panel, preserving keyboard controls.
  bool Update(SdlPlatform &, GameState &, FlightInput &);
  void Draw(SdlPlatform &, GameState &);

private:
  NovaFontCache fonts_;
  evnova::util::ScrollWheelSteps wheel_;
  bool dragging_ = false;
  SDL_FPoint last_pointer_{};
  std::string notice_;
};
} // namespace game
