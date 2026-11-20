#include "flight_automation.hpp"

#include "outfit.hpp"
#include "ship_ai.hpp"
#include "travel.hpp"
#include "weapon.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>

namespace game {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kDegToRad = kPi / 180.0F;
// Conservative fallback when the caller cannot resolve a target sprite: the
// arrival gate itself falls back to 75 (0x4b) but the un-prepared case can
// still land, so 100 keeps the stop point inside every ordinary envelope.
constexpr float kDefaultLandingEnvelope = 100.0F;
// The normal-arrival gate rejects per-axis velocity above 0.75; brake to a
// margin below it so the one-frame tap is not lost to a transient overshoot.
constexpr float kLandingSpeedGate = 0.55F;
constexpr float kSettledSpeed = 0.30F;
// Extra alignment slack (degrees) beyond one frame of turn before committing
// thrust; keeps a small misalignment from stalling the approach.
constexpr float kAlignSlackDeg = 5.0F;
// Turn-radius speed limit. A pure-seeking ship with turn rate omega orbiting a
// target at distance d is stable only while v <= omega * d; without this cap a
// slow-turning hull that misses the target keeps circling it forever. Half the
// theoretical limit keeps a comfortable margin so the nose can keep up.
constexpr float kTurnRadiusSpeedFraction = 0.5F;
// Velocity-error deadband (px/tick): below this the desired velocity is already
// matched, so the ship coasts instead of chattering thrust on and off.
constexpr float kArriveVelocityDeadband = 0.05F;
// kDestroy: approach while beyond this fraction of the best weapon reach, then
// hold station inside it and keep firing. Leaves a margin so a target drifting
// outward still stays in range rather than straddling the reach edge.
constexpr float kDestroyRangeFraction = 0.9F;
// kDestroy target-cycle budget per squad half. The backquote cycle does not
// wrap; a full traversal plus the pass that clears the selection to "none" is
// at most kMaxShips+2 presses, so that bound cannot skip a matching ship.
constexpr int kCycleTapsPerPass = static_cast<int>(GameState::kMaxShips) + 2;

std::string Fold(std::string value);

// A destroy target is identified by its class display name / instance
// ship_name (case-insensitive) and/or an explicit ship identifier. The
// identifier matches either the live slot or the ship's instance id so callers
// can use whichever the probe/scenario exposes. `ship_id < 0` disables the id
// arm; an empty `folded` disables the name arm.
bool ShipMatchesDestroyTarget(const GameState &state,
                              const Ship &ship,
                              const std::string &folded,
                              std::int16_t ship_id) {
  if (ship_id >= 0) {
    const std::size_t id_slot = static_cast<std::size_t>(ship_id);
    if (ship.ship_instance_id == ship_id ||
        (state.SlotInRange(id_slot) && &ship == &state.ShipAt(id_slot))) {
      return true;
    }
  }
  if (folded.empty()) {
    return false;
  }
  const ShipClass *cls =
      state.scenario.Ship(static_cast<std::int16_t>(ship.ship_class_id + 0x80));
  if (cls != nullptr && Fold(cls->display_name) == folded) {
    return true;
  }
  return !ship.ship_name.empty() && Fold(ship.ship_name) == folded;
}

// First active, non-destroyed ship in the player's system matching the destroy
// target. Used both to fail a destroy request fast and to confirm a cycled
// selection landed on the wanted ship.
std::optional<std::int16_t> FindShipSlot(const GameState &state,
                                         const std::string &folded,
                                         std::int16_t ship_id) {
  for (std::size_t slot = 1; slot < GameState::kMaxShips; ++slot) {
    const Ship &ship = state.ShipAt(slot);
    if (!ship.is_active || NovaAiShip_IsDestroyed(ship) ||
        ship.current_system_id != state.player.current_system_id) {
      continue;
    }
    if (ShipMatchesDestroyTarget(state, ship, folded, ship_id)) {
      return static_cast<std::int16_t>(slot);
    }
  }
  return std::nullopt;
}

// Parses a pure-decimal target string as a ship identifier, or -1 when it is
// not a number. This is the id fallback for callers that pass the id via the
// target string (the explicit `ship_id` argument takes precedence).
std::int16_t ParseShipId(const std::string &text) {
  if (text.empty() || text.size() > 5) {
    return -1;
  }
  std::int32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return -1;
    }
    value = value * 10 + (c - '0');
  }
  return value <= 0x7fff ? static_cast<std::int16_t>(value) : -1;
}

std::string Fold(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
  return value;
}

// Game convention: heading 0 points up, increasing clockwise.
float Bearing(float dx, float dy) { return std::atan2(dx, -dy); }

float AngleDelta(float from, float to) {
  return std::remainder(to - from, 2.0F * kPi);
}

// Turns toward `desired`, treating one frame of turn (`elapsed_ticks` worth) as
// the deadband so accelerated frames do not overshoot the heading.
void Face(float heading,
          float desired,
          float turn_rate_deg,
          float elapsed_ticks,
          FlightInput &input) {
  const float step_deg = turn_rate_deg * elapsed_ticks;
  const float tolerance = std::max(1.0F, step_deg) * kDegToRad;
  const float delta = AngleDelta(heading, desired);
  if (delta < -tolerance)
    input.turn_left = true;
  else if (delta > tolerance)
    input.turn_right = true;
}

// Pick the player's primary bank to lead with. The primary-fire loop fires
// every non-secondary bank, so with mixed weapons there is no single true
// intercept; prefer the first armed, ready, lead-capable projectile bank
// (mode -1/4/6..9, the Ship_AimWeaponPredictive gate) and otherwise fall back
// to the first armed ready primary bank, for which the aim helper returns the
// straight bearing a beam/guided weapon wants. -1 when no primary bank is
// ready. This is automation-only selection, not gameplay state.
std::int16_t LeadWeaponBank(const GameState &state, const Ship &ship) {
  std::int16_t first_primary = -1;
  for (std::int16_t bank = 0;
       bank < static_cast<std::int16_t>(kWeaponBankCount);
       ++bank) {
    if (state.player.weapon_banks[static_cast<std::size_t>(bank)].mounted <=
        0) {
      continue;
    }
    const Weapon *w =
        state.scenario.Weapon(static_cast<std::int16_t>(bank + 0x80));
    if (w == nullptr || (w->flags & 0x0002U) != 0U ||
        !NovaWeapon_CanFireWeaponBank(state, ship, bank)) {
      continue;
    }
    if (first_primary < 0) {
      first_primary = bank;
    }
    const std::int16_t mode = w->weapon_mode_code;
    if (mode == -1 || mode == 4 || (mode >= 6 && mode <= 9)) {
      return bank;
    }
  }
  return first_primary;
}

// Remake guidance only. Earliest positive interception at a constant speed;
// the velocity is world-relative for travel, shooter-relative for a projectile.
std::optional<float>
InterceptTicks(float dx, float dy, float vx, float vy, float speed) {
  const double a = static_cast<double>(vx) * vx + static_cast<double>(vy) * vy -
                   static_cast<double>(speed) * speed;
  const double b =
      2.0 * (static_cast<double>(dx) * vx + static_cast<double>(dy) * vy);
  const double c = static_cast<double>(dx) * dx + static_cast<double>(dy) * dy;
  if (speed <= 0 || c < 1e-6)
    return std::nullopt;
  if (std::abs(a) < 1e-6) {
    if (b < -1e-6)
      return static_cast<float>(-c / b);
    return std::nullopt;
  }
  const double discriminant = b * b - 4.0 * a * c;
  if (discriminant < 0)
    return std::nullopt;
  const double q = -.5 * (b + std::copysign(std::sqrt(discriminant), b));
  const double first = q / a;
  const double second = std::abs(q) > 1e-9 ? c / q : -1;
  const double result =
      std::min(first > 0 ? first : std::numeric_limits<double>::max(),
               second > 0 ? second : std::numeric_limits<double>::max());
  if (result == std::numeric_limits<double>::max())
    return std::nullopt;
  return static_cast<float>(result);
}

struct AutomationMotionStats {
  float turn, thrust, speed;
};

AutomationMotionStats MotionStats(const GameState &state) {
  const auto stats = state.stat_cache_valid
                         ? state.cached_stats
                         : Outfit_ComputePlayerEffectiveStats(state);
  const float ionization =
      state.player.ionization_points > 0
          ? (state.cached_ionization_capacity < 0
                 ? .7F
                 : std::min(.7F,
                            NovaOutfit_NormalizeIonizationIntensity(
                                state.player.ionization_points,
                                state.cached_ionization_capacity)))
          : 0.0F;
  const float turn = std::round(stats.turn_raw * .1F * (1.0F - ionization));
  const float thrust = stats.thrust_raw * .0002F * (1.0F - ionization);
  const float speed = stats.speed_raw * .01F;
  return {turn, thrust, speed};
}

bool AutomaticBoostAvailable(const GameState &state, float elapsed_ticks) {
  if (!state.automatic_afterburner ||
      !Outfit_HasPlayerOwnedAfterburner(state) ||
      NovaAiShip_IsDisabled(state, state.player) || state.travel.engaging ||
      state.player.hyperspace_jump_timer > 0)
    return false;
  std::size_t hops = 0;
  bool found_current = false;
  for (const auto system : state.travel.starmap_route) {
    if (system < 0)
      break;
    if (found_current)
      ++hops;
    if (system == state.player.current_system_id)
      found_current = true;
  }
  const float reserve =
      static_cast<float>(std::max(std::size_t{1}, hops)) * kJumpFuelCost;
  const float burn =
      std::max(0.0F, Outfit_GetPlayerAfterburnerFuelBurnRate(state));
  return state.player.fuel_points >=
         reserve + burn * (elapsed_ticks + 2) + 1.0F;
}

void BoostDeparture(const GameState &state,
                    float elapsed_ticks,
                    FlightInput &input) {
  if (input.thrust && !input.turn_left && !input.turn_right &&
      AutomaticBoostAvailable(state, elapsed_ticks))
    input.afterburner = true;
}

void ApproachMiningTarget(const PlayerShip &ship,
                          float x,
                          float y,
                          float vx,
                          float vy,
                          float radius,
                          float turn,
                          float thrust,
                          float speed,
                          float elapsed_ticks,
                          bool &final_approach,
                          bool boost_available,
                          FlightInput &input) {
  const float dx = x - ship.pos_x;
  const float dy = y - ship.pos_y;
  const float distance = std::hypot(dx, dy);
  const float relative_speed = std::hypot(ship.vel_x - vx, ship.vel_y - vy);
  const float omega = std::max(1.0F, turn) * kDegToRad;
  const float acceleration = std::max(.001F, thrust);
  const float braking = relative_speed * kPi / omega +
                        relative_speed * relative_speed / (2 * acceleration);
  // Latch the arrival phase so shedding speed cannot re-arm a full-speed burn.
  // A wrapped asteroid can move far away without changing its pool slot.
  const float worst_speed = std::max(.1F, speed) + std::hypot(vx, vy);
  const float reset_distance =
      2 * (radius + worst_speed * kPi / omega +
           worst_speed * worst_speed / (2 * acceleration));
  if (distance > std::max(500.0F, reset_distance))
    final_approach = false;
  if (distance <= radius + braking + relative_speed * elapsed_ticks + 15.0F)
    final_approach = true;
  const float boost_relative_speed = speed * 1.8F + std::hypot(vx, vy);
  const float boost_braking =
      boost_relative_speed * kPi / omega +
      boost_relative_speed * boost_relative_speed / (2 * acceleration);
  const bool boost = boost_available && !final_approach &&
                     distance > radius + boost_braking + 80.0F;
  if (!final_approach && speed > .1F) {
    if (boost)
      speed *= 1.8F;
    // Account for coasting during the initial turn, then accelerate onto a
    // world-space intercept course. Do not add target velocity a second time.
    const float delay =
        std::abs(AngleDelta(ship.heading, Bearing(dx, dy))) / omega;
    const float future_dx = dx + (vx - ship.vel_x) * delay;
    const float future_dy = dy + (vy - ship.vel_y) * delay;
    if (const auto time = InterceptTicks(future_dx, future_dy, vx, vy, speed)) {
      const float goal_vx = future_dx / *time + vx;
      const float goal_vy = future_dy / *time + vy;
      const float err_x = goal_vx - ship.vel_x;
      const float err_y = goal_vy - ship.vel_y;
      if (std::hypot(err_x, err_y) >
          std::max(kArriveVelocityDeadband, thrust * elapsed_ticks)) {
        const float desired = Bearing(err_x, err_y);
        Face(ship.heading, desired, turn, elapsed_ticks, input);
        if (std::abs(AngleDelta(ship.heading, desired)) <=
            (turn * elapsed_ticks + 2.0F) * kDegToRad)
          input.thrust = true;
        input.afterburner =
            boost && input.thrust && !input.turn_left && !input.turn_right;
      }
      return;
    }
  }
  FlightAutomationController::MoveToAndStopAt(
      ship, x, y, radius, turn, thrust, input, elapsed_ticks, vx, vy, false);
}
} // namespace

std::optional<std::int16_t>
FlightAutomationController::ResolveStellar(const GameState &state,
                                           const std::string &name) {
  const System *system = state.scenario.System(
      static_cast<std::int16_t>(state.player.current_system_id + 0x80));
  if (!system)
    return std::nullopt;
  // A pure-decimal target is a stellar resource id, matching the destroy_ship
  // identifier fallback. This is the reliable way to name a stellar whose
  // MacRoman resource name cannot round-trip through UTF-8 (e.g. Kinik\x8e,
  // resource 0xC9). The id must still be one of the system's nav points.
  std::int32_t numeric = 0;
  if (!name.empty()) {
    const auto parsed =
        std::from_chars(name.data(), name.data() + name.size(), numeric);
    if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size()) {
      if (numeric < 0x80 || numeric > 0xFFFF) {
        return std::nullopt;
      }
      const auto id = static_cast<std::int16_t>(numeric);
      if (std::find(system->nav_defs.begin(), system->nav_defs.end(), id) ==
          system->nav_defs.end()) {
        return std::nullopt;
      }
      return id;
    }
  }
  const std::string folded = Fold(name);
  std::optional<std::int16_t> found;
  for (const std::int16_t id : system->nav_defs) {
    const Stellar *stellar = state.scenario.Stellar(id);
    if (!stellar || Fold(stellar->name) != folded)
      continue;
    if (found && *found != id)
      return std::nullopt;
    found = id;
  }
  return found;
}

std::optional<std::int16_t>
FlightAutomationController::ResolveLinkedSystem(const GameState &state,
                                                const std::string &name) {
  const System *system = state.scenario.System(
      static_cast<std::int16_t>(state.player.current_system_id + 0x80));
  if (!system)
    return std::nullopt;
  const std::string folded = Fold(name);
  std::optional<std::int16_t> found;
  for (const std::int16_t resource_id : system->links) {
    const System *candidate = state.scenario.System(resource_id);
    if (!candidate || Fold(candidate->name) != folded)
      continue;
    const auto index = static_cast<std::int16_t>(resource_id - 0x80);
    if (index == state.player.current_system_id || (found && *found != index))
      return std::nullopt;
    found = index;
  }
  return found;
}

bool FlightAutomationController::LandAt(const GameState &state,
                                        std::string name,
                                        std::uint64_t now_ms,
                                        std::uint64_t timeout_ms,
                                        float landing_envelope_axis_range) {
  const auto id = ResolveStellar(state, name);
  if (!id) {
    Fail("missing or ambiguous current-system stellar");
    return false;
  }
  status_ = {FlightAutomationGoal::kLandAt,
             FlightAutomationPhase::kSelect,
             std::move(name),
             {}};
  target_id_ = *id;
  deadline_ms_ = now_ms + timeout_ms;
  landing_envelope_axis_range_ = landing_envelope_axis_range;
  tap_release_ = false;
  return true;
}

bool FlightAutomationController::JumpTo(const GameState &state,
                                        std::string name,
                                        std::uint64_t now_ms,
                                        std::uint64_t timeout_ms) {
  const auto id = ResolveLinkedSystem(state, name);
  if (!id) {
    Fail("missing, ambiguous, current, or non-adjacent system");
    return false;
  }
  status_ = {FlightAutomationGoal::kJumpTo,
             FlightAutomationPhase::kSelect,
             std::move(name),
             {}};
  target_id_ = *id;
  deadline_ms_ = now_ms + timeout_ms;
  landing_envelope_axis_range_ = -1.0F;
  tap_release_ = false;
  return true;
}

bool FlightAutomationController::DestroyShip(const GameState &state,
                                             std::string name,
                                             std::uint64_t now_ms,
                                             std::uint64_t timeout_ms,
                                             std::int16_t ship_id,
                                             bool allow_missing) {
  status_ = {FlightAutomationGoal::kDestroy,
             FlightAutomationPhase::kSelect,
             std::move(name),
             {}};
  target_id_ = -1;
  target_ship_id_ = ship_id;
  destroy_allow_missing_ = allow_missing;
  if (target_ship_id_ < 0) {
    target_ship_id_ = ParseShipId(status_.target);
  }
  deadline_ms_ = now_ms + timeout_ms;
  landing_envelope_axis_range_ = -1.0F;
  tap_release_ = false;
  cycle_pass_ = 0;
  cycle_taps_left_ = kCycleTapsPerPass;
  // Fail fast when nothing in the system matches: cycling could only exhaust
  // both halves and time out. Probe scenarios may instead opt into treating an
  // already-destroyed target as success (e.g. another ship got the kill).
  if (!FindShipSlot(state, Fold(status_.target), target_ship_id_)) {
    if (destroy_allow_missing_) {
      status_.phase = FlightAutomationPhase::kComplete;
      status_.detail = "target already gone";
      return true;
    }
    Fail("no ship matching target in system");
    return false;
  }
  return true;
}

bool FlightAutomationController::AssistBlockedJump(const GameState &state,
                                                   std::uint64_t now_ms) {
  if (!state.player.is_active || state.travel.engaging ||
      state.player.hyperspace_jump_timer > 0 ||
      NovaAiShip_IsDisabled(state, state.player) ||
      state.player.fuel_points < kJumpFuelCost ||
      !NovaTravel_CanStartJump(state) || NovaTravel_PlayerInJumpRange(state))
    return false;
  const auto &travel = state.travel;
  const auto *system = state.scenario.System(
      static_cast<std::int16_t>(state.player.current_system_id + 0x80));
  if (!system || travel.starmap_destination_system_id < 0 ||
      travel.travel_slot < 0 ||
      static_cast<std::size_t>(travel.travel_slot) >= system->links.size())
    return false;
  const auto link = system->links[static_cast<std::size_t>(travel.travel_slot)];
  if (link < 0x80 || NovaSystem_ResolveVisibleForTravel(
                         state, static_cast<std::int16_t>(link - 0x80)) !=
                         travel.starmap_destination_system_id)
    return false;
  target_id_ = travel.starmap_destination_system_id;
  status_ = {FlightAutomationGoal::kJumpTo,
             FlightAutomationPhase::kMove,
             state.scenario.systems[static_cast<std::size_t>(target_id_)].name,
             "Flying clear, then jumping"};
  deadline_ms_ = now_ms + 180000;
  tap_release_ = false;
  return true;
}

void FlightAutomationController::Cancel() {
  status_.phase = FlightAutomationPhase::kCancelled;
  status_.detail = "cancelled";
  target_id_ = -1;
  tap_release_ = false;
}

bool FlightAutomationController::FollowingRoute() const {
  return status_.goal == FlightAutomationGoal::kFollowRoute &&
         (status_.phase == FlightAutomationPhase::kSelect ||
          status_.phase == FlightAutomationPhase::kMove ||
          status_.phase == FlightAutomationPhase::kEngage ||
          status_.phase == FlightAutomationPhase::kWaiting);
}

void FlightAutomationController::NavigationChanged(const GameState &state,
                                                   std::uint64_t now_ms) {
  if (state.travel.engaging || state.player.hyperspace_jump_timer > 0 ||
      status_.phase == FlightAutomationPhase::kIdle ||
      status_.phase == FlightAutomationPhase::kComplete ||
      status_.phase == FlightAutomationPhase::kFailed ||
      status_.phase == FlightAutomationPhase::kCancelled)
    return;
  if (FollowingRoute() && NovaStarmap_RouteHasHops(state)) {
    (void)FollowRoute(state, now_ms);
    return;
  }
  if (!FollowingRoute() && status_.goal != FlightAutomationGoal::kJumpTo)
    return;
  const auto destination = state.travel.starmap_destination_system_id;
  if (destination < 0 ||
      static_cast<std::size_t>(destination) >= state.scenario.systems.size())
    return;
  (void)JumpTo(
      state,
      state.scenario.systems[static_cast<std::size_t>(destination)].name,
      now_ms);
}

bool FlightAutomationController::FollowRoute(const GameState &state,
                                             std::uint64_t now_ms) {
  if (!NovaStarmap_RouteHasHops(state))
    return false;
  // A quick second J may arrive after the first press has started the jump.
  // Continue that exact route leg without restarting the transition.
  if (state.travel.engaging &&
      state.travel.destination_system_id != state.travel.starmap_route[1])
    return false;
  route_plan_.clear();
  for (const auto id : state.travel.starmap_route) {
    if (id < 0)
      break;
    route_plan_.push_back(id);
  }
  if (route_plan_.size() < 2 ||
      route_plan_.front() != state.player.current_system_id)
    return false;
  route_leg_ = 1;
  deadline_ms_ = now_ms + 180000;
  tap_release_ = false;
  status_ = {FlightAutomationGoal::kFollowRoute,
             FlightAutomationPhase::kSelect,
             "Plotted route",
             {}};
  return true;
}

void FlightAutomationController::TickRoute(const GameState &state,
                                           std::uint64_t now_ms,
                                           FlightInput &input,
                                           float elapsed_ticks) {
  if (state.travel.engaging) {
    status_.phase = FlightAutomationPhase::kWaiting;
    return;
  }
  if (state.player.current_system_id == route_plan_[route_leg_]) {
    ++route_leg_;
    deadline_ms_ = now_ms + 180000; // Allow each leg a full departure window.
    tap_release_ = false;
    if (route_leg_ == route_plan_.size()) {
      status_.phase = FlightAutomationPhase::kComplete;
      status_.detail = "Route complete.";
      return;
    }
  }
  if (state.player.current_system_id != route_plan_[route_leg_ - 1] ||
      state.travel.starmap_route[1] != route_plan_[route_leg_] ||
      state.travel.starmap_destination_system_id != route_plan_[route_leg_]) {
    Fail("Auto-route stopped: navigation changed.");
    return;
  }
  const auto *system = state.scenario.System(
      static_cast<std::int16_t>(state.player.current_system_id + 0x80));
  const auto slot = state.travel.travel_slot;
  if (!system || slot < 0 ||
      static_cast<std::size_t>(slot) >= system->links.size() ||
      system->links[static_cast<std::size_t>(slot)] < 0x80 ||
      NovaSystem_ResolveVisibleForTravel(
          state,
          static_cast<std::int16_t>(
              system->links[static_cast<std::size_t>(slot)] - 0x80)) !=
          route_plan_[route_leg_]) {
    Fail("Auto-route stopped: next jump is unavailable.");
    return;
  }
  if (NovaAiShip_IsDisabled(state, state.player)) {
    Fail("Auto-route stopped: ship disabled.");
    return;
  }
  if (state.player.fuel_points < kJumpFuelCost) {
    Fail("Auto-route stopped: refuel, then double-press J to continue.");
    return;
  }
  if (!NovaTravel_CanStartJump(state)) {
    Fail("Auto-route stopped: ship cannot jump.");
    return;
  }
  if (state.arrival_command_grace_frames > 0 ||
      state.player.hyperspace_jump_timer > 0) {
    status_.phase = FlightAutomationPhase::kWaiting;
    return;
  }
  if (!NovaTravel_PlayerInJumpRange(state)) {
    const auto *cls = state.scenario.Ship(
        static_cast<std::int16_t>(state.player.ship_class_id + 0x80));
    MoveAwayFrom(state.player,
                 0,
                 0,
                 cls ? std::round(cls->turn_rate * 0.1F) : 1.0F,
                 input,
                 elapsed_ticks);
    BoostDeparture(state, elapsed_ticks, input);
    status_.phase = FlightAutomationPhase::kMove;
    return;
  }
  status_.phase = FlightAutomationPhase::kEngage;
  Tap(&FlightInput::travel, input);
}

void FlightAutomationController::ObservedDocked() {
  if (status_.goal == FlightAutomationGoal::kLandAt &&
      (status_.phase == FlightAutomationPhase::kSelect ||
       status_.phase == FlightAutomationPhase::kMove ||
       status_.phase == FlightAutomationPhase::kEngage ||
       status_.phase == FlightAutomationPhase::kWaiting)) {
    status_.phase = FlightAutomationPhase::kComplete;
    status_.detail = "Spaceport observed";
    target_id_ = -1;
    tap_release_ = false;
  }
}

bool FlightAutomationController::Mining() const {
  return status_.goal == FlightAutomationGoal::kMine &&
         status_.phase != FlightAutomationPhase::kComplete &&
         status_.phase != FlightAutomationPhase::kFailed &&
         status_.phase != FlightAutomationPhase::kCancelled;
}

bool FlightAutomationController::MineAsteroid(const GameState &state,
                                              std::int16_t slot,
                                              std::uint64_t now_ms) {
  Cancel();
  status_ = {FlightAutomationGoal::kMine,
             FlightAutomationPhase::kSelect,
             "Asteroid",
             "Auto-mining. Steer to cancel."};
  if (slot < 0 ||
      static_cast<std::size_t>(slot) >= state.asteroid_pool.size() ||
      !state.asteroid_pool[static_cast<std::size_t>(slot)].active) {
    Fail("Asteroid is no longer available.");
    return false;
  }
  if (!NovaOutfit_HasMiningScoopOutfit(state, state.player)) {
    Fail("Auto-mine needs an asteroid scoop.");
    return false;
  }
  mining_bank_ = -1;
  for (std::size_t bank = 0; bank < state.player.weapon_banks.size(); ++bank) {
    const auto *weapon =
        state.scenario.Weapon(static_cast<std::int16_t>(bank + 0x80));
    if (state.player.weapon_banks[bank].mounted > 0 && weapon &&
        (weapon->flags_secondary & 0x8000U) != 0 &&
        (weapon->flags_quaternary & 1U) == 0 &&
        (weapon->weapon_mode_code == -1 || weapon->weapon_mode_code == 0 ||
         weapon->weapon_mode_code == 6 || weapon->weapon_mode_code == 7)) {
      mining_bank_ = static_cast<std::int16_t>(bank);
      break;
    }
  }
  if (mining_bank_ < 0) {
    Fail("Auto-mine needs a forward-firing mining weapon.");
    return false;
  }
  mining_group_ =
      state.asteroid_pool[static_cast<std::size_t>(slot)].mining_group;
  mining_system_ = state.player.current_system_id;
  mining_cargo_start_ = Player_ComputeCargoAndJunkTotal(state);
  mining_target_key_ = -1;
  mining_final_approach_ = false;
  deadline_ms_ = now_ms + 180000;
  tap_release_ = true; // release any selection latch inherited from the menu
  return true;
}

void FlightAutomationController::TickMining(const GameState &state,
                                            FlightInput &input,
                                            float elapsed_ticks) {
  if (state.player.current_system_id != mining_system_ ||
      state.travel.engaging || state.player.hyperspace_jump_timer > 0 ||
      NovaAiShip_IsDisabled(state, state.player)) {
    Fail("Auto-mine stopped: ship cannot maneuver.");
    return;
  }
  if (Player_ComputeCargoAndJunkTotal(state) >=
      Player_ComputeFleetCargoCapacity(state)) {
    Fail("Auto-mine stopped: cargo hold full.");
    return;
  }
  if (!NovaOutfit_HasMiningScoopOutfit(state, state.player)) {
    Fail("Auto-mine stopped: no asteroid scoop.");
    return;
  }
  const auto stats = MotionStats(state);
  const float turn = stats.turn, thrust = stats.thrust, speed = stats.speed;
  const auto approach =
      [&](int key, float x, float y, float vx, float vy, float radius) {
        if (key != mining_target_key_) {
          mining_target_key_ = key;
          mining_final_approach_ = false;
        }
        ApproachMiningTarget(state.player,
                             x,
                             y,
                             vx,
                             vy,
                             radius,
                             turn,
                             thrust,
                             speed,
                             elapsed_ticks,
                             mining_final_approach_,
                             AutomaticBoostAvailable(state, elapsed_ticks),
                             input);
      };
  // Collect existing yield first: it expires, whereas the remaining fragments
  // do not.
  const FreeflightObjectState *cargo = nullptr;
  float nearest = std::numeric_limits<float>::max();
  for (const auto &object : state.freeflight_objects) {
    if (!object.persistent || object.lifetime_ticks < 0 ||
        object.system_id != mining_system_ ||
        object.mining_group != mining_group_)
      continue;
    const float distance = std::hypot(object.pos_x - state.player.pos_x,
                                      object.pos_y - state.player.pos_y);
    if (distance < nearest) {
      nearest = distance;
      cargo = &object;
    }
  }
  if (cargo) {
    status_.phase = FlightAutomationPhase::kMove;
    status_.detail = "Scooping asteroid cargo. Steer to cancel.";
    approach(-2 - static_cast<int>(cargo - state.freeflight_objects.data()),
             cargo->pos_x,
             cargo->pos_y,
             cargo->vel_x,
             cargo->vel_y,
             1.0F);
    return;
  }
  const AsteroidState *asteroid = nullptr;
  nearest = std::numeric_limits<float>::max();
  for (const auto &candidate : state.asteroid_pool) {
    if (!candidate.active || candidate.mining_group != mining_group_ ||
        candidate.integrity <= -32000)
      continue;
    const float distance =
        std::hypot(candidate.target_pos_x - state.player.pos_x,
                   candidate.target_pos_y - state.player.pos_y);
    if (distance < nearest) {
      nearest = distance;
      asteroid = &candidate;
    }
  }
  if (!asteroid) {
    Stop(state.player, turn, input, elapsed_ticks);
    if (std::hypot(state.player.vel_x, state.player.vel_y) < kSettledSpeed) {
      status_.phase = FlightAutomationPhase::kComplete;
      status_.detail =
          "Auto-mine finished: " +
          std::to_string(std::max(0,
                                  Player_ComputeCargoAndJunkTotal(state) -
                                      mining_cargo_start_)) +
          " tons collected.";
    }
    return;
  }
  const auto *weapon =
      state.scenario.Weapon(static_cast<std::int16_t>(mining_bank_ + 0x80));
  if (!weapon ||
      state.player.weapon_banks[static_cast<std::size_t>(mining_bank_)]
              .mounted <= 0 ||
      !NovaWeapon_CanFireWeaponBank(state, state.player, mining_bank_)) {
    Fail("Auto-mine stopped: mining weapon unavailable.");
    return;
  }
  if ((weapon->flags & 2U) != 0 &&
      state.player.active_weapon_bank_slot != mining_bank_) {
    Tap(&FlightInput::cycle_secondary, input);
    return;
  }
  const float reach = weapon->weapon_mode_code == 0
                          ? static_cast<float>(weapon->beam_length_px)
                          : weapon->range_scalar;
  input.prepare_mining_bank = mining_bank_;
  // Break rocks close to the scoop so their short-lived yield can be reached.
  const float firing_distance = std::min(65.0F, reach * .75F);
  status_.phase = FlightAutomationPhase::kMove;
  status_.detail = "Approaching asteroid. Steer to cancel.";
  approach(static_cast<int>(asteroid - state.asteroid_pool.data()),
           asteroid->target_pos_x,
           asteroid->target_pos_y,
           asteroid->target_vel_x,
           asteroid->target_vel_y,
           20.0F);
  if (nearest > firing_distance)
    return;
  if (nearest < 1.0F) {
    // A fragment can spawn directly on the ship. Create an aiming baseline
    // instead of waiting forever for a positive projectile intercept time.
    input.turn_left = input.turn_right = input.thrust = false;
    MoveToAndStopAt(state.player,
                    asteroid->target_pos_x + 20.0F,
                    asteroid->target_pos_y,
                    1.0F,
                    turn,
                    thrust,
                    input,
                    elapsed_ticks,
                    asteroid->target_vel_x,
                    asteroid->target_vel_y,
                    false);
    return;
  }
  Ship target;
  target.pos_x = asteroid->target_pos_x;
  target.pos_y = asteroid->target_pos_y;
  target.vel_x = asteroid->target_vel_x;
  target.vel_y = asteroid->target_vel_y;
  float desired = static_cast<float>(NovaAi_AimWeaponPredictive(
                      state, state.player, target, mining_bank_)) *
                  kDegToRad;
  const float rel_x = target.vel_x - state.player.vel_x;
  const float rel_y = target.vel_y - state.player.vel_y;
  if (std::hypot(rel_x, rel_y) > kSettledSpeed)
    return;
  const float dx = target.pos_x - state.player.pos_x;
  const float dy = target.pos_y - state.player.pos_y;
  float aim_x = dx;
  float aim_y = dy;
  if (weapon->weapon_mode_code == -1 || weapon->weapon_mode_code == 7) {
    const auto time =
        InterceptTicks(dx, dy, rel_x, rel_y, weapon->projectile_speed * .01F);
    if (!time || *time > weapon->lifetime_ticks)
      return;
    aim_x += rel_x * *time;
    aim_y += rel_y * *time;
    desired = Bearing(aim_x, aim_y);
  }
  input.turn_left = input.turn_right = input.thrust = false;
  Face(state.player.heading, desired, turn, elapsed_ticks, input);
  // A frame-sized angular tolerance alone fires wide on fast-turning ships.
  // Require the actual outgoing heading to cross the rock's inner footprint.
  const float angle = std::abs(AngleDelta(state.player.heading, desired));
  const float next_heading =
      state.player.heading +
      (static_cast<int>(input.turn_right) - static_cast<int>(input.turn_left)) *
          turn * elapsed_ticks * kDegToRad;
  const float next_angle = std::abs(AngleDelta(next_heading, desired));
  const float corridor =
      std::clamp(asteroid->collision_radius_px * .4F, 2.0F, 8.0F);
  const float tolerance = std::asin(
      std::min(1.0F, corridor / std::max(1.0F, std::hypot(aim_x, aim_y))));
  bool on_target = std::max(angle, next_angle) <= tolerance;
  if (weapon->weapon_mode_code == -1 || weapon->weapon_mode_code == 7) {
    const auto crosses_rock = [&](float heading) {
      heading = std::round(heading / kDegToRad) * kDegToRad;
      const float shot_x =
          std::sin(heading) * weapon->projectile_speed * .01F - rel_x;
      const float shot_y =
          -std::cos(heading) * weapon->projectile_speed * .01F - rel_y;
      const float speed_sq = shot_x * shot_x + shot_y * shot_y;
      if (speed_sq <= 1e-6F)
        return false;
      const float time = (dx * shot_x + dy * shot_y) / speed_sq;
      return time > 0 && time <= weapon->lifetime_ticks &&
             std::hypot(dx - shot_x * time, dy - shot_y * time) <= corridor;
    };
    on_target = on_target && crosses_rock(state.player.heading) &&
                crosses_rock(next_heading);
  }
  if (on_target) {
    status_.phase = FlightAutomationPhase::kEngage;
    status_.detail = "Mining asteroid. Steer to cancel.";
    if ((weapon->flags & 2U) != 0)
      input.fire_secondary = true;
    else
      input.fire = true;
  }
}

void FlightAutomationController::Fail(std::string detail) {
  status_.phase = FlightAutomationPhase::kFailed;
  status_.detail = std::move(detail);
  target_id_ = -1;
}

void FlightAutomationController::Tap(bool FlightInput::*field,
                                     FlightInput &input) {
  if (tap_release_) {
    tap_release_ = false;
    return;
  }
  input.*field = true;
  tap_release_ = true;
}

void FlightAutomationController::Stop(const PlayerShip &ship,
                                      float turn_rate_deg,
                                      FlightInput &input,
                                      float elapsed_ticks,
                                      float reference_vel_x,
                                      float reference_vel_y) {
  // Brake the velocity RELATIVE to the reference. For a stationary reference
  // this is a full stop; for a moving boarding target it matches its velocity.
  const float rel_vel_x = ship.vel_x - reference_vel_x;
  const float rel_vel_y = ship.vel_y - reference_vel_y;
  if (std::hypot(rel_vel_x, rel_vel_y) <= kSettledSpeed)
    return;
  // Point the nose opposite the relative velocity and burn: the ship coasts
  // through the turnaround, then the inverse thrust decays the relative
  // velocity. Its bearing is unchanged by the deceleration, so the target
  // stays valid for the whole maneuver.
  const float reverse = Bearing(rel_vel_x, rel_vel_y) + kPi;
  Face(ship.heading, reverse, turn_rate_deg, elapsed_ticks, input);
  const float delta = std::abs(AngleDelta(ship.heading, reverse));
  const float align_deg = turn_rate_deg * elapsed_ticks + 2.0F;
  if (delta <= align_deg * kDegToRad)
    input.thrust = true;
}

void FlightAutomationController::MoveAwayFrom(const PlayerShip &ship,
                                              float x,
                                              float y,
                                              float turn_rate_deg,
                                              FlightInput &input,
                                              float elapsed_ticks) {
  float dx = ship.pos_x - x;
  float dy = ship.pos_y - y;
  // A ship exactly at the reference point has no radial bearing. Preserve its
  // current heading so the maneuver still makes deterministic progress.
  const float desired =
      (dx == 0.0F && dy == 0.0F) ? ship.heading : Bearing(dx, dy);
  Face(ship.heading, desired, turn_rate_deg, elapsed_ticks, input);
  const float delta = std::abs(AngleDelta(ship.heading, desired));
  const float align_deg = turn_rate_deg * elapsed_ticks + kAlignSlackDeg;
  if (delta <= align_deg * kDegToRad)
    input.thrust = true;
}

void FlightAutomationController::MoveToAndStopAt(const PlayerShip &ship,
                                                 float x,
                                                 float y,
                                                 float radius,
                                                 float turn_rate_deg,
                                                 float thrust,
                                                 FlightInput &input,
                                                 float elapsed_ticks,
                                                 float target_vel_x,
                                                 float target_vel_y,
                                                 bool lead_moving_target) {
  const float dx = x - ship.pos_x;
  const float dy = y - ship.pos_y;
  const float distance = std::hypot(dx, dy);
  // All control is in the target's frame so a moving target (boarding) settles
  // with matched velocity instead of a full stop.
  const float rel_vel_x = ship.vel_x - target_vel_x;
  const float rel_vel_y = ship.vel_y - target_vel_y;
  // `radius` is the arrival tolerance, not a standoff: the ship drives to the
  // point itself and this branch only decides when it is close enough to stop.
  if (distance <= radius) {
    Stop(ship, turn_rate_deg, input, elapsed_ticks, target_vel_x, target_vel_y);
    return;
  }
  const float omega = std::max(1.0F, turn_rate_deg) * kDegToRad;
  const float turn_time_ticks = kPi / omega;
  // Speed whose 180-degree turnaround drift plus reverse-thrust stop exactly
  // fits the distance to the point: speed * pi/omega + speed^2/(2*thrust).
  float speed_cap = 0.0F;
  if (thrust > 0.0F) {
    speed_cap = thrust * (-turn_time_ticks +
                          std::sqrt(turn_time_ticks * turn_time_ticks +
                                    2.0F * distance / thrust));
  } else {
    speed_cap = distance / turn_time_ticks;
  }
  // Follow a moving target by leading it by the estimated closing time.
  float aim_x = x;
  float aim_y = y;
  if (lead_moving_target && speed_cap > kArriveVelocityDeadband) {
    const float lead_ticks = std::min(distance / speed_cap, 120.0F);
    aim_x += target_vel_x * lead_ticks;
    aim_y += target_vel_y * lead_ticks;
  }
  const float adx = aim_x - ship.pos_x;
  const float ady = aim_y - ship.pos_y;
  const float aim_distance = std::hypot(adx, ady);
  // Do not outrun the turn radius: a slow-turning ship must slow down to curve
  // onto the target instead of orbiting it.
  speed_cap =
      std::min(speed_cap, kTurnRadiusSpeedFraction * omega * aim_distance);
  // Velocity-error control: steer the relative velocity toward the desired
  // velocity (toward the aim point at the safe speed) instead of phase-
  // switching between "face target" and "face reverse velocity". The error
  // automatically encodes acceleration, braking, and lateral correction, so a
  // slow hull tightens its turn as it slows.
  const float dir_x = aim_distance > 1e-4F ? adx / aim_distance : 0.0F;
  const float dir_y = aim_distance > 1e-4F ? ady / aim_distance : 0.0F;
  const float err_x = dir_x * speed_cap - rel_vel_x;
  const float err_y = dir_y * speed_cap - rel_vel_y;
  if (std::hypot(err_x, err_y) <= kArriveVelocityDeadband)
    return;
  const float desired = Bearing(err_x, err_y);
  Face(ship.heading, desired, turn_rate_deg, elapsed_ticks, input);
  const float delta = std::abs(AngleDelta(ship.heading, desired));
  const float align_deg = turn_rate_deg * elapsed_ticks + kAlignSlackDeg;
  if (delta <= align_deg * kDegToRad)
    input.thrust = true;
}

void FlightAutomationController::Tick(const GameState &state,
                                      std::uint64_t now_ms,
                                      FlightInput &input,
                                      float elapsed_ticks) {
  if (status_.phase == FlightAutomationPhase::kIdle ||
      status_.phase == FlightAutomationPhase::kComplete ||
      status_.phase == FlightAutomationPhase::kFailed ||
      status_.phase == FlightAutomationPhase::kCancelled)
    return;
  if (state.game_over_pending || !state.player.is_active) {
    Fail("player died or left flight");
    return;
  }
  if (now_ms > deadline_ms_) {
    Fail(FollowingRoute() ? "Auto-route stopped: departure timed out."
                          : "simulation-time deadline exceeded");
    return;
  }
  if (status_.goal == FlightAutomationGoal::kFollowRoute) {
    TickRoute(state, now_ms, input, elapsed_ticks);
    return;
  }
  if (status_.goal == FlightAutomationGoal::kDestroy) {
    TickDestroy(state, input, elapsed_ticks);
    return;
  }
  if (status_.goal == FlightAutomationGoal::kMine) {
    TickMining(state, input, elapsed_ticks);
    return;
  }
  if (status_.goal == FlightAutomationGoal::kJumpTo) {
    if (state.player.current_system_id == target_id_ &&
        !state.travel.engaging) {
      status_.phase = FlightAutomationPhase::kComplete;
      return;
    }
    if (state.travel.engaging) {
      status_.phase = FlightAutomationPhase::kWaiting;
      return;
    }
    // Cycling/plotting already arms a jump without the HUD's hyperspace-mode
    // flag. Sending H here can repeatedly reopen the route overlay when the
    // plotted-jump mode is already 3, never reaching the movement phase.
    if (state.travel.starmap_destination_system_id != target_id_ ||
        state.travel.travel_slot < 0) {
      input.quiet_navigation = true;
      Tap(&FlightInput::cycle_destination_next, input);
      return;
    }
    if (!NovaTravel_PlayerInJumpRange(state)) {
      status_.phase = FlightAutomationPhase::kMove;
      const ShipClass *cls = state.scenario.Ship(
          static_cast<std::int16_t>(state.player.ship_class_id + 0x80));
      const float turn = cls ? std::round(cls->turn_rate * 0.1F) : 1.0F;
      MoveAwayFrom(state.player, 0.0F, 0.0F, turn, input, elapsed_ticks);
      BoostDeparture(state, elapsed_ticks, input);
      return;
    }
    status_.phase = FlightAutomationPhase::kEngage;
    Tap(&FlightInput::travel, input);
    return;
  }
  if (ResolveStellar(state, status_.target) != target_id_) {
    Fail("landing destination was invalidated");
    return;
  }
  if (state.travel.selected_stellar_id != target_id_) {
    status_.phase = FlightAutomationPhase::kSelect;
    Tap(&FlightInput::cycle_target_next, input);
    return;
  }
  const Stellar *stellar = state.scenario.Stellar(target_id_);
  if (!stellar) {
    Fail("landing destination disappeared");
    return;
  }
  const auto stats = MotionStats(state);
  const float turn = stats.turn, thrust = stats.thrust;
  const float envelope = landing_envelope_axis_range_ > 0.0F
                             ? landing_envelope_axis_range_
                             : kDefaultLandingEnvelope;
  const float dx = static_cast<float>(stellar->pos_x) - state.player.pos_x;
  const float dy = static_cast<float>(stellar->pos_y) - state.player.pos_y;
  // Match the original arrival gate exactly (PlayerTick_LandCommandDispatch):
  // an axis-aligned envelope and a per-axis velocity limit, not a circular
  // radius. Tapping with the old fixed 220 px radius missed small-sprite
  // stellars whose real envelope is as small as 75 px.
  const bool inside_envelope =
      std::abs(dx) < envelope && std::abs(dy) < envelope;
  const bool slow_enough = std::abs(state.player.vel_x) <= kLandingSpeedGate &&
                           std::abs(state.player.vel_y) <= kLandingSpeedGate;
  if (inside_envelope && slow_enough && state.travel.engage_timer >= 0x2ee) {
    status_.phase = FlightAutomationPhase::kEngage;
    Tap(&FlightInput::land, input);
    return;
  }
  status_.phase = FlightAutomationPhase::kMove;
  // Arrive within half the gate so the tap has margin against drift; the
  // maneuver drives to the stellar and stops once inside that tolerance.
  MoveToAndStopAt(state.player,
                  static_cast<float>(stellar->pos_x),
                  static_cast<float>(stellar->pos_y),
                  envelope * 0.5F,
                  turn,
                  thrust,
                  input,
                  elapsed_ticks);
  const float boosted_speed = std::max(
      stats.speed * 1.8F, std::hypot(state.player.vel_x, state.player.vel_y));
  const float braking =
      boosted_speed * kPi / (std::max(1.0F, turn) * kDegToRad) +
      boosted_speed * boosted_speed / (2 * std::max(.001F, thrust));
  if (std::hypot(dx, dy) > envelope + braking + 80.0F &&
      std::abs(AngleDelta(state.player.heading, Bearing(dx, dy))) <
          5 * kDegToRad)
    BoostDeparture(state, elapsed_ticks, input);
}

void FlightAutomationController::TickDestroy(const GameState &state,
                                             FlightInput &input,
                                             float elapsed_ticks) {
  const std::string folded = Fold(status_.target);
  // An already-vanished target (destroyed by another AI before the cycle
  // reached it) satisfies the goal in allow-missing mode instead of leaving
  // the scenario cycling for a ship that is no longer there.
  if (target_id_ < 0 && destroy_allow_missing_ &&
      !FindShipSlot(state, folded, target_ship_id_)) {
    status_.phase = FlightAutomationPhase::kComplete;
    status_.detail = "target gone";
    return;
  }
  if (target_id_ < 0) {
    // Lock immediately when the player already holds a matching target; this
    // also catches the frame after a successful cycle tap.
    const std::int16_t current = state.player.primary_target_ship_slot;
    if (current >= 0 && state.SlotInRange(static_cast<std::size_t>(current))) {
      const Ship &held = state.ShipAt(static_cast<std::size_t>(current));
      if (ShipMatchesDestroyTarget(state, held, folded, target_ship_id_)) {
        // Fall through so the lock frame already steers and fires.
        target_id_ = current;
      }
    }
  }
  if (target_id_ < 0) {
    // Drive the backquote cycle one press at a time: first the non-squad half
    // (no modifier), where a hostile destroy target lives, then the player's
    // own squad/escort half (Ctrl modifier set).
    if (cycle_pass_ > 1) {
      Fail("cycled every eligible ship without finding the target");
      return;
    }
    if (cycle_taps_left_ <= 0) {
      ++cycle_pass_;
      cycle_taps_left_ = kCycleTapsPerPass;
      return;
    }
    input.cycle_ship_escorts = (cycle_pass_ == 1);
    const bool will_press = !tap_release_;
    Tap(&FlightInput::cycle_ship_target_next, input);
    if (will_press) {
      --cycle_taps_left_;
    }
    status_.phase = FlightAutomationPhase::kSelect;
    return;
  }

  // Locked: validate every frame so a destroyed or departed target ends the
  // goal instead of leaving the controller firing at empty space.
  if (!state.SlotInRange(static_cast<std::size_t>(target_id_))) {
    status_.phase = FlightAutomationPhase::kComplete;
    status_.detail = "target gone";
    return;
  }
  const Ship &target = state.ShipAt(static_cast<std::size_t>(target_id_));
  if (!target.is_active || NovaAiShip_IsDestroyed(target)) {
    status_.phase = FlightAutomationPhase::kComplete;
    status_.detail = "target destroyed";
    return;
  }
  if (target.current_system_id != state.player.current_system_id ||
      target.ai_state_code == 0x15) {
    // AI state 0x15 is the jump-out approach; the target is leaving.
    Fail("target left the system");
    return;
  }
  // Face and fire are re-derived from the live geometry each frame, so weapon
  // pushback simply moves the aim point and the hull keeps tracking.
  const ShipClass *cls = state.scenario.Ship(
      static_cast<std::int16_t>(state.player.ship_class_id + 0x80));
  const float turn = cls ? std::round(cls->turn_rate * 0.1F) : 1.0F;
  const float dx = target.pos_x - state.player.pos_x;
  const float dy = target.pos_y - state.player.pos_y;
  // Lead a crossing target: fire commands shoot along the current heading, so
  // aim at the predicted intercept instead of the target's stale position.
  // NovaAi_AimWeaponPredictive (Ghidra 0x0043b740) handles the flight-time
  // math and falls back to the straight bearing for beams/guided weapons.
  float desired = Bearing(dx, dy);
  const std::int16_t lead_bank = LeadWeaponBank(state, state.player);
  if (lead_bank >= 0) {
    const std::int16_t lead_deg =
        NovaAi_AimWeaponPredictive(state, state.player, target, lead_bank);
    desired = static_cast<float>(lead_deg) * kDegToRad;
  }
  Face(state.player.heading, desired, turn, elapsed_ticks, input);
  const float distance = std::hypot(dx, dy);
  const int reach = NovaWeapon_GetShipMaxWeaponRange(state, state.player);
  if (reach > 0 && distance <= static_cast<float>(reach)) {
    // Eager fire: the primary banks fire along the current heading on the
    // loop's own weapon-command pass.
    input.fire = true;
  }
  // Close the distance until inside the firing envelope, but only while the
  // nose is already on the target; otherwise turning (not thrusting) is the
  // right correction and thrusting would drift the aim.
  const float align_deg = turn * elapsed_ticks + kAlignSlackDeg;
  const bool aligned = std::abs(AngleDelta(state.player.heading, desired)) <=
                       align_deg * kDegToRad;
  const bool need_close = reach <= 0 || distance > static_cast<float>(reach) *
                                                       kDestroyRangeFraction;
  if (need_close && aligned) {
    input.thrust = true;
  }
  status_.phase = FlightAutomationPhase::kEngage;
}
} // namespace game
