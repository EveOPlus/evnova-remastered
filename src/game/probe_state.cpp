#include "probe_state.hpp"

#include "game_state.hpp"
#include "nova_font.hpp"
#include "outfit.hpp"
#include "scenario_data.hpp"
#include "ship_ai.hpp"
#include "story_journal.hpp"
#include "travel.hpp"
#include "weapon.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace game {
namespace {
// The ionization-tinted hull fog level the visual tick rolled, or -1 when the
// hull keeps its plain murk fog.
int IonizationTintLevel(const Ship &ship) {
  const ShipHullFog &fog = ship.hull_fog;
  if (!fog.ionized) {
    return -1;
  }
  return fog.pre_tint ? fog.pre_tint->amount : fog.hull.amount;
}

// Tiny JSON string builder (escaped strings, flat members).
struct Json {
  std::string text = "{";
  bool first_member = true;

  void key(std::string_view name) {
    if (!first_member) {
      text += ",";
    }
    first_member = false;
    text += "\"";
    text += name;
    text += "\":";
  }

  void str(std::string_view name, std::string_view value) {
    // JSON must be valid UTF-8; game strings are raw MacRoman.
    const std::string encoded = NovaText_EncodeUtf8(value);
    key(name);
    text += "\"";
    for (const char c : encoded) {
      switch (c) {
      case '"':
        text += "\\\"";
        break;
      case '\\':
        text += "\\\\";
        break;
      case '\n':
        text += "\\n";
        break;
      case '\r':
        text += "\\r";
        break;
      case '\t':
        text += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          text += ' ';
        } else {
          text += c;
        }
      }
    }
    text += "\"";
  }

  template <typename T> void num(std::string_view name, T value) {
    key(name);
    if constexpr (std::is_floating_point_v<T>) {
      char buffer[32];
      std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
      text += buffer;
    } else {
      text += std::to_string(value);
    }
  }

  void boolean(std::string_view name, bool value) {
    key(name);
    text += value ? "true" : "false";
  }

  // Adds a pre-rendered array as one member (see JsonArr).
  void array(std::string_view name, const std::string &elements) {
    key(name);
    text += elements;
  }

  std::string done() {
    text += "}";
    return text;
  }
};

struct JsonArr {
  std::string text = "[";
  bool first = true;

  void raw(const std::string &element) {
    if (!first) {
      text += ",";
    }
    first = false;
    text += element;
  }

  std::string done() {
    text += "]";
    return text;
  }
};

[[nodiscard]] std::string StellarName(const GameState &state,
                                      std::int16_t stellar_id) {
  if (const auto *stellar = state.scenario.Stellar(stellar_id);
      stellar != nullptr) {
    return stellar->name;
  }
  return "";
}

} // namespace

std::string ProbeState_Snapshot(const GameState &state,
                                const std::string &query) {
  const std::int16_t system_id = state.player.current_system_id;
  const System *system =
      state.scenario.System(static_cast<std::int16_t>(system_id + 0x80));
  const ShipClass *ship_class = state.scenario.Ship(
      static_cast<std::int16_t>(state.player.ship_class_id + 0x80));

  if (query.empty() || query == "summary") {
    Json j;
    j.boolean("game_active", state.game_active);
    j.boolean("player_disabled", NovaAiShip_IsDisabled(state, state.player));
    j.str("pilot", state.pilot.first_name);
    j.num("credits", state.player.credits);
    j.num("system_id", system_id);
    j.str("system", system != nullptr ? system->name : "");
    j.num("ship_class_id", state.player.ship_class_id);
    j.str("ship_class", ship_class != nullptr ? ship_class->display_name : "");
    j.num("pos_x", state.player.pos_x);
    j.num("pos_y", state.player.pos_y);
    j.num("vel_x", state.player.vel_x);
    j.num("vel_y", state.player.vel_y);
    j.num("heading", state.player.heading);
    j.num("speed", state.player.speed);
    j.num("shield", state.player.shield_points);
    j.num("armor", state.player.armor_points);
    j.num("fuel", state.player.fuel_points);
    j.num("max_shield", state.cached_stats.max_shield_points);
    j.num("max_armor", state.cached_stats.max_armor_points);
    std::size_t active_missions = 0;
    for (const auto &flags : state.active_mission_runtime_flags) {
      if (flags.is_active) {
        ++active_missions;
      }
    }
    j.num("active_missions", active_missions);
    // Aggregate career score (Ghidra g_player_combat_rating_points). Exposed so
    // routes can gate on it, e.g. the pirate-hunt loop in
    // tests/scenarios/tutorial_to_combat_rating.toml.
    j.num("combat_rating", state.player_combat_rating_points);
    j.boolean("travel_engaging", state.travel.engaging);
    j.boolean("route_map_visible", state.route_map.overlay_visible);
    j.num("jump_phase", static_cast<int>(state.travel.jump_phase));
    j.num("jump_fade", state.screen_flash_intensity);
    j.boolean("jump_fade_black", state.screen_flash_black);
    // Accelerated scheduler state: `x2_mode_active` is the port's stand-in for
    // the original g_x2_mode_active, set from the accelerated speed multiplier
    // (probe HTTP acceleration or the x2 key).
    j.boolean("x2_mode_active", state.x2_mode_active);
    j.num("gameplay_speed_multiplier", state.gameplay_speed_multiplier);
    j.boolean("jump_x2_mode", state.travel.jump_x2_mode);
    j.num("jump_speed_multiplier", state.travel.jump_speed_multiplier);
    // Global 1/60 s gameplay tick (Ghidra g_frame_tick_count_60hz, 0x00865858).
    // Exposed so a harness can align samples to simulation frames rather than
    // wall-clock or request order.
    j.num("tick_60hz", state.tick_60hz);
    return j.done();
  }

  if (query == "mining") {
    Json j;
    j.boolean("scoop_owned",
              NovaOutfit_HasMiningScoopOutfit(state, state.player));
    j.boolean("scoop_active", state.player.mining_scoop_active);
    j.num("cargo_used", Player_ComputeCargoAndJunkTotal(state));
    j.num("cargo_capacity", Player_ComputeFleetCargoCapacity(state));
    j.num("selected_weapon", state.player.active_weapon_bank_slot);
    j.num("last_fire_tick", state.player.last_weapon_fire_time_ms);
    j.num("shot_count", state.active_shots.size());
    j.num("maneuver_timer", state.player.ai_maneuver_timer_ms);
    j.num("jump_timer", state.player.hyperspace_jump_timer);
    j.num("fold_a", state.player.waypoint_arrival_marker_a);
    j.num("fold_b", state.player.waypoint_arrival_marker_b);
    j.num("sprite_flags", ship_class ? ship_class->sprite_behavior_flags : 0);
    j.num("base_sets", ship_class ? ship_class->base_set_count : 0);
    j.num("animation_count",
          ship_class ? ship_class->animation_cycle_count : 0);
    j.num("dwell", ship_class ? ship_class->combat_state_init_range : 0);
    j.num("animation_timer", state.player.turn_bank_animation_phase);
    JsonArr outfits;
    for (std::size_t i = 0; i < state.inventory.outfit_owned_count.size();
         ++i) {
      const auto count = state.inventory.outfit_owned_count[i];
      if (count <= 0)
        continue;
      const auto *outfit =
          state.scenario.Outfit(static_cast<std::int16_t>(i + 0x80));
      Json row;
      row.num("id", i);
      row.num("count", count);
      row.str("name", outfit ? outfit->short_name : "");
      outfits.raw(row.done());
    }
    j.array("outfits", outfits.done());
    JsonArr weapons;
    for (std::size_t i = 0; i < state.player.weapon_banks.size(); ++i) {
      const auto &bank = state.player.weapon_banks[i];
      if (bank.mounted <= 0)
        continue;
      const auto *weapon =
          state.scenario.Weapon(static_cast<std::int16_t>(i + 0x80));
      if (!weapon)
        continue;
      Json row;
      row.num("id", i);
      row.str("name", weapon->name);
      row.num("mounted", bank.mounted);
      row.num("ammo", bank.ammo);
      row.num("cooldown", bank.cooldown);
      row.num("mode", weapon->weapon_mode_code);
      row.num("attempts",
              NovaWeapon_GetWeaponBurstAttempts(
                  state, state.player, static_cast<std::int16_t>(i)));
      row.num("flags", weapon->flags);
      row.num("flags3", weapon->flags_tertiary);
      row.num("ammo_type", weapon->ammo_type);
      row.num("range", weapon->range_scalar);
      row.boolean("can_fire",
                  NovaWeapon_CanFireWeaponBank(
                      state, state.player, static_cast<std::int16_t>(i)));
      row.boolean("secondary", (weapon->flags & 2U) != 0);
      row.boolean("passes_asteroids", (weapon->flags_quaternary & 1U) != 0);
      row.boolean("mining_bonus", (weapon->flags_secondary & 0x8000U) != 0);
      weapons.raw(row.done());
    }
    j.array("weapons", weapons.done());
    JsonArr asteroids;
    for (std::size_t i = 0; i < state.asteroid_pool.size(); ++i) {
      const auto &asteroid = state.asteroid_pool[i];
      if (!asteroid.active)
        continue;
      Json row;
      row.num("slot", i);
      row.num("group", asteroid.mining_group);
      row.num("type", asteroid.wander_type);
      row.num("x", asteroid.target_pos_x);
      row.num("y", asteroid.target_pos_y);
      row.num("vx", asteroid.target_vel_x);
      row.num("vy", asteroid.target_vel_y);
      row.num("integrity", asteroid.integrity);
      if (const auto *def = state.scenario.AsteroidType(
              static_cast<std::int16_t>(asteroid.wander_type + 0x80))) {
        row.num("yield", def->yield_qty);
        row.num("yield_type", def->yield_type);
      }
      asteroids.raw(row.done());
    }
    j.array("asteroids", asteroids.done());
    JsonArr debris;
    for (const auto &object : state.freeflight_objects) {
      if (object.lifetime_ticks < 0 || !object.persistent)
        continue;
      Json row;
      row.num("x", object.pos_x);
      row.num("y", object.pos_y);
      row.num("cargo_type", object.extra);
      row.num("group", object.mining_group);
      debris.raw(row.done());
    }
    j.array("debris", debris.done());
    return j.done();
  }

  if (query == "player") {
    Json j;
    j.str("pilot", state.pilot.first_name);
    j.str("nickname", state.pilot.last_name);
    j.num("credits", state.player.credits);
    j.num("ship_class_id", state.player.ship_class_id);
    j.str("ship_name", state.player.ship_name);
    j.str("ship_class", ship_class != nullptr ? ship_class->display_name : "");
    j.num("pos_x", state.player.pos_x);
    j.num("pos_y", state.player.pos_y);
    j.num("vel_x", state.player.vel_x);
    j.num("vel_y", state.player.vel_y);
    j.num("heading", state.player.heading);
    j.num("speed", state.player.speed);
    j.num("shield", state.player.shield_points);
    j.num("armor", state.player.armor_points);
    j.num("fuel", state.player.fuel_points);
    j.num("ionization_points", state.player.ionization_points);
    j.num("ionization_color", state.player.ionization_color);
    j.num("ionization_tint_level", IonizationTintLevel(state.player));
    j.num("max_shield", state.cached_stats.max_shield_points);
    j.num("max_armor", state.cached_stats.max_armor_points);
    j.num("fuel_capacity", state.cached_stats.fuel_capacity);
    j.num("primary_target_ship_slot", state.player.primary_target_ship_slot);
    j.num("combat_rating", state.player_combat_rating_points);
    j.num("current_system_id", system_id);
    j.boolean("x2_mode_active", state.x2_mode_active);
    j.num("gameplay_speed_multiplier", state.gameplay_speed_multiplier);
    j.boolean("jump_x2_mode", state.travel.jump_x2_mode);
    j.num("jump_speed_multiplier", state.travel.jump_speed_multiplier);
    return j.done();
  }

  if (query == "missions") {
    Json j;
    JsonArr rows;
    for (std::size_t slot = 0; slot < state.active_missions.size(); ++slot) {
      const auto &flags = state.active_mission_runtime_flags[slot];
      if (!flags.is_active) {
        continue;
      }
      const auto &mission = state.active_missions[slot];
      Json row;
      row.num("slot", slot);
      row.num("template_id", mission.mission_template_id);
      row.num("flags_primary", mission.flags_primary);
      row.num("flags_secondary", mission.flags_secondary);
      row.boolean("can_abort", mission.can_abort);
      row.boolean("failed", flags.is_failed);
      row.boolean("objective_complete", flags.objective_complete);
      row.boolean("carrying_resources", mission.carrying_resources);
      row.num("cargo_type_id", mission.cargo_type_id);
      row.num("cargo_qty_tons", mission.cargo_qty_tons);
      row.num("pickup_mode", mission.pickup_mode);
      row.num("drop_off_mode", mission.drop_off_mode);
      row.num("travel_stellar_id", mission.travel_stellar_id);
      // MisnActive stellar ids are 0-based indices; StellarName takes the
      // 0x80-based resource id.
      row.str("travel_stellar",
              StellarName(
                  state,
                  static_cast<std::int16_t>(mission.travel_stellar_id + 0x80)));
      row.num("return_stellar_id", mission.return_stellar_id);
      row.str("return_stellar",
              StellarName(
                  state,
                  static_cast<std::int16_t>(mission.return_stellar_id + 0x80)));
      row.num("reward", mission.resource_delta_or_cost);
      row.num("time_limit_days_remaining", mission.time_limit_days_remaining);
      row.num("goal_count_remaining", mission.goal_count_remaining);
      rows.raw(row.done());
    }
    j.array("missions", rows.done());
    JsonArr leads;
    for (const auto &lead : BuildStoryLeads(state)) {
      Json row;
      row.num("row", lead.row);
      row.num("stellar_id", lead.stellar_id);
      row.str("title", lead.title);
      row.str("summary", lead.summary);
      row.str("description", lead.description);
      leads.raw(row.done());
    }
    j.array("story_leads", leads.done());
    return j.done();
  }

  if (query == "ships") {
    Json j;
    j.num("tick_60hz", state.tick_60hz);
    JsonArr rows;
    for (std::size_t slot = 1; slot < GameState::kMaxShips; ++slot) {
      const Ship &ship = state.ShipAt(slot);
      if (!ship.is_active ||
          ship.current_system_id != state.player.current_system_id) {
        continue;
      }
      const ShipClass *cls = state.scenario.Ship(
          static_cast<std::int16_t>(ship.ship_class_id + 0x80));
      Json row;
      row.num("slot", slot);
      row.num("ship_class_id", ship.ship_class_id);
      row.str("ship_class", cls != nullptr ? cls->display_name : "");
      row.num("ship_instance_id", ship.ship_instance_id);
      row.num("government_id", ship.faction_or_government_id);
      // Mission/pers tagging and the capture gates. pers_def_slot is the
      // 0x80-based personality index; mission_fleet_slot == -1 marks an
      // ordinary ship (the boarding selector skips mission-fleet ships).
      row.num("pers_def_slot", ship.pers_def_slot);
      row.num("mission_fleet_slot", ship.mission_fleet_slot);
      // `squad_leader_ship_slot == 0` is the player's-attached test used by
      // Ship_CanPlayerHaveMoreEscorts (0x00468920); a non-escort has -1 and an
      // NPC escort carries its leader's slot, so the harness can separate the
      // player's fleet from system traffic.
      row.num("squad_leader_ship_slot", ship.squad_leader_ship_slot);
      row.num("boarded_target_latch", ship.boarded_target_latch);
      row.num("fleet_recovery_hint", ship.fleet_recovery_hint);
      // The values the capture-variant arbitration actually reads.
      row.num("class_default_ai_behavior",
              cls != nullptr ? cls->default_ai_behavior : -1);
      row.num("class_crew", cls != nullptr ? cls->crew : -1);
      row.num("pos_x", ship.pos_x);
      row.num("pos_y", ship.pos_y);
      row.num("vel_x", ship.vel_x);
      row.num("vel_y", ship.vel_y);
      row.num("heading", ship.heading);
      row.num("speed", ship.speed);
      row.num("shield", ship.shield_points);
      row.num("armor", ship.armor_points);
      // Ionization charge against the class capacity (IonizeMax) and decay
      // (Deionize * 0.01 per tick), plus the hull tint rolled from it.
      row.num("ionization_points", ship.ionization_points);
      row.num("ionization_capacity",
              cls != nullptr ? cls->ionization_capacity : 0);
      row.num("ionization_decay_rate",
              cls != nullptr ? cls->ionization_decay_rate : 0.0F);
      row.num("ionization_tint_level", IonizationTintLevel(ship));
      // Raw tank plus the travel/jump locks, so a probe reader can tell a
      // fuel-starved ship from one held by its recorded destination or the
      // velocity-match gate in Stellar_CanShipInitiateJumpSequence.
      row.num("fuel_points", ship.fuel_points);
      row.num("jump_destination_stellar_id", ship.jump_destination_stellar_id);
      row.num("velocity_match_target_ship_slot",
              ship.velocity_match_target_ship_slot);
      row.num("ai_state_code", ship.ai_state_code);
      row.num("ai_behavior_code", ship.ai_behavior_code);
      row.num("ai_control_mode", ship.ai_control_mode);
      row.num("ai_desired_speed", ship.ai_desired_speed);
      row.num("ai_forward_thrust_cmd", ship.ai_forward_thrust_cmd);
      row.num("hyperspace_jump_timer", ship.hyperspace_jump_timer);
      row.num("ai_maneuver_timer", ship.ai_maneuver_timer_ms);
      row.num("random_ai_render_cadence", ship.random_ai_render_cadence);
      // Ship_AcquirePrimaryTargetForShip (0x0040ed37): the radius wraps
      // through a signed short and applies independently to both axes.
      row.num("player_acquisition_radius",
              static_cast<std::int16_t>(ship.random_ai_render_cadence * 600));
      row.num("weapon_ammo_readiness",
              NovaWeapon_ClassifyAmmoReadiness(state, ship));
      row.num("perceived_combat_strength",
              NovaAiShip_ComputePerceivedCombatStrength(state, ship));
      row.num("player_perceived_combat_strength",
              NovaAiShip_ComputePerceivedCombatStrength(state, state.player));
      row.boolean(
          "can_engage_player_under_cloak_rules",
          NovaAiShip_CanEngageTargetUnderCloakRules(state, state.player, ship));
      if (const Government *govt =
              state.scenario.GovernmentByIndex(ship.faction_or_government_id);
          govt != nullptr) {
        row.num("government_flags_primary", govt->flags_primary);
        row.num("government_crime_tol", govt->crime_tol);
        row.num("government_max_odds", govt->max_odds);
        row.boolean("government_rank_no_auto_attack",
                    govt->rank_privileges.no_auto_attack);
        row.boolean("government_rank_always_land",
                    govt->rank_privileges.always_land);
        row.boolean("government_iff_scrambler_active",
                    govt->iff_scrambler_active);
      }
      row.num("primary_target_ship_slot", ship.primary_target_ship_slot);
      // Escort-order investigation: the command selected by the squad
      // supervisor, the polymorphic travel/ship target slot, and the AI's
      // resolved leader. Along with the +0xC0..+0xC2 leader flags below they
      // discriminate "orders never issued" from "faithful defensive hold".
      row.num("escort_command_code", ship.escort_command_code);
      row.num("escort_command_pending", ship.escort_command_pending);
      row.num("ai_secondary_target_slot", ship.ai_secondary_target_slot);
      row.num("resolved_squad_leader_ship_slot",
              ship.resolved_squad_leader_ship_slot);
      row.boolean("is_any_ships_squad_leader", ship.is_any_ships_squad_leader);
      row.boolean("ai_followed_as_leader", ship.ai_followed_as_leader);
      row.boolean("ai_selected_as_resolved_target",
                  ship.ai_selected_as_resolved_target);
      row.num("active_weapon_bank_slot", ship.active_weapon_bank_slot);
      row.num("ai_fire_trigger_latch", ship.ai_fire_trigger_latch);
      row.num("inbound_weapon_threat", ship.inbound_weapon_threat);
      if (ship.active_weapon_bank_slot >= 0 &&
          ship.active_weapon_bank_slot <
              static_cast<std::int16_t>(kWeaponBankCount)) {
        const std::size_t bank =
            static_cast<std::size_t>(ship.active_weapon_bank_slot);
        row.num("active_weapon_mounted", ship.weapon_banks[bank].mounted);
        row.num("active_weapon_ammo", ship.weapon_banks[bank].ammo);
        row.num("active_weapon_cooldown", ship.weapon_banks[bank].cooldown);
      }
      row.boolean("arrival_monitor_active", ship.arrival_monitor_active);
      row.num("arrival_monitor_ticks", ship.arrival_monitor_elapsed_ticks);
      row.boolean("player_target",
                  state.player.primary_target_ship_slot ==
                      static_cast<std::int16_t>(slot));
      rows.raw(row.done());
    }
    j.array("ships", rows.done());
    return j.done();
  }

  if (query == "cargo") {
    Json j;
    j.num("credits", state.player.credits);
    // `capacity` is the player hull's own holds (cached outfit aggregate);
    // `fleet_capacity` adds eligible escort freighters' holds
    // (Player_ComputeFleetCargoCapacity 0x00469760), which is what the trade
    // center and HUD actually enforce.
    j.num("capacity", state.cached_stats.cargo_capacity);
    j.num("fleet_capacity", Player_ComputeFleetCargoCapacity(state));
    JsonArr bins;
    for (const auto tons : state.inventory.cargo_bins) {
      Json row;
      row.num("tons", tons);
      bins.raw(row.done());
    }
    j.array("bins", bins.done());
    JsonArr junk;
    for (std::size_t id = 0; id < state.inventory.junk_counts.size(); ++id) {
      const auto count = state.inventory.junk_counts[id];
      if (count == 0) {
        continue;
      }
      Json row;
      row.num("id", id);
      row.num("count", count);
      junk.raw(row.done());
    }
    j.array("junk", junk.done());
    return j.done();
  }

  if (query == "travel") {
    Json j;
    j.num("travel_slot", state.travel.travel_slot);
    j.boolean("engaging", state.travel.engaging);
    j.num("engaged_stellar_id", state.travel.engaged_stellar_id);
    j.num("destination_system_id", state.travel.destination_system_id);
    j.num("starmap_destination_system_id",
          state.travel.starmap_destination_system_id);
    j.num("selected_stellar_id", state.travel.selected_stellar_id);
    j.boolean("selected_stellar_is_manual",
              state.travel.selected_stellar_is_manual);
    j.boolean("hyperspace_mode", state.travel.hyperspace_mode);
    JsonArr route;
    for (const auto id : state.travel.starmap_route) {
      if (id < 0)
        break;
      Json hop;
      hop.num("id", id);
      if (static_cast<std::size_t>(id) < state.scenario.systems.size()) {
        hop.str("name",
                state.scenario.systems[static_cast<std::size_t>(id)].name);
      }
      route.raw(hop.done());
    }
    j.array("route", route.done());
    return j.done();
  }

  if (query == "galaxy") {
    Json j;
    JsonArr systems;
    for (std::size_t i = 0; i < state.scenario.systems.size(); ++i) {
      const auto &entry = state.scenario.systems[i];
      if (!entry.is_visible || !entry.has_explored_flag)
        continue;
      Json row;
      row.num("id", i);
      row.str("name", entry.name);
      row.boolean("visited", entry.discovery_state > 0);
      row.boolean("on_map",
                  entry.discovery_state > 0 || entry.discovered_this_rebuild);
      JsonArr links;
      for (const auto link : entry.links) {
        if (link < 0x80)
          continue;
        const auto resolved = NovaSystem_ResolveVisibleForTravel(
            state, static_cast<std::int16_t>(link - 0x80));
        if (resolved >= 0)
          links.raw(std::to_string(resolved));
      }
      row.array("links", links.done());
      systems.raw(row.done());
    }
    j.array("systems", systems.done());
    return j.done();
  }

  if (query == "stellars") {
    Json j;
    JsonArr rows;
    for (std::size_t i = 0; i < state.scenario.stellars.size(); ++i) {
      const auto &stellar = state.scenario.stellars[i];
      if (stellar.system_id != state.player.current_system_id ||
          !stellar.is_available)
        continue;
      Json row;
      row.num("id", i + 0x80);
      row.str("name", stellar.name);
      row.num("pos_x", stellar.pos_x);
      row.num("pos_y", stellar.pos_y);
      rows.raw(row.done());
    }
    j.array("stellars", rows.done());
    return j.done();
  }

  if (query == "system") {
    Json j;
    j.num("id", system_id);
    j.str("name", system != nullptr ? system->name : "");
    j.num("government_id", system != nullptr ? system->government_id : -1);
    j.num("avg_ships", system != nullptr ? system->avg_ships : 0);
    if (system_id >= 0 &&
        system_id < static_cast<std::int16_t>(state.system_reputation.size())) {
      j.num("reputation", state.system_reputation[system_id]);
    }
    JsonArr links;
    if (system != nullptr) {
      for (const auto link : system->links) {
        if (link < 0x80)
          continue;
        if (const auto *destination = state.scenario.System(link)) {
          Json row;
          row.num("id", link - 0x80);
          row.str("name", destination->name);
          links.raw(row.done());
        }
      }
    }
    j.array("links", links.done());
    return j.done();
  }

  Json j;
  j.str("error", "unknown query");
  j.str("query", query);
  return j.done();
}

} // namespace game
