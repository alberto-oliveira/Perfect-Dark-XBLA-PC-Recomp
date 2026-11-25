// Debug code of the Detonator Crossbow mod (branch detonator-crossbow): the
// custom-weapon system, the tint, the left-hand detonator and the blinking bolts.
// Not built; kept only so the code can be recovered.
//
// Removed in: the cleanup commit that added this file
// ("Move the Detonator Crossbow debug code to the debug bank").

// ============================================================================
// TEMP_XBOW
// ============================================================================
//
// What for: following crossbow bolts and explosions in a network match (stage 1):
// does each PC make its own copy of a remote player's bolt, do copies share a sync
// id, does one prop_explode on the owner's PC explode everywhere. Logged each
// Detonate press, each prop_explode (with the remote mines' state), each bolt's
// phase changes (flying, landing, landed, in what), bolts gone, and the End key
// (pd_dev_hotkeys, off while pd_solo_mark_keys) dumped the live bolts. Inline lines
// in the mod's real code logged each bolt made (and by which fire path: the hooks
// passed "local" / "replay" to PdXbowBoltCreated as a `const char* path`), the
// scorch alignment, and why a bolt was deleted or left pending.
//
// Hooks / toml entries:
//   PdHookXbowDetonateLog at 0x8220D220 (player_activate_remote_mine_detonator's
//   start, r3 = player) and PdHookXbowExplodeLog at 0x82205A18 (prop_explode's
//   start, r3 = prop, r4 = explosion type), below. PdXbowFrame was called from
//   PdNoteGameFrame; PdXbowBoltTickLog from PdXbowBoltTick.
//
// ---- perfect_dark_config.toml ------------------------------------------------------
//
// # TEMP_XBOW: player_activate_remote_mine_detonator's start (sub_8220D220, r3 = player).
// [[midasm_hook]]
// address = 0x8220D220
// name = "PdHookXbowDetonateLog"
// registers = ["r3"]
//
// # TEMP_XBOW: prop_explode's start (sub_82205A18, r3 = prop, r4 = explosion type).
// [[midasm_hook]]
// address = 0x82205A18
// name = "PdHookXbowExplodeLog"
// registers = ["r3", "r4"]

// ---- src/perfect_dark_hooks.cpp ----------------------------------------------------

// TEMP_XBOW: player_activate_remote_mine_detonator's start (r3 = player number).
void PdHookXbowDetonateLog(PPCRegister& r3) {
  PdXbowDetonateLog(HookBase(), r3.u32);
}

// TEMP_XBOW: prop_explode's start (r3 = prop, r4 = explosion type).
void PdHookXbowExplodeLog(PPCRegister& r3, PPCRegister& r4) {
  PdXbowExplodeLog(HookBase(), r3.u32, r4.u32);
}

// ---- src/perfect_dark_app.h --------------------------------------------------------

void PdXbowRequestDump();  // TEMP_XBOW (main.cpp)
  // TEMP_XBOW: End lists the live crossbow bolts (PdXbowRequestDump, main.cpp).
  bool end_was_down_ = false;
    // TEMP_XBOW (End belongs to the pad marking keys while pd_solo_mark_keys is on)
    bool end_down = !REXCVAR_GET(pd_solo_mark_keys) && (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    if (end_down && !end_was_down_) {
      PdXbowRequestDump();
    }
    end_was_down_ = end_down;

// ---- src/main.cpp ------------------------------------------------------------------

// ---- TEMP_XBOW: detonator crossbow investigation (step 1.1) ----------------------
// Logging only, no change to the game. It answers how bolts and explosions behave
// in a network match: does each PC make its own copy of a remote player's bolt
// (the replay path), do copies share a network id, and does one prop_explode on
// the owner's PC make the same explosion on the others?
namespace {
// What the log last said about a bolt: one line each time it changes.
struct XbowBoltState {
  uint32_t obj = 0;
  int phase = 0;            // XbowPhase: flying, landing (shaking), landed
  uint32_t parent = 0;      // the prop it is stuck in
  uint32_t last_frame = 0;  // xbow_frame when the weapon tick last saw it
};
// weapon_tick's use of a bolt's timer240: -1 while it flies, then a short
// countdown from 13 while it shakes where it hit, then 1 (or 0) once at rest.
int XbowPhase(int16_t timer) {
  return timer < 0 ? 0 : (timer >= 2 ? 1 : 2);
}
const char* const kXbowPhaseNames[] = {"flying", "landing", "landed"};
std::map<uint32_t, XbowBoltState> xbow_bolts;  // by prop; game thread only
uint32_t xbow_frame = 0;                       // counts PdXbowFrame calls
std::atomic<bool> xbow_dump_wanted{false};     // set by the End key (UI thread)

std::string XbowDescribeBolt(const uint8_t* base, uint32_t prop, uint32_t obj) {
  namespace xbow = pd::crossbow;
  return fmt::format("prop 0x{:08X} obj 0x{:08X} owner {} sync id {} timer {} in {}", prop, obj,
                     xbow::OwnerPlayer(base, obj), int32_t(xbow::LoadU32(base, prop + xbow::kPropSyncId)),
                     xbow::LoadS16(base, obj + xbow::kObjTimer240), xbow::DescribeParent(base, prop));
}
}  // namespace

// TEMP_XBOW: sub_8220D220's start (r3 = the player pressing Detonate).
void PdXbowDetonateLog(uint8_t* base, uint32_t player) {
  namespace xbow = pd::crossbow;
  REXLOG_INFO("[xbow] frame {}: detonate by player {} ({}), mask before 0x{:X}",
              xbow::LoadU32(base, xbow::kLevelFrames), player, xbow::PlayerName(base, int(player)),
              xbow::LoadU32(base, xbow::kDetonatingMines));
}

// TEMP_XBOW: prop_explode's start (sub_82205A18; r3 = prop, r4 = explosion type).
void PdXbowExplodeLog(uint8_t* base, uint32_t prop, uint32_t type) {
  namespace xbow = pd::crossbow;
  const uint32_t obj = xbow::LoadU32(base, prop + xbow::kPropObj);
  const bool weapon = base[prop + xbow::kPropType] == xbow::kPropTypeWeapon;
  if (xbow::g_pending.count(prop)) return;  // a bolt's retry: logged once already
  REXLOG_INFO("[xbow] frame {}: prop_explode type {} prop 0x{:08X} (prop type {}, weapon {}) owner {} sync id {} in {}",
              xbow::LoadU32(base, xbow::kLevelFrames), type, prop, base[prop + xbow::kPropType],
              weapon ? int(base[obj + xbow::kObjWeaponNum]) : -1, xbow::OwnerPlayer(base, obj),
              int32_t(xbow::LoadU32(base, prop + xbow::kPropSyncId)), xbow::DescribeParent(base, prop));
  // Test 1.3 found remote mines (weapon 34) whose explosion kept failing: likely
  // mine pickups lying in the level (owner bits 0 = this PC's own player). This
  // shows their state: hidden flags without the owner, timer240, first room
  // (-1 = not in any room, which makes explosion_create refuse).
  if (weapon && base[obj + xbow::kObjWeaponNum] == 34) {
    REXLOG_INFO("[xbow]   remote mine 0x{:08X}: hidden 0x{:07X} timer240 {} room {}", prop,
                xbow::LoadU32(base, obj + xbow::kObjHidden) & 0x0FFFFFFF,
                xbow::LoadS16(base, obj + xbow::kObjTimer240), xbow::LoadS16(base, prop + xbow::kPropRooms));
  }
}

// TEMP_XBOW: a fire path just made a bolt (r3 = its object). `path` = which one.

// TEMP_XBOW: the weapon tick's bolt branch (r27 = prop, r31 = obj). Logs a bolt
// the first time it is seen and each time it lands or changes what it is in.
void PdXbowBoltTickLog(uint8_t* base, uint32_t prop, uint32_t obj) {
  namespace xbow = pd::crossbow;
  const int phase = XbowPhase(xbow::LoadS16(base, obj + xbow::kObjTimer240));
  const uint32_t parent = xbow::LoadU32(base, prop + xbow::kPropParent);
  auto found = xbow_bolts.find(prop);
  const bool is_new = found == xbow_bolts.end() || found->second.obj != obj;
  XbowBoltState& state = xbow_bolts[prop];
  if (is_new || state.phase != phase || state.parent != parent) {
    REXLOG_INFO("[xbow] frame {}: bolt {} {}", xbow::LoadU32(base, xbow::kLevelFrames), kXbowPhaseNames[phase],
                XbowDescribeBolt(base, prop, obj));
  }
  state = {obj, phase, parent, xbow_frame};
}

// TEMP_XBOW: the End key (pd_dev_hotkeys) asks for a list of the live bolts; the
// game thread writes it at its next frame.
void PdXbowRequestDump() {
  xbow_dump_wanted = true;
}

// TEMP_XBOW: each frame (from PdNoteGameFrame, game thread). A bolt the weapon
// tick has not seen for 2 s is gone (exploded, picked up or freed).
void PdXbowFrame(uint8_t* base) {
  namespace xbow = pd::crossbow;
  ++xbow_frame;
  for (auto it = xbow_bolts.begin(); it != xbow_bolts.end();) {
    if (xbow_frame - it->second.last_frame > 120) {
      REXLOG_INFO("[xbow] bolt prop 0x{:08X} obj 0x{:08X} gone", it->first, it->second.obj);
      it = xbow_bolts.erase(it);
    } else {
      ++it;
    }
  }
  if (xbow_dump_wanted.exchange(false)) {
    REXLOG_INFO("[xbow] dump, frame {}: {} live bolts", xbow::LoadU32(base, xbow::kLevelFrames), xbow_bolts.size());
    for (const auto& [prop, state] : xbow_bolts) {
      REXLOG_INFO("[xbow]   {}", XbowDescribeBolt(base, prop, state.obj));
    }
  }
}
// ---- end TEMP_XBOW ---------------------------------------------------------------

// ---- src/main.cpp: lines inside the real code --------------------------------------

  REXLOG_INFO("[xbow] frame {}: {} bolt obj 0x{:08X} made by the {} fire path (current player {})",  // TEMP_XBOW
              xbow::LoadU32(base, xbow::kLevelFrames), detonator ? "Detonator" : "vanilla", obj, path,
              xbow::LoadU32(base, xbow::kGVars + 1196));
    REXLOG_INFO("[xbow] bolt prop 0x{:08X}: no surface along its line (at {:.0f} {:.0f} {:.0f}, line {:.2f} {:.2f} {:.2f}, "  // TEMP_XBOW
                "length {:.1f}, room {}), scorch faces back along the line",
                prop, pos[0], pos[1], pos[2], forward[0] / length, forward[1] / length, forward[2] / length,
                zmax * length, xbow::LoadS16(base, prop + xbow::kPropRooms));
  REXLOG_INFO("[xbow] bolt prop 0x{:08X}: scorch on the surface at ({:.0f} {:.0f} {:.0f}) facing ({:.2f} {:.2f} {:.2f})",  // TEMP_XBOW
              prop, at[0], at[1], at[2], n[0], n[1], n[2]);
  PdXbowBoltTickLog(base, prop, obj);  // TEMP_XBOW
    REXLOG_INFO("[xbow] bolt prop 0x{:08X}: remote owner {} detonated, copy deleted", prop, owner);  // TEMP_XBOW
    REXLOG_INFO("[xbow] bolt prop 0x{:08X}: cannot explode ({}), removed", prop,  // TEMP_XBOW
                outside ? "outside every room" : "no explosion slot for 10 s");
    if (!was_pending) {
      REXLOG_INFO("[xbow] bolt prop 0x{:08X}: no free explosion yet, pending", prop);  // TEMP_XBOW
    }
  PdXbowFrame(base);  // TEMP_XBOW

// ============================================================================
// TEMP_CUSTOM
// ============================================================================
//
// What for: seeing where the game meets a custom weapon number (stage 2): each
// generic custom hook logged what it did (a gun-line check passed, a number seen
// as the base weapon, a spare function bit used), a few lines per kind per stage,
// and the replay of a remote shot logged the weapon number its message carried.
// PdCustomStageStart (called from PdSetupCreateProps) reset the counts.
//
// Hooks: no toml entries of its own; the lines sat in PdHookCustomPass,
// PdHookCustomAsBase, PdHookCustomFuncBit and PdHookCustomFireReplay.
//
// ---- src/perfect_dark_hooks.cpp: declarations --------------------------------------

void PdXbowDetonateLog(uint8_t* base, uint32_t player);              // TEMP_XBOW
void PdXbowExplodeLog(uint8_t* base, uint32_t prop, uint32_t type);  // TEMP_XBOW
void PdRecolourGunLoaded(uint8_t* base, uint32_t result, uint32_t buffer, uint32_t player);  // TEMP_RECOLOUR
void PdRecolourModelLoaded(uint8_t* base, uint32_t model, uint32_t model_x8);              // TEMP_RECOLOUR
void PdCustomSiteLog(uint8_t* base, const char* what, uint32_t value);   // TEMP_CUSTOM
void PdCustomReplayLog(uint8_t* base, uint32_t weapon);                 // TEMP_CUSTOM
void PdCustomLeftHandLog(uint8_t* base, uint32_t weapon, uint32_t modeldef, uint32_t cmds_size);  // TEMP_DET
bool PdDetSiteOff(int site);                         // TEMP_DET
void PdDetGiveTick(PPCContext& ctx, uint8_t* base);  // TEMP_DET

// ---- src/perfect_dark_hooks.cpp: lines inside the real hooks -----------------------

  if (!pd::custom::IsCustom(weapon.u32)) return false;
  PdCustomSiteLog(HookBase(), "a gun-line check (passed)", weapon.u32);  // TEMP_CUSTOM
  return true;
  PdCustomSiteLog(HookBase(), "a weapon-number check (seen as the base weapon)", weapon.u32);  // TEMP_CUSTOM
  PdCustomSiteLog(HookBase(), "a function bit (spare bit used)", weapon_minus_1.u32 + 1);  // TEMP_CUSTOM
// shot's weapon number): logged (TEMP_CUSTOM), then as PdHookCustomAsBase.
void PdHookCustomFireReplay(PPCRegister& r11) {
  PdCustomReplayLog(HookBase(), r11.u32);  // TEMP_CUSTOM

// ---- src/main.cpp (includes the TEMP_DET helpers) ----------------------------------

void PdCustomStageStart();            // TEMP_CUSTOM
  PdCustomStageStart();   // TEMP_CUSTOM
// ---- TEMP_CUSTOM: where the game meets a custom weapon number -------------------
// The custom weapon hooks (perfect_dark_hooks.cpp) log what they did: a few lines
// per kind per stage, so the log stays readable.
namespace {
std::map<std::string, int> custom_site_logs;  // lines logged per kind this stage
}

void PdCustomSiteLog(uint8_t* base, const char* what, uint32_t value) {
  int& logged = custom_site_logs[what];
  if (logged >= 3) return;
  ++logged;
  REXLOG_INFO("[custom] frame {}: weapon 0x{:02X} at {}{}", pd::weapons::LoadU32(base, pd::weapons::kLevelFrames),
              value, what, logged == 3 ? " (no more lines of this kind this stage)" : "");
}

// The replay of a remote player's shot (sub_82171430, at its crossbow check):
// the weapon number its message carried, once per number per stage.
// TEMP_DET: test helpers for stage 4a, driven by the test pipe's `cvar` command.
// pd_det_give: a little after the match starts, the local player is given the
// Detonator Crossbow (inv_give_single_weapon sub_82107108), so a script can switch
// to it (`btn b`) without walking to a pickup. pd_det_off: a bit mask that turns
// single left-hand weapon hooks off, live (bit = the site's number in
// perfect_dark_config.toml), to find which one does what.
REXCVAR_DEFINE_BOOL(pd_det_give, false, "PerfectDark", "TEMP_DET: give the Detonator Crossbow at match start");
REXCVAR_DEFINE_INT32(pd_det_off, 0, "PerfectDark", "TEMP_DET: bit mask of left-hand weapon hooks to turn off");
namespace {
bool det_given = false;
}
void PdDetStageStart() { det_given = false; }
bool PdDetSiteOff(int site) { return (REXCVAR_GET(pd_det_off) >> site) & 1; }
void PdDetGiveTick(PPCContext& ctx, uint8_t* base) {
  if (det_given || !REXCVAR_GET(pd_det_give)) return;
  if (pd::weapons::LoadU32(base, pd::weapons::kLevelFrames) < 120) return;
  det_given = true;
  for (const uint32_t weapon : {uint32_t(pd::crossbow::kWeaponCrossbow), uint32_t(pd::crossbow::kWeaponDetonator)}) {
    PPCContext call = ctx;
    call.r3.u64 = weapon;
    sub_82107108(call, base);  // inv_give_single_weapon (the vanilla crossbow too, as a reference)
    REXLOG_INFO("[det] gave weapon {} (inv_give_single_weapon returned {})", weapon, call.r3.u32);
  }
  PPCContext call = ctx;
  call.r3.u64 = 1;
  sub_8217D6D8(call, base);  // bgun_give_max_ammo(force): every ammo type full
}

// TEMP_DET: the left hand's model was made from a custom weapon's own model.
void PdCustomLeftHandLog(uint8_t* base, uint32_t weapon, uint32_t modeldef, uint32_t cmds_size) {
  REXLOG_INFO("[det] frame {}: weapon 0x{:02X}: left hand model from 0x{:08X}, command list {} bytes{}",
              pd::weapons::LoadU32(base, pd::weapons::kLevelFrames), weapon, modeldef, cmds_size,
              cmds_size > pd::custom::kLeftCmdsSize ? " TOO BIG" : "");
}

void PdCustomReplayLog(uint8_t* base, uint32_t weapon) {
  const std::string key = "replay " + std::to_string(weapon);
  if (custom_site_logs[key]++) return;
  REXLOG_INFO("[custom] frame {}: a remote player's shot was replayed with weapon {} (0x{:02X}){}",
              pd::weapons::LoadU32(base, pd::weapons::kLevelFrames), weapon, weapon,
              pd::custom::IsCustom(weapon) ? ", a custom weapon" : "");
}

void PdCustomStageStart() {
  custom_site_logs.clear();
  PdDetStageStart();  // TEMP_DET
}
// ---- end TEMP_CUSTOM -----------------------------------------------------------------

// ============================================================================
// TEMP_DET
// ============================================================================
//
// What for: stage 4a, the left-hand detonator.
//  - pd_det_give (cvar): at level frame 120 the local player got the vanilla
//    crossbow and the Detonator (inv_give_single_weapon sub_82107108) and full
//    ammo (bgun_give_max_ammo sub_8217D6D8(1)), so a test script could switch to
//    it with `btn b` (the first switch press after the give was ignored). Called
//    from PdHookCustomLeftHandTick.
//  - pd_det_off (cvar): a bit mask turning single left-hand hooks off live, to
//    find which one moved the right hand's crossbow (it was the shared matrix
//    cache). Bits 0-10: the bgun_get_weapon_num sites (one wrapper each,
//    PdHookDetSite0..10, below); 11 the equip hook; 12 the left model instance;
//    13 the tick's model swap; 14 hiding the left hand during reload; 15 skipping
//    the matrix cache.
//  - Logs of the left-hand setup and of the gun model loader (PdCustomLoadGunModel,
//    "[det]" lines; it now logs one "[custom]" line per model).
//
// Hooks / toml entries: each bgun_get_weapon_num site's toml name was
// PdHookDetSiteN instead of the shared PdHookCustomLeftHandWeapon(Addr), as below
// (site 0 = 0x821722FC, 1 = 0x82174F50, 2 = 0x82176490, 3 = 0x82178AFC,
// 4 = 0x82178C70, 5 = 0x82178D24, 6 = 0x8217AC6C, 7 = 0x8217C008, 8 = 0x8217C2FC,
// 9 = 0x8217CCF4, 10 = 0x8218071C).
//
// ---- perfect_dark_config.toml: the site names --------------------------------------
//
// name = "PdHookDetSite0"  # TEMP_DET site 0 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite1"  # TEMP_DET site 1 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite2"  # TEMP_DET site 2 (was PdHookCustomLeftHandWeaponAddr)
// name = "PdHookDetSite3"  # TEMP_DET site 3 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite4"  # TEMP_DET site 4 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite5"  # TEMP_DET site 5 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite6"  # TEMP_DET site 6 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite7"  # TEMP_DET site 7 (was PdHookCustomLeftHandWeaponAddr)
// name = "PdHookDetSite8"  # TEMP_DET site 8 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite9"  # TEMP_DET site 9 (was PdHookCustomLeftHandWeapon)
// name = "PdHookDetSite10"  # TEMP_DET site 10 (was PdHookCustomLeftHandWeapon)

// ---- src/perfect_dark_hooks.cpp ----------------------------------------------------

// TEMP_DET: one wrapper per site (its number = the bit in pd_det_off), so single
// sites can be turned off live to find which one moves the right hand's crossbow
// (test 4a.3c: it points backwards). The cleanup puts the shared names back.
#define PD_DET_SITE2(n)   void PdHookDetSite##n(PPCRegister& hand, PPCRegister& weapon) {     if (!PdDetSiteOff(n)) PdHookCustomLeftHandWeapon(hand, weapon);   }
#define PD_DET_SITE3(n)   void PdHookDetSite##n(PPCRegister& hand_address, PPCRegister& player, PPCRegister& weapon) {     if (!PdDetSiteOff(n)) PdHookCustomLeftHandWeaponAddr(hand_address, player, weapon);   }
PD_DET_SITE2(0)
PD_DET_SITE2(1)
PD_DET_SITE3(2)
PD_DET_SITE2(3)
PD_DET_SITE2(4)
PD_DET_SITE2(5)
PD_DET_SITE2(6)
PD_DET_SITE3(7)
PD_DET_SITE2(8)
PD_DET_SITE2(9)
PD_DET_SITE2(10)

// ---- src/perfect_dark_hooks.cpp: lines inside the real hooks -----------------------

  if (PdDetSiteOff(11)) return;  // TEMP_DET
  PdCustomSiteLog(base, "the weapon switch (left hand set up)", w->number);  // TEMP_DET
  if (PdDetSiteOff(12)) return;  // TEMP_DET
  sub_82172D58(call, base);  // bgun_create_model_cmd_list: returns its size
  const uint32_t size = call.r3.u32;
  custom::StoreU32(base, hand + custom::kHandGunModelCmds, custom::g_left_cmds[i]);
  PdCustomLeftHandLog(base, custom::kList[i].number, custom::g_left_model[i], size);  // TEMP_DET
  PdDetGiveTick(HookContext(), base);  // TEMP_DET
  if (!PdDetSiteOff(13)) r22.u64 = custom::g_left_model[i];  // TEMP_DET bit 13
  if (!PdDetSiteOff(14) && int32_t(
  return i >= 0 && custom::g_left_model[i] && !PdDetSiteOff(15);  // TEMP_DET bit 15

// ---- src/main.cpp: the gun model loader's logs -------------------------------------

  REXLOG_INFO("[det] file 0x{:03X}: inflated {} bytes, load size {}, loaded {} bytes, modeldef 0x{:08X} (block 0x{:08X})",
              id, inflated, load_size, loaded, modeldef, ptr);
  REXLOG_INFO("[det] relocated: root 0x{:08X}, {} parts, {} matrices, {} textures at 0x{:08X}; pool from 0x{:08X}, {} bytes",
              pd::weapons::LoadU32(base, modeldef), int16_t(pd::weapons::LoadU16(base, modeldef + 12)),
              int16_t(pd::weapons::LoadU16(base, modeldef + 14)), int16_t(pd::weapons::LoadU16(base, modeldef + 0x16)),
              pd::weapons::LoadU32(base, modeldef + 0x18), end, remaining);
  call = ctx;
  call.r3.u64 = det_pool;
  sub_823544C8(call, base);  // tex_get_pool_left_pos
  const uint32_t pool_left = call.r3.u32;
  REXLOG_INFO("[det] {} textures loaded; texture pool used up to 0x{:08X} ({} of {} bytes); rwdata {} words; done",
              textures, pool_left, pool_left > end ? pool_left - end : 0, remaining,
              int16_t(pd::weapons::LoadU16(base, modeldef + 0x14)));

// ---- tools/test-scripts/detonator/host.txt (with pd_det_give) ----------------------
//
// # Scenario "detonator": the Detonator Crossbow mod (stages 4a-5: the left-hand
// # detonator, the blinking bolts).
// # Runs after lobby-host.txt. TEMP_DET: pd_det_give gives the host the vanilla
// # crossbow, the Detonator and full ammo. The host equips the Detonator, fires two
// # bolts at the floor, steps aside and takes four screenshots 0.2 s apart (the blink),
// # detonates them, reloads, with screenshots; then the copies are left in the
// # match to play by hand. Buttons: b = previous weapon (the first press after the give
// # is ignored; from unarmed it wraps to the Detonator), y = next, rb = primary /
// # secondary, x = reload, right trigger = fire.
//
// cvar pd_det_give true
// waitplayers {players} 120
// waitsignal ready {clients} 120
// wait 1000
// pick 825FA2F8                # Ready / Start
// waitmenu none 60
// wait 7000
// log detonator: in the match
// btn b                        # (ignored)
// wait 5000
// btn b                        # the Detonator
// wait 6000
// shot det-1-idle
// rstick 0 -24000 350          # look down at the floor, so the bolts land close
// wait 500
// trig 0 255 150
// wait 800
// trig 0 255 150
// wait 1000
// stick -32000 0 500            # step left, to see the bolts from the side
// wait 1000
// shot det-2-fired-a
// wait 200
// shot det-2-fired-b
// wait 200
// shot det-2-fired-c
// wait 200
// shot det-2-fired-d
// btn rb                       # Detonate
// wait 1500
// trig 0 255 150
// wait 300
// shot det-3-detonating
// wait 2500
// btn rb                       # back to the bolt
// wait 1500
// btn x                        # reload
// wait 400
// shot det-4-reloading
// wait 4000
// shot det-5-after
// log detonator: done (the copies stay in the match)

// ============================================================================
// TEMP_RECOLOUR
// ============================================================================
//
// What for: stage 3, learning how the crossbow's models are made before tinting
// the Detonator Crossbow. Logged each gun file the gun loader loaded and each
// model setupLoadModeldef loaded, and dumped the crossbow's and the Hammer's files
// once per session to <exe folder>\dumps\file_XXXX.bin (read with
// tools/re/modeldump.py). Later also log lines in the tint hooks (each tinted
// colour, and in stage 5 the Detonator bolts' blink strength).
//
// Hooks / toml entries: PdHookRecolourGunLoaded at 0x82170524 (r3, r30, r31) and
// PdHookRecolourModelLoaded at 0x821FD678 (r3, r30), below.
//
// ---- perfect_dark_config.toml ------------------------------------------------------
//
// # ---- TEMP_RECOLOUR (step 3.1): the crossbow's model files ------------------------------
// # The gun loader sub_82170488 right after `bl file_load_to_addr` (0x82170520; r3 = its
// # result, r30 = the buffer, r31 = the player, file id at player+5594), and
// # setupLoadModeldef sub_821FD640 at `stwx r3,r30,r31` (r3 = the loaded model, r30 =
// # model number * 8). Log and dump the files.
// [[midasm_hook]]
// address = 0x82170524
// name = "PdHookRecolourGunLoaded"
// registers = ["r3", "r30", "r31"]
//
// [[midasm_hook]]
// address = 0x821FD678
// name = "PdHookRecolourModelLoaded"
// registers = ["r3", "r30"]

// ---- src/perfect_dark_hooks.cpp ----------------------------------------------------

  PdCustomSiteLog(base, "the first-person gun colour (tinted)", weapon);  // TEMP_RECOLOUR
    PdCustomSiteLog(base, "a Detonator bolt colour", strength);  // TEMP_RECOLOUR (value = strength)
  PdCustomSiteLog(base, "a held gun colour (tinted)", base[obj + 92]);  // TEMP_RECOLOUR
  PdCustomSiteLog(base, "an object colour (tinted)", base[obj + 92]);  // TEMP_RECOLOUR
// TEMP_RECOLOUR (step 3.1): right after the gun loader loads a gun file, and right
// after setupLoadModeldef loads a model's file. Log and dump only.
void PdHookRecolourGunLoaded(PPCRegister& r3, PPCRegister& r30, PPCRegister& r31) {
  PdRecolourGunLoaded(HookBase(), r3.u32, r30.u32, r31.u32);
}
void PdHookRecolourModelLoaded(PPCRegister& r3, PPCRegister& r30) {
  PdRecolourModelLoaded(HookBase(), r3.u32, r30.u32);
}

// ---- src/main.cpp ------------------------------------------------------------------

// ---- TEMP_RECOLOUR: step 3.1, the crossbow's model files ------------------------------
// Logging and file dumps only, to learn how the crossbow's models are made before
// tinting the Detonator Crossbow's. Each file is dumped once per session to
// <exe folder>\dumps\file_XXXX.bin (XXXX = file id), as it is right after loading.
namespace {
constexpr uint32_t kFileInfo = 0x826302F0;  // g_FileInfo: per file id {u32 loaded size, u32 allocated size}
std::set<uint32_t> recolour_dumped;          // file ids dumped this session

void RecolourDump(const uint8_t* base, uint32_t file_id, uint32_t address) {
  if (!recolour_dumped.insert(file_id).second) return;
  uint32_t size = pd::weapons::LoadU32(base, kFileInfo + file_id * 8);
  if (size == 0) size = pd::weapons::LoadU32(base, kFileInfo + file_id * 8 + 4);
  if (size == 0 || size > 4 * 1024 * 1024) {
    REXLOG_INFO("[recolour] file 0x{:04X}: no usable size ({}), not dumped", file_id, size);
    return;
  }
  wchar_t exe[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const std::filesystem::path folder = std::filesystem::path(std::wstring(exe, n)).parent_path() / L"dumps";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const std::filesystem::path path = folder / fmt::format("file_{:04X}.bin", file_id);
  FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return;
  std::fwrite(base + address, 1, size, f);
  std::fclose(f);
  REXLOG_INFO("[recolour] file 0x{:04X}: {} bytes from 0x{:08X} dumped to {}", file_id, size, address, path.string());
}
}  // namespace

// The gun loader (sub_82170488) right after file_load_to_addr loaded a gun file
// (0x82170524; r3 = what it returned, r30 = the buffer, r31 = the player).
void PdRecolourGunLoaded(uint8_t* base, uint32_t result, uint32_t buffer, uint32_t player) {
  const uint32_t file_id = pd::weapons::LoadU16(base, player + 5594);
  std::string fields;  // the player's gun fields around the load, to find the weapon being loaded
  for (uint32_t o = 5536; o < 5612; ++o) fields += fmt::format("{:02X}{}", base[player + o], (o % 4 == 3) ? " " : "");
  REXLOG_INFO("[recolour] gun file 0x{:04X} loaded to 0x{:08X} (returned 0x{:08X}); weapon byte +5544 = {}; "
              "player+5536..5611: {}",
              file_id, buffer, result, int(int8_t(base[player + 5544])), fields);
  RecolourDump(base, file_id, buffer);
}

// setupLoadModeldef (sub_821FD640) right after it loaded a model's file
// (0x821FD678; r3 = the loaded model, r30 = model number * 8).
void PdRecolourModelLoaded(uint8_t* base, uint32_t model, uint32_t model_x8) {
  const uint32_t number = model_x8 / 8;
  const uint32_t file_id = pd::weapons::LoadU16(base, pd::weapons::kModelStates + model_x8 + 4);
  REXLOG_INFO("[recolour] model 0x{:03X} (file 0x{:04X}) loaded to 0x{:08X}", number, file_id, model);
  if (number == 0x10D || number == 0x198) RecolourDump(base, file_id, model);  // the crossbow's, the Hammer's
}
// ---- end TEMP_RECOLOUR -----------------------------------------------------------------
