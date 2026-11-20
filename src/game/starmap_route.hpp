#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace game {
struct GameState;

// Remake route planning: fewest directed jumps using known departure links.
// Includes the current system; an unexplored destination may be the last hop.
// This adds automatic planning to the original route/arrival machinery.
[[nodiscard]] std::optional<std::vector<std::int16_t>>
FindShortestStarmapRoute(const GameState &state, std::int16_t destination);

// Replaces and arms a complete route atomically. Failure preserves the current
// route and travel selection. Selecting the current system clears the route.
[[nodiscard]] bool PlotShortestStarmapRoute(GameState &state,
                                            std::int16_t destination);
} // namespace game
