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
#include <cstring>
#include <map>

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

// ---- Guest memory (big-endian) --------------------------------------------------------

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
inline void StoreF32(uint8_t* base, uint32_t address, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  StoreU32(base, address, bits);
}

// ---- Its two functions: the Mauler's, slower ----------------------------------------
//
// A weapon definition points at one function definition per function (+0x14 the
// primary, +0x18 the secondary). The Mauler's are two "shoot single" functions (N64
// struct funcdef_shoot, invitems.c invfunc_mauler_singleshot / _chargeshot):
//   +0x18 s8 recoverytime60, +0x1C f32 damage, +0x24..+0x27 s8 the recoil animation's
//   timings (unk24-unk27), +0x3A u16 the shot sound (the Mauler's own is kept).
// When the gun may fire again (N64 bondgun.c, bgun_tick_inc_attacking_shoot's helper):
// once unk24 + unk25 frames (the recoil, 3 + 9 for the Mauler) have passed, a NEW press
// of the trigger fires at once if unk27 >= 0 - that is why the Mauler fires as fast as
// you can click. With unk27 = -1 every shot waits unk24 + unk25 + recoverytime60.
// So the copies get unk27 = -1 and a long recovery time; their damage is lowered too
// (the hit's real work is the slow and the poison).
constexpr uint32_t kDefPrimary = 0x14;
constexpr uint32_t kDefSecondary = 0x18;
constexpr uint32_t kFuncSize = 0x40;  // struct funcdef_shoot is 0x3D bytes
constexpr uint32_t kFuncRecovery = 0x18;
constexpr uint32_t kFuncDamage = 0x1C;
constexpr uint32_t kFuncUnk27 = 0x27;
constexpr int8_t kRecovery60 = 60;       // + the Mauler's 12 frames of recoil = one shot per 1.2 s
constexpr float kDamageFactor = 0.5f;    // of the Mauler's damage (1.2 a shot)

// Gives weapon `number`'s definition (already made by pd::custom::Install) its own
// copies of the two functions. `alloc(size)` returns guest heap memory.
template <typename Alloc>
inline void InstallFunctions(uint8_t* base, uint32_t weapons_table, uint32_t number, Alloc alloc) {
  const uint32_t def = LoadU32(base, weapons_table + number * 4);
  if (!def) return;
  for (const uint32_t slot : {kDefPrimary, kDefSecondary}) {
    const uint32_t original = LoadU32(base, def + slot);
    const uint32_t copy = original ? alloc(kFuncSize) : 0;
    if (!copy) continue;
    std::memcpy(base + copy, base + original, kFuncSize);
    base[copy + kFuncRecovery] = uint8_t(kRecovery60);
    base[copy + kFuncUnk27] = uint8_t(int8_t(-1));
    StoreF32(base, copy + kFuncDamage, LoadF32(base, original + kFuncDamage) * kDamageFactor);
    StoreU32(base, def + slot, copy);
  }
}

// ---- The charge: the whole clip ------------------------------------------------------
//
// bgun_tick_mauler_charge (sub_82175F98) raises a hand's charge (hand+572, a float,
// 0-5) by 0.05 a frame while the secondary is selected and more than one round is
// loaded, and takes ONE round each time the charge passes a whole number
// (`loadedammo[0]--` at 0x821760A4): a full Mauler charge costs 5 rounds. The Slow
// Gun keeps the 0-5 scale (the damage, the colour and the sound follow it) but each
// step takes its share of the rounds left, keeping one for the shot itself: with a
// full clip of 20 that is 4 + 4 + 4 + 4 + 3, and the shot fires the last round.
constexpr uint32_t kHandCharge = 572;       // f32 mm_maulercharge, 0-5
constexpr uint32_t kHandLoadedAmmo = 544;   // s32 loadedammo[0]
constexpr int32_t kChargeSteps = 5;

// The rounds the step up to charge `level` (1-5) takes from `loaded`.
inline int32_t ChargeDrain(int32_t loaded, int32_t level) {
  const int32_t steps_left = kChargeSteps + 1 - std::clamp(level, 1, kChargeSteps);  // this one included
  const int32_t spare = loaded - 1;                                                   // one stays for the shot
  if (spare <= 0) return 0;
  return (spare + steps_left - 1) / steps_left;  // rounded up, so the last step empties the clip
}

// ---- Colours ---------------------------------------------------------------------------
//
// The gun renderer (sub_821761F8) blends a charging Mauler towards red (0xFF00007F,
// `colour_blend` at 0x821767D0, by charge x 50); the Slow Gun blends towards this
// colour instead (0xRRGGBBAA).
inline uint32_t g_charge_colour = 0x9030FF7F;

// ---- Tracers ------------------------------------------------------------------------------
//
// A Mauler shot leaves a short tracer beam (beam_create_for_hand sub_821823A8 fills the
// hand's beam and, in a match, the player's fireslot beam; every PC does this for every
// player's shot). beam_render picks the beam's texture by its weapon number and
// multiplies it by a colour: an unknown number gets the ORANGE texture, which a violet
// colour turns pinkish red (R1b screenshots). So the Slow Gun's beams get the Cyclone's
// number (its texture is BLUE) and are noted by address while beam_create_for_hand
// fills them (the Ricochet Laser's way, pd_ricochet.h); at beam_render's colour join
// (0x82182C90) a noted beam gets this colour, fully opaque, and is drawn wider the more
// the shot was charged (as the Mauler's charged beams are).
constexpr uint8_t kBeamTextureWeapon = 11;  // WEAPON_CYCLONE: beam_render's blue texture
inline uint32_t g_beam_colour = 0xB050FF;   // 0xRRGGBB
inline std::map<uint32_t, float> g_beams;   // beam address -> the shot's charge (0-5)
inline bool g_filling = false;              // beam_create_for_hand is filling a Slow Gun shot's beams
inline float g_fill_charge = 0.0f;
inline uint32_t g_beams_drawn = 0;          // TEMP_SLOW: beams drawn in its colour

// beam_create (each beam it fills): noted for this shot, or no longer a Slow Gun beam.
inline void NoteBeam(uint32_t beam) {
  if (g_filling) {
    g_beams[beam] = g_fill_charge;
  } else {
    g_beams.erase(beam);
  }
}
// The beam's width factor if it is a Slow Gun beam, else 0.
inline float BeamWidth(uint32_t beam) {
  const auto it = g_beams.find(beam);
  return it == g_beams.end() ? 0.0f : 1.0f + it->second;
}

}  // namespace pd::slow
