// Perfect Dark XBLA PC Recomp - the detonator crossbow.
//
// A crossbow whose bolts lodge in walls, objects and players (vanilla behaviour:
// bolts already stick), and whose secondary function detonates every bolt the
// player has fired, like the remote mine's detonator.
//
// How the game detonates remote mines (verified in the XBLA code):
//   - Pressing Detonate calls sub_8220D220(playernum), which sets bit `playernum`
//     of the u32 at kDetonatingMines. The game clears that word once a frame.
//   - Every weapon object is ticked by sub_82214A40 (weapon_tick in the N64
//     decomp). Its remote-mine branch explodes a mine whose owner's bit is set:
//     prop_explode(prop, type), then timer240 = -1 and the DELETING flag.
//   - A bolt is its own weapon object, weapon number kWeaponBolt; its owner is in
//     the top 4 bits of the object's `hidden` word, like a mine's.
// So the mod gives the bolt branch of the same tick the remote mine's check.
//
// Network play (checked with logs, 2026-11-14):
//   - Every PC makes its own copy of every bolt: a remote player's shot is
//     replayed locally (sub_82171430). In each copy the owner is the shooter's
//     player number on that PC.
//   - Every PC also replays a remote player's Detonate press (sub_8220D220 runs
//     for them too), so every PC knows when any player detonates.
//   - An explosion is made on one PC and sent to the others (explosion_create,
//     sub_821476E8 -> sub_822ABB20); they show it too (sub_822AFA30). The message
//     can name the prop that exploded (its sync id, prop+44) so the others delete
//     their copy, but bolts have no sync id (-1).
// So only the PC where the owner plays explodes the owner's bolts (one explosion,
// sent to everyone), and every other PC deletes its copies without an explosion
// of its own. Exploding the copies too showed each bolt's explosion twice.
//
// Header-only, C++17. The game glue (settings, hooks) is in main.cpp.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace pd::crossbow {

// ---- Game addresses --------------------------------------------------------------

constexpr uint32_t kGVars = 0x82648BF8;
constexpr uint32_t kDetonatingMines = 0x82643AE0;  // g_PlayersDetonatingMines: bit n = player n detonates
constexpr uint32_t kPlayers = kGVars + 100;         // g_Vars players[8], u32 pointers
constexpr uint32_t kPlayerStats = kGVars + 132;     // 128 bytes a player; +112 = the player's config index
constexpr uint32_t kConfigs = 0x82641950;           // player configs, 216 bytes each
constexpr uint32_t kConfigGamertag = 75;            // the player's gamertag in a network match
constexpr uint32_t kConfigPad = 109;                // s8: its controller, -1 = a player on another PC
constexpr uint32_t kLevelFrames = kGVars + 0x0C;    // g_Vars lvframenum

constexpr uint8_t kWeaponCrossbow = 27;  // 0x1B
constexpr uint8_t kWeaponBolt = 86;      // 0x56, the object a fired crossbow bolt is

// Weapon definitions (g_Weapons entries). +0x14 functions[0], +0x18 functions[1].
constexpr uint32_t kCrossbowDef = 0x825D1660;
constexpr uint32_t kSecondaryFunction = 0x18;
constexpr uint32_t kCrossbowLethal = 0x825D1568;   // the crossbow's own secondary: the 100-damage bolt
constexpr uint32_t kMineDetonate = 0x825D0F60;     // the remote mine's Detonate function

// Explosion types (rows of the table at 0x825CBC70; blast radius / damage radius / damage).
constexpr uint32_t kExplosionSmall = 22;   // the Phoenix's secondary: 100 / 200 / 0.25
constexpr uint32_t kExplosionMedium = 21;  // a SuperDragon grenade:   140 / 270 / 3.5
constexpr uint32_t kExplosionLarge = 13;   // a rocket:                200 / 400 / 4
constexpr uint32_t kExplosionTypeCount = 26;

// pd_xbow_explosion: "small", "medium", "large" or a table row 1-25. Anything
// else is medium.
inline uint32_t ParseExplosion(const std::string& text) {
  if (text == "small") return kExplosionSmall;
  if (text == "large") return kExplosionLarge;
  if (text == "medium" || text.empty()) return kExplosionMedium;
  char* end = nullptr;
  const long row = std::strtol(text.c_str(), &end, 10);
  if (end && *end == '\0' && row >= 1 && row < long(kExplosionTypeCount)) return uint32_t(row);
  return kExplosionMedium;
}

// A prop: +0 u8 type, +4 its object (or chr), +24 its parent prop (what it is
// stuck in), +44 the sync id XBLA added (the same for a prop's copies on every
// PC; an explosion message names the prop it came from by it), +48 another
// network id XBLA added (-1 = none; -1 for bolts and mines).
constexpr uint32_t kPropType = 0;
constexpr uint32_t kPropObj = 4;
constexpr uint32_t kPropParent = 24;
constexpr uint32_t kPropSyncId = 44;
constexpr uint32_t kPropNetId = 48;
constexpr uint32_t kPropPos = 8;     // f32 x, y, z
constexpr uint32_t kPropRooms = 58;  // s16 rooms[], ending in -1

// Collision (the N64's lib/collision.c), for the scorch mark below.
constexpr uint32_t kCdTypeBg = 0x20;  // test against the stage's walls and floors only
constexpr uint32_t kPlayerProp = 188;  // a player's prop

// Prop types (N64 PROPTYPE_*).
constexpr uint8_t kPropTypeObj = 1;
constexpr uint8_t kPropTypeDoor = 2;
constexpr uint8_t kPropTypeChr = 3;
constexpr uint8_t kPropTypeWeapon = 4;
constexpr uint8_t kPropTypePlayer = 6;

// A weapon object: +64 `hidden` (bits 28-31 = owner player, 4 = DELETING),
// +92 weapon number, +95 the gun function that fired it, +98 s16 timer240.
// For a bolt the timer is -1 while it flies and 0 once it has landed.
constexpr uint32_t kObjModel = 24;
constexpr uint32_t kObjRealRot = 28;  // 3x3 f32 rows: the object's axes (times its scale)
constexpr uint32_t kObjHidden = 64;
constexpr uint32_t kObjWeaponNum = 92;
constexpr uint32_t kObjTimer240 = 98;
constexpr uint32_t kHiddenDeleting = 4;

// ---- Guest memory (big-endian) -------------------------------------------------

inline uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  return (uint32_t(base[address]) << 24) | (uint32_t(base[address + 1]) << 16) |
         (uint32_t(base[address + 2]) << 8) | uint32_t(base[address + 3]);
}
inline int16_t LoadS16(const uint8_t* base, uint32_t address) {
  return int16_t((base[address] << 8) | base[address + 1]);
}

// The player number (0-7) that owns a weapon object (who fired or threw it).
inline int OwnerPlayer(const uint8_t* base, uint32_t obj) {
  return int(LoadU32(base, obj + kObjHidden) >> 28);
}

// The player number whose prop is `prop`, or -1 if it is no player's.
inline int PlayerOfProp(const uint8_t* base, uint32_t prop) {
  for (int i = 0; i < 8; ++i) {
    const uint32_t player = LoadU32(base, kPlayers + uint32_t(i) * 4);
    if (player && LoadU32(base, player + kPlayerProp) == prop) return i;
  }
  return -1;
}

// True if player `player` plays on this PC: its config's +109 is its controller
// number, -1 for a player on another PC.
inline bool IsLocalPlayer(const uint8_t* base, int player) {
  if (player < 0 || player > 7) return false;
  const uint32_t cfg = LoadU32(base, kPlayerStats + uint32_t(player) * 128 + 112);
  return cfg < 8 && int8_t(base[kConfigs + cfg * 216 + kConfigPad]) >= 0;
}

// Player `player`'s gamertag in a network match (its config's +75), or "".
inline std::string PlayerName(const uint8_t* base, int player) {
  if (player < 0 || player > 7) return "";
  const uint32_t cfg = LoadU32(base, kPlayerStats + uint32_t(player) * 128 + 112);
  if (cfg >= 8) return "";
  // The name ends at its first control character: a remote player's name is
  // followed by a line break.
  const char* name = reinterpret_cast<const char*>(base + kConfigs + cfg * 216 + kConfigGamertag);
  size_t length = 0;
  while (length < 16 && uint8_t(name[length]) >= 0x20) ++length;
  return std::string(name, length);
}

// What a prop is stuck in, for the logs: "nothing", "player 1 (Zqclient)",
// "object", "door", "chr" or "type N".
inline std::string DescribeParent(const uint8_t* base, uint32_t prop) {
  const uint32_t parent = LoadU32(base, prop + kPropParent);
  if (!parent) return "nothing";
  switch (base[parent + kPropType]) {
    case kPropTypePlayer: {
      const int player = PlayerOfProp(base, parent);
      return "player " + std::to_string(player) + " (" + PlayerName(base, player) + ")";
    }
    case kPropTypeObj: return "object";
    case kPropTypeDoor: return "door";
    case kPropTypeChr: return "chr";
    case kPropTypeWeapon: return "weapon";
    default: return "type " + std::to_string(base[parent + kPropType]);
  }
}

// ---- Detonation ---------------------------------------------------------------

// Bolts whose owner pressed Detonate but whose explosion could not be made yet
// (prop_explode fails while every explosion slot is in use), by prop, with the
// object they had (a freed prop can be reused by a new bolt). The game keeps a
// mine's pending explosion in its timer; a bolt's timer has other uses, so the
// mod keeps its own list, tried again at each tick until each one explodes.
// Emptied at each stage start.
inline std::map<uint32_t, uint32_t> g_pending;

}  // namespace pd::crossbow
