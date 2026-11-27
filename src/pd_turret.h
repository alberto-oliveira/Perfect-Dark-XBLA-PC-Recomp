// Perfect Dark XBLA PC Recomp - the Remote Turret.
//
// A custom weapon (pd_custom_weapons.h) made from the Laptop Gun:
//   - primary: throw a turret (the Laptop's sentry). Unlike the Laptop, the gun
//     stays in the hand;
//   - secondary, "Control": pull the trigger to take control of the turret. The
//     view moves to the turret, the player's own view angles aim it and the
//     trigger fires it. The body stands still meanwhile.
//   - leaving (the function toggle, a weapon switch, death) makes the turret
//     explode like a rocket. A turret nobody controls stays idle: it neither
//     targets nor fires.
//
// How the game's sentry works online (docs/research/custom-weapons.md, "Remote
// Turret"): every PC makes its own copy of a thrown sentry, but only the owner's
// copy chooses where to aim and whether to fire. autogun_tick (sub_82206E58)
// works out goal angles on its stack; the owner's PC sends them with the firing
// flag, and the other PCs' copies turn and fire as told. So the mod only sets
// the goals and the flag on the owner's PC, and the game's own sync does the rest.
//
// The turret's angles are world angles: it shoots along
// (cos(xrot) sin(yrot), sin(xrot), cos(xrot) cos(yrot)) (N64 autogun_tick_shoot),
// so the goals come straight from the player's look direction.
//
// Header-only, C++17. The game glue (the per-frame tick, the explosion, the
// settings, the hooks) is in main.cpp and perfect_dark_hooks.cpp.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "pd_crossbow.h"  // IsLocalPlayer
#include "pd_custom_weapons.h"
#include "pd_player.h"

namespace pd::turret {

constexpr uint8_t kWeaponTurret = 0x42;  // the custom weapon's number (pd_custom_weapons.h)
constexpr uint8_t kWeaponLaptop = 14;    // its base

// ---- Its two functions --------------------------------------------------------------
//
// A weapon definition holds two function definitions (N64 struct funcdef: +0 s32
// type, +4 u16 name, +7 s8 ammoindex, +8 noise settings, +0x0C fire animation,
// +0x10 flags; then the type's own fields). The Laptop's are its burst fire and
// its "Deploy" (a throw, funcdef_throw: +0x14 projectile model, +0x18 activate time,
// +0x1C recovery time, +0x20 damage).
constexpr uint32_t kDefFunctions = 0x14;  // functions[0], functions[1] at +0x18
constexpr uint32_t kFuncType = 0x00;
constexpr uint32_t kFuncName = 0x04;
constexpr uint32_t kFuncAmmoIndex = 0x07;
constexpr uint32_t kFuncFlags = 0x10;
constexpr uint32_t kFuncCopySize = 0x30;  // funcdef_throw is 0x24 bytes
constexpr uint32_t kFuncDiscardWeapon = 0x00040000;  // FUNCFLAG_DISCARDWEAPON
//
// Primary = a copy of the Laptop's Deploy:
//   - without FUNCFLAG_DISCARDWEAPON, so the gun is not thrown away with the turret;
//   - with ammo index 0 instead of -1: without the discard flag, the throw goes
//     through the game's ordinary throw (bgun_tick_inc_attacking_throw), which
//     takes one round from loadedammo[ammoindex]; -1 would write outside the
//     array. So a throw needs (and uses) one loaded round, and the turret then
//     takes up to 200 rounds from the player's SMG ammo, as the Laptop's does.
//
// Secondary = a "special" function (INVENTORYFUNCTYPE_SPECIAL, N64 funcdef_special:
// +0x14 special function, +0x18 recovery time). The game fires a special
// function with no ammo and passes its special number to the hand's attack
// (hand_tick_attack sub_821FC9D8), whose switch only knows 1-12: our number does
// nothing there. Taking control is done by the mod's tick instead, which watches
// the trigger.
constexpr uint32_t kFuncTypeSpecial = 4;
constexpr uint32_t kSpecialControl = 20;    // a special function number the game does not have
constexpr uint32_t kSpecialRecovery = 30;   // frames (60 a second) between "attacks"
constexpr uint32_t kSpecialSize = 0x20;
// Input steps the trigger must stay let go before the gun sees it again: the step that
// toggles the function reads the trigger as off for a moment (test R2).
constexpr int kReleaseSteps = 6;
constexpr uint16_t kTextControl = 0x4CE3;   // borrowed text id (solo item 0x44's name)
inline const char* const kControlName = "Control";

// ---- Fields ------------------------------------------------------------------------

// ---- Ammo -------------------------------------------------------------------------
// The weapon has an ammo definition of its own (N64 struct ammodef: +0 u32 ammo type,
// +4 casing, +8 s16 clip size, +0x0C reload animation, +0x10 flags), with an ammo type
// no weapon uses: AMMOTYPE_DART (0x11). It must be below 20: a multiplayer ammo crate
// has slots only for types 1-19 (setup.c, `priammotype < 20`). The clip holds kClip
// rounds; the player carries up to kReserve more (the type's capacity in g_AmmoTypes,
// 12 bytes per type: s32 capacity, ...). A throw hands the clip to the turret, whose ammo
// is one byte where 255 means "unlimited": at most 254. Reload while controlling tops
// the turret up from the clip and the reserve (PdTurretTick).
//   - Picking up a Remote Turret gives kPickupAmmo (weapon_get_pickup_ammo_qty only knows
//     the game's own types: 1 round otherwise; hook at its exit);
//   - an ammo crate gives kCrateAmmo (the multiplayer entry: +1 ammo type, +2 quantity);
//   - picking up one's own turret puts its rounds back into the reserve.
// The pickup message's "dart"/"darts" texts (only darts use them) become turret rounds.
constexpr uint32_t kDefAmmo = 0x1C;             // weapondef ammos[0]
constexpr uint32_t kAmmoDefSize = 0x14;
constexpr uint32_t kAmmoType = 0x11;            // AMMOTYPE_DART, used by no weapon
constexpr int16_t kClip = 250;
constexpr uint32_t kReserve = 500;
constexpr uint32_t kPickupAmmo = 250;
constexpr uint8_t kCrateAmmo = 125;
constexpr uint32_t kAmmoTypes = 0x825CDAA8;     // g_AmmoTypes
constexpr uint32_t kHandLoadedAmmo = 544;       // s32 loadedammo[2] (hand-relative)
constexpr uint32_t kTurretMaxAmmo = 254;
constexpr uint16_t kTextDart = 0x5219;          // L_PROPOBJ_025 (bank 0x29 << 9 | 25)
constexpr uint16_t kTextDarts = 0x5254;         // L_PROPOBJ_084
// The turret's reload (Reload pressed while controlling): it cannot fire for
// this many frames (60 a second), then it is topped up.
constexpr uint32_t kReloadFrames = 90;

// ---- The cloak after a throw (P3) -------------------------------------------------
// The cloaking device: player devicesactive (+196) |= DEVICE_CLOAKDEVICE, while the
// player holds AMMOTYPE_CLOAK ammo (reserve at ammoheldarr, player+6096 + type*4), in
// 1/60 s; chr_update_cloak uses it up while cloaked and turns the device off at 0.
constexpr uint32_t kPlayerDevices = 196;
constexpr uint32_t kDeviceCloak = 0x40;
constexpr uint32_t kPlayerAmmoHeld = 6096;
constexpr uint32_t kAmmoTypeCloak = 0x14;
constexpr int32_t kCloakAmmoMax = 18000;        // its capacity in g_AmmoTypes (5 minutes)

// ---- The view (P7) -----------------------------------------------------------------
constexpr uint32_t kPlayerVisionMode = 16;      // u16: 2 = Slayer rocket, 3 = one frame of static
constexpr uint16_t kVisionStatic = 3;

// Hands (XBLA: N64 struct hand, at player+1632, 1956 bytes each).
constexpr uint32_t kRightHand = pd::custom::kPlayerHands;
constexpr uint32_t kHandFunc = 3;        // u8 gset.weaponfunc: 0 primary, 1 secondary
// Player.
constexpr uint32_t kPlayerIsDead = 224;           // bool (s32) isdead
constexpr uint32_t kPlayerSpeedSideways = 368;    // f32
constexpr uint32_t kPlayerSpeedStrafe = 372;      // f32
constexpr uint32_t kPlayerSpeedForwards = 376;    // f32
// Objects (N64 struct defaultobj) and the autogun (struct autogunobj).
constexpr uint32_t kObjType = 3;           // u8: 13 = OBJTYPE_AUTOGUN
constexpr uint8_t kObjTypeAutogun = 13;
constexpr uint32_t kObjProp = 20;          // struct prop *
constexpr uint32_t kObjRealRot = 28;       // f32[3][3]; row 1 (+40) = the up axis
constexpr uint32_t kObjHidden = 64;        // u32: owner player in bits 28-31, 4 = deleting
constexpr uint32_t kHiddenDeleting = 4;
constexpr uint32_t kObjDamage = 76;        // s16 damage, +78 s16 maxdamage
constexpr uint32_t kGunFiring = 94;        // s8
constexpr uint32_t kGunYRot = 108;         // f32, radians 0..2pi
constexpr uint32_t kGunYSpeed = 112;
constexpr uint32_t kGunXRot = 120;         // f32, radians (up > 0)
constexpr uint32_t kGunXSpeed = 124;
constexpr uint32_t kGunMaxSpeed = 128;     // f32 radians per tick (the Laptop's: 0.0697)
constexpr uint32_t kGunTarget = 164;       // struct prop *
constexpr uint32_t kGunAmmo = 169;         // u8 (255 = unlimited)
// autogun_tick's goal angles, on its stack: apply_rotation(&yrot, 84(r1)), then
// (&xrot, 88(r1)) (0x822078F4; test R2 had them the wrong way round: the other PCs'
// copies turned with the angles swapped).
constexpr uint32_t kGoalYRot = 84;         // (r1)
constexpr uint32_t kGoalXRot = 88;
// Props: +44 s32 sync id. An explosion's network message carries the exploding prop's
// sync id; each receiving PC deletes its prop with that id (sub_822AFA30 ->
// sub_821FA840; negative ids are skipped). A sentry has -1, so the other PCs kept
// their copy of an exploded turret (test R2). Each Remote Turret's prop gets the same
// id on every PC instead: kSyncIdBase + the thrown-laptop slot (the object's owner
// bits, the same on every PC).
// For a sentry the receiver does not delete the prop: it sets OBJFLAG_DEACTIVATED
// (obj flags +8 |= 0x10000000), and a deactivated sentry no longer ticks or fires but
// stays. So each PC deletes another PC's deactivated Remote Turret itself.
constexpr uint32_t kPropSyncId = 44;
constexpr uint32_t kObjFlags = 8;
constexpr uint32_t kFlagDeactivated = 0x10000000;
constexpr uint32_t kSyncIdBase = 0x7FFF0000;

using pd::player::LoadF32;
using pd::player::LoadU32;
using pd::player::StoreF32;
using pd::player::StoreU32;
using pd::player::Vec;

// ---- Settings (main.cpp sets them every frame; online the host's) --------------------
inline float g_turn_speed = 0.6f;    // the turret's maxspeed while controlled
inline uint32_t g_explosion = 13;    // explosion type when leaving (13 = a rocket's)
inline uint32_t g_cloak_seconds = 30;  // the thrower's cloak after a throw (0 = none)
// The view while controlling (pd_turret_eye_*): over the barrels. The gun's pivot is
// g_eye_pivot units out of the surface; the camera g_eye_back behind it along the look
// and g_eye_up above it.
inline float g_eye_pivot = 15.0f;
inline float g_eye_back = 35.0f;
inline float g_eye_up = 12.0f;
// How far below the surface the turret may aim, in degrees (pd_turret_aim_below), and
// how far from the direction it rests in (pd_turret_aim_cone): its model looks wrong
// turned further.
inline float g_aim_below = 5.0f;
inline float g_aim_cone = 70.0f;
// The Slayer rocket's "monitor" picture while controlling (pd_turret_monitor_view).
inline bool g_monitor_view = true;

constexpr uint32_t kBlobKey = 0x54555252;  // "TURR"
constexpr uint8_t kBlobVersion = 2;
struct Rules {
  uint32_t explosion = 13;
  uint32_t turn_speed_percent = 60;  // maxspeed in hundredths of a radian per tick
  uint32_t cloak_seconds = 30;
  bool operator!=(const Rules& o) const {
    return explosion != o.explosion || turn_speed_percent != o.turn_speed_percent || cloak_seconds != o.cloak_seconds;
  }
};
inline std::vector<uint8_t> EncodeBlob(const Rules& r) {
  return {kBlobVersion, uint8_t(r.explosion), uint8_t(r.turn_speed_percent), uint8_t(r.cloak_seconds)};
}
inline bool DecodeBlob(const std::vector<uint8_t>& blob, Rules* out) {
  if (blob.size() < 4 || blob[0] != kBlobVersion || blob[1] < 1 || blob[1] > 25 || blob[2] == 0) return false;
  out->explosion = blob[1];
  out->turn_speed_percent = blob[2];
  out->cloak_seconds = blob[3];
  return true;
}

// ---- State -------------------------------------------------------------------------

// The turret each player threw on this PC (autogun objects; 0 = none), by player
// number (the current player when laptop_deploy made it). Filled by the
// deploy hook on every PC, so every PC knows which sentries are Remote Turrets.
inline uint32_t g_turrets[8] = {};
// The weapon of the throw laptop_deploy is about to make (deploy hook, noted at the
// Laptop check just before the call).
inline uint32_t g_deploying_weapon = 0;

// The local player's control (players[0] on this PC).
struct Control {
  bool on = false;           // the view is at the turret
  uint32_t obj = 0;          // the controlled turret
  bool need_release = true;  // the trigger must be let go before it fires (or takes control)
  bool fire = false;         // the trigger is held: fire
  // The gun does not see the trigger while the player controls a turret (it would
  // start the Control function's own attack, which a toggle back to the primary
  // turns into a throw), nor after leaving until the trigger is let go (the primary,
  // selected again, would throw a new turret at once).
  bool block_trigger = false;
  int released_steps = 0;    // input steps in a row with the trigger let go
  bool trigger = false;      // the trigger itself, as the input step read it
  bool reload_request = false;  // Reload was pressed while controlling
  uint32_t reload_until = 0;    // the turret is reloading until this level frame (0 = not)

};
inline Control g_control;

// The player number (on this PC) who threw turret `obj`, or -1. Not the object's
// owner bits: laptop_deploy puts the thrower's index in the thrown-laptop table
// there, the same on every PC (test R1: the host's turret had owner bits 0 on the
// client too, where the host is player 1). The deploy hook takes the current
// player instead, which is the thrower on every PC.
inline int Owner(const uint8_t* base, uint32_t obj) {
  (void)base;
  for (int n = 0; n < 8; ++n) {
    if (obj && g_turrets[n] == obj) return n;
  }
  return -1;
}

// True if `obj` is a Remote Turret (any player's) that still exists on this PC.
inline bool IsTurret(const uint8_t* base, uint32_t obj) {
  if (!obj) return false;
  for (const uint32_t t : g_turrets) {
    if (t == obj) {
      return LoadU32(base, obj + kObjProp) != 0 && base[obj + kObjType] == kObjTypeAutogun &&
             !(LoadU32(base, obj + kObjHidden) & kHiddenDeleting);
    }
  }
  return false;
}

// The local player's turret, or 0.
inline uint32_t LocalTurret(const uint8_t* base) {
  for (const uint32_t t : g_turrets) {
    if (t && IsTurret(base, t) && pd::crossbow::IsLocalPlayer(base, Owner(base, t))) return t;
  }
  return 0;
}

// True while the current player (g_Vars.currentplayer) is the local player and
// controls a turret: the camera, freeze and gun-hiding hooks ask this.
inline bool CurrentPlayerControls(const uint8_t* base) {
  if (!g_control.on) return false;
  const uint32_t player = pd::player::Player(base, 0);
  return player && LoadU32(base, pd::player::kGVars + 1188) == player;
}

// ---- Small vector helpers ------------------------------------------------------------
inline Vec Add(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec Scale(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec Normalised(Vec a, Vec fallback) {
  const float len = std::sqrt(Dot(a, a));
  return len > 0.0001f ? Scale(a, 1.0f / len) : fallback;
}

// The surface the turret is stuck to faces along the turret's up axis (realrot row 1):
// (0, 1, 0) on a floor, sideways on a wall.
inline Vec SurfaceNormal(const uint8_t* base, uint32_t obj) {
  return Normalised(pd::player::LoadVec(base, obj + kObjRealRot + 12), {0.0f, 1.0f, 0.0f});
}

// The turret's gun: g_eye_pivot units out of its surface.
inline Vec PivotOf(const uint8_t* base, uint32_t obj) {
  const uint32_t prop = LoadU32(base, obj + kObjProp);
  return Add(pd::player::LoadVec(base, prop + pd::player::kPropPos), Scale(SurfaceNormal(base, obj), g_eye_pivot));
}

// Where the camera wants to be while controlling (look = the view direction): just
// behind and above the turret's barrels, so they show low in the middle of the view and
// the shots visibly leave them. main.cpp pulls it in if a wall is closer.
inline Vec WantedEye(const uint8_t* base, uint32_t obj, Vec look) {
  return Add(Add(PivotOf(base, obj), Scale(Normalised(look, {0, 0, 1}), -g_eye_back)), {0.0f, g_eye_up, 0.0f});
}

// The aim limit: a direction may point at most g_aim_below degrees into the surface
// (dot(dir, normal) >= -sin(limit)). A direction beyond it is turned back onto the limit,
// keeping its heading along the surface. Returns true if it changed `dir`.
inline bool ClampAim(Vec* dir, Vec normal) {
  const float min_dot = -std::sin(g_aim_below * 3.14159265f / 180.0f);
  const Vec d = Normalised(*dir, normal);
  const float along = Dot(d, normal);
  if (along >= min_dot) return false;
  // The part of the direction along the surface; looking straight into the wall leaves
  // none, so then any direction along the surface does (the one nearest "up").
  Vec tangent = Add(d, Scale(normal, -along));
  if (Dot(tangent, tangent) < 1e-6f) tangent = Add({0, 1, 0}, Scale(normal, -normal.y));
  tangent = Normalised(tangent, {1, 0, 0});
  *dir = Add(Scale(normal, min_dot), Scale(tangent, std::sqrt(1.0f - min_dot * min_dot)));
  return true;
}

// The cone limit: at most g_aim_cone degrees from `axis` (the resting aim). A direction
// outside is turned back onto the cone's edge, keeping its side. True if it changed.
inline bool ClampCone(Vec* dir, Vec axis) {
  const float max_rad = g_aim_cone * 3.14159265f / 180.0f;
  const Vec d = Normalised(*dir, axis);
  const float c = Dot(d, axis);
  if (c >= std::cos(max_rad)) return false;
  Vec side = Add(d, Scale(axis, -c));
  // Straight behind: swing round sideways (along the horizontal at right angles to the axis).
  if (Dot(side, side) < 1e-6f) side = {axis.z, 0.0f, -axis.x};
  side = Normalised(side, {1, 0, 0});
  *dir = Add(Scale(axis, std::cos(max_rad)), Scale(side, std::sin(max_rad)));
  return true;
}
// The direction a turret faces when it settles: out of a wall (levelled), or, on a floor
// or ceiling, horizontally where its thrower faced (`thrower_dir`).
inline Vec RestingAim(Vec normal, Vec thrower_dir) {
  if (std::fabs(normal.y) > 0.7f) return Normalised({thrower_dir.x, 0.0f, thrower_dir.z}, {0, 0, 1});
  return Normalised({normal.x, 0.0f, normal.z}, {0, 0, 1});
}

// A direction from the turret's angles (the shot direction, N64 autogun_tick_shoot).
inline Vec DirOf(float yrot, float xrot) {
  return {std::cos(xrot) * std::sin(yrot), std::sin(xrot), std::cos(xrot) * std::cos(yrot)};
}

// The turret's goal angles for a look direction: yrot = atan2(x, z) in 0..2pi,
// xrot = asin(y).
inline void AnglesOf(const Vec& look, float* yrot, float* xrot) {
  constexpr float kTwoPi = 6.28318530718f;
  float y = std::atan2(look.x, look.z);
  if (y < 0.0f) y += kTwoPi;
  const float len = std::sqrt(look.x * look.x + look.y * look.y + look.z * look.z);
  const float s = len > 0.0001f ? look.y / len : 0.0f;
  *yrot = y;
  *xrot = std::asin(s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s));
}

// The direction each thrower faced at its throw (the resting aim of a floor turret), by
// player number; and the loaded rounds of this PC's player just before its throw (they
// all go into the turret, P1).
inline Vec g_thrower_dir[8] = {};
inline int32_t g_throw_clip = -1;
inline int32_t g_throw_reserve = -1;
// The level frame at which this PC's player's turret cloak ends (0 = none running): the
// cloaking device is switched off then, whatever cloak ammo is left.
inline uint32_t g_cloak_until = 0;

// Both limits for turret `obj` (owner `owner`): the cone around its resting aim, then the
// surface. True if `dir` changed.
inline bool ClampTurretAim(const uint8_t* base, uint32_t obj, int owner, Vec* dir) {
  const Vec normal = SurfaceNormal(base, obj);
  const Vec rest = RestingAim(normal, owner >= 0 && owner < 8 ? g_thrower_dir[owner] : Vec{0, 0, 1});
  const bool a = ClampCone(dir, rest);
  const bool b = ClampAim(dir, normal);
  return a || b;
}

// Forgets every turret (a new stage).
inline void Reset() {
  std::memset(g_turrets, 0, sizeof(g_turrets));
  g_control = Control{};
  g_deploying_weapon = 0;
  g_cloak_until = 0;
}

// ---- Install ------------------------------------------------------------------------

// Gives the Remote Turret its two functions (after pd::custom::Install made its
// definition as a copy of the Laptop's). `alloc(size)` returns guest heap memory.
template <typename Alloc>
inline void Install(uint8_t* base, Alloc alloc) {
  const uint32_t def = LoadU32(base, pd::custom::kWeapons + uint32_t(kWeaponTurret) * 4);
  const uint32_t laptop = LoadU32(base, pd::custom::kWeapons + uint32_t(kWeaponLaptop) * 4);
  if (!def || !laptop || def == laptop) return;
  const uint32_t deploy = LoadU32(base, laptop + kDefFunctions + 4);  // the Laptop's functions[1]
  const uint32_t primary = alloc(kFuncCopySize);
  const uint32_t special = alloc(kSpecialSize);
  if (!deploy || !primary || !special) return;

  std::memcpy(base + primary, base + deploy, kFuncCopySize);
  StoreU32(base, primary + kFuncFlags, LoadU32(base, primary + kFuncFlags) & ~kFuncDiscardWeapon);
  base[primary + kFuncAmmoIndex] = 0;

  std::memset(base + special, 0, kSpecialSize);
  StoreU32(base, special + kFuncType, kFuncTypeSpecial);
  pd::weapons::StoreU16(base, special + kFuncName, kTextControl);
  base[special + kFuncAmmoIndex] = 0xFF;  // -1: no ammo
  StoreU32(base, special + 8, LoadU32(base, deploy + 8));  // the Deploy's (silent) noise settings
  StoreU32(base, special + 0x14, kSpecialControl);
  StoreU32(base, special + 0x18, kSpecialRecovery);

  StoreU32(base, def + kDefFunctions, primary);
  StoreU32(base, def + kDefFunctions + 4, special);

  // Its own ammo: definition, the type's capacity (the reserve), the crate quantity.
  const uint32_t laptop_ammo = LoadU32(base, laptop + kDefAmmo);
  const uint32_t ammo = alloc(kAmmoDefSize + 0x0C);
  if (laptop_ammo && ammo) {
    std::memcpy(base + ammo, base + laptop_ammo, kAmmoDefSize);
    StoreU32(base, ammo + 0, kAmmoType);
    pd::weapons::StoreU16(base, ammo + 8, uint16_t(kClip));
    StoreU32(base, def + kDefAmmo, ammo);
    StoreU32(base, kAmmoTypes + kAmmoType * 12, kReserve);
  }
  const int index = pd::custom::IndexOf(kWeaponTurret);
  if (pd::custom::g_entries && index >= 0) {
    const uint32_t entry = pd::custom::g_entries + uint32_t(index) * pd::weapons::kWeaponEntrySize;
    base[entry + 1] = uint8_t(kAmmoType);
    base[entry + 2] = kCrateAmmo;
  }
  REXLOG_INFO("[turret] weapon 0x{:02X}: primary 0x{:08X} (the Laptop's Deploy 0x{:08X}, flags 0x{:08X}), "
              "secondary 0x{:08X} (special {}, name 0x{:04X}), ammo 0x{:08X} (type 0x{:02X}, clip {}, reserve {})",
              kWeaponTurret, primary, deploy, LoadU32(base, primary + kFuncFlags), special, kSpecialControl,
              kTextControl, ammo, kAmmoType, kClip, kReserve);
}

}  // namespace pd::turret
