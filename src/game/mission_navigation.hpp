#pragma once

#include "game_state.hpp"

namespace game {
// Shared map/market destination selection from the stellar arm of 0x004aa980.
// Returns a 0x80-based resource ID; -1 means no currently available
// destination.
inline std::int16_t
MissionDestinationStellar(const GameState &state,
                          const ActiveMission &mission,
                          const MissionRuntimeFlags &flags) {
  const auto available = [&](std::int16_t index) {
    if (index < 0)
      return false;
    const auto *stellar =
        state.scenario.Stellar(static_cast<std::int16_t>(index + 0x80));
    return stellar && stellar->is_available && stellar->system_id >= 0;
  };
  std::int16_t target =
      available(mission.travel_stellar_id) ? mission.travel_stellar_id : -1;
  if (mission.return_stellar_id != mission.travel_stellar_id &&
      flags.travel_stellar_reached && available(mission.return_stellar_id))
    target = mission.return_stellar_id;
  return target >= 0 ? static_cast<std::int16_t>(target + 0x80) : -1;
}
} // namespace game
