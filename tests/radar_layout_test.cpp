#include "game/radar_layout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using game::RadarContact;
using game::RadarIcon;

TEST_CASE("Radar icons remain visible below native HUD scale", "[radar]") {
  const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{
      SDL_CreateSurface(40, 40, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface};
  REQUIRE(surface);
  const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{
      SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer};
  REQUIRE(renderer);
  for (const float scale : {.5F, .933F, 1.3F}) {
    for (const RadarIcon icon : {RadarIcon::player,
                                 RadarIcon::ship,
                                 RadarIcon::planet,
                                 RadarIcon::station,
                                 RadarIcon::gate,
                                 RadarIcon::disabled}) {
      CAPTURE(scale, static_cast<int>(icon));
      SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
      SDL_RenderClear(renderer.get());
      SDL_SetRenderScale(renderer.get(), scale, scale);
      game::DrawRadarIcon(
          renderer.get(), icon, 20, 20, 5, .6F, {255, 255, 255, 255}, false);
      REQUIRE(SDL_FlushRenderer(renderer.get()));
      int visible = 0;
      for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
          SDL_Color color{};
          REQUIRE(SDL_ReadSurfacePixel(
              surface.get(), x, y, &color.r, &color.g, &color.b, &color.a));
          if (color.r > 100)
            ++visible;
        }
      }
      CHECK(visible > 0);
    }
  }
}

TEST_CASE("Radar separates a full system of coincident contacts", "[radar]") {
  // 63 NPC slots, 16 stellars, and the centered player. Test the center and
  // all corners, including brackets on both a stellar and a ship target.
  for (const SDL_FPoint anchor : std::array<SDL_FPoint, 5>{
           {{110, 110}, {1, 1}, {219, 1}, {1, 219}, {219, 219}}}) {
    std::array<RadarContact, 80> contacts{};
    for (std::size_t i = 0; i < contacts.size(); ++i) {
      contacts[i].anchor = anchor;
      contacts[i].radius = 4;
      contacts[i].icon = i < 16 ? RadarIcon::planet : RadarIcon::ship;
      contacts[i].selected = i == 0 || i == 16;
    }
    contacts.back().icon = RadarIcon::player;
    contacts.back().anchor = {110, 110};
    contacts.back().radius = 5;
    game::LayoutRadarContacts(contacts, {0, 0, 220, 220});
    CHECK(contacts.front().position.x == 110);
    CHECK(contacts.front().position.y == 110);
    for (std::size_t i = 0; i < contacts.size(); ++i) {
      const auto &c = contacts[i];
      REQUIRE(c.placed);
      CHECK(c.position.x - c.extent() >= 0);
      CHECK(c.position.y - c.extent() >= 0);
      CHECK(c.position.x + c.extent() <= 220);
      CHECK(c.position.y + c.extent() <= 220);
      for (std::size_t j = 0; j < i; ++j) {
        const auto &other = contacts[j];
        CHECK((std::abs(c.position.x - other.position.x) >=
                   c.extent() + other.extent() ||
               std::abs(c.position.y - other.position.y) >=
                   c.extent() + other.extent()));
      }
    }
  }
}

TEST_CASE(
    "Radar keeps isolated contacts in place and excludes offscreen anchors",
    "[radar]") {
  std::array<RadarContact, 3> contacts{};
  contacts[0].anchor = {20, 40};
  contacts[1].anchor = {80, 70};
  contacts[2].anchor = {-1, 50};
  game::LayoutRadarContacts(contacts, {0, 0, 100, 100});
  REQUIRE(contacts[0].placed);
  CHECK(contacts[0].position.x == 20);
  CHECK(contacts[0].position.y == 40);
  REQUIRE(contacts[1].placed);
  CHECK(contacts[1].position.x == 80);
  CHECK_FALSE(contacts[2].placed);
}

TEST_CASE(
    "Expanded cockpit fits supported windows without losing its lower panels",
    "[radar]") {
  for (const SDL_FPoint window :
       std::array<SDL_FPoint, 3>{{{1024, 768}, {1920, 1080}, {3840, 2160}}}) {
    for (const float requested : {.5F, 1.0F, 1.5F, 4.0F}) {
      const float scale = game::RadarHudScale(window, requested, true);
      CHECK((game::kGameplayHudStripHeight + game::kRadarHudExpansion) *
                scale <=
            window.y + .01F);
      CHECK((game::kGameplayHudStripWidth + game::kRadarHudExpansion) * scale <=
            window.x + .01F);
    }
  }
  CHECK(game::RadarHudScale({1024, 768}, 1, false) == 1);
}
