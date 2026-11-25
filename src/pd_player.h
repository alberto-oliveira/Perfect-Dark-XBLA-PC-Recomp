// The local player, for test scripts: where it is, and a few things a test can
// make it do (give and equip a weapon, look somewhere, move somewhere).
//
// The test pipe (perfect_dark_app.h TickTestPipe) runs on the UI thread, but
// giving a weapon or moving the player means calling game functions that work on
// g_Vars.currentplayer, so they must run on the game thread while the game is
// ticking the local player. The pipe puts one request here; the hook at the start
// of the player's input step (PdHookTestPlayerTick, perfect_dark_hooks.cpp) picks
// it up there and carries it out (PdTestPlayerTick, main.cpp), then marks it done.
//
// In a network match every PC puts its own player first, so the local player is
// always players[0] (g_Vars+100).
//
// XBLA player offsets used here (N64 struct player + 8 in this part of the struct,
// read from bondstartnewlife sub_820FDA68, the respawn, unless noted):
//   +4/+8/+12 memcampos, +20 memcamroom     +116 vv_manground   +120 vv_ground
//   +188 prop                                +332 vv_theta (mouse look, verified)
//   +388/+392/+396 bondshotspeed (the knockback velocity; chr_damage adds the hit
//                  vector to +388/+396 at 0x8212973C)
//   +792 bondprevpos   +884 bond2 (theta vector +884, radius +896, pos +900,
//                       look +912, up +924)
//   +5544 weapon held, +5546 the weapon being switched to (bgun_equip_weapon)
//   +5698/+5700/+5702 floor colour/flags/type, +6616 floor room
//   +6636 vv_eyeheight (the prop's y is the ground + this)
//   +6692 the previous frame's vv_theta (render interpolation)
//   +7112 room, +7128 pos, +7140 look, +7152 up: the camera's copy (the respawn
//   sets these after moving the player)
//   +7324 combat boost time left, +7328 combat boost on (XBLA: per player)
//
// Header-only, like pd_crossbow.h.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace pd::player {

constexpr uint32_t kGVars = 0x82648BF8;
constexpr uint32_t kPlayers = kGVars + 100;  // u32 players[8]
constexpr uint32_t kLevelFrames = kGVars + 0x0C;

constexpr uint32_t kMemCamPos = 4;
constexpr uint32_t kMemCamRoom = 20;
constexpr uint32_t kManGround = 116;
constexpr uint32_t kGround = 120;
constexpr uint32_t kProp = 188;
constexpr uint32_t kTheta = 332;
constexpr uint32_t kShotSpeed = 388;
constexpr uint32_t kPrevPos = 792;
constexpr uint32_t kBond = 884;
constexpr uint32_t kBondTheta = kBond + 0;
constexpr uint32_t kBondRadius = kBond + 12;
constexpr uint32_t kBondPos = kBond + 16;
constexpr uint32_t kBondLook = kBond + 28;
constexpr uint32_t kBondUp = kBond + 40;
constexpr uint32_t kWeapon = 5544;
constexpr uint32_t kSwitchToWeapon = 5546;
constexpr uint32_t kFloorCol = 5698;
constexpr uint32_t kFloorFlags = 5700;
constexpr uint32_t kFloorType = 5702;
constexpr uint32_t kFloorRoom = 6616;
constexpr uint32_t kEyeHeight = 6636;
constexpr uint32_t kPrevTheta = 6692;
constexpr uint32_t kCamRoom = 7112;
constexpr uint32_t kCamPos = 7128;
constexpr uint32_t kCamLook = 7140;
constexpr uint32_t kCamUp = 7152;
// Combat boost is per player on XBLA (bgun_add_boost sub_82178668): +7324 s32 the
// time left, +7328 u8 "on" (the player's own tick runs at double speed, sub_820F4D40).
constexpr uint32_t kBoostTime = 7324;
constexpr uint32_t kBoostOn = 7328;

// Props: position f32 x/y/z at +8, rooms at +58 (s16 list ending in -1).
constexpr uint32_t kPropPos = 8;
constexpr uint32_t kPropRooms = 58;

// Big-endian guest memory.
inline uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}
inline void StoreU32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, 4);
}
inline float LoadF32(const uint8_t* base, uint32_t address) {
  const uint32_t bits = LoadU32(base, address);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
inline void StoreF32(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  StoreU32(base, address, bits);
}
inline int16_t LoadS16(const uint8_t* base, uint32_t address) {
  return int16_t((base[address] << 8) | base[address + 1]);
}
inline void StoreS16(uint8_t* base, uint32_t address, int16_t value) {
  base[address] = uint8_t(uint16_t(value) >> 8);
  base[address + 1] = uint8_t(value);
}

// players[n], 0 if there is none.
inline uint32_t Player(const uint8_t* base, int n) {
  return n >= 0 && n < 8 ? LoadU32(base, kPlayers + uint32_t(n) * 4) : 0;
}
inline uint32_t PropOf(const uint8_t* base, uint32_t player) {
  return player ? LoadU32(base, player + kProp) : 0;
}

struct Vec {
  float x = 0, y = 0, z = 0;
};
inline Vec LoadVec(const uint8_t* base, uint32_t address) {
  return {LoadF32(base, address), LoadF32(base, address + 4), LoadF32(base, address + 8)};
}
inline void StoreVec(uint8_t* base, uint32_t address, const Vec& v) {
  StoreF32(base, address, v.x);
  StoreF32(base, address + 4, v.y);
  StoreF32(base, address + 8, v.z);
}

// The view angles that look from `from` at `to`, in the game's degrees. The game
// faces (-sin theta, cos theta) on the ground plane (bond2.theta at the respawn);
// verta is up from the horizon.
inline void AnglesTowards(const Vec& from, const Vec& to, float* theta, float* verta) {
  constexpr double kDeg = 180.0 / 3.14159265358979323846;
  const double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
  double t = std::atan2(-dx, dz) * kDeg;
  if (t < 0) t += 360.0;
  *theta = float(t);
  *verta = float(std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * kDeg);
}

// One line describing player n: position, first room, view, weapon, knockback.
inline std::string Describe(const uint8_t* base, int n) {
  const uint32_t player = Player(base, n);
  const uint32_t prop = PropOf(base, player);
  if (!prop) return "no player " + std::to_string(n);
  const Vec pos = LoadVec(base, prop + kPropPos);
  const Vec push = LoadVec(base, player + kShotSpeed);
  char text[256];
  std::snprintf(text, sizeof(text),
                "player %d at %.1f %.1f %.1f room %d theta %.1f weapon %d push %.3f %.3f boost %u/%d", n, pos.x,
                pos.y, pos.z, LoadS16(base, prop + kPropRooms), LoadF32(base, player + kTheta),
                int(int8_t(base[player + kWeapon])), push.x, push.z, base[player + kBoostOn],
                int(LoadU32(base, player + kBoostTime)));
  return text;
}

// The player number whose gamertag is `name`, or -1. Player n uses player config n
// (0x82641950, 216 bytes each); in a network match the gamertag is at +75 (a remote
// one ends in a line break, so the comparison stops at the first control character).
// Player numbers differ between PCs (each puts itself first) and with the join
// order, so test scripts name players instead.
constexpr uint32_t kConfigs = 0x82641950;
constexpr uint32_t kConfigSize = 216;
constexpr uint32_t kConfigGamertag = 75;
inline int PlayerByName(const uint8_t* base, const std::string& name) {
  for (int n = 0; n < 8; ++n) {
    if (!Player(base, n)) continue;
    const uint32_t tag = kConfigs + uint32_t(n) * kConfigSize + kConfigGamertag;
    std::string have;
    for (uint32_t i = 0; i < 32 && base[tag + i] >= 0x20; ++i) have += char(base[tag + i]);
    if (have == name) return n;
  }
  return -1;
}

// ---- Requests from the test pipe ----------------------------------------------------

enum class Action { kNone, kGive, kEquip, kLook, kFace, kTeleport, kTeleportNear, kPos };

struct Request {
  Action action = Action::kNone;
  int number = 0;     // the weapon (give/equip) or the other player (face/tp near)
  std::string who;    // face/tp near: the other player's gamertag, if not given by number
  float a = 0, b = 0, c = 0, d = 0;  // look: theta verta; tp: x y z theta; tp near: distance
  std::string tag;    // pos: a label for the log line
};

// The one request in flight. The pipe sets it and waits for `done`.
inline std::mutex g_mutex;
inline Request g_request;
inline bool g_pending = false;
inline bool g_done = false;

// `track`: log player 0 every `g_track_frames` level frames (0 = off).
inline int g_track_frames = 0;

// The pipe: hand a request to the game thread.
inline void Submit(const Request& request) {
  std::lock_guard lock(g_mutex);
  g_request = request;
  g_pending = true;
  g_done = false;
}
// The pipe: has the game thread carried it out?
inline bool Done() {
  std::lock_guard lock(g_mutex);
  return g_done;
}
// The game thread: take the request, if there is one.
inline bool Take(Request* out) {
  std::lock_guard lock(g_mutex);
  if (!g_pending) return false;
  *out = g_request;
  g_pending = false;
  return true;
}
inline void Finish() {
  std::lock_guard lock(g_mutex);
  g_done = true;
}

}  // namespace pd::player
