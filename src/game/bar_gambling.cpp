#include "bar_gambling.hpp"

#include "../brgr_archive.hpp"
#include "../log.hpp"
#include "../util/format.hpp"
#include "docked_dialog_internal.hpp"
#include "hud_overlay.hpp"
#include "mission.hpp"
#include "nova_font.hpp"
#include "nova_random.hpp"
#include "selection_text_dialog.hpp"
#include "services_buttons.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace game {
std::optional<BarRaceResult>
PlaceBarRaceBet(GameState &state, int choice, int wager) {
  if (choice < 0 || choice >= 4 || wager <= 0 || wager > 10000 ||
      wager > state.player.credits)
    return std::nullopt;
  state.player.credits -= wager;
  int winner = RandomBelow(state, 4);
  // Original quirk: only a repeated first roll rerolls, and those retries
  // reject both the previous winner AND the player's chosen color.
  if (winner == state.bar_race_previous_winner) {
    do {
      winner = RandomBelow(state, 4);
    } while (winner == state.bar_race_previous_winner || winner == choice);
  }
  state.bar_race_previous_winner = winner;
  return BarRaceResult{winner, wager, winner == choice ? wager * 4 : 0};
}

namespace {
struct GamblingView {
  SDL_FRect window{};
  std::array<SDL_FRect, 6> buttons{};
  std::unique_ptr<SdlTexture> background;
  std::array<std::array<std::unique_ptr<SdlTexture>, 4>, 4> cards;
  ServicesButtonArt button_art;
  NovaFontCache fonts;
  int winner = -1;
  int races = 0;
};

// @port 0x004A2C30 100% divergence
// Ghidra 0x004a2c30 OutfitterMenu_FUN_004a2c30. The racing-window button
// renderer: two standard buttons plus four caller-supplied color cards.
void DrawBarGamblingButtons(SdlPlatform &platform, GamblingView &view) {
  // DIVERGENCE(original): SDL hover events replace native click tracking.
  const auto pointer = platform.mouse_position();
  for (std::size_t i = 0; i < view.buttons.size(); ++i) {
    const auto &rect = view.buttons[i];
    const bool hovered = Contains(rect, pointer);
    if (i < 2) {
      view.button_art.Draw(
          platform, rect, hovered ? ButtonState::kHover : ButtonState::kNormal);
      const auto label_id = i == 1 ? 0x39 : (view.races == 0 ? 0x21 : 4);
      const auto label = NovaHud_LoadStringEntry(0x96, label_id + 1)
                             .value_or(i == 1 ? "Help" : "Done");
      DrawThreeStateButtonLabel(
          platform, view.fonts, rect, label, SDL_Color{255, 255, 255, 255});
    } else {
      const auto card = i - 2;
      const std::size_t variant = view.winner == static_cast<int>(card) ? 2
                                  : view.winner >= 0                    ? 3
                                  : hovered                             ? 1
                                                                        : 0;
      if (const auto &texture = view.cards[card][variant])
        SDL_RenderTexture(platform.renderer(), texture->get(), nullptr, &rect);
    }
  }
}

// @port 0x0047E1E0 100% divergence
// Ghidra 0x0047e1e0 NovaUi_DrawBarGamblingWindow. Rebuilds the race backdrop,
// button/card states and current credit display over the parent bar.
void DrawBarGamblingWindow(SdlPlatform &platform,
                           const GameState &state,
                           GamblingView &view,
                           const std::function<void()> &render_background) {
  // DIVERGENCE(original): a readable footer shows credits and the custom-bet
  // shortcut; the original racing art and authored hit rectangles are retained.
  if (render_background)
    render_background();
  platform.SetPlacement(PlaceContained({view.window.w, view.window.h + 32},
                                       platform.logical_playfield_size(),
                                       platform.ui_scale()));
  SDL_SetRenderDrawColor(platform.renderer(), 18, 20, 24, 255);
  const SDL_FRect panel{0, 0, view.window.w, view.window.h + 32};
  SDL_RenderFillRect(platform.renderer(), &panel);
  if (view.background)
    SDL_RenderTexture(
        platform.renderer(), view.background->get(), nullptr, &view.window);
  DrawBarGamblingButtons(platform, view);
  NovaText_Draw(
      platform,
      view.fonts,
      NovaFontFamily::kGeneva,
      11,
      kNovaFontStyleRegular,
      {220, 224, 232, 255},
      12,
      view.window.h + 20,
      "Credits: " + evnova::util::GroupThousands(state.player.credits) +
          "    Bet: " +
          evnova::util::GroupThousands(
              std::clamp(state.player.credits, 0, 1000)) +
          "    Alt-click: custom bet");
}
} // namespace

// @port 0x0047DC50 95% ui,divergence,moddata
// Ghidra 0x0047dc50 NovaUi_RunBarGamblingWindow. DLOG 0x3ff, four race choices,
// default 1000-credit stake, Alt quantity prompt up to 10000, result
// desc/movie, four-times gross payout. The betting arm lives in PlaceBarRaceBet
// above.
void RunBarGamblingWindow(SdlPlatform &platform,
                          GameState &state,
                          const std::function<void()> &render_background) {
  // DIVERGENCE(original): session mt19937 replaces the original PRNG; SDL
  // handles modal events and movies. Saturate extreme edited credit balances.
  // TODO(decomp): verify additional key bindings in callback LAB_0047e150.
  const auto dlog = NovaResource_LoadDialogDefinition(0x3ff);
  const auto items =
      dlog ? NovaResource_LoadDialogItems(dlog->dialog_item_list_id)
           : std::nullopt;
  if (!dlog || !items) {
    NovaLog::Error("gambling DLOG/DITL 0x3ff unavailable");
    NovaUi_RunTextReaderDialog(platform,
                               state,
                               "The racing game resources could not be loaded.",
                               false,
                               render_background);
    return;
  }
  const SdlPlatform::ScopedPlacement restore(platform,
                                             platform.current_placement());
  const ProbeUiAutoClear probe_guard(platform);
  GamblingView view;
  view.window = {0,
                 0,
                 static_cast<float>(dlog->right - dlog->left),
                 static_cast<float>(dlog->bottom - dlog->top)};
  for (const auto &item : *items) {
    if (item.index < view.buttons.size())
      view.buttons[item.index] = {static_cast<float>(item.left),
                                  static_cast<float>(item.top),
                                  static_cast<float>(item.right - item.left),
                                  static_cast<float>(item.bottom - item.top)};
  }
  view.background = LoadPictTexture(platform, 0x2151);
  (void)view.button_art.Initialize(platform);
  for (std::size_t color = 0; color < 4; ++color)
    for (std::size_t variant = 0; variant < 4; ++variant)
      view.cards[color][variant] = LoadPictTexture(
          platform, static_cast<std::uint16_t>(0x2152 + variant * 10 + color));
  const auto draw = [&] {
    DrawBarGamblingWindow(platform, state, view, render_background);
  };
  const auto describe = [&](std::uint16_t id, int payout) {
    auto message = Mission_LoadSelectionDialogText(state, id, false, -1);
    if (payout > 0)
      message.text +=
          "\r\r" +
          NovaHud_LoadStringEntry(0x7d2, 0x172).value_or("Your winnings") +
          ": " + evnova::util::GroupThousands(payout) + " credits.";
    NovaUi_RunTextReaderDialog(platform,
                               state,
                               message.text,
                               false,
                               draw,
                               message.dialog_variant,
                               false,
                               message.movie);
  };
  constexpr std::array<const char *, 6> names{
      "done", "help", "bet.blue", "bet.green", "bet.yellow", "bet.red"};
  while (!platform.quit_requested()) {
    draw();
    std::vector<std::pair<std::string, SDL_FRect>> controls;
    for (std::size_t i = 0; i < names.size(); ++i)
      controls.emplace_back(
          names[i], platform.current_placement().ToWindowRect(view.buttons[i]));
    platform.PublishProbeUi("bar_gambling", std::move(controls));
    platform.Present();
    for (auto event = platform.PollTextEvent(); event;
         event = platform.PollTextEvent()) {
      if (event->key == TextKey::escape || event->key == TextKey::enter)
        return;
      int action = -1;
      if (event->key == TextKey::primary) {
        for (std::size_t i = 0; i < view.buttons.size(); ++i)
          if (Contains(view.buttons[i], platform.mouse_position()))
            action = static_cast<int>(i);
      }
      if (action == 0)
        return;
      if (action == 1) {
        describe(0x7ffc, 0);
        break;
      }
      if (action < 2)
        continue;
      if (view.winner >= 0) {
        view.winner =
            -1; // A click after the result resets without another bet.
        break;
      }
      int wager = std::min(state.player.credits, 1000);
      if (event->alt || platform.IsOriginalKeyCodeHeld(0x38) ||
          platform.IsOriginalKeyCodeHeld(0x6f))
        wager = RunStoreQuantityPrompt(platform,
                                       static_cast<std::int16_t>(std::clamp(
                                           state.player.credits, 0, 10000)),
                                       draw,
                                       true);
      const auto result = PlaceBarRaceBet(state, action - 2, wager);
      if (!result)
        break;
      view.winner = result->winner;
      ++view.races;
      describe(static_cast<std::uint16_t>(0x7ff8 + result->winner),
               result->payout);
      state.player.credits = static_cast<std::int32_t>(std::min<std::int64_t>(
          static_cast<std::int64_t>(state.player.credits) + result->payout,
          std::numeric_limits<std::int32_t>::max()));
      break;
    }
    SDL_Delay(10);
  }
}
} // namespace game
