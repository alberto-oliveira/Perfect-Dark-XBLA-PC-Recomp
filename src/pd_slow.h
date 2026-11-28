// Perfect Dark XBLA PC Recomp - the Slow Gun.
//
// A custom weapon (pd_custom_weapons.h) made from the DY357 Magnum (its model, its
// animations, its ammo), with a laser-like shot and the Mauler's charge as its
// secondary. A player it hits is slowed (movement, aim and fire rate) and made dizzy
// (blur, heavy breathing, a swaying view), and a poison takes some health away over a
// few seconds. The charge drains the whole clip; a charged hit slows for longer and
// is dizzier.
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
#include <atomic>
#include <cmath>
#include <map>
#include <vector>

namespace pd::slow {

constexpr uint8_t kWeaponSlow = 0x44;   // the custom weapon's number (pd_custom_weapons.h)
constexpr uint8_t kWeaponMauler = 6;    // whose charge its secondary is
constexpr uint8_t kWeaponMagnum = 8;    // its base weapon (DY357 Magnum)

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

// ---- Its two functions: the Magnum's shot, slower ------------------------------------
//
// A weapon definition points at one function definition per function (+0x14 the
// primary, +0x18 the secondary). Its definition is a copy of the Magnum's (its base),
// whose primary is a "shoot single" function (N64 struct funcdef_shoot,
// invitems.c invfunc_dy357_singleshot) and whose secondary is the pistol whip:
//   +0x04 u16 the function's name, +0x0C the fire animation (the Magnum's),
//   +0x18 s8 recoverytime60, +0x1C f32 damage, +0x24..+0x27 s8 the recoil animation's
//   timings (unk24-unk27), +0x3A u16 the shot sound.
// Both functions become copies of the Magnum's shot (its animations stay), with the
// Mauler's shot sound (the laser-like one); the secondary takes the name of the Mauler's
// charge ("Charge-up shot"): the charge itself is not in the function but in the
// game's checks for the Mauler, which see the Slow Gun as a Mauler (main.cpp,
// PdHookSlowAsMauler sites). When the gun may fire again (N64 bondgun.c,
// bgun_tick_inc_attacking_shoot's helper): once unk24 + unk25 frames (the recoil) have
// passed, a NEW press of the trigger fires at once if unk27 >= 0. With unk27 = -1 every
// shot waits unk24 + unk25 + recoverytime60. So the copies get unk27 = -1 and a long
// recovery time, and a low damage (the hit's real work is the slow and the poison).
constexpr uint32_t kDefPrimary = 0x14;
constexpr uint32_t kDefSecondary = 0x18;
constexpr uint32_t kFuncSize = 0x40;  // struct funcdef_shoot is 0x3D bytes
constexpr uint32_t kFuncName = 0x04;
constexpr uint32_t kFuncRecovery = 0x18;
constexpr uint32_t kFuncDamage = 0x1C;
constexpr uint32_t kFuncUnk27 = 0x27;
constexpr uint32_t kFuncSound = 0x3A;
constexpr int8_t kRecovery60 = 60;   // + the Magnum's recoil = one shot per ~1.3 s
constexpr float kDamage = 0.6f;      // a shot's damage (the Mauler's does 1.2; full health is about 8)

// Gives weapon `number`'s definition (already made by pd::custom::Install, a copy of the
// Magnum's) its own two functions. `alloc(size)` returns guest heap memory.
template <typename Alloc>
inline void InstallFunctions(uint8_t* base, uint32_t weapons_table, uint32_t number, Alloc alloc) {
  const uint32_t def = LoadU32(base, weapons_table + number * 4);
  const uint32_t mauler = LoadU32(base, weapons_table + kWeaponMauler * 4);
  if (!def || !mauler) return;
  const uint32_t shot = LoadU32(base, def + kDefPrimary);          // the Magnum's shot
  const uint32_t mauler_shot = LoadU32(base, mauler + kDefPrimary);
  const uint32_t mauler_charge = LoadU32(base, mauler + kDefSecondary);
  if (!shot || !mauler_shot || !mauler_charge) return;
  for (const uint32_t slot : {kDefPrimary, kDefSecondary}) {
    const uint32_t copy = alloc(kFuncSize);
    if (!copy) continue;
    std::memcpy(base + copy, base + shot, kFuncSize);
    base[copy + kFuncRecovery] = uint8_t(kRecovery60);
    base[copy + kFuncUnk27] = uint8_t(int8_t(-1));
    StoreF32(base, copy + kFuncDamage, kDamage);
    std::memcpy(base + copy + kFuncSound, base + mauler_shot + kFuncSound, 2);
    if (slot == kDefSecondary) std::memcpy(base + copy + kFuncName, base + mauler_charge + kFuncName, 2);
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
// full clip (the Magnum's 6) that is 1 + 1 + 1 + 1 + 1, and the shot fires the last round.
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
// A Magnum shot leaves a short tracer beam (beam_create_for_hand sub_821823A8 fills the
// hand's beam and, in a match, the player's fireslot beam; every PC does this for every
// player's shot). beam_render picks the beam's texture and width by its weapon number
// and multiplies it by a colour: an unknown number gets the thin ORANGE texture, which a
// violet colour turns pinkish red (R1b screenshots). So the Slow Gun's beams get the
// Laser's number (the Laser's texture, width 50 and long streak: a laser-like shot) and
// are noted by address while beam_create_for_hand fills them (the Ricochet Laser's way,
// pd_ricochet.h); at beam_render's colour join (0x82182C90) a noted beam gets this
// colour, fully opaque, g_beam_width % as wide, and wider still the more the shot was
// charged (as the Mauler's charged beams are).
constexpr uint8_t kBeamTextureWeapon = 29;  // WEAPON_LASER: the Laser's beam
inline uint32_t g_beam_colour = 0xB050FF;   // 0xRRGGBB
inline int32_t g_beam_width = 200;          // % of the Laser's width (pd_slow_beam_width)
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
// The beam's width factor if it is a Slow Gun beam, else 0: g_beam_width %, and up to 3.5
// times that for a full charge (5).
inline float BeamWidth(uint32_t beam) {
  const auto it = g_beams.find(beam);
  return it == g_beams.end() ? 0.0f : float(g_beam_width) / 100.0f * (1.0f + it->second * 0.5f);
}

// ---- The slow (on the victim's own PC) ------------------------------------------------
//
// Each PC keeps the slow of its own player only (players[0]): a player's movement, aim
// and gun run on that player's PC, and the others see the result.
//   - Movement and stick aim: the player's move tick (sub_820F4D40) runs the input step
//     (`bl 0x820f2988` at 0x820F4DB0) with g_Vars' frame clock (+52 lvupdate240, +56
//     lvupdate60, +76 lvupdate60freal), which combat boost doubles for a boosted player.
//     It saves the clock first and puts it back at 0x820F56C4, so the slow just scales
//     the clock at 0x820F4DB0 (on top of a boost: a boost cancels a 50% slow).
//   - Fire rate: the gun ticks elsewhere, in bgun_tick_gameplay2 (sub_8217CD40), called
//     from the player's per-frame function sub_820FC6E0 at 0x820FC798; the clock is
//     scaled before that call and put back after it (0x820FC79C).
//   - Mouse aim: the port's mouse look (perfect_dark_app.h OnMouseLookDelta) turns by
//     less (g_aim_factor).
//   - Dizziness, three ways:
//       blur: the player's chr blur amount (chr+728, the Tranquilizer's dizziness; the
//         game wears it off, and draws it up to amount 5000) is topped up each frame;
//       breathing: player+416 (N64 bondbreathing, +0x198 + 8: how out of breath the
//         player is, 0-1; the game makes the head bob and the gun sway up to 6 times as
//         much with it, bondhead.c) is held up;
//       sway: the view drifts to and fro, a slow wave on the turn and a faster one on the
//         pitch (as the game sways a drugged bot's aim, bot.c), through the port's mouse
//         look path (perfect_dark_app.h AddLookTurn), so the player can fight it.
// The timer only runs while the player is alive, so a slow carries into the next life.

// The settings (the host's, in a match: step 6). Percentages are of normal speed.
struct Rules {
  float seconds = 15.0f;       // a primary hit's slow
  float full_charge = 2.5f;    // a fully charged hit's slow lasts this much longer
  int32_t move = 50;           // movement (and stick aim) speed, %
  int32_t aim = 30;            // mouse aim speed, %
  int32_t fire = 50;           // gun speed (fire rate, charge, reload), %
  int32_t blur = 3000;         // the blur kept up while slowed (chr+728; the game draws up to 5000)
  int32_t blur_full = 5000;    // ... after a fully charged hit
  float sway = 4.0f;           // the view's sway, degrees each way (the pitch: 60% of it)
  float sway_full = 10.0f;     // ... after a fully charged hit
  float poison_seconds = 8.0f; // how long the poison lasts (one tick a second)
  float poison_damage = 0.3f;  // the damage of each tick (chr_damage units; full health is about 8)
};
inline Rules g_rules;

// The host's rules (a slow and its poison are applied on the victim's own PC, so every PC
// must use the same ones: online, the host's). The host sends them as a host blob
// (netplay::SetHostBlob, as the Detonator Crossbow's and the Ricochet Laser's) and each
// joined PC uses what it last heard. Blob: [version], then u16s (big-endian): seconds x10,
// full charge x10, move, aim, fire, blur, blur after a full charge, poison seconds x10,
// poison damage x100, sway x10, sway after a full charge x10.
constexpr uint32_t kBlobKey = 0x534C4F57;  // "SLOW"
constexpr uint8_t kBlobVersion = 2;
constexpr int kBlobValues = 11;

inline bool operator!=(const Rules& a, const Rules& b) {
  return a.seconds != b.seconds || a.full_charge != b.full_charge || a.move != b.move || a.aim != b.aim ||
         a.fire != b.fire || a.blur != b.blur || a.blur_full != b.blur_full ||
         a.poison_seconds != b.poison_seconds || a.poison_damage != b.poison_damage || a.sway != b.sway ||
         a.sway_full != b.sway_full;
}

inline std::vector<uint8_t> EncodeBlob(const Rules& r) {
  const int32_t values[] = {int32_t(r.seconds * 10.0f + 0.5f), int32_t(r.full_charge * 10.0f + 0.5f), r.move, r.aim,
                            r.fire, r.blur, r.blur_full, int32_t(r.poison_seconds * 10.0f + 0.5f),
                            int32_t(r.poison_damage * 100.0f + 0.5f), int32_t(r.sway * 10.0f + 0.5f),
                            int32_t(r.sway_full * 10.0f + 0.5f)};
  std::vector<uint8_t> blob{kBlobVersion};
  for (const int32_t v : values) {
    const uint32_t u = uint32_t(std::clamp(v, 0, 0xFFFF));
    blob.push_back(uint8_t(u >> 8));
    blob.push_back(uint8_t(u));
  }
  return blob;
}
inline bool DecodeBlob(const std::vector<uint8_t>& blob, Rules* r) {
  if (blob.size() < 1 + kBlobValues * 2 || blob[0] != kBlobVersion) return false;
  auto at = [&](int i) { return int32_t((blob[1 + i * 2] << 8) | blob[2 + i * 2]); };
  r->seconds = float(at(0)) / 10.0f;
  r->full_charge = float(at(1)) / 10.0f;
  r->move = at(2);
  r->aim = at(3);
  r->fire = at(4);
  r->blur = at(5);
  r->blur_full = at(6);
  r->poison_seconds = float(at(7)) / 10.0f;
  r->poison_damage = float(at(8)) / 100.0f;
  r->sway = float(at(9)) / 10.0f;
  r->sway_full = float(at(10)) / 10.0f;
  return true;
}

// The local player's slow.
struct State {
  float left60 = 0.0f;  // 60ths of a second of slow left (counts down while alive)
  uint32_t tier = 0;    // the hit that started it (kTierShot...)
  int32_t attacker = -1;  // who hit them (this PC's player number), for the poison's credit
  float poison_left60 = 0.0f;  // 60ths of a second of poison left (death ends it)
  float poison_next60 = 0.0f;  // 60ths of a second to the next tick
  int32_t poison_due = 0;      // ticks to apply (the frame tick counts, the player tick applies)
};
inline State g_state;

// True while the poison's own damage call runs: chr_damage then sends a hit with the
// Slow Gun's gset, which must not be marked as a new Slow Gun hit.
inline bool g_poisoning = false;

inline bool Active() { return g_state.left60 > 0.0f; }

// How much longer a hit of `tier` slows than a primary shot.
inline float TierLength(const Rules& r, uint32_t tier) {
  if (tier >= kTierFull) return r.full_charge;
  if (tier == kTierCharged) return (1.0f + r.full_charge) / 2.0f;
  return 1.0f;
}
inline int32_t TierBlur(const Rules& r, uint32_t tier) {
  if (tier >= kTierFull) return r.blur_full;
  if (tier == kTierCharged) return (r.blur + r.blur_full) / 2;
  return r.blur;
}
inline float TierSway(const Rules& r, uint32_t tier) {
  if (tier >= kTierFull) return r.sway_full;
  if (tier == kTierCharged) return (r.sway + r.sway_full) / 2.0f;
  return r.sway;
}
// The breathing held up (0-1): half for a shot, full for a full charge.
inline float TierBreathing(uint32_t tier) {
  if (tier >= kTierFull) return 1.0f;
  if (tier == kTierCharged) return 0.75f;
  return 0.5f;
}

// The view's sway: an offset (degrees) that follows two slow waves; each frame the view
// turns by how much the offset changed, so the player's own aiming still adds up. Its
// size fades in and out with the slow, so the view ends where the player left it.
struct Sway {
  float t60 = 0.0f;               // 60ths of a second of sway so far
  float theta = 0.0f, verta = 0.0f;  // the offset given so far
};
inline Sway g_sway;
// The offset at time t60 for a sway of `degrees`: the turn on a 3.2 s wave, the pitch
// (60% as big) on a 1.9 s one.
inline void SwayOffset(float t60, float degrees, float* theta, float* verta) {
  constexpr float kTwoPi = 6.2831853f;
  *theta = degrees * std::sin(kTwoPi * t60 / (3.2f * 60.0f));
  *verta = degrees * 0.6f * std::sin(kTwoPi * t60 / (1.9f * 60.0f) + 1.0f);
}

// A Slow Gun hit on the local player: the slow starts again from its full length (a
// new hit does not add to what is left, but a stronger one is not cut short by a
// weaker one).
inline void Start(uint32_t tier, int32_t attacker) {
  const float length = g_rules.seconds * 60.0f * TierLength(g_rules, tier);
  if (length >= g_state.left60) {
    g_state.left60 = length;
    g_state.tier = tier;
  }
  g_state.attacker = attacker;
  // The poison starts again too: a tick a second from now, for poison_seconds.
  g_state.poison_left60 = g_rules.poison_seconds * 60.0f;
  g_state.poison_next60 = 60.0f;
}

// The poison's clock for one frame of `dt60` 60ths (the player alive): counts the ticks
// that fall due; they are applied from the player's own tick (main.cpp, PdSlowPoison).
inline void PoisonClock(float dt60) {
  State& s = g_state;
  if (s.poison_left60 <= 0.0f) return;
  s.poison_left60 -= dt60;
  s.poison_next60 -= dt60;
  if (s.poison_next60 <= 0.0f) {
    ++s.poison_due;
    s.poison_next60 += 60.0f;
  }
}

// The speed factor (0-1) for a percentage while slowed, 1 otherwise.
inline float Factor(int32_t percent) {
  if (!Active()) return 1.0f;
  return std::clamp(percent, 5, 100) / 100.0f;
}

// For the mouse look, read on the input thread.
inline std::atomic<float> g_aim_factor{1.0f};

// Scales g_Vars' frame clock by `factor`. The two integer counts keep their remainders
// from frame to frame, so a 50% clock alternates lvupdate60 1, 0, 1, 0... instead of
// rounding every frame down to 0.
struct ClockScaler {
  float rest240 = 0.0f, rest60 = 0.0f;
  int32_t saved240 = 0, saved60 = 0;
  float saved_freal = 0.0f;
};
constexpr uint32_t kGVars = 0x82648BF8;
constexpr uint32_t kClock240 = kGVars + 52, kClock60 = kGVars + 56, kClockFreal = kGVars + 76;

inline int32_t ScaleCount(int32_t value, float factor, float* rest) {
  const float exact = float(value) * factor + *rest;
  const int32_t whole = int32_t(exact);
  *rest = exact - float(whole);
  return whole;
}
inline void ScaleClock(uint8_t* base, float factor, ClockScaler* c) {
  c->saved240 = int32_t(LoadU32(base, kClock240));
  c->saved60 = int32_t(LoadU32(base, kClock60));
  c->saved_freal = LoadF32(base, kClockFreal);
  StoreU32(base, kClock240, uint32_t(ScaleCount(c->saved240, factor, &c->rest240)));
  StoreU32(base, kClock60, uint32_t(ScaleCount(c->saved60, factor, &c->rest60)));
  StoreF32(base, kClockFreal, c->saved_freal * factor);
}
inline void RestoreClock(uint8_t* base, const ClockScaler& c) {
  StoreU32(base, kClock240, uint32_t(c.saved240));
  StoreU32(base, kClock60, uint32_t(c.saved60));
  StoreF32(base, kClockFreal, c.saved_freal);
}
inline ClockScaler g_move_clock, g_gun_clock;
inline bool g_gun_clock_scaled = false;
inline uint32_t g_move_scaled = 0, g_gun_scaled = 0, g_gun_calls = 0;  // TEMP_SLOW: hook counts

// ---- Who is slowed, as each PC sees it --------------------------------------------------
//
// A slowed player is drawn with a sickly overlay on every PC. Only the victim's PC gets
// the hit message, but every PC replays every player's shot (shot_create runs there too,
// from the shooter's hand, and shot_calculate_hits finds what it hit: 0x821FC9B8, r3 = the
// prop hit), so each PC marks a player hit by a Slow Gun shot itself, for the slow's length
// by the shot's charge. It can disagree with the victim's PC on a shot that barely hit
// or missed: it is only the look. The mark, like the slow, waits while its player is dead.
inline float g_marked60[8] = {};      // by this PC's player number: 60ths of a second left
inline uint32_t g_player_tint = 0x80FF30;  // 0xRRGGBB (pd_slow_player_tint), or kNoTint
inline uint32_t g_player_tint_strength = 50;  // %

inline uint32_t TierOfCharge(float charge) {
  if (charge < 1.0f) return kTierShot;
  return charge >= 5.0f ? kTierFull : kTierCharged;
}
inline void MarkSlowed(int player, uint32_t tier) {
  if (player < 0 || player >= 8) return;
  g_marked60[player] = std::max(g_marked60[player], g_rules.seconds * 60.0f * TierLength(g_rules, tier));
}

}  // namespace pd::slow
