#include "game/system_markets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("System market comparison lists each local trading port once",
          "[markets][trade]") {
  game::GameState state;
  state.scenario.systems.resize(2);
  state.scenario.stellars.resize(5);
  state.player.current_system_id = 0;
  state.player.credits = 5000;
  state.inventory.cargo_bins[0] = 7;
  auto &system = state.scenario.systems[0];
  system.nav_defs.fill(-1);
  system.nav_defs[0] = 0x80;
  system.nav_defs[1] = 0x81;
  system.nav_defs[2] = 0x82;
  system.nav_defs[3] = 0x80; // Malformed duplicate must not duplicate a market.
  system.nav_defs[4] = 0x83; // No commodity exchange.
  system.nav_defs[5] = 0x7fff; // Invalid resource reference.
  for (auto &stellar : state.scenario.stellars)
    stellar.flags = 2;
  state.scenario.stellars[0].name = "Low port";
  state.scenario.stellars[0].flags = 0x10000002U;
  state.scenario.stellars[1].name = "Medium port";
  state.scenario.stellars[1].flags = 0x20000002U;
  state.scenario.stellars[2].name = "High port";
  state.scenario.stellars[2].flags = 0x40000002U;
  state.scenario.stellars[3].flags = 0x20;
  state.scenario.stellars[4].name = "Other system";
  const auto markets = game::BuildSystemMarkets(state);
  REQUIRE(markets.size() == 3);
  CHECK(markets[0].name == "Low port");
  CHECK(markets[0].prices.rows[0].price == 60);
  CHECK(markets[1].prices.rows[0].price == 75);
  CHECK(markets[2].prices.rows[0].price == 93);
  CHECK(markets[0].prices.rows[1].price == 0);
  CHECK(state.player.credits == 5000);
  CHECK(state.inventory.cargo_bins[0] == 7);
}

TEST_CASE("System market prices include current reputation, disasters and "
          "special cargo",
          "[markets][trade]") {
  game::GameState state;
  state.player.current_system_id = 0;
  state.scenario.systems.resize(1);
  state.scenario.systems[0].nav_defs.fill(-1);
  state.scenario.systems[0].nav_defs[0] = 0x80;
  state.scenario.stellars.resize(1);
  auto &stellar = state.scenario.stellars[0];
  stellar.flags = 0x11000002U;
  stellar.government_id = 0;
  stellar.system_id = 0;
  state.system_reputation = {-10};
  state.scenario.disaster_defs.resize(1);
  auto &disaster = state.scenario.disaster_defs[0];
  disaster.present = true;
  disaster.days_remaining = 2;
  disaster.active_stellar = 0;
  disaster.commodity = 0;
  disaster.price_delta = 100;
  state.scenario.junk_defs.resize(1);
  auto &junk = state.scenario.junk_defs[0];
  junk.present = true;
  junk.base_price = 100;
  junk.bought_at.fill(-1);
  junk.sold_at.fill(-1);
  junk.bought_at[0] = 0;
  junk.sold_at[0] = 0;
  const auto markets = game::BuildSystemMarkets(state);
  REQUIRE(markets.size() == 1);
  const auto &rows = markets.front().prices.rows;
  CHECK(rows[0].price == 175);
  CHECK(rows[0].has_disaster);
  CHECK(rows[1].price == 318);
  CHECK(rows[6].price == 110);
  CHECK(rows[7].price == 90);
  state.scenario.systems[0].nav_defs.fill(-1);
  CHECK(game::BuildSystemMarkets(state).empty());
  state.player.current_system_id = -1;
  CHECK(game::BuildSystemMarkets(state).empty());
}

TEST_CASE("Market goods include every local offer and carried cargo",
          "[markets][trade]") {
  game::GameState state;
  state.inventory.cargo_bins[5] = 9;
  state.inventory.junk_counts[0] = 2;
  state.inventory.junk_counts[3] = 4;
  state.scenario.junk_defs.resize(4);
  for (std::size_t i = 0; i < 4; ++i) {
    auto &junk = state.scenario.junk_defs[i];
    junk.present = true;
    junk.display_name = "Cargo " + std::to_string(i);
    junk.base_price = 100;
  }
  state.active_mission_runtime_flags[0].is_active = true;
  state.active_missions[0].carrying_resources = true;
  state.active_missions[0].cargo_type_id = 12;
  state.active_missions[0].cargo_qty_tons = 20;
  std::vector<game::SystemMarket> markets(2);
  markets[0].prices.rows[0].price = 75;
  markets[0].prices.rows[6] = {90, 1, 0};
  markets[0].prices.rows[7] = {110, 4, 0};
  markets[1].prices.rows[6] = {50, 1, 1};
  markets[1].prices.rows[7] = {200, 4, 2};
  const auto goods = game::BuildSystemMarketGoods(state, markets);
  REQUIRE(goods.size() == 7);
  const auto find =
      [&](std::string_view key) -> const game::SystemMarketGood & {
    const auto it =
        std::find_if(goods.begin(), goods.end(), [&](const auto &good) {
          return good.key == key;
        });
    REQUIRE(it != goods.end());
    return *it;
  };
  CHECK(find("commodity.0").prices[0].minimum == 75);
  CHECK(find("commodity.0").prices[1].minimum == 0);
  CHECK(find("commodity.5").held == 9);
  CHECK(find("commodity.5").prices[0].maximum == 0);
  const auto &both_offers = find("junk.0");
  CHECK(both_offers.prices[0].minimum == 90);
  CHECK(both_offers.prices[0].maximum == 110);
  CHECK(both_offers.held == 2);
  CHECK(find("junk.1").prices[1].minimum == 50);
  CHECK(find("junk.2").prices[1].minimum == 200);
  CHECK(find("junk.3").held == 4);
  CHECK(find("junk.3").prices[1].maximum == 0);
  CHECK(find("mission.12").mission_cargo);
  CHECK(find("mission.12").held == 20);
  CHECK(find("mission.12").prices[0].maximum == 0);
  CHECK(state.inventory.junk_counts[3] == 4);
  const auto cargo_only = game::BuildSystemMarketGoods(state, {});
  CHECK(cargo_only.size() == 4);
  CHECK(std::all_of(cargo_only.begin(), cargo_only.end(), [](const auto &good) {
    return good.held > 0 && good.prices.empty();
  }));
}
