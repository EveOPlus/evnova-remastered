#pragma once

#include <algorithm>
#include <cmath>

namespace evnova::util {

inline float ClampedWheelDelta(float delta) {
  return std::isfinite(delta) ? std::clamp(delta, -20.0F, 20.0F) : 0.0F;
}

// Preserve fractional trackpad motion until a whole row/page step is ready.
class ScrollWheelSteps {
public:
  int Consume(float delta) {
    delta = ClampedWheelDelta(delta);
    if (delta * remainder_ < 0)
      remainder_ = 0;
    remainder_ += delta;
    const auto steps = static_cast<int>(remainder_);
    remainder_ -= static_cast<float>(steps);
    return steps;
  }

private:
  float remainder_ = 0;
};

} // namespace evnova::util
