#pragma once

#include <cstdint>
#include <optional>

namespace game {

enum class JumpPress { none, single, route };

// Remake key gesture. The first press acts immediately; a second down edge
// within 450 wall-clock milliseconds promotes that departure to route flight.
class JumpKeyControl {
public:
  JumpPress Update(bool held,
                   std::uint64_t now_ms,
                   std::int16_t system,
                   std::int16_t destination) {
    if (system != system_ || destination != destination_)
      CancelPending();
    system_ = system;
    destination_ = destination;
    const bool pressed = held && !was_held_;
    was_held_ = held;
    if (!pressed)
      return JumpPress::none;
    if (first_press_ms_ && now_ms >= *first_press_ms_ &&
        now_ms - *first_press_ms_ <= 450) {
      CancelPending();
      return JumpPress::route;
    }
    first_press_ms_ = now_ms;
    return JumpPress::single;
  }

  void CancelPending() { first_press_ms_.reset(); }

private:
  bool was_held_ = false;
  std::optional<std::uint64_t> first_press_ms_;
  std::int16_t system_ = -1;
  std::int16_t destination_ = -1;
};

} // namespace game
