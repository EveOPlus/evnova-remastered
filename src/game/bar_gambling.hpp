#pragma once

#include "game_state.hpp"

#include <functional>
#include <optional>

class SdlPlatform;

namespace game {
struct BarRaceResult {
  int winner = -1;
  int wager = 0;
  int payout = 0;
};

// Betting/outcome arm of RunBarGamblingWindow. Deducts the stake once;
// the modal pays the returned winnings after the race result is dismissed.
[[nodiscard]] std::optional<BarRaceResult>
PlaceBarRaceBet(GameState &state, int choice, int wager);
void RunBarGamblingWindow(SdlPlatform &platform,
                          GameState &state,
                          const std::function<void()> &render_background);
} // namespace game
