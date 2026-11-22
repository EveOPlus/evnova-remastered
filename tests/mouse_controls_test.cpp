#include <catch2/catch_test_macros.hpp>

#include "game/game_state.hpp"
#include "game/mouse_controls.hpp"
#include "game/ship_ai.hpp"
#include "game/spaceflight.hpp"
#include "sdl_platform.hpp"

#include <cmath>
#include <limits>
#include <numbers>

TEST_CASE("Double-click cruise keeps its heading and does not brake on release",
          "[mouse][cruise]") {
  game::MouseFlightControl control;
  FlightInput input;
  input.primary_clicked = true;
  input.window_mouse_x = 500;
  input.window_mouse_y = 200;
  control.Update(input, true, true, 100, {100, -200});
  control.Update({}, true, true, 150);
  input.primary_held = true; // Hold the second click, then release it.
  control.Update(input, true, true, 250, {100, -200});
  REQUIRE(control.cruising());
  input.primary_clicked = false;
  control.Update(input, true, true, 600, {-200, 0});
  REQUIRE_FALSE(control.braking());
  control.Update({}, false, true, 800, {-200, 0}); // Pointer over the HUD.
  CHECK(control.steering());
  CHECK_FALSE(control.braking());
  CHECK(control.Direction({-200, 0}).x == 100);
  CHECK(control.Direction({-200, 0}).y == -200);
  SECTION("keyboard takes over") {
    input = {};
    input.reverse = true;
    control.Update(input, true, true, 900);
    CHECK_FALSE(control.cruising());
    CHECK_FALSE(control.braking());
  }
  SECTION("new hold restores release braking") {
    input.primary_clicked = true;
    input.primary_held = true;
    control.Update(input, true, true, 900, {-200, 0});
    CHECK_FALSE(control.cruising());
    control.Update({}, true, true, 950);
    CHECK_FALSE(control.steering());
    CHECK(control.braking());
  }
  SECTION("jump or disability stops cruise") {
    control.Update({}, true, false, 900);
    CHECK_FALSE(control.cruising());
  }
}

TEST_CASE("Cruise ignores slow clicks, separated clicks, UI and the center",
          "[mouse][cruise]") {
  game::MouseFlightControl control;
  FlightInput input;
  input.primary_clicked = true;
  control.Update(input, true, true, 100, {200, 0});
  bool can_start = true;
  std::uint64_t when = 200;
  SDL_FPoint direction{200, 0};
  SECTION("slow") { when = 601; }
  SECTION("separated") { input.window_mouse_x = 50; }
  SECTION("UI") { can_start = false; }
  SECTION("center") { direction = {1, 1}; }
  control.Update(input, can_start, true, when, direction);
  CHECK_FALSE(control.cruising());
}

TEST_CASE("mouse release stops ordinary and inertialess ships", "[mouse]") {
  game::MouseFlightControl control;
  game::PlayerShip ship;
  game::ShipClass cls;
  cls.accel = 1250;
  cls.speed = 900; // Dart's effective non-Strict speed, in integrator units.
  cls.turn_rate = 100;
  game::PlayerMovementOptions opts;
  SECTION("ordinary ship") { opts.inertialess = false; }
  SECTION("Vell-os Dart") { opts.inertialess = true; }

  FlightInput input;
  input.primary_clicked = input.primary_held = true;
  control.Update(input, true, true, 100, {0, -200});
  REQUIRE(control.steering());
  for (int frame = 0; frame < 60; ++frame) {
    input = {};
    REQUIRE(game::ApplyMouseSteering(ship.heading, {0, -200}, 0.1F, input));
    (void)game::NovaPlayer_IntegrateMovement(ship, input, cls, 0.63F, opts);
  }
  REQUIRE(ship.vel_y < -1);
  // Releasing over a HUD/panel must still brake the flight hold.
  control.Update({}, false, true, 2000);
  REQUIRE_FALSE(control.steering());
  REQUIRE_FALSE(control.cruising());
  REQUIRE(control.braking());
  for (int frame = 0; frame < 150; ++frame) {
    control.Update({}, true, true, 2100 + frame * 21);
    REQUIRE_FALSE(control.steering());
    game::ApplyMouseBraking(ship, cls.accel * 0.0002F, 0.63F);
    (void)game::NovaPlayer_IntegrateMovement(ship, {}, cls, 0.63F, opts);
    REQUIRE(ship.vel_y <= 0);
  }
  CHECK(ship.speed == 0);
  CHECK(ship.vel_y == 0);
  CHECK(ship.heading == 0);

  input = {};
  input.primary_clicked = input.primary_held = true;
  control.Update(input, true, true, 6000, {200, 0});
  CHECK(control.steering());
  CHECK_FALSE(control.cruising());
  CHECK_FALSE(control.braking());
  control.Update({}, true, false, 6100);
  CHECK_FALSE(control.steering());
  CHECK_FALSE(control.braking());
}

TEST_CASE("keyboard and flight actions cancel mouse braking", "[mouse]") {
  game::MouseFlightControl control;
  FlightInput input;
  input.primary_clicked = input.primary_held = true;
  control.Update(input, true, true, 100, {0, -200});
  control.Update({}, true, true, 200);
  REQUIRE(control.braking());
  input = {};
  SECTION("turn") { input.turn_left = true; }
  SECTION("thrust") { input.thrust = true; }
  SECTION("reverse") { input.reverse = true; }
  SECTION("boost") { input.afterburner = true; }
  SECTION("face target") { input.face_target = true; }
  SECTION("jump") { input.travel = true; }
  SECTION("land") { input.land = true; }
  SECTION("board") { input.board = true; }
  control.Update(input, true, true, 300);
  CHECK_FALSE(control.braking());
  control.Update({}, true, true, 400);
  CHECK_FALSE(control.braking());
}

TEST_CASE("mouse braking keeps direction and scales with elapsed time",
          "[mouse]") {
  game::Ship ship;
  ship.vel_x = 3;
  ship.vel_y = 4;
  ship.speed = 5;
  ship.heading = 1;
  auto split = ship;
  game::ApplyMouseBraking(ship, 0.5F, 3);
  for (int i = 0; i < 3; ++i)
    game::ApplyMouseBraking(split, 0.5F, 1);
  CHECK(std::abs(ship.vel_x - split.vel_x) < 0.0001F);
  CHECK(std::abs(ship.vel_y - split.vel_y) < 0.0001F);
  CHECK(ship.speed == split.speed);
  CHECK(std::abs(ship.vel_x / ship.vel_y - 0.75F) < 0.0001F);
  CHECK(ship.heading == 1);
  game::ApplyMouseBraking(ship, 2, 3);
  CHECK(ship.vel_x == 0);
  CHECK(ship.vel_y == 0);
  CHECK(ship.speed == 0);
}

TEST_CASE("HUD clicks and quick taps do not start mouse flight", "[mouse]") {
  game::MouseFlightControl control;
  FlightInput input;
  input.primary_clicked = input.primary_held = true;
  control.Update(input, false, true);
  control.Update({}, true, true);
  CHECK_FALSE(control.steering());
  CHECK_FALSE(control.braking());
  input.primary_held = false;
  control.Update(input, true, true);
  CHECK_FALSE(control.steering());
  CHECK_FALSE(control.braking());
}

TEST_CASE("mouse flight turns toward the pointer before applying thrust",
          "[mouse]") {
  FlightInput right;
  REQUIRE(game::ApplyMouseSteering(0.0F, {200, 0}, 0.04F, right));
  CHECK(right.turn_right);
  CHECK_FALSE(right.turn_left);
  CHECK_FALSE(right.thrust);

  FlightInput aligned;
  REQUIRE(game::ApplyMouseSteering(0.0F, {0, -200}, 0.04F, aligned));
  CHECK(aligned.thrust);
  CHECK_FALSE(aligned.turn_left);
  CHECK_FALSE(aligned.turn_right);

  FlightInput left;
  REQUIRE(game::ApplyMouseSteering(0.0F, {-200, 0}, 0.04F, left));
  CHECK(left.turn_left);
  CHECK_FALSE(left.turn_right);
}

TEST_CASE("mouse steering crosses north by the shortest turn", "[mouse]") {
  FlightInput input;
  REQUIRE(game::ApplyMouseSteering(
      2.0F * std::numbers::pi_v<float> - 0.1F, {0, -200}, 0.04F, input));
  CHECK(input.turn_right);
  CHECK_FALSE(input.turn_left);
  CHECK(input.thrust);
}

TEST_CASE(
    "mouse steering has a center dead zone and respects keyboard override",
    "[mouse]") {
  FlightInput input;
  CHECK_FALSE(game::ApplyMouseSteering(0, {2, 2}, 0.04F, input));
  CHECK_FALSE(input.thrust);
  input.turn_left = true;
  CHECK_FALSE(game::ApplyMouseSteering(0, {200, 0}, 0.04F, input));
  CHECK(input.turn_left);
  CHECK_FALSE(input.turn_right);
  CHECK_FALSE(input.thrust);
  input = {};
  input.reverse = true;
  CHECK_FALSE(game::ApplyMouseSteering(0, {0, -200}, 0.04F, input));
  CHECK_FALSE(input.thrust);
  input = {};
  CHECK_FALSE(game::ApplyMouseSteering(
      0, {std::numeric_limits<float>::infinity(), 0}, 0.04F, input));
}

TEST_CASE("radial sectors match the four visible compass actions",
          "[mouse][radial]") {
  CHECK(game::RadialSector({0, -95}, 43, 143) == 0);
  CHECK(game::RadialSector({95, 0}, 43, 143) == 1);
  CHECK(game::RadialSector({0, 95}, 43, 143) == 2);
  CHECK(game::RadialSector({-95, 0}, 43, 143) == 3);
  CHECK(game::RadialSector({0, 0}, 43, 143) == -1);
  CHECK(game::RadialSector({150, 0}, 43, 143) == -1);
}

TEST_CASE("radial menu stays inside the window near every edge",
          "[mouse][radial]") {
  const auto first = game::ClampRadialCenter({0, 0}, {1920, 1080}, 185);
  CHECK(first.x == 185);
  CHECK(first.y == 185);
  const auto last = game::ClampRadialCenter({1919, 1079}, {1920, 1080}, 185);
  CHECK(last.x == 1735);
  CHECK(last.y == 895);
  const auto tiny = game::ClampRadialCenter({0, 0}, {200, 200}, 185);
  CHECK(tiny.x == 100);
  CHECK(tiny.y == 100);
}

TEST_CASE("planet radial actions address the planet even with a ship targeted",
          "[mouse][radial]") {
  game::GameState state;
  state.player.primary_target_ship_slot = 7;
  game::SelectMouseTarget(state,
                          {game::MouseTargetKind::stellar, 128, "Earth"});
  CHECK(state.player.primary_target_ship_slot == -1);
  CHECK(state.travel.selected_stellar_id == 128);
  CHECK(state.travel.selected_stellar_is_manual);
  CHECK(state.player.travel_transfer_mode == 2);
  game::SelectMouseTarget(state, {game::MouseTargetKind::ship, 9, "Shuttle"});
  CHECK(state.player.primary_target_ship_slot == 9);
  CHECK(state.travel.selected_stellar_id == 128);
}

TEST_CASE(
    "damaged ships retain radial access without restoring flight controls",
    "[mouse]") {
  game::GameState state;
  state.scenario.ships.resize(1);
  state.scenario.ships[0].base_armor = 30;
  state.player.ship_class_id = 0;
  state.player.is_active = true;
  state.player.armor_points = 5;
  REQUIRE(game::NovaAiShip_IsDisabled(state, state.player));
  CHECK(game::CanOpenMouseRadial(state));
  game::MouseFlightControl flight;
  FlightInput input;
  input.primary_clicked = input.primary_held = true;
  flight.Update(input, true, !game::NovaAiShip_IsDisabled(state, state.player));
  CHECK_FALSE(flight.steering());
  SECTION("jump engagement keeps the modal closed") {
    state.travel.engaging = true;
    CHECK_FALSE(game::CanOpenMouseRadial(state));
  }
  SECTION("hyperspace transition keeps the modal closed") {
    state.player.hyperspace_jump_timer = 1;
    CHECK_FALSE(game::CanOpenMouseRadial(state));
  }
  SECTION("inactive ship keeps the modal closed") {
    state.player.is_active = false;
    CHECK_FALSE(game::CanOpenMouseRadial(state));
  }
}
