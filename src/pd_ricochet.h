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

#include <cmath>
#include <cstdint>
#include <set>

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

// One straight part of a shot's path: bounce `bounce` (0 = the shot itself).
struct Segment {
  Vec from, to;
  int bounce;
};

constexpr float kPulseRange = 20000.0f;   // how far a pulse (or one bounce of it) is followed
constexpr float kStreamRange = 300.0f;    // the Stream's whole path, bounces included (the game's 300.0 at 0x82000B08)
constexpr float kOffWall = 2.0f;          // a bounce starts this far off the wall, so its own wall is not hit again
constexpr int kMaxBounces = 10;

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

}  // namespace pd::ricochet
