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
//
// With `avoid` (pads already in use, e.g. the spawn points) the picks keep away
// from those too, and the first pick is the candidate farthest from them
// (`start` is then not used).
inline float Distance2(const Pad& a, const Pad& b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

inline std::vector<Pad> PickSpread(const std::vector<Pad>& candidates, const Pad& start, size_t count,
                                   const std::vector<Pad>& avoid = {}) {
  std::vector<Pad> picked;
  if (candidates.empty() || count == 0) return picked;
  const auto dist2 = Distance2;
  // nearest[i] = squared distance from candidate i to the closest picked (or avoided) pad.
  std::vector<float> nearest(candidates.size(), INFINITY);
  size_t first = 0;
  if (avoid.empty()) {
    for (size_t i = 0; i < candidates.size(); ++i) {
      if (dist2(candidates[i], start) < dist2(candidates[first], start)) first = i;
    }
  } else {
    for (size_t i = 0; i < candidates.size(); ++i) {
      for (const Pad& a : avoid) nearest[i] = std::min(nearest[i], dist2(candidates[i], a));
      if (nearest[i] > nearest[first]) first = i;
    }
  }
  size_t next = first;
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

// ---- The solo AI lists: which lifts the mission switches on --------------------------
//
// A lift moves by itself, but the bots only know how to ride it once an AI list
// has run activate_lift(lift number, tag) for it (it fills the game's lift table,
// g_Lifts, which the bots' route finding reads). The solo setup's own AI lists
// hold those commands, so we copy them from there.

constexpr uint32_t kAiCommandLengths = 0x825CA130;  // u16 per opcode: the command's length in bytes
constexpr uint32_t kAiCommandCount = 0x1E0;         // opcodes in the N64 table
constexpr uint16_t kAiEndList = 0x0004;
constexpr uint16_t kAiPrint = 0x00B5;               // the one command with a variable length (a string)
constexpr uint16_t kAiActivateLift = 0x018D;        // activate_lift(lift number, tag): 4 bytes

struct LiftActivation {
  uint8_t lift;  // lift number (1-9, the game's g_Lifts index)
  uint8_t tag;   // tag id of the lift object
};

// Walks every AI list of a setup file (`lengths` = the game's command length
// table, a host pointer) and collects its activate_lift commands, without repeats.
inline std::vector<LiftActivation> SoloLiftActivations(const uint8_t* setup, uint32_t size,
                                                       const uint8_t* lengths) {
  std::vector<LiftActivation> found;
  if (size < 0x20) return found;
  // The ailists table: {u32 list offset, u32 id} pairs, ending in offset 0.
  for (uint32_t entry = U32(setup + 0x18); entry + 8 <= size; entry += 8) {
    const uint32_t list = U32(setup + entry);
    if (list == 0) break;
    uint32_t at = list;
    for (int guard = 0; guard < 100000 && at + 2 <= size; ++guard) {
      const uint16_t op = U16(setup + at);
      if (op == kAiEndList) break;
      if (op >= kAiCommandCount) break;  // not a command we know: stop reading this list
      uint32_t length = U16(lengths + op * 2);
      if (op == kAiPrint) {
        length = 2;
        while (at + length < size && setup[at + length] != 0) ++length;
        ++length;
      }
      if (op == kAiActivateLift && at + 4 <= size) {
        const LiftActivation lift{setup[at + 2], setup[at + 3]};
        const bool seen = std::any_of(found.begin(), found.end(), [&](const LiftActivation& f) {
          return f.lift == lift.lift && f.tag == lift.tag;
        });
        if (!seen) found.push_back(lift);
      }
      if (length == 0) break;
      at += length;
    }
  }
  return found;
}

// ---- The solo objects: what the arena keeps -----------------------------------------
//
// The solo setup's props are a list of commands, each an object or a link
// between objects. Some fields point at another command by RELATIVE INDEX (the
// target's place in the list minus this command's place): a door's sibling, a
// lift's doors, a lift door link, a tag, an object inside another object. When
// we leave commands out, every such index has to be recounted.
//
// Kept: the level's furniture - doors, lifts, glass, crates and other scenery.
// Left out: the mission - guards and what they carry, objectives, keys, weapons
// and ammo (the arena gets its own), cameras and guns that shoot, vehicles the
// AI drives, and every link that needs them.

// Object fields (struct defaultobj and the N64's per-type structs).
constexpr uint32_t kObjPad = 0x06;     // s16 pad number; with kFlagInside: relative index of the owner
constexpr uint32_t kObjFlags = 0x08;   // u32
constexpr uint32_t kObjFlags2 = 0x0C;  // u32
constexpr uint32_t kDoorKeyFlags = 0x74;  // u32: which keys open it (0 = none needed)
constexpr uint32_t kDoorSibling = 0xBC;   // s32 relative index of the door that opens with it, or 0
constexpr uint32_t kLiftDoors = 0x64;     // 4 x s32 relative index of the lift's doors, or 0
constexpr uint32_t kLinkLiftDoorDoor = 0x04;  // lift_door command: s32 relative index of the door
constexpr uint32_t kLinkLiftDoorLift = 0x08;  //   ... and of the lift
constexpr uint32_t kTagId = 0x04;      // tag command: u16 tag id
constexpr uint32_t kTagOffset = 0x06;  //   ... and s16 relative index of the tagged object

constexpr uint32_t kFlagAssignedToChr = 0x00004000;  // carried by a guard (pad = the guard)
constexpr uint32_t kFlagInside = 0x00008000;         // inside another object (pad = its owner)
constexpr uint32_t kFlagCannotActivate = 0x02000000; // pressing B on it does nothing
constexpr uint32_t kFlagLiftDeactivated = 0x10000000;  // a lift that stands still (on doors: has a portal)
constexpr uint32_t kFlag2ExcludeA = 0x00000010;      // not on Agent difficulty
constexpr uint32_t kFlag2Difficulties = 0x000000F0;  // not on Agent / Special Agent / Perfect Agent / Perfect Dark
constexpr uint32_t kFlag2PlayerCounts = 0x01C00000;  // not with 2 / 3 / 4 players
constexpr uint32_t kFlag2OneWayLocks = 0x18000000;   // locked from the front / from the back

constexpr uint8_t kTypeDoor = 0x01;
constexpr uint8_t kTypeSingleMonitor = 0x0A;
constexpr uint8_t kTypeLinkLiftDoor = 0x13;
constexpr uint8_t kTypeTag = 0x16;
constexpr uint8_t kTypeLift = 0x30;
constexpr uint8_t kTypeEnd = 0x34;

// The object types an arena keeps (a tag or lift door link is kept when what it
// links is kept: see CopySoloProps).
inline bool KeptType(uint8_t type) {
  switch (type) {
    case 0x01:  // door
    case 0x02:  // door scale
    case 0x03:  // basic (furniture, boxes, plants...)
    case 0x0A:  // single monitor
    case 0x0B:  // multi monitor
    case 0x0C:  // hanging monitors
    case 0x0F:  // debris
    case 0x24:  // gas bottle
    case 0x29:  // (a basic object kind)
    case 0x2A:  // glass
    case 0x2B:  // safe
    case 0x2F:  // tinted glass
    case 0x30:  // lift
    case 0x33:  // hoverbike
    case 0x35:  // hover prop (hovering crates and beds)
    case 0x36:  // fan
    case 0x3B:  // escalator step
      return true;
    default:
      return false;
  }
}

// One command of the solo setup's props, found by the caller with the game's own
// command length function (setup_get_cmd_length).
struct SoloCommand {
  uint32_t offset = 0;  // bytes from the start of the setup file
  uint32_t words = 0;   // its length in 4-byte words
};

struct CopiedProps {
  std::vector<uint8_t> bytes;  // the kept commands, links recounted (no end marker)
  std::vector<LiftActivation> lifts;  // the activate_lift commands whose lift was kept
  std::map<uint8_t, int> kept, dropped;  // per object type, for the log
  int unlocked = 0;       // doors that needed a key or were locked one way
  int still_closed = 0;   // doors the player cannot open (left as they are)
  int unlinked = 0;       // links cleared because their target was left out
};

inline CopiedProps CopySoloProps(const uint8_t* setup, uint32_t size, const std::vector<SoloCommand>& cmds,
                                 const std::vector<LiftActivation>& solo_lifts) {
  CopiedProps out;
  const size_t n = cmds.size();
  const auto type_of = [&](size_t i) { return setup[cmds[i].offset + 3]; };
  const auto at = [&](size_t i, uint32_t field) { return setup + cmds[i].offset + field; };
  // The command a relative index points at, or n if it is outside the list.
  const auto target = [&](size_t i, int32_t relative) {
    const int64_t t = int64_t(i) + relative;
    return (t >= 0 && t < int64_t(n)) ? size_t(t) : n;
  };

  // 1. Each command on its own: its type, the Agent difficulty, not on a guard.
  std::vector<bool> keep(n, false);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t type = type_of(i);
    if (!KeptType(type) || cmds[i].words < 5) continue;
    const uint32_t flags = U32(at(i, kObjFlags));
    const uint32_t flags2 = U32(at(i, kObjFlags2));
    if (flags2 & kFlag2ExcludeA) continue;  // another difficulty's copy of the object
    if (flags & kFlagAssignedToChr) continue;
    if (type == kTypeSingleMonitor && S16(at(i, kObjPad)) < 0 && !(flags & kFlagInside)) continue;  // hangs off something
    keep[i] = true;
  }
  // 2. An object inside another one goes only with its owner (repeated, as an
  // owner can itself be inside something).
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < n; ++i) {
      if (!keep[i] || !(U32(at(i, kObjFlags)) & kFlagInside)) continue;
      const size_t owner = target(i, S16(at(i, kObjPad)));
      if (owner == n || !keep[owner]) {
        keep[i] = false;
        changed = true;
      }
    }
  }
  // 3. Links: a lift door link needs its door and lift; a tag is kept only for a
  // kept lift (the lift AI list finds the lift by its tag).
  for (size_t i = 0; i < n; ++i) {
    const uint8_t type = type_of(i);
    if (type == kTypeLinkLiftDoor && cmds[i].words >= 3) {
      const size_t door = target(i, S32(at(i, kLinkLiftDoorDoor)));
      const size_t lift = target(i, S32(at(i, kLinkLiftDoorLift)));
      keep[i] = door != n && lift != n && keep[door] && keep[lift];
    } else if (type == kTypeTag && cmds[i].words >= 2) {
      const size_t tagged = target(i, S16(at(i, kTagOffset)));
      keep[i] = tagged != n && keep[tagged] && type_of(tagged) == kTypeLift;
    }
  }

  // The kept commands' new places in the list.
  std::vector<int32_t> new_index(n, -1);
  int32_t count = 0;
  for (size_t i = 0; i < n; ++i) {
    if (keep[i]) new_index[i] = count++;
  }
  // A relative index recounted for the new list; 0 (no link) if its target was left out.
  const auto relink = [&](size_t i, int32_t relative) -> int32_t {
    if (relative == 0) return 0;
    const size_t t = target(i, relative);
    if (t == n || !keep[t]) {
      ++out.unlinked;
      return 0;
    }
    return new_index[t] - new_index[i];
  };
  const auto put32 = [](uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
  };
  const auto put16 = [](uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); };

  // 4. Copy the kept commands and fix them up.
  std::vector<uint8_t> kept_tags;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t type = type_of(i);
    if (!keep[i]) {
      ++out.dropped[type];
      continue;
    }
    ++out.kept[type];
    const size_t start = out.bytes.size();
    out.bytes.insert(out.bytes.end(), at(i, 0), at(i, 0) + cmds[i].words * 4);
    uint8_t* cmd = out.bytes.data() + start;

    if (type == kTypeLinkLiftDoor) {
      put32(cmd + kLinkLiftDoorDoor, uint32_t(relink(i, S32(cmd + kLinkLiftDoorDoor))));
      put32(cmd + kLinkLiftDoorLift, uint32_t(relink(i, S32(cmd + kLinkLiftDoorLift))));
      continue;
    }
    if (type == kTypeTag) {
      put16(cmd + kTagOffset, uint16_t(relink(i, S16(cmd + kTagOffset))));
      kept_tags.push_back(uint8_t(U16(cmd + kTagId)));
      continue;
    }

    // An object. Every difficulty and player count gets it (only Agent's copies
    // were kept), so every PC makes the same objects.
    uint32_t flags = U32(cmd + kObjFlags);
    uint32_t flags2 = U32(cmd + kObjFlags2) & ~(kFlag2Difficulties | kFlag2PlayerCounts);
    if (flags & kFlagInside) put16(cmd + kObjPad, uint16_t(relink(i, S16(cmd + kObjPad))));

    if (type == kTypeDoor && cmds[i].words * 4 >= kDoorSibling + 4) {
      // Unlocked: no key needed, no one-way lock. Doors the player can never
      // open (only the mission's scripts did) stay closed.
      if (U32(cmd + kDoorKeyFlags) != 0 || (flags2 & kFlag2OneWayLocks)) ++out.unlocked;
      put32(cmd + kDoorKeyFlags, 0);
      flags2 &= ~kFlag2OneWayLocks;
      if (flags & kFlagCannotActivate) ++out.still_closed;
      put32(cmd + kDoorSibling, uint32_t(relink(i, S32(cmd + kDoorSibling))));
    } else if (type == kTypeLift && cmds[i].words * 4 >= kLiftDoors + 16) {
      flags &= ~kFlagLiftDeactivated;  // every lift runs from the start
      for (uint32_t d = 0; d < 4; ++d) {
        put32(cmd + kLiftDoors + d * 4, uint32_t(relink(i, S32(cmd + kLiftDoors + d * 4))));
      }
    }
    put32(cmd + kObjFlags, flags);
    put32(cmd + kObjFlags2, flags2);
  }

  for (const LiftActivation& lift : solo_lifts) {
    if (std::find(kept_tags.begin(), kept_tags.end(), lift.tag) != kept_tags.end()) out.lifts.push_back(lift);
  }
  return out;
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

// The background list 0x1001: switches on the stage's lifts for the bots, as the
// arenas' own setups do (activate_lift for each), then idles.
inline std::vector<uint8_t> StartLiftsList(const std::vector<LiftActivation>& lifts) {
  std::vector<uint8_t> list;
  for (const LiftActivation& lift : lifts) {
    list.insert(list.end(), {0x01, 0x8D, lift.lift, lift.tag});  // activate_lift(lift, tag)
  }
  list.insert(list.end(), {0x00, 0x05, 0xFD, 0x00, 0x00});  // set_ailist(CHR_SELF, GAILIST_IDLE)
  list.insert(list.end(), {0x00, 0x04});                    // endlist
  return list;
}

// Writes a big-endian setup file in a host buffer, section by section.
class SetupWriter {
 public:
  std::vector<int> spawn_pads;       // intro spawn(pad) commands
  std::vector<uint8_t> props;        // object commands, without the end marker
  std::vector<LiftActivation> lifts; // for the lift AI list

  std::vector<uint8_t> Build() const {
    std::vector<uint8_t> out(0x20, 0);  // the header, filled in at the end
    const uint32_t intro = Here(out);
    for (const int pad : spawn_pads) {
      Word(out, 0);  // INTROCMD_SPAWN
      Word(out, uint32_t(pad));
      Word(out, 0);  // 0 = a multiplayer spawn point
    }
    Word(out, 12);  // INTROCMD_END

    const uint32_t props_offset = Here(out);
    out.insert(out.end(), props.begin(), props.end());
    Word(out, 0x34);  // OBJTYPE_END

    const uint32_t paths = Here(out);
    Word(out, 0);  // pads = none: no paths
    Word(out, 0);

    // The AI lists: the table of {offset, id} first, then the lists' bytes.
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> lists = {{0x1000, StartSimulantsList()}};
    if (!lifts.empty()) lists.push_back({0x1001, StartLiftsList(lifts)});
    const uint32_t ailists = Here(out);
    uint32_t list_offset = ailists + uint32_t(lists.size() + 1) * 8;  // after the table and its terminator
    for (const auto& [id, bytes] : lists) {
      Word(out, list_offset);
      Word(out, id);
      list_offset += uint32_t((bytes.size() + 3) / 4 * 4);
    }
    Word(out, 0);  // terminator
    Word(out, 0);
    for (const auto& [id, bytes] : lists) {
      out.insert(out.end(), bytes.begin(), bytes.end());
      while (out.size() % 4) out.push_back(0);
    }

    Put(out, 0x0C, intro);
    Put(out, 0x10, props_offset);
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

// ---- Weapon spawns ---------------------------------------------------------------------
//
// An arena's weapon spawn is a weapon object whose weapon number is a spawn
// LOCATION, 240 + 0..15: when the stage is made, setupPlaceWeapon asks which
// weapon that location gets (mpGetMpWeaponByLocation, which the weapons mod
// answers over its 16 slots) and puts that weapon there. Ammo crates placed
// right after a weapon spawn are filled with that weapon's ammo.

constexpr int kWeaponLocations = 16;            // locations 240-255
constexpr float kCrateMaxDistance = 400.0f;     // a crate's pad: at most this far from its weapon
constexpr float kCrateMaxHeight = 120.0f;       // ...and about the same floor height

struct WeaponSpawn {
  Pad pad;
  int crate_pad = -1;  // -1 = no crate (no pad close enough)
};

// The weapon spawns: 16 waypoints spread over the stage and kept away from the
// spawn points (so nobody is born standing on a gun), each with an ammo crate on
// the nearest free waypoint, if one is close and on the same floor.
inline std::vector<WeaponSpawn> ChooseWeaponSpawns(const PadsFile& pads, const std::vector<Pad>& spawns) {
  std::vector<Pad> candidates;
  Pad pad;
  for (const int num : pads.WaypointPads()) {
    if (!pads.Read(num, &pad) || !UsableForSpawn(pad)) continue;
    const bool is_spawn = std::any_of(spawns.begin(), spawns.end(), [&](const Pad& s) { return s.num == num; });
    if (!is_spawn) candidates.push_back(pad);
  }
  std::vector<WeaponSpawn> weapons;
  for (const Pad& p : PickSpread(candidates, Pad{}, kWeaponLocations, spawns)) weapons.push_back({p, -1});

  // Crates: nearest candidate not already used by a weapon or another crate.
  std::vector<int> used;
  for (const WeaponSpawn& w : weapons) used.push_back(w.pad.num);
  for (WeaponSpawn& w : weapons) {
    float best = kCrateMaxDistance * kCrateMaxDistance;
    for (const Pad& c : candidates) {
      if (std::find(used.begin(), used.end(), c.num) != used.end()) continue;
      if (std::fabs(c.y - w.pad.y) > kCrateMaxHeight) continue;
      const float d = Distance2(c, w.pad);
      if (d < best) {
        best = d;
        w.crate_pad = c.num;
      }
    }
    if (w.crate_pad >= 0) used.push_back(w.crate_pad);
  }
  return weapons;
}

// The object commands (N64 props.h macros weapon() and ammocratemulti()).
// A generic object: {scale << 16 | type, model << 16 | pad, flags, flags2,
// flags3, 14 zero words, maxdamage, 0, 0, 0x0fff0000}, then the type's own words.
constexpr uint32_t kObjFlagFall = 0x00000001;  // drops to the floor below its pad
constexpr uint16_t kModelMultiAmmoCrate = 0x00C1;

inline void AppendGenericObject(std::vector<uint8_t>& out, uint8_t type, uint16_t model, int pad,
                                uint32_t maxdamage) {
  const auto word = [&](uint32_t v) {
    out.insert(out.end(), {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)});
  };
  word(0x0100u << 16 | type);  // scale 0x100 = 1.0
  word(uint32_t(model) << 16 | (uint32_t(pad) & 0xFFFF));
  word(kObjFlagFall);
  word(0);  // flags2
  word(0);  // flags3
  for (int i = 0; i < 14; ++i) word(0);
  word(maxdamage);
  word(0);
  word(0);
  word(0x0FFF0000);
}

// weapon(0x0100, 0, pad, OBJFLAG_FALL, 0, 0, 240 + location): 26 words.
inline void AppendWeaponSpawn(std::vector<uint8_t>& out, int pad, int location) {
  AppendGenericObject(out, 0x08, 0, pad, 1000);
  const uint32_t weapon = uint32_t(240 + location);
  out.insert(out.end(), {uint8_t(weapon), 0, 0, 0});  // weapon number in the top byte
  out.insert(out.end(), {0x00, 0xFF, 0xFF, 0xFF});
  out.insert(out.end(), {0, 0, 0, 0});
}

// ammocratemulti(0x0100, MODEL_MULTI_AMMO_CRATE, pad, OBJFLAG_FALL, 0, 0, 1000):
// 42 words; its 19 ammo slots {u16 model, u16 quantity} start empty (0xFFFF, 0)
// and the game fills them from the weapon before it.
inline void AppendAmmoCrate(std::vector<uint8_t>& out, int pad) {
  AppendGenericObject(out, 0x14, kModelMultiAmmoCrate, pad, 1000);
  for (int i = 0; i < 19; ++i) out.insert(out.end(), {0xFF, 0xFF, 0x00, 0x00});
}

// All the weapon spawns and their crates as object commands, in location order.
inline std::vector<uint8_t> WeaponSpawnCommands(const std::vector<WeaponSpawn>& weapons) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < weapons.size() && i < size_t(kWeaponLocations); ++i) {
    AppendWeaponSpawn(out, weapons[i].pad.num, int(i));
    if (weapons[i].crate_pad >= 0) AppendAmmoCrate(out, weapons[i].crate_pad);
  }
  return out;
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
