#include <catch2/catch_test_macros.hpp>

#include "game/game_state.hpp"
#include "game/mouse_controls.hpp"
#include "game/ship_ai.hpp"
#include "sdl_platform.hpp"

#include <cmath>
#include <limits>
#include <numbers>

TEST_CASE("mouse flight release brakes until movement takes over", "[mouse]") {
  game::MouseFlightControl control;
  FlightInput input;
  input.primary_clicked = input.primary_held = true;
  control.Update(input, true, true);
  REQUIRE(control.steering());
  input = {};
  control.Update(input, true, true);
  CHECK_FALSE(control.steering());
  REQUIRE(control.braking());
  control.Update(input, true, true);
  CHECK(control.braking());
  SECTION("a fresh mouse hold resumes flight") {
    input.primary_clicked = input.primary_held = true;
    control.Update(input, true, true);
    CHECK(control.steering());
    CHECK_FALSE(control.braking());
  }
  SECTION("keyboard flight overrides braking") {
    input.thrust = true;
    control.Update(input, true, true);
    CHECK_FALSE(control.braking());
    control.Update({}, true, true);
    CHECK_FALSE(control.braking());
  }
  SECTION("jump and disabled controls clear the assist") {
    control.Update({}, true, false);
    CHECK_FALSE(control.steering());
    CHECK_FALSE(control.braking());
  }
}

TEST_CASE("HUD clicks and quick taps do not arm mouse braking", "[mouse]") {
  game::MouseFlightControl control;
  FlightInput input;
  input.primary_clicked = input.primary_held = true;
  control.Update(input, false, true);
  control.Update({}, true, true);
  CHECK_FALSE(control.steering());
  CHECK_FALSE(control.braking());
  input.primary_held = false;
  control.Update(input, true, true);
  CHECK_FALSE(control.braking());
}

TEST_CASE("mouse braking stops diagonally without reversing or turning",
          "[mouse]") {
  game::Ship ship;
  ship.vel_x = 3;
  ship.vel_y = 4;
  ship.speed = 5;
  ship.heading = 1;
  game::ApplyMouseBraking(ship, 1, 1);
  CHECK(std::abs(ship.vel_x - 2.4F) < 0.0001F);
  CHECK(std::abs(ship.vel_y - 3.2F) < 0.0001F);
  CHECK(ship.speed == 4);
  CHECK(ship.heading == 1);
  game::ApplyMouseBraking(ship, 2, 3);
  CHECK(ship.vel_x == 0);
  CHECK(ship.vel_y == 0);
  CHECK(ship.speed == 0);
  game::ApplyMouseBraking(ship, 2, 1);
  CHECK(ship.vel_x == 0);
  CHECK(ship.vel_y == 0);
}

TEST_CASE("mouse braking scales with elapsed simulation time", "[mouse]") {
  game::Ship one;
  one.vel_x = -6;
  one.vel_y = 8;
  one.speed = 10;
  auto split = one;
  game::ApplyMouseBraking(one, 0.5F, 3);
  for (int i = 0; i < 3; ++i)
    game::ApplyMouseBraking(split, 0.5F, 1);
  CHECK(std::abs(one.vel_x - split.vel_x) < 0.0001F);
  CHECK(std::abs(one.vel_y - split.vel_y) < 0.0001F);
  CHECK(one.speed == split.speed);
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
  CHECK_FALSE(flight.braking());
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
