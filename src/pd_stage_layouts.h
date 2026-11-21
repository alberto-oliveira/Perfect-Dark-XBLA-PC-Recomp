// Perfect Dark XBLA PC Recomp - hand-picked layouts for the solo stages as arenas.
//
// pd_stages.h picks each solo stage's spawn points and weapon spawns by itself.
// Where its picks play badly, a stage's entry here overrides them:
//   spawns   the spawn pads (up to 24). Empty = picked automatically.
//   weapons  weapon spawn pads, in location order (up to 16): the first gets
//            location 0 (weapon slot 1), and so on. Locations left over are
//            picked automatically, away from the spawns and these pads.
//
// Pad numbers come from the pad marking keys (pd_solo_mark_keys): in a match on
// the stage, stand where you want a spawn point or a weapon and press the key; the
// log line gives the pad. Every PC must have the same table (the same build), as
// each one builds the stage's setup itself.
#pragma once

#include <cstdint>
#include <vector>

namespace pd::stages {

struct StageLayout {
  uint8_t stagenum;
  std::vector<int> spawns;
  std::vector<int> weapons;
};

// The hand-picked layout of a stage, or nullptr (all picks automatic).
inline const StageLayout* FindStageLayout(uint32_t stagenum) {
  static const std::vector<StageLayout> layouts = {
      // {stage, {spawn pads}, {weapon pads}}, e.g.
      // {0x30, {}, {101, 245, 17}},  // Defection: 3 weapons by hand, 13 automatic

      // Defection: 15 spawn points, 7 weapons by hand (9 automatic).
      {0x30,
       {484, 477, 486, 491, 499, 500, 527, 599, 478, 473, 468, 423, 251, 225, 414},
       {484, 493, 499, 223, 431, 308, 276}},
  };
  for (const StageLayout& layout : layouts) {
    if (layout.stagenum == stagenum) return &layout;
  }
  return nullptr;
}

}  // namespace pd::stages
