#include "game/boarding_plunder.hpp"
#include "game/flight_automation.hpp"
#include "game/jump_controls.hpp"
#include "game/outfit.hpp"
#include "game/spaceflight.hpp"
#include "game/starmap_route.hpp"
#include "game/travel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE(
    "Radial boarding matches a disabled target and aligns before boarding",
    "[boarding][automation]") {
  using namespace game;
  GameState state;
  state.player.is_active = true;
  state.player.current_system_id = 0;
  state.player.ship_class_id = 0;
  state.player.armor_points = 100;
  state.player.primary_target_ship_slot = 1;
  state.scenario.ships.resize(1);
  auto &cls = state.scenario.ships[0];
  cls.base_armor = 100;
  cls.crew = 10;
  cls.accel = 500;
  cls.speed = 400;
  cls.turn_rate = 40;
  auto &target = state.ShipAt(1);
  target.is_active = true;
  target.ship_instance_id = 1;
  target.current_system_id = 0;
  target.ship_class_id = 0;
  target.armor_points = 20;
  target.heading = 1.57F;
  SECTION("already alongside but facing across the target") {}
  SECTION("moving disabled hull") {
    state.player.pos_x = -800;
    state.player.pos_y = 300;
    target.vel_x = .6F;
    target.vel_y = -.15F;
  }
  SECTION("target disappears") {
    FlightAutomationController controller;
    REQUIRE(controller.BoardTarget(state, 1, 0));
    target.is_active = false;
    FlightInput input;
    controller.Tick(state, 33, input);
    CHECK_FALSE(input.board);
    CHECK(controller.status().phase == FlightAutomationPhase::kFailed);
    return;
  }
  SECTION("undamaged ship is rejected") {
    target.armor_points = 100;
    FlightAutomationController controller;
    CHECK_FALSE(controller.BoardTarget(state, 1, 0));
    return;
  }
  FlightAutomationController controller;
  REQUIRE(controller.BoardTarget(state, 1, 0));
  bool boarded = false;
  for (int tick = 0; tick < 5000; ++tick) {
    FlightInput input;
    controller.Tick(state, static_cast<std::uint64_t>(tick * 33), input);
    if (input.board) {
      const auto approach = InspectBoardingApproach(state.player, target);
      CHECK(approach.in_range);
      CHECK(approach.speed_matched);
      CHECK(approach.aligned);
      CHECK_FALSE(input.fire);
      boarded = true;
      break;
    }
    (void)NovaPlayer_IntegrateMovement(state.player, input, cls, 1.0F);
    target.pos_x += target.vel_x;
    target.pos_y += target.vel_y;
  }
  CHECK(boarded);
  CHECK(controller.status().phase == FlightAutomationPhase::kComplete);
}

using namespace game;

TEST_CASE("jump key distinguishes single presses, double presses and holds",
          "[jump-controls]") {
  JumpKeyControl key;
  CHECK(key.Update(true, 0, 0, 1) == JumpPress::single);
  CHECK(key.Update(true, 100, 0, 1) == JumpPress::none);
  CHECK(key.Update(false, 150, 0, 1) == JumpPress::none);
  CHECK(key.Update(true, 250, 0, 1) == JumpPress::route);
  CHECK(key.Update(true, 300, 0, 1) == JumpPress::none);
  CHECK(key.Update(false, 350, 0, 1) == JumpPress::none);
  CHECK(key.Update(true, 400, 0, 1) == JumpPress::single);
  CHECK(key.Update(false, 450, 0, 1) == JumpPress::none);
  CHECK(key.Update(true, 1000, 0, 1) == JumpPress::single);
}

TEST_CASE("jump double press cannot cross a navigation change or modal",
          "[jump-controls]") {
  JumpKeyControl key;
  REQUIRE(key.Update(true, 0, 0, 1) == JumpPress::single);
  REQUIRE(key.Update(false, 50, 0, 1) == JumpPress::none);
  SECTION("new destination") {
    CHECK(key.Update(true, 100, 0, 2) == JumpPress::single);
  }
  SECTION("new system") {
    CHECK(key.Update(true, 100, 1, 2) == JumpPress::single);
  }
  SECTION("steering, cancellation or modal") {
    key.CancelPending();
    CHECK(key.Update(true, 100, 0, 1) == JumpPress::single);
  }
}

namespace {
GameState AutomationState() {
  GameState state;
  state.player.is_active = true;
  state.player.current_system_id = 0;
  state.scenario.systems.resize(3);
  state.scenario.systems[0].name = "Sol";
  state.scenario.systems[0].nav_defs[0] = 0x80;
  state.scenario.systems[0].nav_defs[1] = 0x81;
  state.scenario.systems[0].links[0] = 0x81;
  state.scenario.systems[1].name = "Alpha Centauri";
  state.scenario.systems[2].name = "Unlinked";
  state.scenario.stellars.resize(2);
  state.scenario.stellars[0].name = "Earth";
  state.scenario.stellars[1].name = "Mars";
  return state;
}
} // namespace

namespace {
GameState MiningState() {
  auto state = AutomationState();
  state.scenario.ships.resize(1);
  auto &cls = state.scenario.ships[0];
  cls.cargo_holds = 100;
  cls.turn_rate = 40;
  cls.accel = 500;
  cls.speed = 400;
  state.player.ship_class_id = 0;
  state.player.armor_points = 100;
  state.player.active_weapon_bank_slot = 0;
  state.scenario.outfits.resize(1);
  state.scenario.outfits[0].mod_type = 0x1f;
  state.inventory.outfit_owned_count[0] = 1;
  state.scenario.weapons.resize(1);
  auto &weapon = state.scenario.weapons[0];
  weapon.flags = 2;
  weapon.flags_secondary = 0x8000;
  weapon.weapon_mode_code = -1;
  weapon.ammo_type = -1;
  weapon.range_scalar = 120;
  weapon.projectile_speed = 600;
  weapon.lifetime_ticks = 20;
  state.player.weapon_banks[0].mounted = 6;
  auto &rock = state.asteroid_pool[0];
  rock.active = true;
  rock.mining_group = 42;
  rock.target_pos_y = -600;
  rock.integrity = 60;
  return state;
}
} // namespace

TEST_CASE("auto-mine settles before firing and checks projectile reach",
          "[mining][automation]") {
  auto state = MiningState();
  auto &rock = state.asteroid_pool[0];
  rock.target_pos_y = -45;
  SECTION("fast approach brakes even while pointing directly at the rock") {
    state.player.vel_y = -4;
  }
  SECTION("short-lived mining shots cannot reach the target yet") {
    state.scenario.weapons[0].lifetime_ticks = 2;
  }
  SECTION("a coarse turn step does not allow a shot outside the rock") {
    state.scenario.ships[0].turn_rate = 180;
    state.player.heading = .28F;
  }
  FlightAutomationController controller;
  REQUIRE(controller.MineAsteroid(state, 0, 0));
  FlightInput input;
  controller.Tick(state, 1, input);
  CHECK_FALSE(input.fire_secondary);
}

TEST_CASE("auto-mine intercepts a crossing asteroid and fires into its path",
          "[mining][automation]") {
  for (const float dt : {.63F, 1.0F, 2.0F}) {
    auto state = MiningState();
    auto &rock = state.asteroid_pool[0];
    rock.target_vel_x = 1;
    FlightAutomationController controller;
    REQUIRE(controller.MineAsteroid(state, 0, 0));
    bool fired = false;
    float path_length = 0;
    float ticks = 0;
    for (; ticks < 1800; ticks += dt) {
      FlightInput input;
      controller.Tick(state, static_cast<std::uint64_t>(ticks * 33), input, dt);
      if (input.fire_secondary) {
        const float dx = rock.target_pos_x - state.player.pos_x;
        const float dy = rock.target_pos_y - state.player.pos_y;
        const float shot_heading = std::round(state.player.heading * 180.0F /
                                              3.14159265358979323846F) *
                                   3.14159265358979323846F / 180.0F;
        const float vx =
            std::sin(shot_heading) * 6 + state.player.vel_x - rock.target_vel_x;
        const float vy = -std::cos(shot_heading) * 6 + state.player.vel_y -
                         rock.target_vel_y;
        const float time = (dx * vx + dy * vy) / (vx * vx + vy * vy);
        CHECK(time > 0);
        CHECK(time <= 20);
        CHECK(std::hypot(dx - vx * time, dy - vy * time) < 8);
        CHECK(std::hypot(state.player.vel_x - rock.target_vel_x,
                         state.player.vel_y - rock.target_vel_y) <= .3F);
        fired = true;
        break;
      }
      const float before_x = state.player.pos_x;
      const float before_y = state.player.pos_y;
      (void)NovaPlayer_IntegrateMovement(
          state.player, input, state.scenario.ships[0], dt);
      path_length += std::hypot(state.player.pos_x - before_x,
                                state.player.pos_y - before_y);
      rock.target_pos_x += rock.target_vel_x * dt;
      rock.target_pos_y += rock.target_vel_y * dt;
    }
    CAPTURE(dt, ticks, path_length, state.player.pos_x, state.player.pos_y);
    CHECK(fired);
    CHECK(ticks < 650);
    CHECK(path_length <
          1.15F * std::hypot(state.player.pos_x, state.player.pos_y));
  }
}

TEST_CASE("auto-mine selects mining secondary, gates range and collects its "
          "own yield",
          "[mining][automation]") {
  auto state = MiningState();
  state.player.active_weapon_bank_slot = -1;
  auto &rock = state.asteroid_pool[0];
  rock.target_pos_y = -300;
  FlightAutomationController controller;
  REQUIRE(controller.MineAsteroid(state, 0, 0));
  FlightInput input;
  controller.Tick(state, 1, input);
  input = {};
  controller.Tick(state, 2, input);
  CHECK(input.cycle_secondary);
  state.player.active_weapon_bank_slot = 0;
  input = {};
  controller.Tick(state, 3, input);
  CHECK(input.thrust);
  CHECK_FALSE(input.fire_secondary);
  rock.target_pos_y = -45;
  input = {};
  controller.Tick(state, 4, input);
  CHECK(input.fire_secondary);
  rock.active = false;
  auto &cargo = state.freeflight_objects[0];
  cargo.persistent = true;
  cargo.lifetime_ticks = 300;
  cargo.system_id = 0;
  cargo.mining_group = 42;
  cargo.pos_y = -35;
  input = {};
  controller.Tick(state, 5, input);
  CHECK(controller.Mining());
  CHECK_FALSE(input.fire_secondary);
  CHECK(input.thrust);
  // A recycled slot with a different group's cargo cannot keep the job alive.
  cargo.mining_group = 43;
  input = {};
  controller.Tick(state, 6, input);
  CHECK(controller.status().phase == FlightAutomationPhase::kComplete);
  rock.active = true;
  REQUIRE(controller.MineAsteroid(state, 0, 7));
  state.inventory.cargo_bins[0] = 100;
  input = {};
  controller.Tick(state, 8, input);
  CHECK(controller.status().phase == FlightAutomationPhase::kFailed);
  CHECK_FALSE(input.fire_secondary);
  state.inventory.cargo_bins[0] = 0;
  REQUIRE(controller.MineAsteroid(state, 0, 9));
  controller.Cancel();
  input = {};
  controller.Tick(state, 10, input);
  CHECK_FALSE(input.thrust);
  CHECK_FALSE(input.fire_secondary);
  state.inventory.outfit_owned_count[0] = 0;
  CHECK_FALSE(controller.MineAsteroid(state, 0, 11));
}

TEST_CASE("auto-mine makes space to aim at an overlapping fragment",
          "[mining][automation]") {
  auto state = MiningState();
  state.asteroid_pool[0].target_pos_y = 0;
  FlightAutomationController controller;
  REQUIRE(controller.MineAsteroid(state, 0, 0));
  bool fired = false;
  for (int tick = 0; tick < 300; ++tick) {
    FlightInput input;
    controller.Tick(state, static_cast<std::uint64_t>(tick * 33), input);
    if (input.fire_secondary) {
      fired = true;
      break;
    }
    (void)NovaPlayer_IntegrateMovement(
        state.player, input, state.scenario.ships[0], 1.0F);
  }
  CHECK(fired);
}

TEST_CASE(
    "automation resolves current-system stellars and direct links exactly") {
  GameState state = AutomationState();
  CHECK(FlightAutomationController::ResolveStellar(state, "eArTh") == 0x80);
  CHECK_FALSE(FlightAutomationController::ResolveStellar(state, "Ear"));
  // Pure-decimal targets are stellar resource ids and must be a nav point.
  CHECK(FlightAutomationController::ResolveStellar(state, "128") == 0x80);
  CHECK(FlightAutomationController::ResolveStellar(state, "129") == 0x81);
  CHECK_FALSE(FlightAutomationController::ResolveStellar(state, "130"));
  CHECK_FALSE(FlightAutomationController::ResolveStellar(state, "127"));
  CHECK_FALSE(FlightAutomationController::ResolveStellar(state, "12x"));
  CHECK(FlightAutomationController::ResolveLinkedSystem(state,
                                                        "alpha centauri") == 1);
  CHECK_FALSE(
      FlightAutomationController::ResolveLinkedSystem(state, "Unlinked"));
}

TEST_CASE("automation stop turns around without thrust until aligned") {
  PlayerShip ship;
  ship.heading = 0.0F;
  ship.vel_y = -4.0F;
  FlightInput input;
  FlightAutomationController::Stop(ship, 5.0F, input);
  CHECK((input.turn_left || input.turn_right));
  CHECK_FALSE(input.thrust);
  ship.heading = 3.14159265358979323846F;
  input = {};
  FlightAutomationController::Stop(ship, 5.0F, input);
  CHECK(input.thrust);
}

TEST_CASE("automation move-away faces radially outward before thrusting") {
  PlayerShip ship;
  ship.pos_x = 100.0F;
  ship.heading = 3.14159265358979323846F;
  FlightInput input;
  FlightAutomationController::MoveAwayFrom(ship, 0.0F, 0.0F, 5.0F, input);
  CHECK((input.turn_left || input.turn_right));
  CHECK_FALSE(input.thrust);

  ship.heading = 3.14159265358979323846F / 2.0F;
  input = {};
  FlightAutomationController::MoveAwayFrom(ship, 0.0F, 0.0F, 5.0F, input);
  CHECK(input.thrust);
}

TEST_CASE("automation edge commands include a release frame") {
  GameState state = AutomationState();
  FlightAutomationController automation;
  REQUIRE(automation.JumpTo(state, "Alpha Centauri", 0));
  FlightInput first;
  automation.Tick(state, 0, first);
  CHECK(first.cycle_destination_next);
  CHECK(first.quiet_navigation);
  CHECK_FALSE(first.hyperspace_mode);
  FlightInput release;
  automation.Tick(state, 1, release);
  CHECK_FALSE(release.cycle_destination_next);
}

TEST_CASE("blocked manual jump flies out before issuing the normal jump") {
  GameState state = AutomationState();
  state.scenario.ships.resize(1);
  state.scenario.ships[0].base_fuel = 200;
  state.player.ship_class_id = 0;
  state.player.fuel_points = 200;
  state.scenario.systems[1].is_visible = true;
  // Plotting on the map arms travel mode 3 while this HUD flag can remain
  // false. The assist must not send H and get stuck reopening the overlay.
  state.travel.hyperspace_mode = false;
  state.player.travel_transfer_mode = 3;
  state.travel.travel_slot = 0;
  state.travel.destination_system_id = 1;
  state.travel.starmap_destination_system_id = 1;
  FlightAutomationController automation;
  REQUIRE_FALSE(NovaTravel_PlayerInJumpRange(state));

  SECTION("flies clear and then jumps, completing at the destination") {
    REQUIRE(automation.AssistBlockedJump(state, 0));
    FlightInput input;
    automation.Tick(state, 1, input);
    CHECK(input.thrust);
    CHECK_FALSE(input.travel);
    CHECK_FALSE(input.hyperspace_mode);
    state.player.pos_y = -1100;
    REQUIRE(NovaTravel_PlayerInJumpRange(state));
    input = {};
    automation.Tick(state, 2, input);
    CHECK(input.travel);
    CHECK_FALSE(input.hyperspace_mode);
    state.travel.engaging = true;
    input = {};
    automation.Tick(state, 3, input);
    CHECK_FALSE(input.travel);
    CHECK(automation.status().phase == FlightAutomationPhase::kWaiting);
    state.travel.engaging = false;
    state.player.current_system_id = 1;
    automation.Tick(state, 4, input);
    CHECK(automation.status().phase == FlightAutomationPhase::kComplete);
  }
  SECTION("a stale previous destination does not cycle or reopen navigation") {
    state.travel.destination_system_id = -1;
    REQUIRE(automation.AssistBlockedJump(state, 0));
    FlightInput input;
    automation.Tick(state, 1, input);
    CHECK(input.thrust);
    CHECK_FALSE(input.cycle_destination_next);
    CHECK_FALSE(input.hyperspace_mode);
    CHECK_FALSE(input.starmap);
  }
  SECTION("cancellation gives control back immediately") {
    REQUIRE(automation.AssistBlockedJump(state, 0));
    automation.Cancel();
    FlightInput input;
    automation.Tick(state, 1, input);
    CHECK_FALSE(input.thrust);
    CHECK_FALSE(input.travel);
  }
  SECTION("no fuel keeps the normal denial") {
    state.player.fuel_points = 0;
    CHECK_FALSE(automation.AssistBlockedJump(state, 0));
  }
  SECTION("no destination keeps the normal denial") {
    state.travel.starmap_destination_system_id = -1;
    CHECK_FALSE(automation.AssistBlockedJump(state, 0));
  }
  SECTION("being clear already keeps the normal jump") {
    state.player.pos_y = -1100;
    CHECK_FALSE(automation.AssistBlockedJump(state, 0));
  }
}

// Runs MoveToAndStopAt against the real free-flight integrator (no gravity) and
// reports where the ship settled. `envelope` mirrors the caller-supplied
// per-axis arrival gate; the maneuver is asked to arrive within half of it.
namespace {
struct ArrivalRun {
  float distance = 0.0F;
  float axis_speed = 0.0F;
  float pos_x = 0.0F;
  int frames = 0;
  bool converged = false;
};

ArrivalRun RunArrival(float start_distance, float envelope) {
  ShipClass cls;
  cls.accel = 500.0F;    // raw -> thrust 0.10 px/tick^2
  cls.speed = 400.0F;    // raw -> 4.0 px/tick
  cls.turn_rate = 40.0F; // raw -> 4.0 deg/tick
  PlayerShip ship;
  ship.pos_x = 0.0F;
  ship.pos_y = 0.0F;
  ship.heading = 0.0F; // pointing up; target is to the right
  const float tx = start_distance;
  const float ty = 0.0F;
  const float radius = envelope * 0.5F;
  ArrivalRun run;
  for (int frame = 0; frame < 3000; ++frame) {
    FlightInput input;
    FlightAutomationController::MoveToAndStopAt(
        ship, tx, ty, radius, 4.0F, 0.1F, input);
    (void)NovaPlayer_IntegrateMovement(ship, input, cls, 1.0F);
    run.frames = frame + 1;
    run.distance = std::hypot(tx - ship.pos_x, ty - ship.pos_y);
    run.axis_speed = std::max(std::abs(ship.vel_x), std::abs(ship.vel_y));
    run.pos_x = ship.pos_x;
    if (run.distance <= radius && run.axis_speed <= 0.55F) {
      run.converged = true;
      break;
    }
  }
  return run;
}
} // namespace

TEST_CASE("automation arrival settles within tolerance without flying past") {
  constexpr float kTargetX = 1400.0F;
  constexpr float kRadius = 110.0F;
  const ArrivalRun run = RunArrival(kTargetX, /*envelope=*/220.0F);
  CHECK(run.converged);
  // The approach comes from -x; crossing the centre is fine, but ending up on
  // the far side by more than the tolerance would mean it lost control.
  CHECK(run.pos_x <= kTargetX + kRadius);
  CHECK(run.distance <= kRadius);
  CHECK(run.axis_speed <= 0.55F);
}

TEST_CASE("automation arrival matches a moving target's velocity") {
  ShipClass cls;
  cls.accel = 500.0F;
  cls.speed = 400.0F;
  cls.turn_rate = 40.0F;
  PlayerShip ship;
  ship.pos_x = 0.0F;
  ship.pos_y = 0.0F;
  ship.heading = 0.0F;
  constexpr float kRadius = 100.0F;
  constexpr float kTargetVelX = 0.0F;
  constexpr float kTargetVelY = -1.5F; // target drifts upward
  float target_x = 1200.0F;
  float target_y = 0.0F;
  bool converged = false;
  for (int frame = 0; frame < 4000 && !converged; ++frame) {
    FlightInput input;
    FlightAutomationController::MoveToAndStopAt(ship,
                                                target_x,
                                                target_y,
                                                kRadius,
                                                4.0F,
                                                0.1F,
                                                input,
                                                1.0F,
                                                kTargetVelX,
                                                kTargetVelY);
    (void)NovaPlayer_IntegrateMovement(ship, input, cls, 1.0F);
    target_x += kTargetVelX;
    target_y += kTargetVelY;
    const float distance =
        std::hypot(target_x - ship.pos_x, target_y - ship.pos_y);
    const float rel_speed =
        std::hypot(ship.vel_x - kTargetVelX, ship.vel_y - kTargetVelY);
    converged = distance <= kRadius && rel_speed <= 0.55F;
  }
  CHECK(converged);
  // The ship should have matched the target's drift, not stopped dead.
  CHECK(std::abs(ship.vel_y - kTargetVelY) <= 0.55F);
}

TEST_CASE("automation arrival sheds speed to tighten a slow turn") {
  // A slow-turning hull approaching with lateral velocity used to circle the
  // target indefinitely (thousands of frames): the seek phase never braked
  // because its speed was well under the stopping cap. The turn-radius cap must
  // shed speed so the nose can curve onto the point.
  ShipClass cls;
  cls.accel = 500.0F;
  cls.speed = 400.0F;
  cls.turn_rate = 10.0F; // 1 deg/tick
  PlayerShip ship;
  ship.pos_x = 0.0F;
  ship.pos_y = 0.0F;
  ship.heading = 0.0F; // pointing at the target
  ship.vel_x = 4.0F;   // but drifting sideways at top speed
  constexpr float kTargetY = -1500.0F;
  constexpr float kRadius = 100.0F;
  bool converged = false;
  int frames = 0;
  for (; frames < 2000 && !converged; ++frames) {
    FlightInput input;
    FlightAutomationController::MoveToAndStopAt(
        ship, 0.0F, kTargetY, kRadius, 1.0F, 0.1F, input);
    (void)NovaPlayer_IntegrateMovement(ship, input, cls, 1.0F);
    const float distance = std::hypot(ship.pos_x, kTargetY - ship.pos_y);
    converged =
        distance <= kRadius && std::hypot(ship.vel_x, ship.vel_y) <= 0.55F;
  }
  CHECK(converged);
  CHECK(frames < 900);
}

TEST_CASE("automation land tap waits for the real per-axis envelope") {
  GameState state = AutomationState();
  state.scenario.ships.resize(1);
  state.scenario.ships[0].accel = 500.0F;
  state.scenario.ships[0].speed = 400.0F;
  state.scenario.ships[0].turn_rate = 40.0F;
  state.player.ship_class_id = 0;
  state.player.is_active = true;
  state.scenario.stellars[0].pos_x = 0;
  state.scenario.stellars[0].pos_y = 0;
  state.scenario.stellars[0].system_id = 0;
  state.travel.selected_stellar_id = 0x80;
  state.travel.engage_timer = 0x2ee;

  FlightAutomationController automation;
  REQUIRE(automation.LandAt(state,
                            "Earth",
                            0,
                            180000,
                            /*landing_envelope_axis_range=*/75.0F));
  // 150 px out is inside the old 220 px circle but outside the 75 px gate.
  state.player.pos_x = 150.0F;
  state.player.pos_y = 0.0F;
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kMove);
  CHECK_FALSE(input.land);

  // Inside the gate and slow: the one-frame land tap is issued.
  state.player.pos_x = 50.0F;
  state.player.vel_x = 0.0F;
  state.player.vel_y = 0.0F;
  input = {};
  automation.Tick(state, 16, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kEngage);
  CHECK(input.land);
}

TEST_CASE("completed auto-docking never re-arms landing after launch") {
  GameState state = AutomationState();
  state.scenario.stellars[0].is_available = true;
  state.scenario.stellars[0].system_id = 0;
  state.travel.selected_stellar_id = 0x80;
  state.travel.engage_timer = 0x2ee;
  FlightAutomationController automation;
  REQUIRE(automation.LandAt(state, "Earth", 0));
  SECTION("autopilot issued the landing command") {
    FlightInput input;
    automation.Tick(state, 1, input);
    REQUIRE(input.land);
  }
  SECTION("player landed before autopilot finished approaching") {
    state.player.pos_x = 1000;
    FlightInput input;
    automation.Tick(state, 1, input);
    REQUIRE(automation.status().phase == FlightAutomationPhase::kMove);
  }
  // This arrival notification comes directly from the game, with no probe.
  automation.ObservedDocked();
  REQUIRE(automation.status().phase == FlightAutomationPhase::kComplete);
  // Launch clears the stellar selection and leaves the ship near the port.
  state.travel.selected_stellar_id = -1;
  state.player.pos_x = 0;
  for (std::uint64_t tick = 2; tick < 302; ++tick) {
    FlightInput input;
    automation.Tick(state, tick * 33, input);
    CHECK_FALSE(input.land);
    CHECK_FALSE(input.cycle_target_next);
    CHECK_FALSE(input.thrust);
    CHECK_FALSE(input.turn_left);
    CHECK_FALSE(input.turn_right);
  }
  // A later explicit docking request is still allowed.
  REQUIRE(automation.LandAt(state, "Earth", 10000));
  state.travel.selected_stellar_id = 0x80;
  FlightInput input;
  automation.Tick(state, 10001, input);
  CHECK(input.land);
}

TEST_CASE("automation arrival reaches a small-sprite envelope") {
  // A 75 px envelope was impossible with the old fixed 220 px stop radius.
  const ArrivalRun run = RunArrival(/*start_distance=*/900.0F,
                                    /*envelope=*/75.0F);
  CHECK(run.converged);
  CHECK(run.distance <= 37.5F);
  CHECK(run.axis_speed <= 0.55F);
}

// Player at the origin with a single unlimited direct-fire bank whose reach is
// 500 px, a matching "Raider" target in slot 1, and the target already
// selected.
GameState DestroyState() {
  GameState state = AutomationState();
  state.player.is_active = true;
  state.player.current_system_id = 0;
  state.player.ship_class_id = 0;
  state.player.heading = 3.14159265358979323846F / 2.0F; // +x

  state.scenario.ships.resize(2);
  state.scenario.ships[0].turn_rate = 40.0F;
  state.scenario.ships[0].accel = 500.0F;
  state.scenario.ships[0].speed = 400.0F;
  state.scenario.ships[1].display_name = "Raider";

  state.scenario.weapons.resize(1);
  state.scenario.weapons[0].weapon_mode_code = -1;
  state.scenario.weapons[0].ammo_type = -1; // free energy, always ready
  state.scenario.weapons[0].range_scalar = 500.0F;
  state.player.weapon_banks.fill({});
  state.player.weapon_banks[0].mounted = 1;

  Ship &target = state.ShipAt(1);
  target.is_active = true;
  target.ship_class_id = 1;
  target.current_system_id = 0;
  target.armor_points = 50.0F;
  target.pos_x = 100.0F;
  target.pos_y = 0.0F;

  state.player.primary_target_ship_slot = 1;
  return state;
}

TEST_CASE("automation destroy locks the selected target and fires in range") {
  GameState state = DestroyState();
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state, "rAiDeR", 0));
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kEngage);
  CHECK(input.fire);
  CHECK_FALSE(input.thrust); // already inside reach, hold and fire
}

TEST_CASE("automation destroy leads a crossing target") {
  GameState state = DestroyState();
  // A real projectile speed makes the intercept point differ from the target's
  // current position; the target crosses perpendicular to the line of sight.
  state.scenario.weapons[0].projectile_speed = 1500.0F; // 15 px/tick
  state.ShipAt(1).vel_y = 10.0F;
  // Nose already on the target's straight bearing (+x, heading pi/2).
  state.player.heading = 3.14159265358979323846F / 2.0F;
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state, "Raider", 0));
  FlightInput input;
  automation.Tick(state, 0, input);
  // The intercept is clockwise of the target (+x toward +y), so a straight-aim
  // implementation would hold the nose still while this one turns right.
  CHECK(input.turn_right);
  CHECK_FALSE(input.turn_left);
}

TEST_CASE("automation destroy closes the distance while out of range") {
  GameState state = DestroyState();
  state.ShipAt(1).pos_x = 2000.0F; // beyond the 500 px reach, dead ahead
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state, "Raider", 0));
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kEngage);
  CHECK(input.thrust);
  CHECK_FALSE(input.fire);

  state.ShipAt(1).pos_x = 100.0F;
  input = {};
  automation.Tick(state, 16, input);
  CHECK(input.fire);
  CHECK_FALSE(input.thrust);
}

TEST_CASE("automation destroy cycles the target hotkey until it lands") {
  GameState state = DestroyState();
  state.player.primary_target_ship_slot = -1;
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state, "Raider", 0));
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kSelect);
  CHECK(input.cycle_ship_target_next);
  CHECK_FALSE(input.cycle_ship_escorts); // first pass is the non-squad half
  CHECK_FALSE(input.fire);

  // Simulate the loop's own target-command pass landing on the Raider.
  state.player.primary_target_ship_slot = 1;
  input = {};
  automation.Tick(state, 16, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kEngage);
  CHECK_FALSE(input.cycle_ship_target_next);
}

TEST_CASE("automation destroy completes when the target is destroyed") {
  GameState state = DestroyState();
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state, "Raider", 0));
  FlightInput input;
  automation.Tick(state, 0, input);
  REQUIRE(automation.status().phase == FlightAutomationPhase::kEngage);
  state.ShipAt(1).armor_points = 0.0F;
  input = {};
  automation.Tick(state, 16, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kComplete);
}

TEST_CASE("automation destroy accepts an explicit ship id") {
  GameState state = DestroyState();
  state.scenario.ships[1].display_name = "Ion Frigate";
  state.player.primary_target_ship_slot = -1;
  FlightAutomationController automation;
  // The name does not match; the id arm (slot or instance id) acquires it.
  REQUIRE(
      automation.DestroyShip(state, "NoSuchShip", 0, 180000, /*ship_id=*/1));
  state.player.primary_target_ship_slot = 1;
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kEngage);
  CHECK(input.fire);
}

TEST_CASE("automation destroy reads a numeric target as a ship id fallback") {
  GameState state = DestroyState();
  state.scenario.ships[1].display_name = "Ion Frigate";
  state.player.primary_target_ship_slot = -1;
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state, "1", 0));
  state.player.primary_target_ship_slot = 1;
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kEngage);
}

TEST_CASE("automation destroy fails fast when no ship matches") {
  GameState state = DestroyState();
  FlightAutomationController automation;
  CHECK_FALSE(automation.DestroyShip(state, "Marauder", 0));
  CHECK(automation.status().phase == FlightAutomationPhase::kFailed);
}

TEST_CASE("automation destroy may complete when the target is already gone") {
  GameState state = DestroyState();
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state,
                                 "Marauder",
                                 0,
                                 180000,
                                 /*ship_id=*/static_cast<std::int16_t>(-1),
                                 /*allow_missing=*/true));
  CHECK(automation.status().phase == FlightAutomationPhase::kComplete);
  CHECK(automation.status().detail == "target already gone");
}

TEST_CASE(
    "automation destroy allow_missing completes when the target vanishes") {
  GameState state = DestroyState();
  state.player.primary_target_ship_slot = -1;
  FlightAutomationController automation;
  REQUIRE(automation.DestroyShip(state,
                                 "Raider",
                                 0,
                                 180000,
                                 /*ship_id=*/static_cast<std::int16_t>(-1),
                                 /*allow_missing=*/true));
  FlightInput input;
  automation.Tick(state, 0, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kSelect);
  // Another ship destroys the target before the cycle reaches it.
  state.ShipAt(1).is_active = false;
  input = {};
  automation.Tick(state, 16, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kComplete);
  CHECK(automation.status().detail == "target gone");
}

TEST_CASE("automation fails on death and simulation deadline") {
  GameState state = AutomationState();
  FlightAutomationController automation;
  REQUIRE(automation.LandAt(state, "Earth", 10, 5));
  FlightInput input;
  automation.Tick(state, 16, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kFailed);
  REQUIRE(automation.LandAt(state, "Earth", 20));
  state.game_over_pending = true;
  automation.Tick(state, 20, input);
  CHECK(automation.status().phase == FlightAutomationPhase::kFailed);
}

namespace {
GameState RouteState() {
  auto state = AutomationState();
  state.scenario.ships.resize(1);
  state.scenario.ships[0].base_fuel = 300;
  state.scenario.ships[0].base_armor = 100;
  state.player.armor_points = 100;
  state.player.ship_class_id = 0;
  state.player.fuel_points = 300;
  for (auto &system : state.scenario.systems) {
    system.is_visible = true;
    system.has_explored_flag = true;
    system.discovery_state = 1;
  }
  state.scenario.systems[1].links[0] = 0x82;
  REQUIRE(PlotShortestStarmapRoute(state, 2));
  return state;
}

void Arrive(GameState &state, std::int16_t id) {
  state.travel.engaging = false;
  state.player.current_system_id = id;
  state.player.fuel_points -= kJumpFuelCost;
  NovaStarmap_NormalizeRouteToCurrentSystem(state);
  NovaStarmap_SyncTravelSelectionFromRoute(state);
}
} // namespace

TEST_CASE("route autopilot departs and continues after each normal arrival",
          "[route-autopilot]") {
  auto state = RouteState();
  FlightAutomationController pilot;
  REQUIRE(pilot.FollowRoute(state, 0));
  FlightInput input;
  pilot.Tick(state, 1, input);
  CHECK(input.thrust);
  CHECK_FALSE(input.travel);
  state.player.pos_y = -1500;
  input = {};
  pilot.Tick(state, 2, input);
  CHECK(input.travel);
  state.travel.engaging = true;
  input = {};
  pilot.Tick(state, 3, input);
  CHECK_FALSE(input.travel);
  Arrive(state, 1);
  state.arrival_command_grace_frames = 2;
  pilot.Tick(state, 4, input);
  CHECK_FALSE(input.travel);
  state.arrival_command_grace_frames = 0;
  pilot.Tick(state, 5, input);
  CHECK(input.travel);
  CHECK_FALSE(input.starmap);
  CHECK_FALSE(input.hyperspace_mode);
  Arrive(state, 2);
  input = {};
  pilot.Tick(state, 6, input);
  CHECK(pilot.status().phase == FlightAutomationPhase::kComplete);
  CHECK_FALSE(pilot.FollowingRoute());
  CHECK_FALSE(input.travel);
}

TEST_CASE("route autopilot stops without losing the plan",
          "[route-autopilot]") {
  auto state = RouteState();
  FlightAutomationController pilot;
  REQUIRE(pilot.FollowRoute(state, 0));
  SECTION("fuel exhaustion between legs") {
    Arrive(state, 1);
    state.player.fuel_points = 0;
    FlightInput input;
    pilot.Tick(state, 1, input);
    CHECK(pilot.status().phase == FlightAutomationPhase::kFailed);
    CHECK(pilot.status().detail.find("refuel") != std::string::npos);
    CHECK_FALSE(input.travel);
    CHECK_FALSE(input.thrust);
    CHECK(state.travel.starmap_route[1] == 2);
    state.player.fuel_points = 100;
    REQUIRE(pilot.FollowRoute(state, 2));
    input = {};
    pilot.Tick(state, 3, input);
    CHECK(pilot.FollowingRoute());
    return;
  }
  SECTION("manual cancellation") { pilot.Cancel(); }
  SECTION("cleared route") { NovaStarmap_ClearRoute(state); }
  SECTION("invalidated link") { state.scenario.systems[1].is_visible = false; }
  SECTION("disabled ship") { state.player.armor_points = 0; }
  SECTION("departure timeout") {
    FlightInput input;
    pilot.Tick(state, 180001, input);
    CHECK_FALSE(pilot.FollowingRoute());
    CHECK_FALSE(input.travel);
    return;
  }
  FlightInput input;
  pilot.Tick(state, 3, input);
  CHECK_FALSE(pilot.FollowingRoute());
  CHECK_FALSE(input.travel);
  CHECK_FALSE(input.thrust);
}

TEST_CASE("route autopilot can join its first jump already in progress",
          "[route-autopilot]") {
  auto state = RouteState();
  FlightAutomationController pilot;
  state.travel.engaging = true;
  state.travel.destination_system_id = 1;
  REQUIRE(pilot.FollowRoute(state, 0));
  FlightInput input;
  pilot.Tick(state, 1, input);
  CHECK(pilot.status().phase == FlightAutomationPhase::kWaiting);
  CHECK_FALSE(input.travel);
  CHECK_FALSE(input.thrust);
  Arrive(state, 1);
  pilot.Tick(state, 2, input);
  CHECK(pilot.FollowingRoute());
  CHECK_FALSE(input.thrust);
  CHECK(input.travel);
  state.travel.engaging = true;
  state.travel.destination_system_id = 0;
  CHECK_FALSE(pilot.FollowRoute(state, 3));
}

TEST_CASE("floating map edits retarget active travel without cancelling it") {
  auto state = RouteState();
  FlightAutomationController pilot;
  SECTION("route follows the replacement plan") {
    REQUIRE(pilot.FollowRoute(state, 0));
    state.scenario.systems[0].links[1] = 0x82;
    REQUIRE(PlotShortestStarmapRoute(state, 2));
    pilot.NavigationChanged(state, 1);
    REQUIRE(pilot.FollowingRoute());
    state.player.pos_y = -1500;
    FlightInput input;
    pilot.Tick(state, 2, input);
    CHECK(input.travel);
    Arrive(state, 2);
    input = {};
    pilot.Tick(state, 3, input);
    CHECK(pilot.status().phase == FlightAutomationPhase::kComplete);
  }
  SECTION("single jump retargets without enabling the whole route") {
    REQUIRE(pilot.JumpTo(state, "Alpha Centauri", 0));
    state.scenario.systems[0].links[1] = 0x82;
    REQUIRE(PlotShortestStarmapRoute(state, 2));
    pilot.NavigationChanged(state, 1);
    CHECK(pilot.status().goal == FlightAutomationGoal::kJumpTo);
    state.player.pos_y = -1500;
    FlightInput input;
    pilot.Tick(state, 2, input);
    CHECK(input.travel);
    CHECK_FALSE(input.cycle_destination_next);
    Arrive(state, 2);
    pilot.Tick(state, 3, input);
    CHECK(pilot.status().phase == FlightAutomationPhase::kComplete);
  }
  SECTION("docking keeps its port") {
    REQUIRE(pilot.LandAt(state, "Earth", 0));
    pilot.NavigationChanged(state, 1);
    CHECK(pilot.status().goal == FlightAutomationGoal::kLandAt);
    CHECK(pilot.status().target == "Earth");
    CHECK(pilot.status().phase == FlightAutomationPhase::kSelect);
  }
  SECTION("mining keeps its asteroid") {
    auto mining_state = MiningState();
    REQUIRE(pilot.MineAsteroid(mining_state, 0, 0));
    pilot.NavigationChanged(mining_state, 1);
    CHECK(pilot.Mining());
  }
  SECTION("planning never starts an idle autopilot") {
    pilot.NavigationChanged(state, 1);
    CHECK(pilot.status().phase == FlightAutomationPhase::kIdle);
  }
}

TEST_CASE("automatic afterburner preserves route fuel and obeys its setting") {
  auto state = RouteState();
  state.scenario.outfits.resize(1);
  state.scenario.outfits[0].mod_type = 15;
  state.scenario.outfits[0].mod_val = 30;
  state.inventory.outfit_owned_count[0] = 1;
  FlightAutomationController pilot;
  REQUIRE(pilot.FollowRoute(state, 0));
  FlightInput input;
  pilot.Tick(state, 1, input);
  CHECK(input.afterburner);
  state.player.fuel_points = 202;
  input = {};
  pilot.Tick(state, 2, input);
  CHECK_FALSE(input.afterburner);
  state.player.fuel_points = 300;
  state.automatic_afterburner = false;
  input = {};
  pilot.Tick(state, 3, input);
  CHECK_FALSE(input.afterburner);
  state.automatic_afterburner = true;
  state.player.pos_y = -1500;
  input = {};
  pilot.Tick(state, 4, input);
  CHECK_FALSE(input.afterburner);
}

TEST_CASE(
    "auto mining boosts a distant approach then prepares and fires safely") {
  auto state = MiningState();
  state.scenario.outfits.resize(2);
  state.scenario.outfits[1].mod_type = 15;
  state.scenario.outfits[1].mod_val = 30;
  state.inventory.outfit_owned_count[1] = 1;
  state.player.fuel_points = 1000;
  auto &rock = state.asteroid_pool[0];
  rock.target_pos_y = -1600;
  rock.target_vel_x = .5F;
  FlightAutomationController controller;
  REQUIRE(controller.MineAsteroid(state, 0, 0));
  bool boosted = false, fired = false;
  for (int tick = 0; tick < 1600; ++tick) {
    FlightInput input;
    controller.Tick(state, static_cast<std::uint64_t>(tick * 33), input);
    CHECK(input.prepare_mining_bank == 0);
    if (input.afterburner) {
      boosted = true;
      CHECK_FALSE(input.fire_secondary);
      CHECK(std::hypot(rock.target_pos_x - state.player.pos_x,
                       rock.target_pos_y - state.player.pos_y) > 200);
      state.player.fuel_points -= 1;
    }
    if (input.fire_secondary) {
      fired = true;
      CHECK_FALSE(input.afterburner);
      CHECK(std::hypot(state.player.vel_x - rock.target_vel_x,
                       state.player.vel_y - rock.target_vel_y) <= .3F);
      break;
    }
    PlayerMovementOptions opts;
    opts.afterburner = input.afterburner;
    opts.afterburner_push_frame_scaled = true;
    (void)NovaPlayer_IntegrateMovement(
        state.player, input, state.scenario.ships[0], 1.0F, opts);
    rock.target_pos_x += rock.target_vel_x;
  }
  CHECK(boosted);
  CHECK(fired);
  CHECK(state.player.fuel_points >= 100);
}

TEST_CASE("automatic docking boosts far away and lands without overshooting") {
  auto state = AutomationState();
  state.scenario.ships.resize(1);
  auto &cls = state.scenario.ships[0];
  cls.accel = 500;
  cls.speed = 400;
  cls.turn_rate = 40;
  cls.base_armor = 100;
  state.player.ship_class_id = 0;
  state.player.armor_points = 100;
  state.player.fuel_points = 1000;
  state.scenario.outfits.resize(1);
  state.scenario.outfits[0].mod_type = 15;
  state.scenario.outfits[0].mod_val = 30;
  state.inventory.outfit_owned_count[0] = 1;
  const auto id = FlightAutomationController::ResolveStellar(state, "Earth");
  REQUIRE(id);
  const auto *port = state.scenario.Stellar(*id);
  REQUIRE(port);
  state.player.pos_x = port->pos_x;
  state.player.pos_y = port->pos_y + 2000;
  state.travel.selected_stellar_id = *id;
  state.travel.engage_timer = 1000;
  FlightAutomationController controller;
  REQUIRE(controller.LandAt(state, "Earth", 0));
  bool boosted = false, docked = false;
  for (int tick = 0; tick < 2000; ++tick) {
    FlightInput input;
    controller.Tick(state, static_cast<std::uint64_t>(tick * 33), input);
    boosted = boosted || input.afterburner;
    if (input.land) {
      CHECK_FALSE(input.afterburner);
      CHECK(std::hypot(state.player.vel_x, state.player.vel_y) < .8F);
      docked = true;
      break;
    }
    PlayerMovementOptions opts;
    opts.afterburner = input.afterburner;
    opts.afterburner_push_frame_scaled = true;
    if (input.afterburner)
      state.player.fuel_points -= 1;
    (void)NovaPlayer_IntegrateMovement(state.player, input, cls, 1, opts);
  }
  CHECK(boosted);
  CHECK(docked);
  CHECK(state.player.fuel_points >= 100);
}
