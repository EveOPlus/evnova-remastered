#pragma once

#include <cstdint>

namespace game {

// Count consecutive presses on one system using unscaled wall time. Both map
// views share the same double-click (plot) and triple-click (follow) gesture.
class StarmapClicks {
public:
  unsigned Press(std::int16_t system, std::uint64_t now_ms) {
    if (system < 0) {
      Reset();
      return 0;
    }
    count_ = system == system_ && now_ms >= last_ms_ &&
                     now_ms - last_ms_ <= 500 && count_ < 3
                 ? count_ + 1
                 : 1;
    system_ = system;
    last_ms_ = now_ms;
    return count_;
  }

  void Reset() {
    system_ = -1;
    count_ = 0;
  }

private:
  std::int16_t system_ = -1;
  unsigned count_ = 0;
  std::uint64_t last_ms_ = 0;
};

} // namespace game
