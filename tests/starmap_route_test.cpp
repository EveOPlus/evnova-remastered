#include "game/game_state.hpp"
#include "game/starmap_clicks.hpp"
#include "game/starmap_route.hpp"
#include "game/travel.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Map multi-click gestures stay on one destination and expire",
          "[starmap-route]") {
  game::StarmapClicks clicks;
  CHECK(clicks.Press(4, 100) == 1);
  CHECK(clicks.Press(4, 250) == 2);
  CHECK(clicks.Press(4, 400) == 3);
  CHECK(clicks.Press(4, 550) == 1);  // A fourth click begins a new gesture.
  CHECK(clicks.Press(7, 650) == 1);  // Different destination.
  CHECK(clicks.Press(7, 1151) == 1); // Too slow.
  CHECK(clicks.Press(7, 1200) == 2);
  clicks.Reset(); // Pan, zoom, other control, close, or system arrival.
  CHECK(clicks.Press(7, 1250) == 1);
  CHECK(clicks.Press(-1, 1300) == 0); // Empty chart or jump lock.
  CHECK(clicks.Press(7, 1400) == 1);
  CHECK(clicks.Press(7, 1900) == 2); // Inclusive 500 ms interval.
  CHECK(clicks.Press(7, 1800) == 1); // No accidental wraparound match.
}

namespace {
game::GameState Galaxy(std::size_t count) {
  game::GameState state;
  state.scenario.systems.resize(count);
  state.player.current_system_id = 0;
  for (auto &system : state.scenario.systems) {
    system.is_visible = true;
    system.has_explored_flag = true;
    system.discovery_state = 1;
    system.links.fill(-1);
    system.nav_defs.fill(-1);
  }
  return state;
}

void Link(game::GameState &state,
          std::size_t from,
          std::size_t slot,
          std::int16_t to) {
  state.scenario.systems[from].links[slot] =
      static_cast<std::int16_t>(to + 0x80);
}
} // namespace

TEST_CASE(
    "Automatic starmap routing chooses the fewest jumps and arms its first leg",
    "[starmap-route]") {
  auto state = Galaxy(6);
  // The first-listed branch is longer: 0 -> 1 -> 2 -> 5 vs 0 -> 4 -> 5.
  Link(state, 0, 0, 1);
  Link(state, 1, 0, 2);
  Link(state, 2, 0, 5);
  Link(state, 0, 1, 4);
  Link(state, 4, 3, 5);
  state.scenario.systems[4].pos_x = 30000; // Map distance is not the cost.
  Link(state, 4, 4, 0); // Cycles and duplicates cannot extend the search.
  Link(state, 4, 5, 5);
  state.scenario.systems[0].links[7] = 32767;
  REQUIRE(game::PlotShortestStarmapRoute(state, 5));
  CHECK(state.travel.starmap_route[0] == 0);
  CHECK(state.travel.starmap_route[1] == 4);
  CHECK(state.travel.starmap_route[2] == 5);
  CHECK(state.travel.starmap_route[3] == -1);
  CHECK(state.travel.travel_slot == 1);
  CHECK(state.travel.starmap_destination_system_id == 4);
  CHECK(state.player.travel_transfer_mode == 3);

  // Arrival consumes exactly one leg and arms the following jump.
  state.player.current_system_id = 4;
  game::NovaStarmap_NormalizeRouteToCurrentSystem(state);
  game::NovaStarmap_SyncTravelSelectionFromRoute(state);
  CHECK(state.travel.starmap_route[0] == 4);
  CHECK(state.travel.starmap_route[1] == 5);
  CHECK(state.travel.travel_slot == 3);
  CHECK(state.travel.starmap_destination_system_id == 5);
}

TEST_CASE("Automatic routing replaces old routes and preserves them on failure",
          "[starmap-route]") {
  auto state = Galaxy(5);
  Link(state, 0, 0, 1);
  Link(state, 1, 0, 2);
  Link(state, 0, 1, 3);
  REQUIRE(game::PlotShortestStarmapRoute(state, 2));
  REQUIRE(game::PlotShortestStarmapRoute(state, 3));
  CHECK(state.travel.starmap_route[1] == 3);
  CHECK(state.travel.starmap_route[2] == -1);
  const auto route = state.travel.starmap_route;
  const auto slot = state.travel.travel_slot;
  CHECK_FALSE(game::PlotShortestStarmapRoute(state, 4));
  CHECK_FALSE(game::PlotShortestStarmapRoute(state, -1));
  CHECK_FALSE(game::PlotShortestStarmapRoute(state, 100));
  CHECK(state.travel.starmap_route == route);
  CHECK(state.travel.travel_slot == slot);
  CHECK(state.travel.starmap_destination_system_id == 3);
  REQUIRE(game::PlotShortestStarmapRoute(state, 0));
  CHECK_FALSE(game::NovaStarmap_RouteHasHops(state));
  CHECK(state.travel.travel_slot == -1);
  CHECK(state.travel.starmap_destination_system_id == -1);
}

TEST_CASE(
    "Route searches obey discovery, one-way links and available story twins",
    "[starmap-route]") {
  auto state = Galaxy(5);
  Link(state, 0, 0, 1);
  Link(state, 1, 0, 2);
  state.scenario.systems[1].discovery_state = 0;
  state.scenario.systems[1].discovered_this_rebuild = true;
  CHECK(game::FindShortestStarmapRoute(state, 1).has_value());
  CHECK_FALSE(game::FindShortestStarmapRoute(state, 2));
  state.scenario.systems[1].discovery_state = 1;
  REQUIRE(game::FindShortestStarmapRoute(state, 2));
  state.player.current_system_id = 2;
  CHECK_FALSE(game::FindShortestStarmapRoute(state, 0));
  state.player.current_system_id = 0;
  state.scenario.systems[1].is_visible = false;
  CHECK_FALSE(game::FindShortestStarmapRoute(state, 2));

  // Links name a hidden root, which must resolve to its currently active twin.
  state.scenario.systems[1].visibility_root_system_id = 1;
  state.scenario.systems[1].visible_parent_system_id = 3;
  state.scenario.systems[3].visibility_root_system_id = 1;
  Link(state, 3, 0, 2);
  const auto route = game::FindShortestStarmapRoute(state, 2);
  REQUIRE(route);
  CHECK(*route == std::vector<std::int16_t>{0, 3, 2});
  const auto twin = game::FindShortestStarmapRoute(state, 1);
  REQUIRE(twin);
  CHECK(*twin == std::vector<std::int16_t>{0, 3});
  state.scenario.systems[3].has_explored_flag = false;
  CHECK_FALSE(game::FindShortestStarmapRoute(state, 2));
}

TEST_CASE("Automatic routes can span more than the original 31-jump limit",
          "[starmap-route]") {
  auto state = Galaxy(42);
  for (std::size_t i = 0; i < 41; ++i) {
    Link(state, i, 0, static_cast<std::int16_t>(i + 1));
  }
  REQUIRE(game::PlotShortestStarmapRoute(state, 41));
  for (std::size_t i = 0; i < 42; ++i) {
    CHECK(state.travel.starmap_route[i] == static_cast<std::int16_t>(i));
  }
  CHECK(state.travel.starmap_route[42] == -1);
  game::GameState empty;
  CHECK_FALSE(game::FindShortestStarmapRoute(empty, 0));
}
