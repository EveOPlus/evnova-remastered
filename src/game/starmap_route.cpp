#include "starmap_route.hpp"

#include "game_state.hpp"
#include "travel.hpp"

#include <algorithm>

namespace game {
std::optional<std::vector<std::int16_t>>
FindShortestStarmapRoute(const GameState &state, std::int16_t destination) {
  const auto &systems = state.scenario.systems;
  const auto current =
      NovaSystem_ResolveVisibleForTravel(state, state.player.current_system_id);
  destination = NovaSystem_ResolveVisibleForTravel(state, destination);
  if (current < 0 || destination < 0 ||
      !systems[static_cast<std::size_t>(current)].has_explored_flag ||
      !systems[static_cast<std::size_t>(destination)].has_explored_flag) {
    return std::nullopt;
  }

  std::vector<std::int16_t> previous(systems.size(), -1);
  std::vector<std::int16_t> pending{current};
  previous[static_cast<std::size_t>(current)] = current;
  for (std::size_t head = 0; head < pending.size(); ++head) {
    const auto id = pending[head];
    if (id == destination) {
      std::vector<std::int16_t> route;
      for (auto hop = destination; hop != current;
           hop = previous[static_cast<std::size_t>(hop)]) {
        route.push_back(hop);
      }
      route.push_back(current);
      std::reverse(route.begin(), route.end());
      return route;
    }
    const auto &system = systems[static_cast<std::size_t>(id)];
    // Grey frontier systems may be destinations, but their outgoing links
    // are not known until visited. Do not discover a hidden shortcut for free.
    if (id != current && system.discovery_state <= 0) {
      continue;
    }
    for (const auto raw_link : system.links) {
      if (raw_link < 0x80) {
        continue;
      }
      const auto next = NovaSystem_ResolveVisibleForTravel(
          state, static_cast<std::int16_t>(raw_link - 0x80));
      if (next < 0 ||
          !systems[static_cast<std::size_t>(next)].has_explored_flag ||
          previous[static_cast<std::size_t>(next)] != -1) {
        continue;
      }
      previous[static_cast<std::size_t>(next)] = id;
      pending.push_back(next);
    }
  }
  return std::nullopt;
}

bool PlotShortestStarmapRoute(GameState &state, std::int16_t destination) {
  const auto route = FindShortestStarmapRoute(state, destination);
  if (!route || route->size() > state.travel.starmap_route.size()) {
    return false;
  }
  if (route->size() == 1) {
    NovaStarmap_ClearRoute(state);
    return true;
  }
  state.travel.starmap_route.fill(-1);
  std::copy(route->begin(), route->end(), state.travel.starmap_route.begin());
  NovaStarmap_SyncTravelSelectionFromRoute(state);
  return true;
}
} // namespace game
