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

// True if `beam` was filled for a Ricochet Laser shot.
inline bool IsRicochetBeam(uint32_t beam) { return g_beams.count(beam) != 0; }

// TEMP_RICO: counters for the debug log (pd_rico_debug, main.cpp): Ricochet Laser
// beams filled and drawn since the last log line, and the current player number
// (g_Vars+1196) when one was last filled (0 = this PC's player shot).
inline int g_debug_filled = 0;
inline int g_debug_drawn = 0;
inline int g_debug_filled_player = -1;

}  // namespace pd::ricochet
