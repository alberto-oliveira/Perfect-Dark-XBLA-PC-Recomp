// Debug code of the Ricochet Laser / Carom LX (branch ricochet-laser,
// src/pd_ricochet.h). Not built; kept only so the code can be recovered.
//
// Removed in: the cleanup commit that added this file
// ("Ricochet Laser: move the TEMP_RICO debug code to the debug bank").
//
// What for (cvars pd_rico_debug*, pd_rico_beam_speed):
// - pd_rico_debug (default on while developing): [rico] log lines:
//     - once a second: Ricochet Laser beams filled (and by which player, this
//       PC's numbering) and drawn, bounce beams made / not made (pool full), the
//       shot hook's time, and how each shot's first wall was found (by the
//       bullets' test, by collision only, none) - the counts that showed the
//       35% / 54-pulse "no bounce" problems of the user's hands-on checks;
//     - each pulse: its path (from, direction, the game's hit point, every
//       bounce segment, "(air)" when it ends nowhere);
//     - each hit: victim, bounces, damage, kill (two hits), shield on this PC;
//     - each change of the function (pulse/Stream) a player's Laser shots have
//       on this PC (found the remote function-bit bug, test R2);
// - pd_rico_beam_speed: bounce beams' speed in percent (slow, for screenshots);
// - pd_rico_debug_shield <gamertag>: that player's chr gets a full shield on
//   this PC (chr+0x180 = 8), once (test R3: a kill through a shield);
// - PdRicoDebugFlash: where the gun loader hook found the Mauler's flash colour
//   entry (the loaded file's layout differs from the dump).
// - A timing wrapper around the shot hook (PdRicoShot -> PdRicoShotImpl).
//
// No hooks or toml entries of their own: they lived inside the Ricochet Laser's
// functions, listed below by file with where each line went.
//
// The scenarios that used these (ricochet-bounce, ricochet-hits, ricochet-stream,
// ricochet-tex, ricochet-high) are in tools/debug-bank/ricochet-laser-scenarios/.

// ============================================================================
// src/pd_ricochet.h (after g_beam_width)
// ============================================================================

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
inline int g_debug_probe_hits = 0;   // first walls found by the bullets' test (visible geometry)
inline int g_debug_eye_hits = 0;     // ... found only by the collision geometry
inline int g_debug_no_wall = 0;      // shots that ended on no wall (no bounce)

// ============================================================================
// src/perfect_dark_hooks.cpp
// ============================================================================

// Declaration next to PdRicoShot's:
void PdRicoDebugFlash(uint8_t* base, uint32_t file, uint32_t at, uint32_t value);  // TEMP_RICO

// In PdHookCustomGunFileLoaded, after FindFlashColour:
//   PdRicoDebugFlash(base, r30.u32, entry, rgba);  // TEMP_RICO

// In PdHookRicoBeamCreate, when a beam is noted:
//   ++rico::g_debug_filled;  // TEMP_RICO
//   rico::g_debug_filled_player = int32_t(pd::custom::LoadU32(HookBase(), 0x82648BF8 + 1196));

// In PdHookRicoBeamColour, after the IsRicochetBeam check:
//   ++rico::g_debug_drawn;  // TEMP_RICO

// ============================================================================
// src/main.cpp
// ============================================================================

// With the other pd_rico_* settings:
REXCVAR_DEFINE_INT32(pd_rico_beam_speed, 100, "PerfectDark",
                     "TEMP_RICO: how fast the Ricochet Laser's bounce beams move, in percent (to see them in "
                     "screenshots)");

// TEMP_RICO: debug log for the Ricochet Laser's development, once a second while
// something happened: its beams filled (and by which player, in this PC's
// numbering) and drawn.
REXCVAR_DEFINE_BOOL(pd_rico_debug, true, "PerfectDark", "TEMP_RICO: Ricochet Laser debug log lines ([rico])");
namespace {
int rico_debug_frames = 0;
void PdRicoDebugFrame() {
  namespace rico = pd::ricochet;
  if (++rico_debug_frames < 60) return;
  rico_debug_frames = 0;
  if (REXCVAR_GET(pd_rico_debug) && (rico::g_debug_filled || rico::g_debug_drawn || rico::g_debug_bounces)) {
    REXLOG_INFO("[rico] last second: {} beams filled (last by player {}), {} drawn; {} beams noted; {} bounce "
                "beams made, {} not (pool full); shot hook {:.0f} us; first walls: {} by the bullet test, {} by "
                "collision only, {} none",
                rico::g_debug_filled, rico::g_debug_filled_player, rico::g_debug_drawn, rico::g_beams.size(),
                rico::g_debug_bounces, rico::g_debug_pool_full, rico::g_debug_shot_us, rico::g_debug_probe_hits,
                rico::g_debug_eye_hits, rico::g_debug_no_wall);
  }
  rico::g_debug_shot_us = 0;
  rico::g_debug_probe_hits = rico::g_debug_eye_hits = rico::g_debug_no_wall = 0;
  rico::g_debug_filled = 0;
  rico::g_debug_drawn = 0;
  rico::g_debug_bounces = 0;
  rico::g_debug_pool_full = 0;
}
}  // namespace

// TEMP_RICO: pd_rico_debug_shield = a gamertag: that player's chr gets a full
// shield (chr+0x180 = 8, N64 chr_set_shield's full amount) on this PC, once.
REXCVAR_DEFINE_STRING(pd_rico_debug_shield, "", "PerfectDark", "TEMP_RICO: give this player a full shield here");
namespace {
void PdRicoDebugShield(uint8_t* base) {
  namespace pl = pd::player;
  const std::string who = REXCVAR_GET(pd_rico_debug_shield);
  if (who.empty()) return;
  REXCVAR_SET(pd_rico_debug_shield, std::string());
  const int n = pl::PlayerByName(base, who);
  const uint32_t prop = n >= 0 ? pl::PropOf(base, pl::Player(base, n)) : 0;
  const uint32_t chr = prop ? pl::LoadU32(base, prop + 4) : 0;
  if (!chr) {
    REXLOG_WARN("[rico] debug shield: no player {}", who);
    return;
  }
  pl::StoreF32(base, chr + 0x180, 8.0f);
  REXLOG_INFO("[rico] debug shield: player {} ({}) has a full shield here", n, who);
}
}  // namespace

// TEMP_RICO: where a newly loaded gun file's flash colour entry was found.
void PdRicoDebugFlash(uint8_t* base, uint32_t file, uint32_t at, uint32_t rgba) {
  REXLOG_INFO("[rico] flash colour: gun file 0x{:08X}: entry {}, set to {:08X}", file,
              at ? fmt::format("at +0x{:X}", at - file) : std::string("not found"), rgba);
}

// In PdRicoFrame: first line PdRicoDebugShield(base); then
//   rico::g_debug_beam_speed = std::clamp(REXCVAR_GET(pd_rico_beam_speed), 1, 1000);
// and last line PdRicoDebugFrame();

// In RicoFirstWall: ++rico::g_debug_probe_hits when the bullets' test found the
// wall, ++rico::g_debug_eye_hits when only a collision test did, else
// ++rico::g_debug_no_wall.

// In RicoDamage, before the hits:
//   const float before_shield = pl::LoadF32(base, chr + 0x180);
// and after them:
//   if (REXCVAR_GET(pd_rico_debug)) {  // TEMP_RICO
//     REXLOG_INFO("[rico] hit: player {} ({}) after {} bounce(s), {} damage {:.2f}{}; shield here {:.2f} -> {:.2f}",
//                 victim, victim == 0 ? "the shooter" : pd::crossbow::PlayerName(base, victim), bounce,
//                 stream ? "Stream" : "pulse", damage, kill ? " (kill: two hits)" : "", before_shield,
//                 pl::LoadF32(base, chr + 0x180));
//   }

// In RicoDrawBounces: const float speed_scale = float(rico::g_debug_beam_speed) / 100.0f;
// a pulse's bounce beam speed was multiplied by it right after beam_create
//   pl::StoreF32(base, beam + rico::kBeamSpeed, pl::LoadF32(base, beam + rico::kBeamSpeed) * speed_scale);
// and ++rico::g_debug_pool_full / ++rico::g_debug_bounces counted beams.

// The shot hook's timing wrapper (PdRicoShot called this, the real work was PdRicoShotImpl):
void PdRicoShot(PPCContext& ctx, uint8_t* base) {
  // TEMP_RICO: the time the bounces take, for the once-a-second log line.
  const auto start = std::chrono::steady_clock::now();
  PdRicoShotImpl(ctx, base);
  pd::ricochet::g_debug_shot_us +=
      std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

// In PdRicoShotImpl, after `stream` was known:
//   {  // TEMP_RICO: which function each player's Laser shots have on this PC (logged when it changes)
//     static uint32_t last[8] = {};
//     const int8_t weapon = int8_t(base[player + rico::kPlayerWeapon]);
//     const uint32_t number = pl::LoadU32(base, pl::kGVars + 1196) & 7;
//     const uint32_t key = 0x100u | (uint32_t(uint8_t(weapon)) << 1) | (stream ? 1u : 0u);
//     if ((weapon == 29 || weapon == int8_t(rico::kWeaponRicochet)) && last[number] != key) {
//       last[number] = key;
//       REXLOG_INFO("[rico] player {} fires weapon {} function {} (hand {}, hand gset weapon {})", number, weapon,
//                   function, ctx.r31.u32, int8_t(base[hand]));
//     }
//   }
// and at its end:
//   if (REXCVAR_GET(pd_rico_debug) && !stream) {  // TEMP_RICO (the Stream would log every frame)
//     std::string text;
//     for (const rico::Segment& seg : path) {
//       text += fmt::format(" | {}: {:.0f} {:.0f} {:.0f} -> {:.0f} {:.0f} {:.0f}{}", seg.bounce, seg.from.x, seg.from.y,
//                           seg.from.z, seg.to.x, seg.to.y, seg.to.z, seg.wall_end ? "" : " (air)");
//     }
//     REXLOG_INFO("[rico] pulse by player {} (frame {}) from {:.0f} {:.0f} {:.0f} dir {:.2f} {:.2f} {:.2f}, game hit "
//                 "{:.0f} {:.0f} {:.0f}: {} bounces{}",
//                 int32_t(pl::LoadU32(base, pl::kGVars + 1196)), pl::LoadU32(base, pl::kLevelFrames), pos.x, pos.y,
//                 pos.z, dir.x, dir.y, dir.z, hitpos.x, hitpos.y, hitpos.z, path.size(), text);
//   }
