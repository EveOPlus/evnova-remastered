#include "story_journal.hpp"

#include "outfit.hpp"
#include "starmap.hpp"
#include "travel.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>

namespace game {
namespace {
struct Chapter {
  std::string family;
  int number = 0;
};

// Resource subtitles are author annotations, not player-facing mission titles.
// Stock main/side stories and numbered plugin chapters use this convention.
Chapter ChapterOf(const MissionDef &mission) {
  const auto separator = mission.resource_name.find(';');
  if (separator == std::string::npos)
    return {};
  const auto suffix =
      std::string_view(mission.resource_name).substr(separator + 1);
  Chapter chapter;
  for (const unsigned char ch : suffix) {
    if (std::isdigit(ch))
      break;
    if (std::isspace(ch)) {
      if (!chapter.family.empty() && chapter.family.back() != ' ')
        chapter.family += ' ';
    } else {
      chapter.family += static_cast<char>(ch);
    }
  }
  while (!chapter.family.empty() && chapter.family.back() == ' ')
    chapter.family.pop_back();
  const auto digit = suffix.find_first_of("0123456789");
  if (digit == std::string_view::npos)
    return {};
  for (std::size_t i = digit;
       i < suffix.size() && std::isdigit(static_cast<unsigned char>(suffix[i]));
       ++i)
    chapter.number = std::min(10000, chapter.number * 10 + suffix[i] - '0');
  if (chapter.family == "Vellos")
    chapter.family = "Vell-os";
  else if (chapter.family == "Fed")
    chapter.family = "Federation";
  return chapter;
}

bool HasNewProgress(const GameState &state,
                    const MissionDef &mission,
                    int depth = 0) {
  // Acceptance, completion and ship-goal scripts can advance a story by
  // setting OR clearing a bit. Follow explicit S-linked helpers, bounded for
  // cyclic plugin scripts, but never execute scripts or consume randomness.
  for (const std::size_t offset : {0x15BU, 0x359U, 0x660U}) {
    for (std::size_t i = offset;
         i < offset + 255 && mission.raw_payload[i] != std::byte{};
         ++i) {
      const char ch = static_cast<char>(mission.raw_payload[i]);
      if (ch != 'b' && ch != 'B' && ch != 's' && ch != 'S')
        continue;
      std::size_t previous = i;
      while (previous > offset && std::isspace(static_cast<unsigned char>(
                                      mission.raw_payload[previous - 1])))
        --previous;
      const bool clear = previous > offset &&
                         mission.raw_payload[previous - 1] == std::byte{'!'};
      std::uint32_t operand = 0;
      bool digits = false;
      while (i + 1 < offset + 255) {
        const char next = static_cast<char>(mission.raw_payload[i + 1]);
        if (next < '0' || next > '9')
          break;
        digits = true;
        operand = std::min(
            10000U, operand * 10 + static_cast<std::uint32_t>(next - '0'));
        ++i;
      }
      if (!digits)
        continue;
      if (ch == 's' || ch == 'S') {
        if (const auto *linked =
                state.scenario.Mission(static_cast<std::int16_t>(operand));
            linked && depth < 4 && HasNewProgress(state, *linked, depth + 1))
          return true;
      } else if (operand < PilotControlState::kControlBitCount &&
                 operand != 6666 &&
                 state.control.ControlBit(operand) == clear) {
        return true;
      }
    }
  }
  return false;
}

bool IsVisible(const GameState &state, std::size_t slot) {
  const auto &flags = state.active_mission_runtime_flags[slot];
  return flags.is_active && (flags.flags_primary_at_accept & 0x400U) == 0;
}
} // namespace

std::vector<StoryLead> BuildStoryLeads(const GameState &state) {
  ControlExpressionState progress;
  progress.get_control_bit = [&](std::uint32_t bit) {
    return state.control.ControlBit(bit);
  };
  progress.is_registered = [&](std::uint32_t) {
    return state.control.registered;
  };
  progress.is_male = [&] { return state.control.male; };
  progress.owns_outfit = [&](std::int16_t id) {
    return Outfit_PlayerHasOutfitForControlExpression(state, id);
  };
  progress.has_explored = [&](std::int16_t id) {
    return NovaSystem_HasExploredToken(state, id);
  };
  // Infer delayed follow-ups without changing the actual availability gate.
  // A plugin that positively requires this bit still uses its live result.
  ControlExpressionState after_delay = progress;
  after_delay.get_control_bit = [&](std::uint32_t bit) {
    return bit != 6666 && state.control.ControlBit(bit);
  };
  ControlExpressionState no_progress = progress;
  no_progress.get_control_bit = [](std::uint32_t) { return false; };

  struct Candidate {
    std::int16_t index;
    Chapter chapter;
  };

  std::map<std::string, Candidate> candidates;
  for (std::size_t index = 0; index < state.scenario.missions.size(); ++index) {
    const auto &mission = state.scenario.missions[index];
    if (!mission.present || mission.avail_random < 1 ||
        mission.link_system_filter == -32000 ||
        (mission.flags_primary & 0x400U) != 0 ||
        mission.availability_expr.empty() ||
        (!NovaControlExpression_Evaluate(mission.availability_expr, progress) &&
         !NovaControlExpression_Evaluate(mission.availability_expr,
                                         after_delay)) ||
        NovaControlExpression_Evaluate(mission.availability_expr,
                                       no_progress) ||
        !HasNewProgress(state, mission))
      continue;
    auto chapter = ChapterOf(mission);
    bool active = false;
    for (std::size_t slot = 0; slot < state.active_missions.size(); ++slot) {
      if (!IsVisible(state, slot))
        continue;
      const auto id = state.active_missions[slot].mission_template_id;
      const auto *definition =
          state.scenario.Mission(static_cast<std::int16_t>(id + 0x80));
      if (id == static_cast<std::int16_t>(index) ||
          (definition && !chapter.family.empty() &&
           ChapterOf(*definition).family == chapter.family &&
           HasNewProgress(state, *definition))) {
        active = true;
        break;
      }
    }
    if (active)
      continue;
    const auto key =
        chapter.family.empty() ? std::to_string(index) : chapter.family;
    const auto found = candidates.find(key);
    if (found == candidates.end() ||
        found->second.chapter.number < chapter.number)
      candidates.insert_or_assign(
          key, Candidate{static_cast<std::int16_t>(index), std::move(chapter)});
  }

  std::vector<StoryLead> leads;
  for (const auto &[key, candidate] : candidates) {
    const auto &mission =
        state.scenario.missions[static_cast<std::size_t>(candidate.index)];
    StoryLead lead;
    lead.row = static_cast<std::int16_t>(-candidate.index - 2);
    lead.title = candidate.chapter.family.empty()
                     ? "Story lead"
                     : candidate.chapter.family + ": story lead";
    constexpr std::array<std::string_view, 7> venues{"mission computer",
                                                     "bar",
                                                     "ship contacts",
                                                     "spaceport",
                                                     "trading center",
                                                     "shipyard",
                                                     "outfitter"};
    const std::string venue{
        mission.avail_location >= 0 &&
                mission.avail_location <
                    static_cast<std::int16_t>(venues.size())
            ? venues[static_cast<std::size_t>(mission.avail_location)]
            : "spaceport"};
    const auto *stellar = state.scenario.Stellar(mission.link_system_filter);
    if (mission.avail_location != 2 && stellar && stellar->is_available &&
        stellar->system_id >= 0 && (mission.flags_primary & 2U) == 0) {
      lead.stellar_id = mission.link_system_filter;
      lead.summary = stellar->name + " / " + venue;
      lead.description = "Follow up at the " + venue + " on " + stellar->name;
      const auto *system = state.scenario.System(
          static_cast<std::int16_t>(stellar->system_id + 0x80));
      if (system)
        lead.description += " in " + system->name;
      lead.description += ".";
    } else {
      std::string area = "previously visited ports";
      if (mission.link_system_filter >= 10000 &&
          mission.link_system_filter < 12048) {
        const auto *government =
            state.scenario.Government(static_cast<std::int16_t>(
                mission.link_system_filter - 10000 + 0x80));
        if (government)
          area = government->name + " ports";
      }
      lead.summary = "Check " + area;
      lead.description = "Look for your next contact at the " + venue + " in " +
                         area + ". The exact destination is not known yet.";
    }
    if (lead.stellar_id >= 0 &&
        stellar->system_id == state.player.current_system_id &&
        !NovaTravel_PlayerMeetsStellarAccess(state, lead.stellar_id))
      lead.description +=
          "\n\nDocking clearance is currently blocked. Hail the port and ask "
          "about a bribe, if offered, or improve your local legal standing. A "
          "story lead itself does not grant landing clearance.";
    lead.description += "\n\nThis is a lead from your story progress, not an "
                        "accepted assignment.";
    if (state.control.ControlBit(6666) &&
        !NovaControlExpression_Evaluate(mission.availability_expr, progress))
      lead.description +=
          " Give your contacts some time; travel and check again later.";
    if (mission.avail_random < 100)
      lead.description += " Your contact may not be there on every visit. "
                          "Visit another port or system before returning; "
                          "reopening the same service alone will not help.";
    if (mission.cargo_qty_tons > 0 && (mission.flags_secondary & 1U) != 0)
      lead.description +=
          " Keep " + std::to_string(mission.cargo_qty_tons) +
          (mission.cargo_qty_tons == 1 ? " ton of cargo space free."
                                       : " tons of cargo space free.");
    if (mission.avail_rating > 0)
      lead.description += " This contact also requires a combat rating of " +
                          std::to_string(mission.avail_rating) + ".";
    if (mission.avail_record != 0)
      lead.description += " Your local legal standing affects whether the "
                          "contact will offer work.";
    leads.push_back(std::move(lead));
  }
  return leads;
}

std::optional<StoryLead> FindStoryLead(const GameState &state,
                                       std::int16_t row) {
  if (row >= -1)
    return std::nullopt;
  for (auto &lead : BuildStoryLeads(state))
    if (lead.row == row)
      return std::move(lead);
  return std::nullopt;
}

bool MissionJournalShowsActiveSlot(const GameState &state, std::size_t slot) {
  if (slot >= state.active_missions.size() || !IsVisible(state, slot))
    return false;
  // Stock destinationless reminders can survive later Vell-os chapters. Keep
  // the real assignment intact, but replace its stale journal row with the
  // current story clue. Exact source names protect plugin replacements.
  const auto id = state.active_missions[slot].mission_template_id + 0x80;
  const auto *definition =
      state.scenario.Mission(static_cast<std::int16_t>(id));
  if (definition && state.control.ControlBit(351) &&
      ((id == 819 && definition->resource_name ==
                         "Drop By Merrol Bar;Rebel Reminder link") ||
       (id == 818 && definition->resource_name ==
                         "Drop By Kane Band Bar;Vell-os 3->4 link misn"))) {
    const auto leads = BuildStoryLeads(state);
    if (std::any_of(leads.begin(), leads.end(), [](const auto &lead) {
          return lead.title == "Vell-os: story lead";
        }))
      return false;
  }
  return true;
}

std::vector<std::int16_t> MissionJournalRows(const GameState &state) {
  std::vector<std::int16_t> rows;
  for (const auto &lead : BuildStoryLeads(state))
    rows.push_back(lead.row);
  for (std::size_t slot = 0; slot < state.active_missions.size(); ++slot)
    if (MissionJournalShowsActiveSlot(state, slot))
      rows.push_back(static_cast<std::int16_t>(slot));
  return rows;
}
} // namespace game
