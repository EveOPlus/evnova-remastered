#pragma once

// Shared drawing primitives for the port's preference dialogs. These are the
// clean-room reimplementation of the original UiWindow_Draw chrome that the
// Settings dialog (DLOG 0xfa3) needs and that the port-only Extra Prefs modal
// in preferences_extra.cpp reuses. They are small and header-inline so both
// translation units share one definition; LoadSettingsArtwork stays in
// preferences.cpp because it loads resource PICTs.

#include "../sdl_platform.hpp"
#include "control_bevel.hpp"
#include "nova_font.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace game::preferences_detail {

// The Settings dialog resource id (DLOG/DITL 0xfa3). Also used for the Extra
// Prefs modal's reused window chrome.
inline constexpr std::uint16_t kSettingsDialogId = 0xfa3;

// Shared dark metal palette, with red buttons and readable warm text.
inline constexpr SDL_Color kWindowFrame{100, 104, 110, 255};
inline constexpr SDL_Color kWindowFill{22, 24, 28, 255};
inline constexpr SDL_Color kControlHighlight{150, 153, 158, 255};
inline constexpr SDL_Color kControlFill{60, 63, 69, 255};
inline constexpr SDL_Color kControlShadow{8, 9, 12, 255};
inline constexpr SDL_Color kControlText{232, 229, 222, 255};
inline constexpr SDL_Color kControlSelectedText{255, 255, 255, 255};
// Locked controls are drawn light grey (a port addition: the original has no
// disabled state because these preferences were live toggles).
inline constexpr SDL_Color kControlDisabledFill{35, 37, 42, 255};
inline constexpr SDL_Color kControlDisabledHighlight{75, 78, 84, 255};
inline constexpr SDL_Color kControlDisabledShadow{12, 13, 16, 255};
inline constexpr SDL_Color kControlDisabledText{124, 127, 134, 255};
// Tint applied to the PICT slider arrows when their control is locked.
inline constexpr std::uint8_t kDisabledArrowTint = 150;

// Remake menu chrome: the original metal surrounds and red controls guide
// this shared palette, including the added Save Pilot and narration controls.
inline void DrawPanel(SDL_Renderer *renderer, const SDL_FRect &box) {
  DrawControlBevel(
      renderer, box, kWindowFill, kControlHighlight, kControlShadow);
  SDL_SetRenderDrawColor(renderer, 112, 28, 32, 255);
  SDL_RenderLine(renderer, box.x + 3, box.y + 3, box.x + box.w - 4, box.y + 3);
}

// Native slider-arrow PICTs (STR# 0x86/0x87).
inline constexpr std::uint16_t kSoundArrowUpPict = 0x0086;
inline constexpr std::uint16_t kSoundArrowDownPict = 0x0087;

// Redraws the owning screen before a modal is composited. With no callback it
// clears to black (the port's stand-in for the retained owner surface).
// DIVERGENCE: the original retains the owner surface and blits every modal
// surface bottom-to-top; the port redraws the owner each frame instead.
inline void DrawOwningScreen(SdlPlatform &platform,
                             const std::function<void()> &render_background) {
  SDL_Renderer *const renderer = platform.renderer();
  if (render_background) {
    render_background();
  } else {
    platform.SetPlacement(PlaceWindow(platform.logical_playfield_size()));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
  }
}

// Centres a window box (def->right-left x def->bottom-top) on the playfield,
// truncating the half-offset toward zero to match Dialog_CreateFromDlog
// (FUN_008730a1) integer arithmetic.
[[nodiscard]] inline SDL_FRect
CenterWindowOnPanel(const SDL_FRect &panel, float win_w, float win_h) {
  (void)panel;
  return SDL_FRect{0.0F, 0.0F, win_w, win_h};
}

// Draws the 17x17 checkbox and label used by UiWindow_Draw. The original uses
// the same four grayscale RGBColor triples as its push buttons.
inline void DrawCheckBox(SdlPlatform &platform,
                         NovaFontCache &font_cache,
                         const SDL_FRect &box,
                         std::string_view label,
                         bool checked,
                         bool disabled) {
  SDL_Renderer *renderer = platform.renderer();
  const SDL_Color fill = disabled ? kControlDisabledFill : kControlFill;
  const SDL_Color highlight =
      disabled ? kControlDisabledHighlight : kControlHighlight;
  const SDL_Color shadow = disabled ? kControlDisabledShadow : kControlShadow;
  const SDL_Color text = disabled ? kControlDisabledText : kControlText;
  const SDL_Color check = disabled ? kControlDisabledText : kControlText;
  const SDL_FRect glyph{box.x, box.y, 17.0F, 17.0F};
  // Checked state inverts the bevel (dark top/left, light bottom/right),
  // exactly as UiWindow_Draw 0x004d0d00 does for DITL control types 5/6.
  const SDL_Color bevel_highlight = checked ? shadow : highlight;
  const SDL_Color bevel_shadow = checked ? highlight : shadow;
  DrawControlBevel(renderer, glyph, fill, bevel_highlight, bevel_shadow);
  SDL_SetRenderDrawColor(renderer,
                         kWindowFrame.r,
                         kWindowFrame.g,
                         kWindowFrame.b,
                         SDL_ALPHA_OPAQUE);
  SDL_RenderRect(renderer, &glyph);
  if (checked) {
    // Checked mark is an X across the glyph square, inset by 3 on each side
    // (both diagonals; UiWindow_Draw 0x004d0d00).
    SDL_SetRenderDrawColor(
        renderer, check.r, check.g, check.b, SDL_ALPHA_OPAQUE);
    SDL_RenderLine(renderer,
                   glyph.x + 3.0F,
                   glyph.y + 3.0F,
                   glyph.x + 14.0F,
                   glyph.y + 14.0F);
    SDL_RenderLine(renderer,
                   glyph.x + 14.0F,
                   glyph.y + 3.0F,
                   glyph.x + 3.0F,
                   glyph.y + 14.0F);
  }
  const float baseline = box.y + box.h / 2.0F + 5.0F;
  NovaText_Draw(platform,
                font_cache,
                NovaFontFamily::kGeneva,
                12.0F,
                kNovaFontStyleRegular,
                text,
                box.x + 20.0F,
                baseline,
                label);
}

// Draws one of the small slider arrows (pointing up in the upper cell, down in
// the lower), as a small filled triangle to keep it font-independent.
inline void DrawSliderArrow(SdlPlatform &platform,
                            NovaFontCache &,
                            const SDL_FRect &box,
                            bool up,
                            bool disabled) {
  const float cx = box.x + box.w / 2.0F;
  const float cy = box.y + box.h / 2.0F;
  const float base = 4.0F; // halfwidth of the triangle base
  const float h = 6.0F;    // triangle height
  const float top_y = up ? cy - h / 2.0F : cy + h / 2.0F;
  // Fill the triangle as horizontal strips, thinnest at the tip, widest at the
  // base.
  const SDL_Color arrow = disabled ? kControlDisabledText : kControlText;
  SDL_SetRenderDrawColor(
      platform.renderer(), arrow.r, arrow.g, arrow.b, SDL_ALPHA_OPAQUE);
  const int rows = 4;
  for (int i = 0; i < rows; ++i) {
    const float t =
        (up ? static_cast<float>(i) : static_cast<float>(rows - 1 - i)) /
        static_cast<float>(rows - 1);
    const float yy =
        up ? top_y + static_cast<float>(i) : top_y - static_cast<float>(i);
    const float half = base * t;
    const float x0 = std::max(cx - half, box.x);
    const float x1 = std::min(cx + half, box.x + box.w);
    if (x1 > x0) {
      const SDL_FRect strip{x0, yy, x1 - x0, 1.0F};
      SDL_RenderFillRect(platform.renderer(), &strip);
    }
  }
}

// Draws a bordered push button with its centred label.
inline void DrawButton(SdlPlatform &platform,
                       NovaFontCache &font_cache,
                       const SDL_FRect &box,
                       std::string_view label,
                       bool highlighted,
                       bool enabled = true,
                       float font_size = 12.0F) {
  SDL_Renderer *renderer = platform.renderer();
  const SDL_Color highlight = highlighted ? kControlShadow : kControlHighlight;
  const SDL_Color shadow = highlighted ? kControlHighlight : kControlShadow;
  DrawControlBevel(renderer, box, kControlFill, highlight, shadow);
  const SDL_FRect face{box.x + 3, box.y + 3, box.w - 6, box.h - 6};
  SDL_SetRenderDrawColor(renderer,
                         enabled ? (highlighted ? 106 : 74) : 36,
                         enabled ? 18 : 36,
                         enabled ? 23 : 40,
                         255);
  SDL_RenderFillRect(renderer, &face);
  SDL_SetRenderDrawColor(renderer,
                         enabled ? (highlighted ? 145 : 104) : 50,
                         enabled ? 38 : 50,
                         enabled ? 43 : 55,
                         255);
  SDL_RenderLine(renderer, face.x, face.y, face.x + face.w, face.y);
  SDL_SetRenderDrawColor(renderer,
                         kWindowFrame.r,
                         kWindowFrame.g,
                         kWindowFrame.b,
                         SDL_ALPHA_OPAQUE);
  SDL_RenderRect(renderer, &box);
  NovaText_DrawCentered(platform,
                        font_cache,
                        NovaFontFamily::kGeneva,
                        font_size,
                        kNovaFontStyleRegular,
                        !enabled      ? kControlDisabledText
                        : highlighted ? kControlSelectedText
                                      : kControlText,
                        box.x + 4.0F,
                        box.x + box.w - 4.0F,
                        box.y + box.h / 2.0F + 4.0F,
                        label);
}

struct SettingsArtwork {
  std::unique_ptr<SdlTexture> arrow_up;
  std::unique_ptr<SdlTexture> arrow_down;
};

// Loads the native slider-arrow PICTs (defined in preferences.cpp).
[[nodiscard]] SettingsArtwork LoadSettingsArtwork(SdlPlatform &platform);

} // namespace game::preferences_detail
