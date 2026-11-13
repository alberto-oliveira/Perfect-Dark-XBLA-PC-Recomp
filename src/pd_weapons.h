// Perfect Dark XBLA PC Recomp - more multiplayer weapons and shuffled weapon respawns.
//
// The game lets a match use 6 weapons (g_MpSetup.weapons[6]), but a stage has up
// to 16 weapon spawn locations. Every spawn location asks one function,
// mpGetMpWeaponByLocation (sub_821CC458), which weapon it gets: that function
// walks the 6 slots round-robin, so location 6 gets slot 1's weapon again, and so
// on. This mod adds up to 10 more slots (7-16) and answers that function itself
// over all of them (PdHookWeaponByLocation).
//
// With the shuffle on, a weapon pickup that respawns comes back as a different
// weapon: another one of the weapons the stage started with (PdHookPropEnable).
//
// In a network match every PC builds the stage's props itself, and the game
// matches pickups between PCs by the order they were made in. So every PC must
// use the same slots and make the same respawn choices: the host decides the
// slots and a seed and sends them to the joined PCs (netplay::SetHostBlob, key
// kBlobKey) as the stage loads. Each respawn's weapon is worked out from the
// seed, the pickup and how many times it has respawned, the same on every PC.
//
// Header-only, C++17. The game glue (settings, hooks, the host/joined exchange)
// is in main.cpp.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/logging/api.h>
#include <rex/logging/macros.h>

namespace pd::weapons {

// ---- Game addresses --------------------------------------------------------------

constexpr uint32_t kGameSlots = 0x826418B0;      // g_MpSetup+24: u8 weapons[6], each an index into kWeaponTable
constexpr uint32_t kWeaponTable = 0x825DC8E8;    // g_MpWeapons: 47 entries of 10 bytes, byte 0 = weapon number
constexpr uint32_t kWeaponEntrySize = 10;
constexpr int kWeaponTableCount = 47;
constexpr uint32_t kSetupList = 0x82643838;      // pointer to the stage's setup objects (0x82643828+16)
constexpr uint32_t kStageNumber = 0x826492E0;    // g_Vars+1768, u32
// setupPlaceWeapon uses the multiplayer spawn locations when either of these
// g_Vars words is set (+1336: a multiplayer match is running).
constexpr uint32_t kMatchRunning = 0x82648BF8 + 1336;
constexpr uint32_t kMatchRunning2 = 0x82648BF8 + 1344;

constexpr int kGameSlotCount = 6;   // slots 1-6 live in the game's own g_MpSetup
constexpr int kExtraSlotCount = 10; // slots 7-16 live here
constexpr int kMaxSlots = kGameSlotCount + kExtraSlotCount;

// Weapon numbers (byte 0 of a table entry) with a special meaning for spawns.
constexpr uint8_t kWeaponNothing = 0;    // the location gets no pickup at all
constexpr uint8_t kWeaponShield = 91;    // the pickup becomes a shield
constexpr uint8_t kWeaponDisabled = 92;  // the slot is skipped: the next slot takes its turn

// Setup objects (the stage file's props, 4-byte words). Byte +3 is the type;
// a weapon pickup's byte +92 is its spawn location, 240 + 0..15, which
// setupPlaceWeapon (sub_821FE370) replaces with the weapon's number.
constexpr uint8_t kSetupTypeWeapon = 8;
constexpr uint8_t kSetupTypeEnd = 52;
constexpr uint32_t kSetupLocation = 92;
constexpr uint8_t kFirstLocation = 240;

// A setup object is also the live object once the stage is made: +0 u16 scale
// (256 = 1.0), +4 s16 model number, +24 the model, +92 the weapon number.
constexpr uint32_t kObjScale = 0;
constexpr uint32_t kObjModelNum = 4;
constexpr uint32_t kObjModel = 24;
constexpr uint32_t kObjWeaponNum = kSetupLocation;
constexpr uint32_t kPropObj = 4;  // a prop's object
// A model instance: +4 its object, +20 its scale (f32). The model table
// g_ModelStates has 8 bytes a model: +0 its loaded file (0 = not loaded), +6
// u16 its scale in 4096ths (objInit, sub_82203B50).
constexpr uint32_t kModelObj = 4;
constexpr uint32_t kModelScale = 20;
constexpr uint32_t kModelStates = 0x825F42E8;
constexpr float kModelStateScaleUnit = 1.0f / 4096.0f;
constexpr uint32_t kLevelFrames = 0x82648BF8 + 0x0C;  // g_Vars lvframenum: 0 while the stage is made

// An ammo crate (setup type 20, the N64's multi ammo crate) holds 19 ammo slots
// from +92, 4 bytes each: u16 ammo model, u16 quantity. Slot t-1 holds ammo
// type t. In a match, setupCreateProps fills a crate with the ammo of the
// weapon placed just before it in the stage: a weapon table entry's bytes 1-4
// are its two ammo types (signed, 1-19) and their quantities.
constexpr uint8_t kSetupTypeAmmoCrate = 20;
constexpr uint32_t kCrateSlots = 92;
constexpr int kCrateSlotCount = 19;
constexpr uint16_t kNoModel = 0xFFFF;

// The weapon table's indices (what pd_mp_weapons lists). The order is the N64
// game's g_MpWeapons; weapon numbers checked against the game image.
//   0 Nothing          12 RC-P120          24 Slayer           36 Combat Boost
//   1 Falcon 2         13 Laptop Gun       25 Combat Knife     37 Shield
//   2 Falcon 2 (sil.)  14 Dragon           26 Crossbow         38 PP9i
//   3 Falcon 2 (scope) 15 K7 Avenger       27 Tranquilizer     39 CC13
//   4 MagSec 4         16 AR34             28 Grenade          40 KL01313
//   5 Mauler           17 SuperDragon      29 N-Bomb           41 KF7 Special
//   6 Phoenix          18 Shotgun          30 Timed Mine       42 ZZT (9mm)
//   7 DY357 Magnum     19 Reaper           31 Proximity Mine   43 DMC
//   8 DY357-LX         20 Sniper Rifle     32 Remote Mine      44 AR53
//   9 CMP150           21 Farsight XR-20   33 Laser            45 RC-P45
//  10 Cyclone          22 Devastator       34 X-Ray Scanner    46 Disabled
//  11 Callisto NTG     23 Rocket Launcher  35 Cloaking Device
constexpr uint8_t kIndexNothing = 0;
constexpr uint8_t kIndexShield = 37;
constexpr uint8_t kIndexDisabled = 46;

// ---- The match's weapon configuration ------------------------------------------

struct MatchConfig {
  std::vector<uint8_t> extra;  // table indices for slots 7.. (at most kExtraSlotCount)
  bool shuffle = false;        // respawning pickups come back as a different weapon
  uint32_t seed = 0;           // the shuffle's seed
};

// The configuration of the stage being played, set as its props are made
// (PdSetupCreateProps) and read by PdHookWeaponByLocation.
inline MatchConfig g_match;

// ---- Guest memory (big-endian) -------------------------------------------------

inline uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  return (uint32_t(base[address]) << 24) | (uint32_t(base[address + 1]) << 16) |
         (uint32_t(base[address + 2]) << 8) | uint32_t(base[address + 3]);
}

// The weapon number of table entry `index`.
inline uint8_t WeaponNumber(const uint8_t* base, uint8_t index) {
  if (index >= kWeaponTableCount) return kWeaponDisabled;
  return base[kWeaponTable + index * kWeaponEntrySize];
}

// All slots in play: the game's 6, then this mod's extra ones.
inline std::vector<uint8_t> AllSlots(const uint8_t* base, const MatchConfig& config) {
  std::vector<uint8_t> slots(base + kGameSlots, base + kGameSlots + kGameSlotCount);
  slots.insert(slots.end(), config.extra.begin(), config.extra.end());
  return slots;
}

// mpGetMpWeaponByLocation over any number of slots: the table index of the
// weapon spawn location `location` gets. The same rules as the game's own:
// location L gets the (L+1)-th slot that is not Disabled, going round the slots
// as often as needed; Nothing if every slot is Disabled.
inline uint8_t IndexForLocation(const uint8_t* base, int location, const std::vector<uint8_t>& slots) {
  int remaining = location + 1;
  if (remaining <= 0 || slots.empty()) return kIndexNothing;
  int remaining_at_round_start = remaining;
  size_t i = 0;
  for (;;) {
    if (WeaponNumber(base, slots[i]) != kWeaponDisabled && --remaining == 0) return slots[i];
    if (++i == slots.size()) {
      i = 0;
      if (remaining == remaining_at_round_start) return kIndexNothing;  // every slot is Disabled
      remaining_at_round_start = remaining;
    }
  }
}

// ---- Settings -------------------------------------------------------------------

// pd_mp_weapons = "2, 7, 18" -> the table indices for slots 7, 8, 9. With `warn`
// off nothing is logged (the menu reads the setting every frame).
inline std::vector<uint8_t> ParseExtraSlots(const std::string& list, bool warn = true) {
  std::vector<uint8_t> out;
  std::stringstream ss(list);
  std::string entry;
  while (std::getline(ss, entry, ',')) {
    entry.erase(0, entry.find_first_not_of(" \t"));
    entry.erase(entry.find_last_not_of(" \t") + 1);
    if (entry.empty()) continue;
    const bool digits = std::all_of(entry.begin(), entry.end(), [](char c) { return std::isdigit(uint8_t(c)); });
    const int index = digits ? std::atoi(entry.c_str()) : -1;
    if (index < 0 || index >= kWeaponTableCount) {
      if (warn) REXLOG_WARN("[weapons] pd_mp_weapons: '{}' is not a weapon index (0-46), ignored", entry);
      continue;
    }
    if (out.size() == size_t(kExtraSlotCount)) {
      if (warn) REXLOG_WARN("[weapons] pd_mp_weapons: only {} extra slots, '{}' and later ignored", kExtraSlotCount, entry);
      break;
    }
    if (index == kIndexShield && warn) {
      // The game looks for a Shield in slots 1-6 only (mpHasShield, sub_820CB268).
      REXLOG_WARN("[weapons] pd_mp_weapons: a Shield works best in slots 1-6");
    }
    out.push_back(uint8_t(index));
  }
  return out;
}

// pd_mp_weapons from a list of slots: the indices separated by commas, with the
// Disabled slots at the end left off (they change nothing).
inline std::string FormatExtraSlots(std::vector<uint8_t> slots) {
  while (!slots.empty() && slots.back() == kIndexDisabled) slots.pop_back();
  std::string list;
  for (const uint8_t index : slots) {
    if (!list.empty()) list += ", ";
    list += std::to_string(index);
  }
  return list;
}

// ---- The slot dropdowns ---------------------------------------------------------
//
// A slot's dropdown lists only the weapons that are unlocked, so option `v` is
// the v-th unlocked table entry. These are the game's own conversions
// (sub_821CC370 and sub_821CC3E0); `unlocked(i)` says whether table entry i is.

template <typename Unlocked>
inline uint8_t IndexFromOption(int option, Unlocked unlocked) {
  int index = option;
  for (int i = 0; i <= index && i < kWeaponTableCount; ++i) {
    if (!unlocked(i)) ++index;  // a locked entry is not in the list: skip past it
  }
  return uint8_t(std::min(index, kWeaponTableCount - 1));
}

template <typename Unlocked>
inline int OptionFromIndex(uint8_t index, Unlocked unlocked) {
  int option = 0;
  for (int i = 0; i < index && i < kWeaponTableCount; ++i) {
    if (unlocked(i)) ++option;
  }
  return option;
}

// ---- The host's message to the joined PCs ---------------------------------------
//
// netplay::SetHostBlob(kBlobKey, ...): version, flags (bit 0 = shuffle), number
// of extra slots, a spare byte, the seed (big-endian), then the extra slots.

constexpr uint32_t kBlobKey = 0x57454150;  // "WEAP"
constexpr uint8_t kBlobVersion = 1;
constexpr size_t kBlobHeader = 8;

inline std::vector<uint8_t> EncodeBlob(const MatchConfig& config) {
  std::vector<uint8_t> blob(kBlobHeader);
  blob[0] = kBlobVersion;
  blob[1] = config.shuffle ? 1 : 0;
  blob[2] = uint8_t(config.extra.size());
  blob[4] = uint8_t(config.seed >> 24);
  blob[5] = uint8_t(config.seed >> 16);
  blob[6] = uint8_t(config.seed >> 8);
  blob[7] = uint8_t(config.seed);
  blob.insert(blob.end(), config.extra.begin(), config.extra.end());
  return blob;
}

inline bool DecodeBlob(const std::vector<uint8_t>& blob, MatchConfig* out) {
  if (blob.size() < kBlobHeader || blob[0] != kBlobVersion) return false;
  const size_t count = blob[2];
  if (count > size_t(kExtraSlotCount) || blob.size() < kBlobHeader + count) return false;
  out->shuffle = (blob[1] & 1) != 0;
  out->seed = (uint32_t(blob[4]) << 24) | (uint32_t(blob[5]) << 16) | (uint32_t(blob[6]) << 8) | blob[7];
  out->extra.assign(blob.begin() + kBlobHeader, blob.begin() + kBlobHeader + count);
  return true;
}

// ---- The stage's pickups and their respawns -------------------------------------

// SplitMix64: a small random generator that gives the same numbers on every PC
// for the same seed (the standard library's distributions are not guaranteed to).
struct SplitMix64 {
  uint64_t state;
  uint64_t Next() {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
};

inline uint16_t LoadU16(const uint8_t* base, uint32_t address) {
  return uint16_t((base[address] << 8) | base[address + 1]);
}
inline void StoreU16(uint8_t* base, uint32_t address, uint16_t value) {
  base[address] = uint8_t(value >> 8);
  base[address + 1] = uint8_t(value);
}

// An ammo crate that holds the ammo of a weapon pickup.
struct Crate {
  uint32_t obj = 0;         // the crate's object
  uint32_t weapon_obj = 0;  // the weapon pickup placed just before it
  uint16_t stage_quantities[kCrateSlotCount] = {};  // its quantities in the stage file
};

// The stage's weapon pickups that have a spawn location (the addresses of their
// setup objects, in the order setupCreateProps makes them), and the ammo crates
// that take their ammo from one of them. Call it before the props are made
// (afterwards the location bytes hold weapon numbers). `object_words(obj)` is
// the game's setup-object size function (sub_821FD198).
template <typename ObjectWords>
inline std::vector<uint32_t> FindPickups(const uint8_t* base, ObjectWords object_words,
                                         std::vector<Crate>* crates) {
  std::vector<uint32_t> pickups;
  uint32_t last_weapon = 0;  // as setupPlaceWeapon's "last location" (0x82643848): 0 = none
  uint32_t obj = LoadU32(base, kSetupList);
  for (int guard = 0; obj && guard < 5000 && base[obj + 3] != kSetupTypeEnd; ++guard) {
    const uint8_t type = base[obj + 3];
    if (type == kSetupTypeWeapon) {
      last_weapon = base[obj + kSetupLocation] >= kFirstLocation ? obj : 0;
      if (last_weapon) pickups.push_back(obj);
    } else if (type == kSetupTypeAmmoCrate && last_weapon) {
      Crate crate;
      crate.obj = obj;
      crate.weapon_obj = last_weapon;
      for (int s = 0; s < kCrateSlotCount; ++s) {
        crate.stage_quantities[s] = LoadU16(base, obj + kCrateSlots + uint32_t(s) * 4 + 2);
      }
      crates->push_back(crate);
    }
    const uint32_t words = object_words(obj);
    if (words == 0) break;
    obj += words * 4;
  }
  return pickups;
}

// Fills a crate for weapon table entry `index` as setupCreateProps does: its
// stage-file quantities, then the weapon's two ammo types.
inline void SetCrateAmmo(uint8_t* base, const Crate& crate, uint8_t index) {
  for (int s = 0; s < kCrateSlotCount; ++s) {
    StoreU16(base, crate.obj + kCrateSlots + uint32_t(s) * 4 + 2, crate.stage_quantities[s]);
  }
  const uint32_t entry = kWeaponTable + index * kWeaponEntrySize;
  for (uint32_t k = 0; k < 2; ++k) {
    const int type = int8_t(base[entry + 1 + k * 2]);
    const uint8_t quantity = base[entry + 2 + k * 2];
    if (type >= 1 && type <= kCrateSlotCount) {
      StoreU16(base, crate.obj + kCrateSlots + uint32_t(type - 1) * 4 + 2, quantity);
    }
  }
}

inline std::vector<Crate> g_crates;

// One weapon pickup of the stage.
struct Pickup {
  uint32_t obj = 0;     // its object (guest address)
  uint8_t index = 0;    // the weapon table index it holds now
  uint32_t respawns = 0;
};

// The stage's weapon pickups, in the order they are made, and its weapon
// selection: the weapons its spawn locations started with (set as the stage
// is made, PdSetupCreateProps).
inline std::vector<Pickup> g_pickups;
inline std::vector<uint8_t> g_selection;

// A pickup can only become a weapon that is a normal pickup: not Nothing (no
// pickup at all), not the Shield (a different kind of object), not Disabled.
inline bool CanRespawnAs(uint8_t index) {
  return index != kIndexNothing && index != kIndexShield && index != kIndexDisabled &&
         index < kWeaponTableCount;
}

// The weapon pickup number `pickup` comes back as on respawn number `respawn`:
// one of the selection other than the one it holds, picked by a generator
// seeded with all three numbers, so every PC picks the same one.
inline uint8_t RespawnIndex(const std::vector<uint8_t>& selection, uint8_t current, uint32_t seed,
                            uint32_t pickup, uint32_t respawn) {
  std::vector<uint8_t> others;
  for (const uint8_t index : selection) {
    if (index != current) others.push_back(index);
  }
  if (others.empty()) return current;
  SplitMix64 rng{(uint64_t(seed) << 32) ^ (uint64_t(pickup) << 20) ^ respawn};
  rng.Next();
  return others[rng.Next() % others.size()];
}

// One line describing a set of slots, e.g. "2(#3) 7(#8) 46(#92)": table index (weapon number).
inline std::string DescribeSlots(const uint8_t* base, const std::vector<uint8_t>& slots) {
  std::string text;
  for (const uint8_t index : slots) {
    if (!text.empty()) text += ' ';
    text += fmt::format("{}(#{})", index, WeaponNumber(base, index));
  }
  return text.empty() ? "-" : text;
}

}  // namespace pd::weapons
