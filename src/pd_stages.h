// Perfect Dark XBLA PC Recomp - the solo mission stages as Combat Simulator arenas.
//
// Every stage in the game's stage table (g_Stages) names two setup files: the solo
// one (guards, objectives, doors, the mission's scripts) and a multiplayer one
// (spawn points, weapon spawns, doors). The game loads the multiplayer one when a
// Combat Simulator match is running. For the solo stages that file exists but is
// an empty stub (as on the N64): no spawn points, no weapons, no doors.
//
// This mod builds the missing multiplayer setup itself, from data every PC already
// has: the stage's pads file (positions in the level, and the bots' navigation
// waypoints) and its solo setup file. It hands that buffer to the game in place of
// the stub, right after the game loaded it (PdHookSoloStageSetup), so the game
// then reads it exactly as it reads a real arena's setup.
//
// In a network match every PC builds the setup itself. It must come out the same
// on every PC (the game matches props between PCs by the order they were made in),
// so the builder only uses the game's files and fixed rules: no randomness.
//
// Setup file layout (the N64's struct stagesetup, big-endian; the offsets are from
// the start of the file and the game turns them into addresses):
//   +0x00 waypoints, +0x04 waygroups, +0x08 cover   (0 in a setup file: the pads file has them)
//   +0x0C intro      s32 words: commands such as spawn(pad) = {0, pad, 0}, ending in 12
//   +0x10 props      the objects (doors, lifts, weapon spawns...), ending in type 0x34
//   +0x14 paths      guard patrol paths {pads, id/flags/len}, ending in pads = 0
//   +0x18 ailists    {list, id} pairs, ending in list = 0; a list is AI bytecode
//   +0x1C padfiledata (0 in the file)
//
// Header-only, C++17. The game glue (settings, hooks) is in main.cpp.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace pd::stages {

// ---- Game addresses --------------------------------------------------------------

constexpr uint32_t kGVars = 0x82648BF8;
constexpr uint32_t kMatchRunning = kGVars + 1336;  // u32, nonzero while a multiplayer match runs
constexpr uint32_t kMpSetupStage = 0x826418A9;     // g_MpSetup+17, u8: the arena's stage number

// g_Stages: 61 entries of 0x38 bytes, the N64's table. g_StageIndex is the entry of
// the stage being loaded.
constexpr uint32_t kStages = 0x825C2790;
constexpr uint32_t kStageEntrySize = 0x38;
constexpr uint32_t kStageCount = 61;
constexpr uint32_t kStageIndex = 0x825C1150;  // u32
constexpr uint32_t kStageId = 0x00;           // s16 stage number
constexpr uint32_t kStagePadsFile = 0x0C;     // u16 file ids
constexpr uint32_t kStageSetupFile = 0x0E;
constexpr uint32_t kStageMpSetupFile = 0x10;

// g_SoloStages: the 21 solo missions, 12 bytes each: +0 u32 stage number, +10 u16
// the text id of the mission's short name ("dataDyne Defection").
constexpr uint32_t kSoloStages = 0x825D9D68;
constexpr uint32_t kSoloStageSize = 12;
constexpr int kSoloStageCount = 21;
constexpr uint32_t kSoloStageName = 10;

// g_FileInfo: 8 bytes per file id: +0 the loaded size of the file's last load.
constexpr uint32_t kFileInfo = 0x826302F0;

// The two lists of multiplayer arena names (sub_8227E590, sub_82281160) look the
// stage up in a 17-entry table and read its name id from this one; for a stage
// not in the table they read one entry past the end. Entry 16 is "Random".
constexpr uint32_t kArenaNameTexts = 0x825FA150;
constexpr uint32_t kArenaNameCount = 17;

// file_load_to_new's "method" argument as setup_load_files passes it.
constexpr uint32_t kLoadMethodDefault = 34;

// ---- Big-endian reads and writes on a host pointer --------------------------------

inline uint16_t U16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline int16_t S16(const uint8_t* p) { return int16_t(U16(p)); }
inline uint32_t U32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline int32_t S32(const uint8_t* p) { return int32_t(U32(p)); }
inline float F32(const uint8_t* p) {
  const uint32_t bits = U32(p);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}

// ---- Solo stages --------------------------------------------------------------------

// Short names for the pd_solo_force_stage setting (the game's own names have spaces
// and capitals). Stage numbers as in g_SoloStages.
struct StageKey {
  const char* key;
  uint8_t stagenum;
};
inline constexpr StageKey kStageKeys[] = {
    {"defection", 0x30},  {"investigation", 0x33}, {"extraction", 0x22}, {"villa", 0x2C},
    {"chicago", 0x1D},    {"g5", 0x1E},            {"infiltration", 0x2F}, {"rescue", 0x35},
    {"escape", 0x19},     {"airbase", 0x27},       {"airforceone", 0x31}, {"crashsite", 0x1C},
    {"pelagic", 0x21},    {"deepsea", 0x38},       {"defense", 0x2D},    {"attackship", 0x34},
    {"skedarruins", 0x2A}, {"mbr", 0x37},          {"maiansos", 0x09},   {"war", 0x16},
    {"duel", 0x4F},
};

// A stage number from a setting: one of the names above or a number ("48", "0x30").
// 0 if it is neither.
inline uint8_t ParseStage(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }),
             text.end());
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (text.empty()) return 0;
  for (const StageKey& k : kStageKeys) {
    if (text == k.key) return k.stagenum;
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 0);
  return (end && *end == 0 && value > 0 && value < 256) ? uint8_t(value) : 0;
}

// Which g_SoloStages entry a stage number is, or -1 (an arena, or not a stage).
inline int SoloStageIndex(const uint8_t* base, uint32_t stagenum) {
  for (int i = 0; i < kSoloStageCount; ++i) {
    if (U32(base + kSoloStages + uint32_t(i) * kSoloStageSize) == stagenum) return i;
  }
  return -1;
}

// The text id of a solo stage's short name, or 0.
inline uint16_t SoloStageName(const uint8_t* base, uint32_t stagenum) {
  const int i = SoloStageIndex(base, stagenum);
  return i < 0 ? 0 : U16(base + kSoloStages + uint32_t(i) * kSoloStageSize + kSoloStageName);
}

// ---- Pads file ------------------------------------------------------------------------
//
// The stage's points of interest. Header: +0 s32 number of pads, +8 s32 offset of
// the waypoints, then from +0x14 one u16 offset per pad. A pad starts with a u32:
//   bits 31-14 flags (PADFLAG_*), bits 13-4 room (signed, -1 = not filled in yet),
//   bits 3-0 lift number
// then its position: 3 s16 (padded to 8 bytes) with PADFLAG_INTPOS, else 3 f32.
// The waypoints (the bots' navigation graph) are 16 bytes each: +0 s32 pad
// number, ending in a negative one. We read the file as loaded, before the game
// fills in the rooms, so rooms may still be -1.

constexpr uint32_t kPadFlagIntPos = 0x0001;
constexpr uint32_t kPadFlagAiWaitLift = 0x0400;
constexpr uint32_t kPadFlagAiOnLift = 0x0800;
constexpr uint32_t kPadFlagAiDrop = 0x2000;
constexpr uint32_t kPadFlagAiIgnoreY = 0x8000;

struct Pad {
  int num = -1;
  float x = 0, y = 0, z = 0;
  int room = -1;
  int lift = 0;
  uint32_t flags = 0;
};

struct PadsFile {
  const uint8_t* data = nullptr;  // host pointer to the loaded file
  uint32_t size = 0;

  int NumPads() const { return size >= 0x14 ? S32(data) : 0; }

  bool Read(int padnum, Pad* out) const {
    if (padnum < 0 || padnum >= NumPads() || 0x14 + uint32_t(padnum) * 2 + 2 > size) return false;
    const uint32_t offset = U16(data + 0x14 + padnum * 2);
    if (offset + 16 > size) return false;
    const uint8_t* p = data + offset;
    const uint32_t header = U32(p);
    out->num = padnum;
    out->flags = header >> 14;
    out->room = int32_t(header << 18) >> 22;  // sign-extend the 10 room bits
    out->lift = int(header & 0xF);
    if (out->flags & kPadFlagIntPos) {
      out->x = S16(p + 4);
      out->y = S16(p + 6);
      out->z = S16(p + 8);
    } else {
      out->x = F32(p + 4);
      out->y = F32(p + 8);
      out->z = F32(p + 12);
    }
    return true;
  }

  // The pad numbers of the bots' waypoints, in file order.
  std::vector<int> WaypointPads() const {
    std::vector<int> pads;
    if (size < 0x14) return pads;
    uint32_t offset = U32(data + 8);
    while (offset + 16 <= size) {
      const int32_t padnum = S32(data + offset);
      if (padnum < 0) break;
      pads.push_back(padnum);
      offset += 16;
    }
    return pads;
  }
};

// A pad a player can be put on: not on or waiting for a lift, not a ledge or drop
// the bots treat specially.
inline bool UsableForSpawn(const Pad& pad) {
  constexpr uint32_t kAvoid = kPadFlagAiWaitLift | kPadFlagAiOnLift | kPadFlagAiDrop | kPadFlagAiIgnoreY;
  return pad.lift == 0 && (pad.flags & kAvoid) == 0;
}

// Picks up to `count` pads spread over the stage: the first is the one nearest to
// `start`, then each next one is the candidate farthest from every pad picked so
// far ("farthest point sampling"). Same input, same result, on every PC.
inline std::vector<Pad> PickSpread(const std::vector<Pad>& candidates, const Pad& start, size_t count) {
  std::vector<Pad> picked;
  if (candidates.empty() || count == 0) return picked;
  const auto dist2 = [](const Pad& a, const Pad& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
  };
  // nearest[i] = squared distance from candidate i to the closest picked pad.
  std::vector<float> nearest(candidates.size());
  size_t first = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (dist2(candidates[i], start) < dist2(candidates[first], start)) first = i;
  }
  size_t next = first;
  for (size_t i = 0; i < candidates.size(); ++i) nearest[i] = INFINITY;
  while (picked.size() < count) {
    picked.push_back(candidates[next]);
    nearest[next] = -1;  // taken
    size_t best = candidates.size();
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (nearest[i] < 0) continue;
      nearest[i] = std::min(nearest[i], dist2(candidates[i], candidates[next]));
      // Ties go to the lower index, so the order of the candidates decides.
      if (best == candidates.size() || nearest[i] > nearest[best]) best = i;
    }
    if (best == candidates.size()) break;  // every candidate picked
    next = best;
  }
  return picked;
}

// ---- Solo setup file -----------------------------------------------------------------

// The solo intro's first spawn(pad): where Joanna starts the mission. -1 if none.
// Intro commands are s32 words; each command's length in words, as the game's
// intro readers (N64 playerreset.c) step over them.
inline int SoloStartPad(const uint8_t* setup, uint32_t size) {
  if (size < 0x20) return -1;
  uint32_t offset = U32(setup + 0x0C);
  while (offset + 4 <= size) {
    const int32_t cmd = S32(setup + offset);
    if (cmd == 12) break;  // INTROCMD_END
    if (cmd == 0 && offset + 12 <= size && S32(setup + offset + 8) == 0) return S32(setup + offset + 4);
    static constexpr uint32_t kWords[] = {3, 4, 4, 8, 2, 2, 10, 3, 2, 3, 3, 2};
    offset += 4 * ((cmd >= 0 && cmd < 12) ? kWords[cmd] : 1);
  }
  return -1;
}

// ---- Building the multiplayer setup ----------------------------------------------------

constexpr int kMaxSpawns = 24;  // the game's spawn list holds 24 (g_SpawnPoints)

// AI bytecode (N64 commands.h; the XBLA command table matches it, 0x825CA130).
// The background list 0x1000 runs once when the stage starts; this one is the
// one every arena has: it puts the simulants into the level and sorts their
// teams, then idles.
inline const std::vector<uint8_t>& StartSimulantsList() {
  static const std::vector<uint8_t> list = {
      0x01, 0x85,                    // mp_init_simulants
      0x01, 0x45,                    // rebuild_teams
      0x01, 0x46,                    // rebuild_squadrons
      0x00, 0x05, 0xFD, 0x00, 0x00,  // set_ailist(CHR_SELF, GAILIST_IDLE)
      0x00, 0x04,                    // endlist
  };
  return list;
}

// Writes a big-endian setup file in a host buffer, section by section.
class SetupWriter {
 public:
  std::vector<int> spawn_pads;  // intro spawn(pad) commands

  std::vector<uint8_t> Build() const {
    std::vector<uint8_t> out(0x20, 0);  // the header, filled in at the end
    const uint32_t intro = Here(out);
    for (const int pad : spawn_pads) {
      Word(out, 0);  // INTROCMD_SPAWN
      Word(out, uint32_t(pad));
      Word(out, 0);  // 0 = a multiplayer spawn point
    }
    Word(out, 12);  // INTROCMD_END

    const uint32_t props = Here(out);
    Word(out, 0x34);  // OBJTYPE_END

    const uint32_t paths = Here(out);
    Word(out, 0);  // pads = none: no paths
    Word(out, 0);

    const uint32_t ailists = Here(out);
    const uint32_t list_offset = ailists + 2 * 8;  // after one {list, id} and the terminator
    Word(out, list_offset);
    Word(out, 0x1000);
    Word(out, 0);  // terminator
    Word(out, 0);
    const std::vector<uint8_t>& list = StartSimulantsList();
    out.insert(out.end(), list.begin(), list.end());
    while (out.size() % 4) out.push_back(0);

    Put(out, 0x0C, intro);
    Put(out, 0x10, props);
    Put(out, 0x14, paths);
    Put(out, 0x18, ailists);
    return out;
  }

 private:
  static uint32_t Here(const std::vector<uint8_t>& out) { return uint32_t(out.size()); }
  static void Word(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(uint8_t(value >> 24));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
  }
  static void Put(std::vector<uint8_t>& out, uint32_t at, uint32_t value) {
    out[at] = uint8_t(value >> 24);
    out[at + 1] = uint8_t(value >> 16);
    out[at + 2] = uint8_t(value >> 8);
    out[at + 3] = uint8_t(value);
  }
};

// Chooses the spawn pads of a solo stage: up to 24 of the bots' waypoints that a
// player can stand on, spread over the stage, starting from the solo start pad.
// Falls back to every usable pad if the stage has no waypoints, and to the solo
// start pad alone if nothing else is usable (the game crashes with no spawn
// point at all: it picks one with random() % count).
inline std::vector<Pad> ChooseSpawns(const PadsFile& pads, int start_pad) {
  std::vector<Pad> candidates;
  Pad pad;
  for (const int num : pads.WaypointPads()) {
    if (pads.Read(num, &pad) && UsableForSpawn(pad)) candidates.push_back(pad);
  }
  if (candidates.empty()) {
    for (int num = 0; num < pads.NumPads(); ++num) {
      if (pads.Read(num, &pad) && UsableForSpawn(pad)) candidates.push_back(pad);
    }
  }
  Pad start;
  const bool has_start = pads.Read(start_pad, &start);
  if (candidates.empty()) {
    if (has_start) return {start};
    return {};
  }
  if (!has_start) start = candidates.front();
  return PickSpread(candidates, start, kMaxSpawns);
}

// ---- TEMP_STAGES: debug descriptions ----------------------------------------------------

// N64 object type names, for the solo setup's object counts in the log.
inline const char* ObjectTypeName(uint8_t type) {
  switch (type) {
    case 0x01: return "door";
    case 0x02: return "doorscale";
    case 0x03: return "basic";
    case 0x04: return "key";
    case 0x05: return "alarm";
    case 0x06: return "cctv";
    case 0x07: return "ammocrate";
    case 0x08: return "weapon";
    case 0x09: return "chr";
    case 0x0A: return "singlemonitor";
    case 0x0B: return "multimonitor";
    case 0x0C: return "hangingmonitors";
    case 0x0D: return "autogun";
    case 0x0E: return "linkguns";
    case 0x0F: return "debris";
    case 0x11: return "hat";
    case 0x12: return "grenadeprob";
    case 0x13: return "linkliftdoor";
    case 0x14: return "multiammocrate";
    case 0x15: return "shield";
    case 0x16: return "tag";
    case 0x17: return "beginobjective";
    case 0x18: return "endobjective";
    case 0x23: return "briefing";
    case 0x24: return "gasbottle";
    case 0x25: return "renameobj";
    case 0x26: return "padlockeddoor";
    case 0x27: return "truck";
    case 0x28: return "heli";
    case 0x2A: return "glass";
    case 0x2B: return "safe";
    case 0x2C: return "safeitem";
    case 0x2D: return "tank";
    case 0x2E: return "camerapreset";
    case 0x2F: return "tintedglass";
    case 0x30: return "lift";
    case 0x31: return "conditionalscenery";
    case 0x32: return "blockedpath";
    case 0x33: return "hoverbike";
    case 0x35: return "hoverprop";
    case 0x36: return "fan";
    case 0x37: return "hovercar";
    case 0x38: return "padeffect";
    case 0x39: return "chopper";
    case 0x3A: return "mine";
    case 0x3B: return "escastep";
    default: return nullptr;  // objective conditions (0x19-0x22) and others: shown by number
  }
}

// "door 52, chr 80, ..." from a count per object type.
inline std::string DescribeTypeCounts(const std::map<uint8_t, int>& counts) {
  std::string text;
  for (const auto& [type, count] : counts) {
    if (!text.empty()) text += ", ";
    const char* name = ObjectTypeName(type);
    text += name ? fmt::format("{} {}", name, count) : fmt::format("type 0x{:02X} {}", type, count);
  }
  return text;
}

}  // namespace pd::stages
