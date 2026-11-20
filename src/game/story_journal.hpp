#pragma once

#include "game_state.hpp"

#include <optional>
#include <string>
#include <vector>

namespace game {
// Community journal rows are transient, never active mission slots or save
// data. -1 is no selection; -(definition index + 2) identifies an inferred
// story lead.
struct StoryLead {
  std::int16_t row = -1;
  std::int16_t stellar_id = -1;
  std::string title;
  std::string summary;
  std::string description;
};

[[nodiscard]] std::vector<StoryLead> BuildStoryLeads(const GameState &state);
[[nodiscard]] std::optional<StoryLead> FindStoryLead(const GameState &state,
                                                     std::int16_t row);
[[nodiscard]] bool MissionJournalShowsActiveSlot(const GameState &state,
                                                 std::size_t slot);
[[nodiscard]] std::vector<std::int16_t>
MissionJournalRows(const GameState &state);
} // namespace game
