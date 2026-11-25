// Debug code of the custom weapons research (branch weapon-research): probes for
// the Ricochet Laser, Remote Turret, Push Gun and Slow Gun. Findings in
// docs/research/custom-weapons.md. Not built; kept only so the code can be recovered.
//
// Removed in: the cleanup commit that added this file
// ("Move the custom weapons research probes to the debug bank").
//
// What for (all off by default, cvars pd_probe_*):
// - pd_probe_hits: log chr_damage, shot_calculate_hits, beam_create, the
//   player-hit function sub_821274A0 (shooter and network side), bgun_add_boost
//   and bgun_disarm on every PC (rounds T0-T2);
// - pd_probe_ricochet (+ _damage, _direct, _tracer_speed): Laser shots bounce
//   once after shot_create's shot_calculate_hits, through the game's hit test or
//   our own segment test + chr_damage_by_laser, with a g_BoltBeams tracer
//   (seen by eye, but thin and view-dependent);
// - pd_probe_hit_player: one direct chr_damage_by_laser hit on any player
//   (killed off screen with frag credit, T3);
// - pd_probe_laser_tint: Laser/Stream beam colour (beam_render) and ALL bolt
//   trails (boltbeams_render; it also tinted the sentry);
// - pd_probe_turret, pd_probe_turret_steer, pd_probe_turret_yrot: log sentries
//   and rockets, steer the owner's sentry through autogun_tick's goal angles and
//   firing flag (the other PCs follow; firing is forced while steering);
// - pd_probe_push_mult, pd_probe_push_cap: knockback factor and cap on the
//   victim's PC;
// - pd_probe_slow_clock: halves the local player's move-tick clock (walking
//   slows, fire rate does not).
//
// Call sites in main.cpp: PdTestPlayerTick called PdProbeHitPlayer(ctx, base)
// first thing after its local-player check (declared above it);
// PdNoteGameFrame called PdProbeTurretFrame(base).
//
// The scenarios that switched these probes on are in
// tools/debug-bank/weapon-research-scenarios/ (they need this code back to work).

// ============================================================================
// main.cpp
// ============================================================================

// ---- TEMP_RESEARCH: custom weapons research (logging only) -------------------------------
//
// What for: learning what each PC runs when a player is hit online (chr_damage
// sub_82128200), for the Push Gun, Slow Gun and Ricochet Laser research
// (docs/research/custom-weapons.md). Off unless pd_probe_hits is on.
REXCVAR_DEFINE_BOOL(pd_probe_hits, false, "PerfectDark", "TEMP_RESEARCH: log every chr_damage call");
REXCVAR_DEFINE_BOOL(pd_probe_ricochet, false, "PerfectDark", "TEMP_RESEARCH: Laser shots bounce once");
REXCVAR_DEFINE_DOUBLE(pd_probe_ricochet_damage, 1.0, "PerfectDark", "TEMP_RESEARCH: damage multiplier of the bounce");

REXCVAR_DEFINE_BOOL(pd_probe_ricochet_direct, false, "PerfectDark",
                    "TEMP_RESEARCH: bounce hits by our own test + chr_damage_by_laser");
REXCVAR_DEFINE_DOUBLE(pd_probe_ricochet_tracer_speed, 6000.0, "PerfectDark",
                      "TEMP_RESEARCH: how fast the bounce tracer shrinks (units/s)");

namespace {
bool g_probe_in_ricochet = false;  // our second shot_calculate_hits is running
bool g_probe_direct = false;       // our own chr_damage_by_laser call (damage already set)
}

namespace {
// The number of the player whose prop is `prop`, or whose chr is `chr`; -1 if none.
int ProbePlayerOf(const uint8_t* base, uint32_t prop, uint32_t chr) {
  namespace pl = pd::player;
  for (int n = 0; n < 8; ++n) {
    const uint32_t p = pl::PropOf(base, pl::Player(base, n));
    if (!p) continue;
    if ((prop && p == prop) || (chr && pl::LoadU32(base, p + 4) == chr)) return n;
  }
  return -1;
}
}  // namespace

// chr_damage's start (after its stack frame): r3 = the chr hit, f1 = damage,
// r5 = the hit vector (coord*), r6 = gset (weapon number +0, function +3),
// r7 = the attacker's prop, r8 = hit part, r9 = damage the shield.
void PdProbeChrDamage(uint8_t* base, uint32_t chr, double& damage, uint32_t vector, uint32_t gset, uint32_t aprop,
                      uint32_t hitpart, uint32_t shield) {
  if (g_probe_in_ricochet && !g_probe_direct) {  // a hit of the bounce: multiply its damage
    damage *= REXCVAR_GET(pd_probe_ricochet_damage);
    REXLOG_INFO("[probe] ricochet hit: damage now {:.3f}", damage);
  }
  if (!REXCVAR_GET(pd_probe_hits)) return;
  namespace pl = pd::player;
  const pl::Vec v = vector ? pl::LoadVec(base, vector) : pl::Vec{};
  REXLOG_INFO(
      "[probe] chr_damage frame {}: victim player {} (chr 0x{:08X}), attacker player {} (prop 0x{:08X}), "
      "weapon {} func {}, damage {:.3f}, vector {:.3f} {:.3f} {:.3f}, hitpart {}, shield {}",
      pl::LoadU32(base, pl::kLevelFrames), ProbePlayerOf(base, 0, chr), chr, ProbePlayerOf(base, aprop, 0), aprop,
      gset ? int(base[gset]) : -1, gset ? int(base[gset + 3]) : -1, damage, v.x, v.y, v.z, int32_t(hitpart), shield);
}
// shot_calculate_hits sub_821FBAA8 after its stack frame (r3 hand, r4 isshooting,
// r7 gunpos3d, r8 gundir3d): which player's shot each PC works out, and from where.
void PdProbeShotHits(uint8_t* base, uint32_t hand, uint32_t shooting, uint32_t pos, uint32_t dir) {
  if (!REXCVAR_GET(pd_probe_hits)) return;
  namespace pl = pd::player;
  const pl::Vec p = pl::LoadVec(base, pos), d = pl::LoadVec(base, dir);
  REXLOG_INFO("[probe] shot frame {}: player {} hand {} shooting {} from {:.1f} {:.1f} {:.1f} dir {:.4f} {:.4f} {:.4f}",
              pl::LoadU32(base, pl::kLevelFrames), int32_t(pl::LoadU32(base, 0x82648BF8 + 1196)), hand, shooting, p.x,
              p.y, p.z, d.x, d.y, d.z);
}

// The player-hit function sub_821274A0 after its stack frame (r1 = its frame; the
// caller's byte at 87(caller r1) = r1 + 288 + 87): called by chr_damage on the
// shooter's PC and by the network handlers sub_822AE858 / sub_822AEA98 on the
// others. It adds the knockback (bondshotspeed += vector * 0.75 at 0x821277D4).
void PdProbePlayerHit(uint8_t* base, uint32_t r1, uint32_t r3, uint32_t r4, double f1, double f2, uint32_t r7,
                      uint32_t r8, uint32_t r9, uint32_t r10) {
  if (!REXCVAR_GET(pd_probe_hits)) return;
  REXLOG_INFO("[probe] player hit frame {}: r3 {} r4 {} f1 {:.3f} f2 {:.3f} r7 0x{:X} r8 0x{:08X} r9 0x{:X} r10 0x{:X} byte87 0x{:02X} (current player {})",
              pd::player::LoadU32(base, pd::player::kLevelFrames), int32_t(r3), int32_t(r4), f1, f2, r7, r8, r9, r10,
              base[r1 + 288 + 87], int32_t(pd::player::LoadU32(base, 0x82648BF8 + 1196)));
}

// bgun_add_boost sub_82178668 (r3 = ticks) and bgun_disarm sub_8217FED0 (r3 = the
// attacker's prop): which PC runs them, for which player.
void PdProbeBoostDisarm(uint8_t* base, const char* what, uint32_t r3) {
  if (!REXCVAR_GET(pd_probe_hits)) return;
  REXLOG_INFO("[probe] {} frame {}: r3 0x{:X}, current player {}", what,
              pd::player::LoadU32(base, pd::player::kLevelFrames), r3,
              int32_t(pd::player::LoadU32(base, 0x82648BF8 + 1196)));
}

// ---- Ricochet probe (A3/A7): pd_probe_ricochet ----
//
// After a Laser shot (shot_create sub_821FC8C0, just after its shot_calculate_hits),
// find the wall the shot ended on, reflect the shot off it and call
// shot_calculate_hits once more from there. Hits of that second shot have their
// damage multiplied by pd_probe_ricochet_damage (chr_damage probe below), to see
// whether a bounce can hit and kill a player, with credit to the shooter.

void PdProbeRicochet(PPCContext& ctx, uint8_t* base) {
  namespace pl = pd::player;
  if (!REXCVAR_GET(pd_probe_ricochet) || g_probe_in_ricochet) return;
  const uint32_t player = pl::LoadU32(base, 0x82648BF8 + 1188);  // the player whose shot this is
  if (!player || int8_t(base[player + pl::kWeapon]) != 29) return;  // the Laser only
  const uint32_t hand = ctx.r31.u32;
  const uint32_t frame_r1 = ctx.r1.u32;  // shot_create's frame: gunpos3d at +144, gundir3d at +112
  const pl::Vec pos = pl::LoadVec(base, frame_r1 + 144);
  const pl::Vec dir = pl::LoadVec(base, frame_r1 + 112);

  // The wall: the same line through the level only (background, doors).
  const uint32_t from = frame_r1 - 0x400, to = from + 16, hit = from + 32, normal = from + 48;
  const uint32_t pos2d = from + 64, dir2d = from + 80, pos3d = from + 96, dir3d = from + 112;
  pl::StoreVec(base, from, pos);
  pl::StoreVec(base, to, {pos.x + dir.x * 20000.0f, pos.y + dir.y * 20000.0f, pos.z + dir.z * 20000.0f});
  PPCContext call = ctx;
  call.r1.u64 = from - 0x200;
  call.r3.u64 = from;
  call.r4.u64 = pl::PropOf(base, player) + pl::kPropRooms;
  call.r5.u64 = to;
  call.r6.u64 = 0x20 | 0x02;
  sub_820E4E88(call, base);  // cd_test_los_oobok_findclosest_autoflags: 0 = hit
  if (call.r3.s32 != 0) {
    REXLOG_INFO("[probe] ricochet: no wall along the shot");
    return;
  }
  call.r3.u64 = hit;
  call.r4.u64 = 0;
  call.r5.u64 = 0;
  sub_820DDDC0(call, base);  // cd_get_obstacle_pos
  call.r3.u64 = normal;
  sub_820E7070(call, base);  // cd_get_obstacle_normal
  const pl::Vec at = pl::LoadVec(base, hit);
  pl::Vec n = pl::LoadVec(base, normal);
  const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
  if (len < 0.001f) return;
  n = {n.x / len, n.y / len, n.z / len};
  float dn = dir.x * n.x + dir.y * n.y + dir.z * n.z;
  if (dn > 0) {  // the normal must face the shooter
    n = {-n.x, -n.y, -n.z};
    dn = -dn;
  }
  // Reflection: r = d - 2 (d.n) n; start 2 units off the wall.
  const pl::Vec r{dir.x - 2 * dn * n.x, dir.y - 2 * dn * n.y, dir.z - 2 * dn * n.z};
  const pl::Vec start{at.x + n.x * 2, at.y + n.y * 2, at.z + n.z * 2};
  pl::StoreVec(base, pos3d, start);
  pl::StoreVec(base, dir3d, r);

  // The camera-space copies shot_calculate_hits also wants (its prop tests use
  // them): world->camera matrix (sub_8214B718: player+5992), point transform
  // sub_822431A8, vector rotation sub_822430C0 - as shot_create does.
  call = ctx;
  call.r1.u64 = from - 0x200;
  sub_8214B718(call, base);
  const uint32_t w2c = call.r3.u32;
  call.r3.u64 = w2c;
  call.r4.u64 = pos3d;
  call.r5.u64 = pos2d;
  sub_822431A8(call, base);
  call.r3.u64 = w2c;
  call.r4.u64 = dir3d;
  call.r5.u64 = dir2d;
  sub_822430C0(call, base);

  REXLOG_INFO("[probe] ricochet frame {}: wall at {:.1f} {:.1f} {:.1f} normal {:.3f} {:.3f} {:.3f}, bounce dir {:.3f} {:.3f} {:.3f}",
              pl::LoadU32(base, pl::kLevelFrames), at.x, at.y, at.z, n.x, n.y, n.z, r.x, r.y, r.z);

  // Where the bounce ends: the same line test from the wall along the bounce.
  pl::Vec end{start.x + r.x * 20000.0f, start.y + r.y * 20000.0f, start.z + r.z * 20000.0f};
  {
    pl::StoreVec(base, from, start);
    pl::StoreVec(base, to, end);
    call = ctx;
    call.r1.u64 = from - 0x200;
    call.r3.u64 = from;
    call.r4.u64 = pl::PropOf(base, player) + pl::kPropRooms;
    call.r5.u64 = to;
    call.r6.u64 = 0x20 | 0x02;
    sub_820E4E88(call, base);
    if (call.r3.s32 == 0) {
      call.r3.u64 = hit;
      call.r4.u64 = 0;
      call.r5.u64 = 0;
      sub_820DDDC0(call, base);
      end = pl::LoadVec(base, hit);
    }
  }

  // A4: a tracer for the bounce, from a free crossbow-bolt trail slot
  // (g_BoltBeams 0x8262E958, 8 x 48 B: +0 owner (-1 = free), +5 tick mode
  // (1 = automatic: shrinks towards the tail and frees itself), +20 head,
  // +32 tail, +44 speed; drawn by boltbeams_render sub_821844F0 with the laser texture).
  for (int i = 0; i < 8; ++i) {
    const uint32_t slot = 0x8262E958 + uint32_t(i) * 48;
    if (int32_t(pl::LoadU32(base, slot)) != -1) continue;
    pl::StoreU32(base, slot, 0);
    base[slot + 5] = 1;
    pl::StoreVec(base, slot + 20, start);
    pl::StoreVec(base, slot + 32, end);
    pl::StoreF32(base, slot + 44, float(REXCVAR_GET(pd_probe_ricochet_tracer_speed)));
    REXLOG_INFO("[probe] ricochet tracer: slot {}, {:.1f} {:.1f} {:.1f} -> {:.1f} {:.1f} {:.1f}", i, start.x, start.y,
                start.z, end.x, end.y, end.z);
    break;
  }

  // Direct mode: our own test of the bounce against every player (a vertical
  // cylinder from the eye down 160 units, radius 30), then chr_damage_by_laser
  // sub_8212DDE0(chr, damage, vector, gset, attacker) - only on the PC whose
  // player shot (players[0]); other PCs only draw the tracer.
  if (REXCVAR_GET(pd_probe_ricochet_direct)) {
    const float seg_len = std::sqrt((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y) +
                                    (end.z - start.z) * (end.z - start.z));
    int best = -1;
    float best_t = seg_len;
    for (int n_player = 0; n_player < 8; ++n_player) {
      const uint32_t other = pl::Player(base, n_player);
      const uint32_t prop = pl::PropOf(base, other);
      if (!prop || other == player) continue;
      const pl::Vec p = pl::LoadVec(base, prop + pl::kPropPos);
      // Closest approach of the line to the cylinder's axis, in the ground plane.
      const float hx = r.x, hz = r.z, h2 = hx * hx + hz * hz;
      if (h2 < 1e-6f) continue;
      const float t = ((p.x - start.x) * hx + (p.z - start.z) * hz) / h2;  // along r (r is a unit vector)
      if (t < 0 || t > best_t) continue;
      const float cx = start.x + r.x * t - p.x, cz = start.z + r.z * t - p.z, cy = start.y + r.y * t;
      if (cx * cx + cz * cz > 30.0f * 30.0f || cy > p.y + 10.0f || cy < p.y - 160.0f) continue;
      best = n_player;
      best_t = t;
    }
    if (best >= 0) {
      REXLOG_INFO("[probe] ricochet direct: the bounce meets player {} at {:.0f} units", best, best_t);
      if (player == pl::Player(base, 0)) {  // our own shot: we apply the damage
        const uint32_t vec = from + 128, gset = from + 144;
        pl::StoreVec(base, vec, r);
        pl::StoreU32(base, gset, 0x1D000000);  // gset {weapon 29, 0, 0, function 0}
        call = ctx;
        call.r1.u64 = from - 0x200;
        call.r3.u64 = pl::LoadU32(base, pl::PropOf(base, pl::Player(base, best)) + 4);  // its chr
        call.f1.f64 = 1.0 * REXCVAR_GET(pd_probe_ricochet_damage);
        call.r5.u64 = vec;
        call.r6.u64 = gset;
        call.r7.u64 = pl::PropOf(base, player);
        g_probe_in_ricochet = true;  // (the chr_damage probe then logs, without multiplying again)
        g_probe_direct = true;
        sub_8212DDE0(call, base);
        g_probe_direct = false;
        g_probe_in_ricochet = false;
      }
    }
    return;
  }

  // Our second shot_calculate_hits moves the hand's hit position (where the main
  // tracer ends, set by bgun_set_hit_pos sub_821776F0: player+3004 right hand,
  // +4960 left): keep the first shot's.
  const pl::Vec hitpos_right = pl::LoadVec(base, player + 3004), hitpos_left = pl::LoadVec(base, player + 4960);

  // shot_calculate_hits(hand, isshooting 1, pos2d, dir2d, pos3d, dir3d, 0,
  // distance f1 (the same 4294836224 shot_create passes), cheap on the stack at 84).
  call = ctx;
  call.r1.u64 = from - 0x200;
  pl::StoreU32(base, call.r1.u32 + 84, 1);
  call.r3.u64 = hand;
  call.r4.u64 = 1;
  call.r5.u64 = pos2d;
  call.r6.u64 = dir2d;
  call.r7.u64 = pos3d;
  call.r8.u64 = dir3d;
  call.r9.u64 = 0;
  call.f1.f64 = 4294836224.0;
  g_probe_in_ricochet = true;
  sub_821FBAA8(call, base);
  g_probe_in_ricochet = false;
  REXLOG_INFO("[probe] ricochet: second shot returned prop 0x{:08X}", call.r3.u32);
  pl::StoreVec(base, player + 3004, hitpos_right);
  pl::StoreVec(base, player + 4960, hitpos_left);
}

// A5: the Laser's colour. beam_render sub_82182A30 at 0x82182C90 (all branches
// joined): r25 = the beam's RGBA, r29 = the beam (weapon number at +1; 29 Laser,
// -2 Stream). boltbeams_render's call at 0x82184554: r7 / r10 = head / tail RGBA.
// pd_probe_laser_tint = 0xRRGGBB (0 = off).
REXCVAR_DEFINE_INT32(pd_probe_laser_tint, 0, "PerfectDark", "TEMP_RESEARCH: Laser beam colour 0xRRGGBB, 0 = off");

void PdProbeBeamColour(uint8_t* base, uint32_t colour, uint32_t beam) {
  const uint32_t tint = uint32_t(REXCVAR_GET(pd_probe_laser_tint));
  if (!tint) return;
  const int8_t weapon = int8_t(base[beam + 1]);
  if (weapon != 29 && weapon != -2) return;
  base[colour] = uint8_t(tint >> 16);
  base[colour + 1] = uint8_t(tint >> 8);
  base[colour + 2] = uint8_t(tint);
}
void PdProbeBoltBeamColour(uint32_t& head, uint32_t& tail) {
  const uint32_t tint = uint32_t(REXCVAR_GET(pd_probe_laser_tint));
  if (!tint) return;
  head = (tint << 8) | (head & 0xFF);
  tail = (tint << 8) | (tail & 0xFF);
}

// ---- Turret probes (B1/B2): pd_probe_turret ----
//
// B1: each Laptop sentry made on this PC (laptop_deploy sub_8221E5C8 returns it in
// r31 at 0x8221E934), then every 15 level frames its aim and target: autogun
// fields as N64 (yrot +108, xrot +120, firing s8 +94, target prop +164,
// ammo +169), and the prop's position. B2: every rocket (weapon objects with
// weapon number 83) in the active prop list (g_Vars+1376, next at +32), with
// its position, every 10 frames: does a steered Slayer rocket follow the same
// path on every PC?
REXCVAR_DEFINE_BOOL(pd_probe_turret, false, "PerfectDark", "TEMP_RESEARCH: log sentries and rockets");

namespace {
std::vector<uint32_t> g_probe_sentries;  // autogun objects, made this stage
}

void PdProbeSentryMade(uint8_t* base, uint32_t obj) {
  if (!REXCVAR_GET(pd_probe_turret)) return;
  namespace pl = pd::player;
  g_probe_sentries.push_back(obj);
  REXLOG_INFO("[probe] sentry made frame {}: obj 0x{:08X} prop 0x{:08X} by current player {} (owner bits {})",
              pl::LoadU32(base, pl::kLevelFrames), obj, pl::LoadU32(base, obj + 20),
              int32_t(pl::LoadU32(base, 0x82648BF8 + 1196)), pl::LoadU32(base, obj + 64) >> 28);
}

void PdProbeTurretFrame(uint8_t* base) {
  if (!REXCVAR_GET(pd_probe_turret)) return;
  namespace pl = pd::player;
  const uint32_t frame = pl::LoadU32(base, pl::kLevelFrames);
  static uint32_t last = 0;
  if (frame == last) return;
  last = frame;
  if (frame < 10) g_probe_sentries.clear();  // a new stage
  if (frame % 15 == 0) {
    for (const uint32_t obj : g_probe_sentries) {
      const uint32_t prop = pl::LoadU32(base, obj + 20);
      const pl::Vec p = prop ? pl::LoadVec(base, prop + pl::kPropPos) : pl::Vec{};
      const uint32_t target = pl::LoadU32(base, obj + 164);
      int target_player = -1;
      for (int n = 0; n < 8; ++n) {
        if (target && pl::PropOf(base, pl::Player(base, n)) == target) target_player = n;
      }
      REXLOG_INFO("[probe] sentry frame {}: obj 0x{:08X} at {:.0f} {:.0f} {:.0f} yrot {:.3f} xrot {:.3f} firing {} target player {} ammo {}",
                  frame, obj, p.x, p.y, p.z, pl::LoadF32(base, obj + 108), pl::LoadF32(base, obj + 120),
                  int(int8_t(base[obj + 94])), target_player, base[obj + 169]);
    }
  }
  if (frame % 10 == 0) {
    uint32_t p = pl::LoadU32(base, 0x82648BF8 + 1376);
    for (int guard = 0; p && guard < 2000; ++guard, p = pl::LoadU32(base, p + 32)) {
      if (base[p] != 4) continue;  // PROPTYPE_WEAPON
      const uint32_t obj = pl::LoadU32(base, p + 4);
      if (!obj || base[obj + 92] != 83) continue;  // WEAPON_ROCKET
      const pl::Vec v = pl::LoadVec(base, p + pl::kPropPos);
      REXLOG_INFO("[probe] rocket frame {}: prop 0x{:08X} at {:.0f} {:.0f} {:.0f} owner bits {}", frame, p, v.x, v.y,
                  v.z, pl::LoadU32(base, obj + 64) >> 28);
    }
  }
}

// B3: steering a sentry (pd_probe_turret_steer, pd_probe_turret_yrot radians).
REXCVAR_DEFINE_BOOL(pd_probe_turret_steer, false, "PerfectDark", "TEMP_RESEARCH: steer this PC's sentry");
REXCVAR_DEFINE_DOUBLE(pd_probe_turret_yrot, 0.0, "PerfectDark", "TEMP_RESEARCH: the steered sentry's yrot");

// B3 (second try): autogun_tick computes goal angles on its stack (84(r1) x,
// 88(r1) y) and, online, the owner's copy SENDS them with its firing flag
// (sub_82290770 at 0x82207B6C) while the other copies RECEIVE them
// (sub_8228DBF0 at 0x822078E4); r18 != 0 = a receiving copy. Overriding the
// goals at 0x822078E8 (before apply_rotation) and firing at 0x82207B08 (before
// the send) lets the game's own sync carry the steering.
void PdProbeAutogunGoals(uint8_t* base, uint32_t r1, uint32_t obj, uint32_t r18) {
  if (!REXCVAR_GET(pd_probe_turret_steer) || r18 != 0) return;
  pd::player::StoreF32(base, r1 + 88, float(REXCVAR_GET(pd_probe_turret_yrot)));
  pd::player::StoreF32(base, r1 + 84, 0.0f);
}
void PdProbeAutogunFiring(uint8_t* base, uint32_t obj, uint32_t r18) {
  if (!REXCVAR_GET(pd_probe_turret_steer) || r18 != 0) return;
  base[obj + 94] = 1;
}


// C2: push strength. In the player-hit function, at `fmadds f11,f13,f0,f12`
// (0x821277D4; f0 = 0.75, used for x and z): f0 *= pd_probe_push_mult. And in
// bmove_dampen_shotspeed sub_820F2098 at 0x820F20CC (f12 = the 1.5 cap):
// f12 = pd_probe_push_cap (0 = keep 1.5).
REXCVAR_DEFINE_DOUBLE(pd_probe_push_mult, 1.0, "PerfectDark", "TEMP_RESEARCH: knockback multiplier");
REXCVAR_DEFINE_DOUBLE(pd_probe_push_cap, 0.0, "PerfectDark", "TEMP_RESEARCH: knockback speed cap (0 = 1.5)");

void PdProbePushScale(double& f0) {
  f0 *= REXCVAR_GET(pd_probe_push_mult);
}
void PdProbePushCap(double& f12) {
  if (REXCVAR_GET(pd_probe_push_cap) > 0.0) f12 = REXCVAR_GET(pd_probe_push_cap);
}

// D: a slow clock for one player. sub_820F4D40 (the player move tick) at
// 0x820F4DB0, after its combat-boost doubling and before the input step: for the
// local player, lvupdate240 / lvupdate60 (g_Vars+52 / +56) and lvupdate60freal
// (+76) times pd_probe_slow_clock; the function puts them back at its end.
REXCVAR_DEFINE_DOUBLE(pd_probe_slow_clock, 1.0, "PerfectDark", "TEMP_RESEARCH: local player's clock factor");

void PdProbeSlowClock(uint8_t* base) {
  const double k = REXCVAR_GET(pd_probe_slow_clock);
  if (k >= 1.0) return;
  namespace pl = pd::player;
  constexpr uint32_t g = 0x82648BF8;
  if (pl::LoadU32(base, g + 1188) != pl::Player(base, 0)) return;
  for (const uint32_t off : {52u, 56u}) {
    const uint32_t v = pl::LoadU32(base, g + off);
    pl::StoreU32(base, g + off, v ? std::max<uint32_t>(1, uint32_t(v * k + 0.5)) : 0);
  }
  pl::StoreF32(base, g + 76, float(pl::LoadF32(base, g + 76) * k));
}

// A7: a one-off Laser hit on player pd_probe_hit_player (this PC's numbering) with
// pd_probe_ricochet_damage, wherever it is (on screen or not), through
// chr_damage_by_laser - as a bounce hit would be applied. Run from the test
// player tick; -1 = nothing to do.
REXCVAR_DEFINE_INT32(pd_probe_hit_player, -1, "PerfectDark", "TEMP_RESEARCH: hit this player once");

void PdProbeHitPlayer(PPCContext& ctx, uint8_t* base) {
  namespace pl = pd::player;
  const int target = REXCVAR_GET(pd_probe_hit_player);
  if (target < 0) return;
  REXCVAR_SET(pd_probe_hit_player, -1);
  const uint32_t me = pl::Player(base, 0), victim = pl::Player(base, target);
  if (!me || !victim) return;
  const pl::Vec a = pl::LoadVec(base, pl::PropOf(base, me) + pl::kPropPos);
  const pl::Vec b = pl::LoadVec(base, pl::PropOf(base, victim) + pl::kPropPos);
  float dx = b.x - a.x, dz = b.z - a.z;
  const float d = std::sqrt(dx * dx + dz * dz);
  if (d > 0.001f) dx /= d, dz /= d;
  const uint32_t vec = ctx.r1.u32 - 0x400, gset = vec + 16;
  pl::StoreVec(base, vec, {dx, 0.0f, dz});
  pl::StoreU32(base, gset, 0x1D000000);
  PPCContext call = ctx;
  call.r1.u64 = vec - 0x200;
  call.r3.u64 = pl::LoadU32(base, pl::PropOf(base, victim) + 4);
  call.f1.f64 = 1.0 * REXCVAR_GET(pd_probe_ricochet_damage);
  call.r5.u64 = vec;
  call.r6.u64 = gset;
  call.r7.u64 = pl::PropOf(base, me);
  REXLOG_INFO("[probe] direct hit on player {} ({:.0f} units away), damage {:.1f}", target, d, call.f1.f64);
  g_probe_direct = true;
  sub_8212DDE0(call, base);  // chr_damage_by_laser
  g_probe_direct = false;
}

// beam_create sub_82182130 (r3 beam, r4 weapon number, r5 from, r6 to): tracers.
void PdProbeBeam(uint8_t* base, uint32_t beam, uint32_t weapon, uint32_t from, uint32_t to) {
  if (!REXCVAR_GET(pd_probe_hits)) return;
  namespace pl = pd::player;
  const pl::Vec f = pl::LoadVec(base, from), t = pl::LoadVec(base, to);
  REXLOG_INFO("[probe] beam frame {}: player {} beam 0x{:08X} weapon {} from {:.1f} {:.1f} {:.1f} to {:.1f} {:.1f} {:.1f}",
              pl::LoadU32(base, pl::kLevelFrames), int32_t(pl::LoadU32(base, 0x82648BF8 + 1196)), beam,
              int32_t(weapon), f.x, f.y, f.z, t.x, t.y, t.z);
}
// ---- end of TEMP_RESEARCH ----------------------------------------------------------------

// ============================================================================
// perfect_dark_hooks.cpp (declarations, then the hooks)
// ============================================================================

// TEMP_RESEARCH probes (main.cpp)
void PdProbeChrDamage(uint8_t* base, uint32_t chr, double& damage, uint32_t vector, uint32_t gset, uint32_t aprop,
                      uint32_t hitpart, uint32_t shield);
void PdProbeShotHits(uint8_t* base, uint32_t hand, uint32_t shooting, uint32_t pos, uint32_t dir);
void PdProbeBeam(uint8_t* base, uint32_t beam, uint32_t weapon, uint32_t from, uint32_t to);
void PdProbePlayerHit(uint8_t* base, uint32_t r1, uint32_t r3, uint32_t r4, double f1, double f2, uint32_t r7,
                      uint32_t r8, uint32_t r9, uint32_t r10);
void PdProbeBoostDisarm(uint8_t* base, const char* what, uint32_t r3);
void PdProbeRicochet(PPCContext& ctx, uint8_t* base);
void PdProbeBeamColour(uint8_t* base, uint32_t colour, uint32_t beam);
void PdProbeBoltBeamColour(uint32_t& head, uint32_t& tail);
void PdProbeSentryMade(uint8_t* base, uint32_t obj);
void PdProbeAutogunGoals(uint8_t* base, uint32_t r1, uint32_t obj, uint32_t r18);
void PdProbeAutogunFiring(uint8_t* base, uint32_t obj, uint32_t r18);
void PdProbePushScale(double& f0);
void PdProbePushCap(double& f12);
void PdProbeSlowClock(uint8_t* base);

// TEMP_RESEARCH: chr_damage sub_82128200 after its stack frame is made (arguments
// still in their registers).
void PdHookProbeChrDamage(PPCRegister& r3, PPCRegister& f1, PPCRegister& r5, PPCRegister& r6, PPCRegister& r7,
                          PPCRegister& r8, PPCRegister& r9) {
  PdProbeChrDamage(HookBase(), r3.u32, f1.f64, r5.u32, r6.u32, r7.u32, r8.u32, r9.u32);
}

// TEMP_RESEARCH: the player-hit function sub_821274A0 after its stack frame.
void PdHookProbePlayerHit(PPCRegister& r1, PPCRegister& r3, PPCRegister& r4, PPCRegister& f1, PPCRegister& f2,
                          PPCRegister& r7, PPCRegister& r8, PPCRegister& r9, PPCRegister& r10) {
  PdProbePlayerHit(HookBase(), r1.u32, r3.u32, r4.u32, f1.f64, f2.f64, r7.u32, r8.u32, r9.u32, r10.u32);
}

// TEMP_RESEARCH: bgun_add_boost sub_82178668 after its stack frame (r3 = ticks).
void PdHookProbeAddBoost(PPCRegister& r3) {
  PdProbeBoostDisarm(HookBase(), "add boost", r3.u32);
}

// TEMP_RESEARCH: bgun_disarm sub_8217FED0 after its stack frame (r3 = attacker prop).
void PdHookProbeDisarm(PPCRegister& r3) {
  PdProbeBoostDisarm(HookBase(), "disarm", r3.u32);
}

// TEMP_RESEARCH: shot_create sub_821FC8C0 right after its shot_calculate_hits.
void PdHookProbeRicochet() {
  PdProbeRicochet(HookContext(), HookBase());
}

// TEMP_RESEARCH: beam_render sub_82182A30 where its colour branches join
// (r25 = the beam's RGBA, r29 = the beam).
void PdHookProbeBeamColour(PPCRegister& r25, PPCRegister& r29) {
  PdProbeBeamColour(HookBase(), r25.u32, r29.u32);
}

// TEMP_RESEARCH: autogun_tick before apply_rotation (r1 frame: goals at 84/88,
// r31 = obj, r18 != 0 = a copy receiving the owner's aim) and before the send.
void PdHookProbeAutogunGoals(PPCRegister& r1, PPCRegister& r31, PPCRegister& r18) {
  PdProbeAutogunGoals(HookBase(), r1.u32, r31.u32, r18.u32);
}
void PdHookProbeAutogunFiring(PPCRegister& r31, PPCRegister& r18) {
  PdProbeAutogunFiring(HookBase(), r31.u32, r18.u32);
}

// TEMP_RESEARCH: the player-hit function's knockback factor (f0 = 0.75).
void PdHookProbePushScale(PPCRegister& f0) {
  PdProbePushScale(f0.f64);
}

// TEMP_RESEARCH: bmove_dampen_shotspeed's cap (f12 = 1.5).
void PdHookProbePushCap(PPCRegister& f12) {
  PdProbePushCap(f12.f64);
}

// TEMP_RESEARCH: the player move tick sub_820F4D40 before the input step.
void PdHookProbeSlowClock() {
  PdProbeSlowClock(HookBase());
}

// TEMP_RESEARCH: laptop_deploy sub_8221E5C8 returning the new sentry (r31).
void PdHookProbeSentryMade(PPCRegister& r31) {
  PdProbeSentryMade(HookBase(), r31.u32);
}

// TEMP_RESEARCH: boltbeams_render's beam_render_generic call (r7 head RGBA, r10 tail RGBA).
void PdHookProbeBoltBeamColour(PPCRegister& r7, PPCRegister& r10) {
  uint32_t head = r7.u32, tail = r10.u32;
  PdProbeBoltBeamColour(head, tail);
  r7.u64 = head;
  r10.u64 = tail;
}

// TEMP_RESEARCH: shot_calculate_hits sub_821FBAA8 after its stack frame.
void PdHookProbeShotHits(PPCRegister& r3, PPCRegister& r4, PPCRegister& r7, PPCRegister& r8) {
  PdProbeShotHits(HookBase(), r3.u32, r4.u32, r7.u32, r8.u32);
}

// TEMP_RESEARCH: beam_create sub_82182130 after its stack frame.
void PdHookProbeBeam(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {
  PdProbeBeam(HookBase(), r3.u32, r4.u32, r5.u32, r6.u32);
}

// ============================================================================
// perfect_dark_config.toml
// ============================================================================
//
// # ---- TEMP_RESEARCH: custom weapons research (logging only, pd_probe_hits) -----------
//
// # chr_damage sub_82128200 after `stwu r1,-672(r1)`: r3 chr, f1 damage, r5 vector,
// # r6 gset, r7 attacker prop, r8 hit part, r9 damage shield.
// [[midasm_hook]]
// address = 0x82128214
// name = "PdHookProbeChrDamage"
// registers = ["r3", "f1", "r5", "r6", "r7", "r8", "r9"]
//
// # shot_calculate_hits sub_821FBAA8 after `stwu r1,-1888(r1)`: r3 hand, r4 isshooting,
// # r7 gunpos3d, r8 gundir3d.
// [[midasm_hook]]
// address = 0x821FBAC0
// name = "PdHookProbeShotHits"
// registers = ["r3", "r4", "r7", "r8"]
//
// # beam_create sub_82182130 after `stwu r1,-128(r1)`: r3 beam, r4 weapon, r5 from, r6 to.
// [[midasm_hook]]
// address = 0x82182148
// name = "PdHookProbeBeam"
// registers = ["r3", "r4", "r5", "r6"]
//
// # The player-hit function sub_821274A0 after `stwu r1,-288(r1)`.
// [[midasm_hook]]
// address = 0x821274B8
// name = "PdHookProbePlayerHit"
// registers = ["r1", "r3", "r4", "f1", "f2", "r7", "r8", "r9", "r10"]
//
// # bgun_add_boost sub_82178668 after `stwu r1,-112(r1)` (r3 = ticks).
// [[midasm_hook]]
// address = 0x8217867C
// name = "PdHookProbeAddBoost"
// registers = ["r3"]
//
// # bgun_disarm sub_8217FED0 after `stwu r1,-176(r1)` (r3 = attacker prop).
// [[midasm_hook]]
// address = 0x8217FEDC
// name = "PdHookProbeDisarm"
// registers = ["r3"]
//
// # shot_create sub_821FC8C0 right after `bl shot_calculate_hits` (pd_probe_ricochet).
// [[midasm_hook]]
// address = 0x821FC9B8
// name = "PdHookProbeRicochet"
//
// # beam_render sub_82182A30 where its colour branches join: r25 = RGBA, r29 = beam.
// [[midasm_hook]]
// address = 0x82182C90
// name = "PdHookProbeBeamColour"
// registers = ["r25", "r29"]
//
// # boltbeams_render sub_821844F0's call to beam_render_generic: r7 / r10 = colours.
// [[midasm_hook]]
// address = 0x82184554
// name = "PdHookProbeBoltBeamColour"
// registers = ["r7", "r10"]
//
// # laptop_deploy sub_8221E5C8's success path, `mr r3,r31` (r31 = the new sentry).
// [[midasm_hook]]
// address = 0x8221E934
// name = "PdHookProbeSentryMade"
// registers = ["r31"]
//
//
// # The player-hit function sub_821274A0: `fmadds f11,f13,f0,f12` (f0 = 0.75 knockback).
// [[midasm_hook]]
// address = 0x821277D4
// name = "PdHookProbePushScale"
// registers = ["f0"]
//
// # bmove_dampen_shotspeed sub_820F2098 after `lfs f12,6188(r10)` (the 1.5 cap).
// [[midasm_hook]]
// address = 0x820F20CC
// name = "PdHookProbePushCap"
// registers = ["f12"]
//
// # The player move tick sub_820F4D40, `bl` to the input step (after the boost doubling).
// [[midasm_hook]]
// address = 0x820F4DB0
// name = "PdHookProbeSlowClock"
//
// # autogun_tick sub_82206E58 at loc_822078E8 (before apply_rotation): goal angles at
// # 84/88(r1), r31 = obj, r18 != 0 = a copy that received the owner's aim.
// [[midasm_hook]]
// address = 0x822078E8
// name = "PdHookProbeAutogunGoals"
// registers = ["r1", "r31", "r18"]
//
// # autogun_tick at loc_82207B08 (before the owner sends its aim and firing flag).
// [[midasm_hook]]
// address = 0x82207B08
// name = "PdHookProbeAutogunFiring"
// registers = ["r31", "r18"]
