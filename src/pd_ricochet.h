// Perfect Dark XBLA PC Recomp - the Ricochet Laser.
//
// A custom weapon (pd_custom_weapons.h) made from the Laser and drawn as a
// white Mauler. Its shots bounce off walls; each bounce raises the damage, and
// a pulse that hits a player after enough bounces kills outright. Its beams are
// red.
//
// How the game draws a Laser beam (N64 gunfx.c, verified in the XBLA code):
//   - A beam is a 44-byte struct (N64 struct beam): +0 s8 age (-1 = not in use),
//     +1 s8 weapon (29 = the Laser's pulse, -2 = its Stream), +4 from, +16 dir,
//     +28 maxdist, +32 speed, +36 mindist, +40 dist. It is a short streak that
//     travels from `from` along `dir`: beam_tick (sub_82183688) moves it on
//     each frame and frees it at maxdist.
//   - When a player fires, beam_create_for_hand (sub_821823A8) fills two beams
//     with beam_create (sub_82182130): the hand's own (drawn by the gun
//     renderer for the player who fired), and in a match the player's fireslot
//     beam (g_Fireslots, drawn by the chr renderer for the other players).
//   - beam_render (sub_82182A30) picks the texture by the beam's weapon number
//     and writes the beam's colour (0xFFFFFFFF for the Laser) into the display
//     list; all its branches join at 0x82182C90.
// So the mod notes which beams beam_create_for_hand fills for a Ricochet Laser
// (they get the Laser's number, so beam_render draws them as Laser beams) and
// colours just those at 0x82182C90.
//
// Header-only, C++17. The game glue (settings, hooks) is in main.cpp and
// perfect_dark_hooks.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace pd::ricochet {

constexpr uint8_t kWeaponRicochet = 0x52;  // the custom weapon's number (pd_custom_weapons.h)
constexpr uint8_t kWeaponLaser = 29;

constexpr uint32_t kPlayerWeapon = 5544;  // s8: the weapon a player holds

// ---- Red beams ---------------------------------------------------------------------

// The beams beam_create_for_hand last filled for a player holding the Ricochet
// Laser, by address. A beam's address is reused for each shot of its hand (or
// fireslot), so it is added or removed at each fill.
inline std::set<uint32_t> g_beams;
// True while beam_create_for_hand fills the beams of a Ricochet Laser shot.
inline bool g_filling = false;

// The beams' colour (0xRRGGBB, or pd::custom::kNoTint for the Laser's own),
// set every frame from pd_rico_beam_tint.
inline uint32_t g_beam_tint = 0xFF3030;

// True if `beam` was filled for a Ricochet Laser shot (bounce beams included).
inline bool InPool(uint32_t beam);
inline bool IsRicochetBeam(uint32_t beam) { return InPool(beam) || g_beams.count(beam) != 0; }

// ---- Bounces ------------------------------------------------------------------------
//
// Every PC works out the bounces of every player's shot itself: XBLA replays a
// remote player's shot on each PC (shot_create runs there too, from the hand),
// so no network message is needed. Right after shot_create's shot_calculate_hits
// (0x821FC9B8), if the shot did not end on a prop (a player, an object), the
// line it was fired along is followed to the wall it meets
// (cd_test_los_oobok_findclosest_autoflags sub_820E4E88, then the wall's point
// sub_820DDDC0 and normal sub_820E7070), reflected (r = d - 2 (d.n) n), and
// followed again, up to g_bounces times. The positions differ a little from PC
// to PC (a remote shot starts at the hand, ~30 units from the eye).
//
// The first segment is the game's own shot (its beam, its hit). Each bounce is a
// segment of its own: drawn as a Laser beam from our pool, and (step 4) tested
// against the players on the shooter's PC.

struct Vec {
  float x, y, z;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }

// The reflection of direction `d` off a surface with unit normal `n`.
inline Vec Reflect(Vec d, Vec n) { return d - n * (2.0f * Dot(d, n)); }

constexpr int kMaxBounces = 10;

// One straight part of a shot's path: bounce `bounce` (0 = the shot itself).
// `victim` = the player (this PC's numbering) it ends on, or -1; `wall_end` =
// it ends on a wall whose normal is `normal` (else in the air or on a player).
struct Segment {
  Vec from, to;
  int bounce = 0;
  int victim = -1;
  bool wall_end = false;
  Vec normal{};
};

// ---- Hitting players ------------------------------------------------------------------
//
// A bounce is tested against every player, the shooter included (a bounce
// coming back can hit its own shooter). A player is a vertical capsule: its axis
// from the feet (the prop's position is at eye height: feet = eye - eye height)
// to just above the eyes, kPlayerRadius thick. The first player a bounce meets
// ends the shot there. Every PC tests (so each draws the shot ending at the
// player); only the shooter's PC applies the damage (the game's rule for
// gunfire: the hit then reaches the victim's PC through the network).
constexpr float kPlayerRadius = 30.0f;
constexpr float kPlayerAboveEyes = 10.0f;

// Closest approach of segment AB to segment CD: returns the squared distance
// and sets `s` to the position along AB (0 = A, 1 = B) where it happens.
// (The standard clamped closest-points computation for two segments.)
inline float SegmentDistance2(Vec a, Vec b, Vec c, Vec d, float* s) {
  const Vec d1 = b - a, d2 = d - c, r = a - c;
  const float aa = Dot(d1, d1), ee = Dot(d2, d2), f = Dot(d2, r);
  float sc = 0.0f, tc = 0.0f;
  if (aa <= 1e-6f && ee <= 1e-6f) {
    sc = tc = 0.0f;
  } else if (aa <= 1e-6f) {
    tc = std::clamp(f / ee, 0.0f, 1.0f);
  } else {
    const float cc = Dot(d1, r);
    if (ee <= 1e-6f) {
      sc = std::clamp(-cc / aa, 0.0f, 1.0f);
    } else {
      const float bb = Dot(d1, d2), denom = aa * ee - bb * bb;
      sc = denom > 1e-6f ? std::clamp((bb * f - cc * ee) / denom, 0.0f, 1.0f) : 0.0f;
      tc = (bb * sc + f) / ee;
      if (tc < 0.0f) {
        tc = 0.0f;
        sc = std::clamp(-cc / aa, 0.0f, 1.0f);
      } else if (tc > 1.0f) {
        tc = 1.0f;
        sc = std::clamp((bb - cc) / aa, 0.0f, 1.0f);
      }
    }
  }
  *s = sc;
  const Vec diff = (a + d1 * sc) - (c + d2 * tc);
  return Dot(diff, diff);
}

// ---- Damage -------------------------------------------------------------------------
//
// A bounce's hit does the function's damage (the Laser's: pulse 1.0, Stream
// 0.1) plus g_growth percent of it for each bounce before the hit; a pulse that
// hits after g_kill_bounce bounces or more kills (0 = never). A shield takes a
// whole hit (N64 chr_damage: damage 0 once it absorbed one), so a kill is two
// hits: the first takes the shield, the second kills.
constexpr float kKillDamage = 100.0f;

inline int g_growth = 50;      // percent of the base damage added per bounce (pd_rico_damage_growth)
inline int g_kill_bounce = 3;  // pd_rico_kill_bounce

inline float BounceDamage(float base_damage, int bounce) {
  return base_damage * (1.0f + float(g_growth) / 100.0f * float(bounce));
}
inline bool BounceKills(int bounce, bool stream) { return !stream && g_kill_bounce > 0 && bounce >= g_kill_bounce; }

// ---- The host's rules -----------------------------------------------------------------
//
// Bounces are drawn on every PC and damage is worked out on the shooter's PC, so
// every PC must use the same rules: online, the host's. It sends them as a host
// blob (netplay::SetHostBlob, as the Detonator Crossbow's explosion) and each
// joined PC uses what it last heard. Blob: [version][bounces][growth][kill bounce].
constexpr uint32_t kBlobKey = 0x5249434F;  // "RICO"
constexpr uint8_t kBlobVersion = 1;

struct Rules {
  int bounces = 3, growth = 50, kill_bounce = 3;
  bool operator!=(const Rules& o) const {
    return bounces != o.bounces || growth != o.growth || kill_bounce != o.kill_bounce;
  }
};

inline std::vector<uint8_t> EncodeBlob(const Rules& r) {
  return {kBlobVersion, uint8_t(r.bounces), uint8_t(r.growth), uint8_t(r.kill_bounce)};
}
inline bool DecodeBlob(const std::vector<uint8_t>& blob, Rules* r) {
  if (blob.size() < 4 || blob[0] != kBlobVersion) return false;
  r->bounces = std::min<int>(blob[1], kMaxBounces);
  r->growth = blob[2];
  r->kill_bounce = blob[3];
  return true;
}

constexpr float kPulseRange = 20000.0f;   // how far a pulse (or one bounce of it) is followed
constexpr float kStreamRange = 300.0f;    // the Stream's whole path, bounces included (the game's 300.0 at 0x82000B08)
constexpr float kOffWall = 2.0f;          // a bounce starts this far off the wall, so its own wall is not hit again

// What a bounce can hit: the stage's walls and floors (CDTYPE_BG 0x20) and doors
// (CDTYPE_DOORS 0x02), as the research probe used.
constexpr uint32_t kCdTypeWalls = 0x20 | 0x02;

// How many times a shot bounces (pd_rico_bounces; the host's in a network match: step 4).
inline int g_bounces = 3;

// ---- Bounce beams ---------------------------------------------------------------------
//
// Each bounce is drawn as a Laser beam of our own: kPoolBeams beams in guest
// memory, filled with the game's beam_create, moved on by beam_tick once a frame
// and drawn by beam_render, from a hook in the player's render just after the
// crossbow bolt trails (0x820FC7A4: r3 = the display list), before the gun.
//
// A pulse is a short streak that crosses its beam in a fixed time (beam_create
// sets its speed in proportion to its length), so a bounce's streak starts once
// the streaks before it have crossed theirs: its `dist` starts that many frames'
// worth below 0, and it is drawn only once its tail has left the wall. The
// Stream's beams are drawn whole for two frames (beam_tick frees them by age),
// and the Stream fires every frame, so its bounces are just made at each shot.
constexpr uint32_t kBeamSize = 44;
constexpr uint32_t kBeamAge = 0;       // s8: -1 = free
constexpr uint32_t kBeamWeapon = 1;    // s8: 29 pulse, -2 Stream
constexpr uint32_t kBeamMaxDist = 28;  // f32
constexpr uint32_t kBeamSpeed = 32;    // f32
constexpr uint32_t kBeamMinDist = 36;  // f32: the streak's length
constexpr uint32_t kBeamDist = 40;     // f32: how far its head has gone
constexpr int kPoolBeams = 64;
constexpr int8_t kBeamStream = -2;

// What each pool beam is (kept on our side, by pool index). A pulse's bounce hits
// a player when its STREAK reaches the player, not when it is fired (the user's
// test: a third bounce arrives ~0.25 s after the shot, so a player walking into
// it was visibly hit but had not been there at the shot). Each frame the part of
// the bounce the streak newly covered is tested against the players where they
// are then; a hit ends the streak there and drops the shot's later bounces.
// The streak reaching a wall makes sparks there, as the shot's own hit does.
struct BeamInfo {
  bool active = false;
  uint32_t shot = 0;        // which shot it belongs to (its later bounces share it)
  uint32_t shooter = 0;     // the shooter's player struct
  int bounce = 0;
  bool stream = false;
  uint8_t function = 0;
  Vec from{}, dir{};        // the bounce: from, unit direction, length
  float length = 0.0f;
  float tested = 0.0f;      // how far along it players have been tested
  bool wall_end = false;    // it ends on a wall (normal), not in the air
  Vec normal{};
  bool sparked = false;
};
inline BeamInfo g_info[kPoolBeams];
inline uint32_t g_next_shot = 1;

// Hand fields: hands[2] at player+1632, 1956 bytes each; +1372 = where the hand's
// last shot hit (bgun_set_hit_pos sub_821776F0: player+3004 right, +4960 left;
// the hand's beam ends there, and the game's sparks are made there).
constexpr uint32_t kHandHitPos = 1372;

// sparks_create (sub_8215CD58: room, prop, position, direction, normal, type) with
// the game's default type, as shot_calculate_hits calls it for a wall hit.
constexpr uint32_t kSparkTypeDefault = 0;

// Where a shot's first bounce starts is probed this far either side of the
// game's hit point along the shot.
constexpr float kProbe = 30.0f;

inline uint32_t g_pool = 0;  // guest address of the pool (kPoolBeams beams, then a scratch beam)
inline uint32_t PoolBeam(int i) { return g_pool + uint32_t(i) * kBeamSize; }
inline uint32_t ScratchBeam() { return PoolBeam(kPoolBeams); }
inline bool InPool(uint32_t beam) { return g_pool && beam >= g_pool && beam < PoolBeam(kPoolBeams); }

// TEMP_RICO: how fast the bounce beams move, in percent (pd_rico_beam_speed), so
// screenshots can catch them.
inline int g_debug_beam_speed = 100;

// TEMP_RICO: counters for the debug log (pd_rico_debug, main.cpp): Ricochet Laser
// beams filled and drawn since the last log line, and the current player number
// (g_Vars+1196) when one was last filled (0 = this PC's player shot).
inline int g_debug_filled = 0;
inline int g_debug_drawn = 0;
inline int g_debug_filled_player = -1;
inline int g_debug_bounces = 0;  // bounce segments made
inline int g_debug_pool_full = 0;  // bounce beams not drawn: no free beam in the pool
inline double g_debug_shot_us = 0;  // time spent in the shot hook (microseconds)
inline int g_debug_probe_hits = 0;   // first bounces found at the game's hit point
inline int g_debug_eye_hits = 0;     // ... found only by the line from the eye
inline int g_debug_no_wall = 0;      // shots that ended on no wall (no bounce)

// The beams' width in percent of the Laser's (pd_rico_beam_width, default 200).
inline int g_beam_width = 200;

}  // namespace pd::ricochet
