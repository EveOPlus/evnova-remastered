#include "system_markets.hpp"

#include "hud_overlay.hpp"
#include "mission.hpp"
#include "mission_navigation.hpp"
#include "nova_font.hpp"
#include "preferences_ui.hpp"
#include "starmap.hpp"
#include "util/format.hpp"
#include "util/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace game {

std::vector<SystemMarket> BuildSystemMarkets(const GameState &state) {
  std::vector<SystemMarket> result;
  const auto *system = state.scenario.System(
      static_cast<std::int16_t>(state.player.current_system_id + 0x80));
  if (!system)
    return result;
  for (const auto id : system->nav_defs) {
    const auto *stellar = state.scenario.Stellar(id);
    if (!stellar ||
        std::any_of(result.begin(), result.end(), [id](const auto &market) {
          return market.prices.stellar_id == id;
        }))
      continue;
    SystemMarket market{stellar->name, NovaTradeCenter_OpenSession(state, id)};
    market.has_market = (stellar->flags & 0x2U) != 0;
    for (std::size_t slot = 0; slot < state.active_missions.size(); ++slot) {
      const auto &mission = state.active_missions[slot];
      const auto &flags = state.active_mission_runtime_flags[slot];
      if (!flags.is_active || flags.is_failed || (mission.flags_primary & 2U))
        continue;
      const auto destination = MissionDestinationStellar(state, mission, flags);
      if (destination >= 0x80 &&
          NovaStellar_AreStellarsEquivalent(
              state,
              static_cast<std::int16_t>(destination - 0x80),
              static_cast<std::int16_t>(id - 0x80)))
        ++market.mission_count;
    }
    if (!market.has_market) {
      if (market.mission_count == 0)
        continue;
      market.prices
          .rows = {}; // A mission stop need not have a commodity exchange.
    }
    result.push_back(std::move(market));
  }
  return result;
}

std::vector<SystemMarketGood>
BuildSystemMarketGoods(const GameState &state,
                       std::span<const SystemMarket> markets) {
  std::vector<SystemMarketGood> goods;
  const auto add = [&](std::string key,
                       std::string name,
                       int held,
                       int base) -> SystemMarketGood & {
    auto existing =
        std::find_if(goods.begin(), goods.end(), [&](const auto &good) {
          return good.key == key;
        });
    if (existing != goods.end())
      return *existing;
    goods.push_back({std::move(key),
                     std::move(name),
                     held,
                     base,
                     false,
                     std::vector<SystemMarketPrice>(markets.size())});
    return goods.back();
  };
  const auto add_price =
      [](SystemMarketGood &good, std::size_t market, int price) {
        if (price <= 0)
          return;
        auto &cell = good.prices[market];
        cell.minimum = cell.minimum > 0 ? std::min(cell.minimum, price) : price;
        cell.maximum = std::max(cell.maximum, price);
      };
  const auto base = TradeCenter_BasePrices();
  for (std::size_t c = 0; c < kTradeCenterCommodityCount; ++c) {
    const int held = state.inventory.cargo_bins[c];
    if (held <= 0 &&
        std::none_of(markets.begin(), markets.end(), [c](const auto &market) {
          return market.prices.rows[c].price > 0;
        }))
      continue;
    auto &good = add("commodity." + std::to_string(c),
                     NovaTradeCenter_RowName(state, {}, static_cast<int>(c)),
                     held,
                     base[c]);
    for (std::size_t m = 0; m < markets.size(); ++m)
      add_price(good, m, markets[m].prices.rows[c].price);
  }
  const auto add_junk = [&](std::size_t id) -> SystemMarketGood & {
    const auto *junk =
        state.scenario.Junk(static_cast<std::int16_t>(id + 0x80));
    const int held = id < state.inventory.junk_counts.size()
                         ? state.inventory.junk_counts[id]
                         : 0;
    return add("junk." + std::to_string(id),
               junk ? junk->display_name
                    : "Special cargo #" + std::to_string(id + 1),
               held,
               junk ? junk->base_price : 0);
  };
  for (std::size_t m = 0; m < markets.size(); ++m) {
    for (std::size_t row = kTradeCenterCommodityCount;
         row < kTradeCenterRowCount;
         ++row) {
      const auto &offer = markets[m].prices.rows[row];
      if (offer.junk_id >= 0 && offer.price > 0)
        add_price(
            add_junk(static_cast<std::size_t>(offer.junk_id)), m, offer.price);
    }
  }
  for (std::size_t id = 0; id < state.inventory.junk_counts.size(); ++id) {
    if (state.inventory.junk_counts[id] > 0)
      (void)add_junk(id);
  }
  // Mission freight is carried but cannot be sold at a commodity exchange.
  // Keep it separate even when its cargo name matches a tradable commodity.
  for (std::size_t slot = 0; slot < state.active_missions.size(); ++slot) {
    const auto &mission = state.active_missions[slot];
    if (!state.active_mission_runtime_flags[slot].is_active ||
        !mission.carrying_resources || mission.cargo_type_id < 0 ||
        mission.cargo_qty_tons <= 0)
      continue;
    auto &good =
        add("mission." + std::to_string(mission.cargo_type_id),
            NovaHud_LoadStringEntry(
                0xfa0, static_cast<std::uint16_t>(mission.cargo_type_id + 1))
                    .value_or("Mission cargo") +
                " (mission)",
            0,
            0);
    good.mission_cargo = true;
    good.held += mission.cargo_qty_tons;
  }
  return goods;
}

namespace {
constexpr int kGoodsPerPage = 10;
constexpr int kMarketsPerPage = 4;
constexpr float kWidth = 900;
constexpr float kHeight = 610;
constexpr SDL_FRect kDone{780, 556, 100, 30};
constexpr SDL_FRect kAutoDock{640, 556, 128, 30};
constexpr SDL_FRect kPreviousMarket{20, 556, 105, 30};
constexpr SDL_FRect kNextMarket{137, 556, 105, 30};
constexpr SDL_FRect kPreviousGood{20, 486, 105, 28};
constexpr SDL_FRect kNextGood{137, 486, 105, 28};
constexpr SDL_Color kMuted{160, 164, 172, 255};
constexpr SDL_Color kBest{116, 242, 155, 255};
constexpr SDL_Color kGood{148, 192, 161, 255};
constexpr SDL_Color kWorstSell{255, 112, 117, 255};
constexpr SDL_Color kLowSell{204, 151, 154, 255};
constexpr SDL_Color kWorstBuy{255, 207, 105, 255};
constexpr SDL_Color kHighBuy{199, 180, 141, 255};

SDL_Color
PriceColor(int price, int lowest, int highest, int base, bool carried) {
  if (price <= 0)
    return kMuted;
  if (lowest < highest) {
    if (price == lowest)
      return carried ? kWorstSell : kBest;
    if (price == highest)
      return carried ? kBest : kWorstBuy;
  }
  if (price < base)
    return carried ? kLowSell : kGood;
  if (price > base)
    return carried ? kGood : kHighBuy;
  return preferences_detail::kControlText;
}

SDL_FRect MarketHeading(int column, int count) {
  const float width = 540.0F / static_cast<float>(count);
  return {340 + static_cast<float>(column) * width, 86, width, 44};
}
} // namespace

std::optional<std::int16_t>
RunSystemMarkets(SdlPlatform &platform,
                 const GameState &state,
                 const std::function<void()> &draw_background) {
  using namespace preferences_detail;
  using evnova::util::Contains;
  using evnova::util::GroupThousands;
  const ProbeUiAutoClear probe_guard(platform);
  const auto markets = BuildSystemMarkets(state);
  const auto goods = BuildSystemMarketGoods(state, markets);
  const auto *system = state.scenario.System(
      static_cast<std::int16_t>(state.player.current_system_id + 0x80));
  const std::string title =
      "System Markets" + (system ? " - " + system->name : std::string{});
  NovaFontCache fonts;
  const auto mission_icons = NovaStarmap_LoadMarkerIcons(platform);
  int market_page = 0;
  int goods_page = 0;
  int selected = 0;
  const int market_pages =
      std::max(1,
               (static_cast<int>(markets.size()) + kMarketsPerPage - 1) /
                   kMarketsPerPage);
  const int goods_pages = std::max(
      1, (static_cast<int>(goods.size()) + kGoodsPerPage - 1) / kGoodsPerPage);
  const auto text = [&](float x,
                        float y,
                        float width,
                        std::string label,
                        float size = 15,
                        SDL_Color color = kControlText,
                        bool bold = false) {
    const auto style = bold ? kNovaFontStyleBold : kNovaFontStyleRegular;
    if (fonts.TextWidth(NovaFontFamily::kGeneva, size, style, label) > width) {
      while (!label.empty() &&
             fonts.TextWidth(
                 NovaFontFamily::kGeneva, size, style, label + "...") > width)
        label.pop_back();
      label += "...";
    }
    NovaText_Draw(platform,
                  fonts,
                  NovaFontFamily::kGeneva,
                  size,
                  style,
                  color,
                  x,
                  y,
                  label);
  };
  while (!platform.quit_requested()) {
    const auto placement = PlaceContained({kWidth, kHeight},
                                          platform.logical_playfield_size(),
                                          platform.ui_scale());
    for (std::optional<TextInput> event; (event = platform.PollTextEvent());) {
      if (event->key == TextKey::escape || event->key == TextKey::enter)
        return std::nullopt;
      const auto mouse = placement.ToAuthored(platform.mouse_window_point());
      if (event->key == TextKey::wheel && std::isfinite(event->wheel_y))
        goods_page = std::clamp(goods_page + (event->wheel_y < 0   ? 1
                                              : event->wheel_y > 0 ? -1
                                                                   : 0),
                                0,
                                goods_pages - 1);
      if (event->key != TextKey::primary)
        continue;
      if (Contains(kDone, mouse))
        return std::nullopt;
      if (Contains(kAutoDock, mouse) && !markets.empty())
        return markets[static_cast<std::size_t>(selected)].prices.stellar_id;
      if (Contains(kPreviousGood, mouse))
        goods_page = std::max(0, goods_page - 1);
      if (Contains(kNextGood, mouse))
        goods_page = std::min(goods_pages - 1, goods_page + 1);
      const int previous_page = market_page;
      if (Contains(kPreviousMarket, mouse))
        market_page = std::max(0, market_page - 1);
      if (Contains(kNextMarket, mouse))
        market_page = std::min(market_pages - 1, market_page + 1);
      if (market_page != previous_page)
        selected = market_page * kMarketsPerPage;
      const int columns = std::min(kMarketsPerPage,
                                   static_cast<int>(markets.size()) -
                                       market_page * kMarketsPerPage);
      for (int column = 0; column < columns; ++column) {
        auto box = MarketHeading(column, columns);
        box.h = 389; // Select a market from its heading or any price cell.
        if (Contains(box, mouse))
          selected = market_page * kMarketsPerPage + column;
      }
    }
    DrawOwningScreen(platform, draw_background);
    const SdlPlatform::ScopedPlacement scope(platform, placement);
    auto *renderer = platform.renderer();
    DrawPanel(renderer, {0, 0, kWidth, kHeight});
    text(24, 39, 850, title, 22);
    text(24,
         68,
         850,
         markets.empty()
             ? "No trading ports here. Showing your cargo."
             : "Auto Dock destination: " +
                   markets[static_cast<std::size_t>(selected)].name +
                   "  -  click a market column to select.",
         14,
         kMuted);
    text(30, 115, 232, "Goods", 15, kMuted);
    text(270, 115, 65, "Tons", 14, kMuted);
    std::vector<ProbeNamedRect> controls;
    const int columns = std::min(kMarketsPerPage,
                                 static_cast<int>(markets.size()) -
                                     market_page * kMarketsPerPage);
    for (int column = 0; column < columns; ++column) {
      const int index = market_page * kMarketsPerPage + column;
      const auto &market = markets[static_cast<std::size_t>(index)];
      const auto box = MarketHeading(column, columns);
      const bool active = index == selected;
      SDL_SetRenderDrawColor(
          renderer, active ? 82 : 40, active ? 27 : 43, active ? 32 : 48, 255);
      SDL_RenderFillRect(renderer, &box);
      const bool mission_here = market.mission_count > 0;
      const float name_inset = mission_here ? 32.0F : 10.0F;
      text(box.x + name_inset,
           mission_here ? 104.0F : 113.0F,
           box.w - name_inset - 10,
           market.name,
           15,
           kControlText,
           active);
      if (mission_here) {
        const SDL_FRect badge{box.x + 8, box.y + 5, 18, 18};
        if (mission_icons.mission_target)
          SDL_RenderTexture(
              renderer, mission_icons.mission_target->get(), nullptr, &badge);
        else
          text(badge.x + 4, badge.y + 16, 14, "!", 17, kWorstBuy, true);
        text(box.x + 10,
             122,
             box.w - 20,
             market.has_market ? (market.mission_count == 1
                                      ? "Mission here"
                                      : std::to_string(market.mission_count) +
                                            " missions here")
                               : "Mission / No market",
             11,
             kWorstBuy);
        ProbeNamedRect mission_item{
            "market." + std::to_string(market.prices.stellar_id) + ".mission",
            placement.ToWindowRect(badge)};
        mission_item.has_value = true;
        mission_item.value = market.mission_count;
        mission_item.label = "Mission destination";
        controls.push_back(std::move(mission_item));
      }
      ProbeNamedRect item{"market." + std::to_string(market.prices.stellar_id),
                          placement.ToWindowRect(box)};
      item.has_value = true;
      item.selected = active;
      item.label = market.name;
      item.value = market.prices.stellar_id;
      controls.push_back(std::move(item));
    }
    for (int row = 0; row < kGoodsPerPage; ++row) {
      const int index = goods_page * kGoodsPerPage + row;
      if (index >= static_cast<int>(goods.size()))
        break;
      const auto &good = goods[static_cast<std::size_t>(index)];
      const bool carried = good.held > 0;
      const SDL_FRect box{20, 135 + static_cast<float>(row) * 34, 860, 34};
      SDL_SetRenderDrawColor(renderer,
                             row % 2 == 0 ? 31 : 26,
                             row % 2 == 0 ? 34 : 29,
                             row % 2 == 0 ? 39 : 33,
                             255);
      SDL_RenderFillRect(renderer, &box);
      text(30, box.y + 23, 232, good.name, 15, kControlText, carried);
      text(270,
           box.y + 23,
           65,
           carried ? GroupThousands(good.held) : "-",
           15,
           carried ? kControlText : kMuted,
           carried);
      ProbeNamedRect item{good.key,
                          placement.ToWindowRect({20, box.y, 310, box.h})};
      item.has_value = true;
      item.value = good.held;
      item.label = good.name;
      controls.push_back(std::move(item));
      int lowest = std::numeric_limits<int>::max();
      int highest = 0;
      for (const auto &price : good.prices) {
        if (price.minimum > 0)
          lowest = std::min(lowest, price.minimum);
        highest = std::max(highest, price.maximum);
      }
      for (int column = 0; column < columns; ++column) {
        const auto market_index =
            static_cast<std::size_t>(market_page * kMarketsPerPage + column);
        const auto &price = good.prices[market_index];
        const auto heading = MarketHeading(column, columns);
        const SDL_FRect cell{heading.x, box.y, heading.w, box.h};
        if (static_cast<int>(market_index) == selected) {
          SDL_SetRenderDrawColor(renderer, row % 2 == 0 ? 46 : 40, 28, 34, 255);
          SDL_RenderFillRect(renderer, &cell);
        }
        const auto draw_price = [&](int amount, float x, float width) {
          text(x,
               box.y + 23,
               width,
               GroupThousands(amount),
               16,
               PriceColor(amount, lowest, highest, good.base_price, carried),
               carried);
        };
        if (price.minimum <= 0)
          text(cell.x + 10,
               box.y + 23,
               cell.w - 20,
               good.mission_cargo ? "Mission" : "-",
               14,
               kMuted);
        else if (price.minimum == price.maximum)
          draw_price(price.minimum, cell.x + 10, cell.w - 20);
        else {
          draw_price(price.minimum, cell.x + 10, cell.w / 2 - 20);
          text(cell.x + cell.w / 2 - 5, box.y + 23, 10, "/", 14, kMuted);
          draw_price(price.maximum, cell.x + cell.w / 2 + 10, cell.w / 2 - 20);
        }
        ProbeNamedRect value{
            "market." +
                std::to_string(markets[market_index].prices.stellar_id) + "." +
                good.key,
            placement.ToWindowRect(cell)};
        value.has_value = true;
        value.label = good.name;
        value.value = price.minimum;
        controls.push_back(std::move(value));
      }
    }
    if (goods.empty())
      text(30,
           170,
           830,
           "No goods for sale here and your cargo hold is empty.",
           17);
    const auto mouse = placement.ToAuthored(platform.mouse_window_point());
    const auto button = [&](const char *key,
                            const SDL_FRect &box,
                            const char *label,
                            bool enabled) {
      DrawButton(
          platform, fonts, box, label, Contains(box, mouse), enabled, 14);
      if (enabled)
        controls.emplace_back(key, placement.ToWindowRect(box));
    };
    if (goods_pages > 1) {
      button("goods_previous", kPreviousGood, "Previous", goods_page > 0);
      button("goods_next", kNextGood, "Next", goods_page + 1 < goods_pages);
    }
    text(260,
         506,
         620,
         "Goods " +
             std::to_string(goods_page * kGoodsPerPage +
                            (goods.empty() ? 0 : 1)) +
             "-" +
             std::to_string(std::min(static_cast<int>(goods.size()),
                                     (goods_page + 1) * kGoodsPerPage)) +
             " of " + std::to_string(goods.size()) +
             ". Bold = in cargo. Prices per ton; - = not traded.",
         12,
         kMuted);
    const auto legend = [&](float y, bool held) {
      text(24,
           y,
           138,
           held ? "In cargo (selling):" : "Other goods (buying):",
           12,
           kMuted);
      text(170, y, 68, "Lowest", 12, held ? kWorstSell : kBest, held);
      text(245, y, 58, "Low", 12, held ? kLowSell : kGood, held);
      text(310, y, 58, "High", 12, held ? kGood : kHighBuy, held);
      text(375, y, 80, "Highest", 12, held ? kBest : kWorstBuy, held);
    };
    legend(530, true);
    legend(548, false);
    text(475,
         530,
         400,
         "Lowest/highest: across all markets in this system.",
         12,
         kMuted);
    text(475,
         548,
         400,
         "Low/high: versus normal price. Dock to trade.",
         12,
         kMuted);
    button("done", kDone, "Done", true);
    button("auto_dock", kAutoDock, "Auto Dock", !markets.empty());
    if (market_pages > 1) {
      button("markets_previous",
             kPreviousMarket,
             "Previous ports",
             market_page > 0);
      button("markets_next",
             kNextMarket,
             "Next ports",
             market_page + 1 < market_pages);
      text(260,
           577,
           360,
           "Markets " + std::to_string(market_page + 1) + " / " +
               std::to_string(market_pages),
           14,
           kMuted);
    }
    platform.PublishProbeUiItems("system_markets", std::move(controls));
    platform.Present();
    platform.PaceFrame();
  }
  return std::nullopt;
}

} // namespace game
