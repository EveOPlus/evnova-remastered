#pragma once

#include "game_state.hpp"
#include "ship_ai.hpp"
#include "starmap_route.hpp"

#include <string>

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

// Community assistance only targets a visible active leg with an ordinary port.
inline std::optional<MissionAutopilotRequest> MissionAutopilotDestination(
    const GameState &state, std::int16_t slot, std::string &reason) {
  reason.clear();
  if (slot < 0 ||
      static_cast<std::size_t>(slot) >= state.active_missions.size()) {
    reason = "Select an active mission.";
    return std::nullopt;
  }
  const auto index = static_cast<std::size_t>(slot);
  const auto &mission = state.active_missions[index];
  const auto &flags = state.active_mission_runtime_flags[index];
  if (!flags.is_active || flags.is_failed) {
    reason = "This mission is no longer active.";
    return std::nullopt;
  }
  if ((flags.flags_primary_at_accept & 0x400U) != 0 ||
      (mission.flags_primary & 2U) != 0) {
    reason = "This mission's destination is hidden.";
    return std::nullopt;
  }
  const auto id = MissionDestinationStellar(state, mission, flags);
  const auto *stellar = state.scenario.Stellar(id);
  if (!stellar || (stellar->flags & 0x20U) != 0 ||
      (stellar->availability_flags & 0x3000U) != 0) {
    reason = "This mission has no available docking destination.";
    return std::nullopt;
  }
  return MissionAutopilotRequest{slot, mission.mission_template_id, id};
}

inline bool MissionAutopilotStillValid(const GameState &state,
                                       const MissionAutopilotRequest &request) {
  std::string reason;
  const auto current = MissionAutopilotDestination(state, request.slot, reason);
  return current &&
         current->mission_template_id == request.mission_template_id &&
         current->stellar_id == request.stellar_id;
}

inline bool QueueMissionAutopilot(GameState &state,
                                  std::int16_t slot,
                                  std::string &reason) {
  if (NovaAiShip_IsDisabled(state, state.player)) {
    reason = "Repair your disabled ship before starting Auto Pilot.";
    return false;
  }
  const auto request = MissionAutopilotDestination(state, slot, reason);
  if (!request)
    return false;
  const auto *stellar = state.scenario.Stellar(request->stellar_id);
  if (stellar->system_id != state.player.current_system_id &&
      !FindShortestStarmapRoute(state, stellar->system_id)) {
    reason = "No known route to this mission's destination.";
    return false;
  }
  state.pending_mission_autopilot = request;
  return true;
}
} // namespace game
