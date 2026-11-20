#pragma once

#include "trade_center.hpp"

#include <functional>
#include <optional>
#include <span>
#include <vector>

class SdlPlatform;

namespace game {

// Remake comparison view. Uses the ordinary Trade Center price snapshot and
// never enters a landing/mission-offer context or modifies cargo and credits.
struct SystemMarket {
  std::string name;
  TradeCenterSession prices;
};

[[nodiscard]] std::vector<SystemMarket>
BuildSystemMarkets(const GameState &state);

struct SystemMarketPrice {
  int minimum = 0;
  int maximum = 0;
};

struct SystemMarketGood {
  std::string key;
  std::string name;
  int held = 0;
  int base_price = 0;
  bool mission_cargo = false;
  // One cell per market; zero means unavailable. Multiple offers for the
  // same special cargo at one port retain both extremes.
  std::vector<SystemMarketPrice> prices;
};

[[nodiscard]] std::vector<SystemMarketGood>
BuildSystemMarketGoods(const GameState &state,
                       std::span<const SystemMarket> markets);
// Returns the selected raw stellar ID only when Auto Dock is chosen.
[[nodiscard]] std::optional<std::int16_t>
RunSystemMarkets(SdlPlatform &platform,
                 const GameState &state,
                 const std::function<void()> &draw_background);

} // namespace game
