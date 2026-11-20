#include "game/bar_gambling.hpp"
#include "game/nova_random.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Bar racing rejects invalid bets without spending or rolling",
          "[bar][gambling]") {
  game::GameState state;
  state.player.credits = 500;
  const auto rng = state.rng;
  const auto previous = state.bar_race_previous_winner;
  CHECK_FALSE(game::PlaceBarRaceBet(state, -1, 100));
  CHECK_FALSE(game::PlaceBarRaceBet(state, 4, 100));
  CHECK_FALSE(game::PlaceBarRaceBet(state, 0, 0));
  CHECK_FALSE(game::PlaceBarRaceBet(state, 0, -1));
  CHECK_FALSE(game::PlaceBarRaceBet(state, 0, 501));
  CHECK(state.player.credits == 500);
  state.player.credits = 20000;
  CHECK_FALSE(game::PlaceBarRaceBet(state, 0, 10001));
  CHECK(state.player.credits == 20000);
  CHECK(state.rng == rng);
  CHECK(state.bar_race_previous_winner == previous);
}

TEST_CASE("Bar race winnings pay four times the stake after the result",
          "[bar][gambling]") {
  game::GameState state;
  auto rng = state.rng;
  const int winner = game::RandomBelow(rng, 4);
  state.bar_race_previous_winner = (winner + 1) % 4;
  state.player.credits = 350;
  const auto result = game::PlaceBarRaceBet(state, winner, 350);
  REQUIRE(result);
  CHECK(result->winner == winner);
  CHECK(result->wager == 350);
  CHECK(result->payout == 1400);
  CHECK(state.player.credits == 0); // Paid only after dismissing the result.
  CHECK(state.bar_race_previous_winner == winner);
  CHECK(state.rng == rng); // A non-repeated first draw must be accepted.
}

TEST_CASE("Bar racing preserves the original repeated-winner reroll rule",
          "[bar][gambling]") {
  game::GameState state;
  for (std::uint32_t seed = 0; seed < 32; ++seed) {
    state.rng.seed(seed);
    auto rng = state.rng;
    const int previous = game::RandomBelow(rng, 4);
    const int choice = (previous + 1) % 4;
    state.bar_race_previous_winner = previous;
    state.player.credits = 12345;
    const auto result = game::PlaceBarRaceBet(state, choice, 10000);
    REQUIRE(result);
    CHECK(result->winner >= 0);
    CHECK(result->winner < 4);
    CHECK(result->winner != previous);
    CHECK(result->winner != choice);
    CHECK(result->payout == 0);
    CHECK(state.player.credits == 2345);
    CHECK(state.bar_race_previous_winner == result->winner);
  }
}
