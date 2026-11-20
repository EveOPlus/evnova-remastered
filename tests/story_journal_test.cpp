#include "game/mission.hpp"
#include "game/mission_navigation.hpp"
#include "game/starmap.hpp"
#include "game/story_journal.hpp"
#include "game/system_markets.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace game;

namespace {
void Definition(GameState &state,
                int id,
                const std::string &name,
                const std::string &condition,
                const std::string &success,
                std::int16_t port = 171) {
  if (state.scenario.missions.size() <= static_cast<std::size_t>(id - 128))
    state.scenario.missions.resize(static_cast<std::size_t>(id - 127));
  auto &mission = state.scenario.missions[static_cast<std::size_t>(id - 128)];
  mission.present = true;
  mission.resource_name = name;
  mission.availability_expr = condition;
  mission.avail_random = 50;
  mission.avail_location = 1;
  mission.link_system_filter = port;
  for (std::size_t i = 0; i < success.size(); ++i)
    mission.raw_payload[0x359 + i] = static_cast<std::byte>(success[i]);
}

void Merrol(GameState &state) {
  state.scenario.stellars.resize(44);
  auto &port = state.scenario.stellars[43];
  port.is_defined = port.is_available = true;
  port.name = "Merrol";
  port.system_id = 13;
  port.government_id = -1;
  state.scenario.systems.resize(14);
  state.scenario.systems[13].name = "Aldebaran";
  state.scenario.systems[13].nav_defs[0] = 171;
  state.player.current_system_id = 13;
  state.system_reputation.assign(14, 0);
  state.in_flight = true;
}
} // namespace

TEST_CASE("Story journal restores the missing Merrol lead without creating a "
          "mission") {
  GameState state;
  Merrol(state);
  Definition(
      state, 193, "Rebel Insertion; Vellos10", "b359 & !b360", "b360 S798");
  state.control.SetControlBit(359, true);
  const auto before = state.control.bits;
  const auto leads = BuildStoryLeads(state);
  REQUIRE(leads.size() == 1);
  CHECK(leads[0].stellar_id == 171);
  CHECK(leads[0].summary == "Merrol / bar");
  CHECK(leads[0].description.find("another port or system") !=
        std::string::npos);
  CHECK(leads[0].description.find("Rebel Insertion") == std::string::npos);
  CHECK(MissionJournalRows(state).front() == leads[0].row);
  CHECK(BuildMissionTargetSystems(state) == std::vector<std::int16_t>{13});
  REQUIRE(BuildSystemMarkets(state).size() == 1);
  CHECK(BuildSystemMarkets(state)[0].mission_count == 1);
  CHECK(state.control.bits == before);
  CHECK_FALSE(state.active_mission_runtime_flags[0].is_active);

  std::string reason;
  REQUIRE(QueueMissionAutopilot(state, leads[0].row, reason));
  const auto request = *state.pending_mission_autopilot;
  CHECK(MissionAutopilotStillValid(state, request));
  state.control.SetControlBit(360, true);
  CHECK(BuildStoryLeads(state).empty());
  CHECK_FALSE(MissionAutopilotStillValid(state, request));
}

TEST_CASE("Story leads retain waits and reject repeat work, hidden offers and "
          "future stories") {
  GameState state;
  Merrol(state);
  Definition(state, 144, "Equipment; Vellos6", "b354 & !b355", "b355");
  Definition(state, 145, "Regular delivery; Vellos6a", "b355 & !b361", "");
  Definition(state, 195, "Scout; Vellos12", "b423 & !(b361 | b6666)", "b361");
  Definition(state, 150, "Introduction; Polaris1", "!b275", "b275");
  CHECK(BuildStoryLeads(state).empty());
  state.control.SetControlBit(423, true);
  state.control.SetControlBit(6666, true);
  auto leads = BuildStoryLeads(state);
  REQUIRE(leads.size() == 1);
  CHECK(leads[0].description.find("Give your contacts some time") !=
        std::string::npos);
  CHECK(state.control.ControlBit(6666));
  state.scenario.missions[195 - 128].flags_primary = 0x400;
  CHECK(BuildStoryLeads(state).empty());
}

TEST_CASE("Active story assignments take precedence while repeat jobs do not "
          "hide the lead") {
  GameState state;
  Merrol(state);
  Definition(state, 145, "Regular delivery; Vellos6a", "b355 & !b361", "");
  Definition(state, 149, "Insert Agent; Vellos9", "b358", "b359 !b358");
  Definition(state, 193, "Insertion; Vellos10", "b359 & !b360", "b360");
  state.control.SetControlBit(358, true);
  state.active_mission_runtime_flags[0].is_active = true;
  state.active_missions[0].mission_template_id = 145 - 128;
  CHECK(BuildStoryLeads(state).size() == 1);
  state.active_missions[0].mission_template_id = 149 - 128;
  CHECK(BuildStoryLeads(state).empty());
  state.active_mission_runtime_flags[0].is_active = false;
  state.control.SetControlBit(358, false);
  state.control.SetControlBit(359, true);
  REQUIRE(BuildStoryLeads(state).size() == 1);
  CHECK(BuildStoryLeads(state)[0].row == -(193 - 128) - 2);
}

TEST_CASE("Hostile story ports retain their clue without granting docking "
          "clearance") {
  GameState state;
  Merrol(state);
  Definition(state, 193, "Insertion; Vellos10", "b359 & !b360", "b360");
  state.control.SetControlBit(359, true);
  state.scenario.governments.resize(1);
  state.scenario.stellars[43].government_id = 0;
  state.scenario.stellars[43].min_status = -10;
  state.system_reputation[13] = -14;
  const auto lead = BuildStoryLeads(state).front();
  CHECK(lead.description.find("Hail the port") != std::string::npos);
  std::string reason;
  CHECK_FALSE(QueueMissionAutopilot(state, lead.row, reason));
  CHECK(reason.find("Clearance needed") != std::string::npos);
  CHECK(state.system_reputation[13] == -14);
  state.travel.engage_timer = 0x2ef; // Ordinary negotiated landing clearance.
  CHECK(QueueMissionAutopilot(state, lead.row, reason));
  state.in_flight = false;
  state.last_offer_list_stellar = 171;
  CHECK_FALSE(QueueMissionAutopilot(state, lead.row, reason));
  CHECK(reason.find("already here") != std::string::npos);
}

TEST_CASE(
    "Every main story and numbered plugin can retain a progress-gated lead") {
  for (const auto *family : {"Vellos",
                             "Fed",
                             "Rebel I",
                             "Rebel II",
                             "Polaris",
                             "Auroran",
                             "Pirate",
                             "Plugin Story"}) {
    CAPTURE(family);
    GameState state;
    Merrol(state);
    Definition(state,
               150,
               std::string("Assignment; ") + family + "2",
               "b10 & !b11",
               "b11");
    state.control.SetControlBit(10, true);
    REQUIRE(BuildStoryLeads(state).size() == 1);
    state.control.SetControlBit(11, true);
    CHECK(BuildStoryLeads(state).empty());
  }
}

TEST_CASE("Unknown plugin chapter metadata and unknown locations keep cautious "
          "clues") {
  GameState state;
  Definition(state, 150, "Plugin mission", "b10 & !b11", "b11", 10000);
  state.control.SetControlBit(10, true);
  state.scenario.governments.resize(1);
  state.scenario.governments[0].name = "Federation";
  const auto leads = BuildStoryLeads(state);
  REQUIRE(leads.size() == 1);
  CHECK(leads[0].stellar_id == -1);
  CHECK(leads[0].summary == "Check Federation ports");
  std::string reason;
  CHECK_FALSE(MissionAutopilotDestination(state, leads[0].row, reason));
}

TEST_CASE("Shipped campaign handoffs name the actual contact service",
          "[data]") {
  GameState state;
  REQUIRE(state.scenario.LoadFromArchives());

  struct Handoff {
    std::vector<int> bits;
    std::int16_t destination;
    const char *family;
    const char *venue;
  };

  // AvailStel and AvailLoc independently inspected from stock resources
  // 151, 193, 340, 383, 429, 654, 695, 519 and 636.
  for (const auto &handoff :
       std::vector<Handoff>{{{275}, 139, "Polaris", "bar"},
                            {{359}, 171, "Vell-os", "bar"},
                            {{132}, 254, "Rebel I", "bar"},
                            {{182}, 421, "Rebel II", "bar"},
                            {{50}, 136, "Federation", "outfitter"},
                            {{123}, 138, "Federation", "bar"},
                            {{234}, 138, "Auroran", "spaceport"},
                            {{433}, 163, "Pirate Offshoot", "spaceport"},
                            {{201, 511}, 215, "Auroran", "bar"},
                            {{601, 511}, 157, "Pirate", "bar"},
                            {{11}, 338, "United Shipping", "bar"},
                            {{804}, 128, "Wild Geese", "spaceport"}}) {
    CAPTURE(handoff.family);
    state.control.bits.reset();
    for (const auto bit : handoff.bits)
      state.control.SetControlBit(static_cast<std::uint32_t>(bit), true);
    NovaResources_EvaluateAvailability(state);
    const auto leads = BuildStoryLeads(state);
    const auto found =
        std::find_if(leads.begin(), leads.end(), [&](const auto &lead) {
          return lead.title == std::string(handoff.family) + ": story lead";
        });
    REQUIRE(found != leads.end());
    CHECK(found->stellar_id == handoff.destination);
    CHECK(found->summary.ends_with(std::string(" / ") + handoff.venue));
  }
}

TEST_CASE("Ship-origin clues cannot become a spurious docking order") {
  GameState state;
  Merrol(state);
  Definition(state, 193, "Contact; Plugin2", "b359 & !b360", "b360");
  state.scenario.missions[193 - 128].avail_location = 2;
  state.control.SetControlBit(359, true);
  const auto leads = BuildStoryLeads(state);
  REQUIRE(leads.size() == 1);
  CHECK(leads[0].stellar_id == -1);
  std::string reason;
  CHECK_FALSE(QueueMissionAutopilot(state, leads[0].row, reason));
}

TEST_CASE("Obsolete destinationless reminder is replaced without changing its "
          "real slot") {
  GameState state;
  Merrol(state);
  Definition(state, 193, "Insertion; Vellos10", "b359 & !b360", "b360");
  Definition(state, 819, "Drop By Merrol Bar;Rebel Reminder link", "", "", -1);
  state.scenario.missions[819 - 128].avail_random = 0;
  state.control.SetControlBit(351, true);
  state.control.SetControlBit(359, true);
  state.active_mission_runtime_flags[0].is_active = true;
  state.active_missions[0].mission_template_id = 819 - 128;
  const auto rows = MissionJournalRows(state);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0] < -1);
  CHECK(state.active_mission_runtime_flags[0].is_active);
  // A plugin replacement retains its own visibility.
  state.scenario.missions[819 - 128].resource_name = "Plugin assignment";
  CHECK(MissionJournalRows(state).size() == 2);
}
