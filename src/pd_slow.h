// Perfect Dark XBLA PC Recomp - the Slow Gun.
//
// A custom weapon (pd_custom_weapons.h) that works like the Mauler and looks like
// the DY357 Magnum. A player it hits is slowed (movement, aim and fire rate, with a
// light blur), and a poison takes some health away over a few seconds. Its
// secondary, the Mauler's charge, drains the whole clip; a charged hit slows for
// longer and blurs more.
//
// How the victim's PC learns of a hit (docs/research/custom-weapons.md, "Slow Gun:
// static research"): gunfire is decided on the shooter's PC, in chr_damage. For a
// player hit, chr_damage sends a network message (sub_822ABD68) that carries a
// "blur code": 1 Tranquilizer, 2 crossbow bolt, 3 N-Bomb, 0 for anything else.
// The victim's PC (only it: probe P1) replays the hit through the player-hit function
// sub_821274A0, which adds blur by that code. The Slow Gun marks its hits (below);
// the victim's PC sees the mark, notes the hit, and clears it before the game uses it.
//
// Header-only, C++17. The game glue (settings, hooks) is in main.cpp and
// perfect_dark_hooks.cpp.
#pragma once

#include <algorithm>
#include <cstdint>

namespace pd::slow {

constexpr uint8_t kWeaponSlow = 0x44;   // the custom weapon's number (pd_custom_weapons.h)
constexpr uint8_t kWeaponMauler = 6;    // its base weapon

// The gset (N64 struct gset, 4 bytes) chr_damage gets: +0 weapon, +2 the Mauler's
// charge x 10 (0-50, gset_populate), +3 the function (0 primary, 1 secondary).
constexpr uint32_t kGsetWeapon = 0;
constexpr uint32_t kGsetCharge = 2;
constexpr uint32_t kGsetFunction = 3;

// The marker in the hit message. The message carries the blur code in only 2 bits
// (0-3: probe P1 sent 0x40 and 0x72, the victim's PC got 0 and 2), and a disarm flag
// (FUNCFLAG_DISARM, Unarmed's secondary). The game never sends both: blur codes come
// only from the Tranquilizer, bolts and the N-Bomb, which cannot disarm. So a Slow Gun
// hit is sent as "disarm + blur code", the blur code being the hit's tier:
//   1 = a primary shot, 2 = a part-charged shot, 3 = a fully charged one.
// The victim's PC catches the pair at the start of the player-hit function, before the
// game disarms anyone, and clears both.
constexpr uint32_t kTierShot = 1;
constexpr uint32_t kTierCharged = 2;
constexpr uint32_t kTierFull = 3;
constexpr uint32_t kChargeFull = 50;  // gset+2 at a full charge (5.0 x 10)

// The tier of a Slow Gun hit, from its gset.
inline uint32_t Tier(uint32_t function, uint32_t charge) {
  if (function == 0 || charge < 10) return kTierShot;  // the primary (or under one step of charge)
  return charge >= kChargeFull ? kTierFull : kTierCharged;
}
inline bool IsMarker(uint32_t disarm, uint32_t blur_code) { return disarm != 0 && blur_code != 0; }

}  // namespace pd::slow
