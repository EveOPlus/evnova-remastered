#pragma once

// Remake controls: input adapters, not reconstructed original behavior.
#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <string>

class SdlPlatform;
struct FlightInput;

namespace game {
struct GameState;
struct Ship;
class SpaceflightView;
class NovaFontCache;

enum class MouseTargetKind { space, ship, stellar, asteroid };
enum class RadialAction {
  cancel,
  target,
  hail,
  board,
  dock,
  map,
  missions,
  markets,
  mine
};

struct MouseTarget {
  MouseTargetKind kind = MouseTargetKind::space;
  std::int16_t id = -1;
  std::string label = "FLIGHT CONTROLS";
};

class MouseFlightControl {
public:
  void Update(const FlightInput &input,
              bool can_start,
              bool controls_available,
              std::uint64_t now_ms = 0,
              SDL_FPoint direction = {});

  void Cancel() { steering_ = braking_ = cruising_ = click_pending_ = false; }

  [[nodiscard]] bool steering() const { return steering_ || cruising_; }

  [[nodiscard]] bool cruising() const { return cruising_; }

  [[nodiscard]] SDL_FPoint Direction(SDL_FPoint pointer_direction) const {
    return cruising_ ? cruise_direction_ : pointer_direction;
  }

  [[nodiscard]] bool braking() const { return braking_; }

private:
  bool steering_ = false;
  bool braking_ = false;
  bool cruising_ = false;
  bool click_pending_ = false;
  std::uint64_t last_click_ms_ = 0;
  SDL_FPoint last_click_point_{};
  SDL_FPoint cruise_direction_{};
};

// Remake braking assist: reduce velocity along its own vector, with no turn
// or overshoot. Deceleration is in world units per normalized 30 Hz tick.
void ApplyMouseBraking(Ship &ship, float deceleration, float elapsed_ticks);

// Direction is relative to the ship in window points. Keyboard steering wins.
bool ApplyMouseSteering(float heading,
                        SDL_FPoint direction,
                        float turn_step_radians,
                        FlightInput &input);
// Four compass sectors, clockwise from up; the center and outside cancel.
int RadialSector(SDL_FPoint offset, float inner_radius, float outer_radius);
SDL_FPoint ClampRadialCenter(SDL_FPoint point, SDL_FPoint window, float radius);

[[nodiscard]] bool CanOpenMouseRadial(const GameState &state);

MouseTarget PickMouseTarget(SdlPlatform &platform,
                            SpaceflightView &view,
                            const GameState &state,
                            SDL_FPoint window_point);
void SelectMouseTarget(GameState &state, const MouseTarget &target);
RadialAction RunMouseRadial(SdlPlatform &platform,
                            const MouseTarget &target,
                            SDL_FPoint window_point,
                            const std::function<void()> &draw_background);
} // namespace game
