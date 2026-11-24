// Perfect Dark XBLA PC Recomp - the codegen hooks named in perfect_dark_config.toml.
//
// The recompiled code is generated from the player's own default.xex, so the port's
// changes to the game cannot be edits to it: each is a hook that ReXGlue's codegen
// calls at a guest address. The registers a hook lists in the config are passed by
// reference; a hook that returns bool decides the config's return or jump. The
// behaviour itself lives in main.cpp and perfect_dark_app.h; these are the glue.

#include "generated/default/perfect_dark_init.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

#include "pd_crossbow.h"        // the Detonator Crossbow
#include "pd_custom_weapons.h"

// main.cpp
bool PdSkipArcadeLogo();
bool PdN64Poison(const uint8_t* base);
int16_t PdN64PoisonRespawnCap();
int32_t PdLegalScreenTicks();
bool PdSmoothGameClock();
uint32_t PdOsCountStep(uint32_t real_step);
void PdNoteGameFrame(uint8_t* base);
uint64_t PdTimerMicroseconds(uint64_t ticks);
void PdGpuWaitBackoff();
bool PdIdleSleep();
void PdNetFrameWaitBackoff();
bool PdMenuSkipScanWanted(uint32_t dialog);
void PdMenuSkipNote(uint32_t item, uint32_t skipped);
bool PdExtraAllCheats();
bool PdExtraAllMultiplayer();
bool PdDefaultAutoAim();
uint32_t PdRoomBrightnessFloor(uint32_t brightness);
bool PdAspect43();
float PdDarkRoomObjectVisibility();
void PdViewBobHold(uint8_t* base, uint32_t sp);
int64_t PdMpUpdateRate();
int64_t PdMpUpdateRateMin();
int64_t PdMpFacingDelayMs();
uint32_t PdNetLocalInput(uint32_t input);
void PdNetLookReceive(uint32_t record, uint32_t input);
void PdNetLookSend(uint32_t input, uint32_t frame);
void PdNetLookApply(uint32_t pad);
void PdMpChooseTrack(PPCContext& ctx, uint8_t* base);
void PdSetupCreateProps(PPCContext& ctx, uint8_t* base);
bool PdWeaponByLocation(const uint8_t* base, int32_t location, uint32_t* entry);
void PdPropEnable(PPCContext& ctx, uint8_t* base, uint32_t prop);
bool PdMakeCrateWithoutAmmo(uint8_t* base, uint32_t crate_obj);
uint32_t PdSoloStageSetup(PPCContext& ctx, uint8_t* base, uint32_t loaded);
uint32_t PdStageNameText(const uint8_t* base, uint32_t offset, uint32_t text);
uint32_t PdArenaRowLabel(PPCContext& ctx, uint8_t* base);
bool PdRandomArena(uint32_t* stage);

void PdXbowDetonateLog(uint8_t* base, uint32_t player);              // TEMP_XBOW
void PdXbowExplodeLog(uint8_t* base, uint32_t prop, uint32_t type);  // TEMP_XBOW
void PdXbowBoltCreated(uint8_t* base, uint32_t obj, const char* path);
void PdRecolourGunLoaded(uint8_t* base, uint32_t result, uint32_t buffer, uint32_t player);  // TEMP_RECOLOUR
void PdRecolourModelLoaded(uint8_t* base, uint32_t model, uint32_t model_x8);              // TEMP_RECOLOUR
bool PdXbowBoltTick(PPCContext& ctx, uint8_t* base, uint32_t prop, uint32_t obj);
void PdCustomSiteLog(uint8_t* base, const char* what, uint32_t value);   // TEMP_CUSTOM
void PdCustomReplayLog(uint8_t* base, uint32_t weapon);                 // TEMP_CUSTOM
void PdCustomLeftHandLog(uint8_t* base, uint32_t weapon, uint32_t modeldef, uint32_t cmds_size);  // TEMP_DET
bool PdDetSiteOff(int site);                         // TEMP_DET
void PdDetGiveTick(PPCContext& ctx, uint8_t* base);  // TEMP_DET

namespace {
// The calling guest thread's full context and guest memory, for a hook that needs
// more than its registers.
PPCContext& HookContext() {
  return *rex::runtime::ThreadState::Get()->context();
}
uint8_t* HookBase() {
  return rex::system::kernel_state()->memory()->virtual_membase();
}
uint32_t LoadU32(uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return __builtin_bswap32(value);
}
void StoreU8(uint8_t* base, uint32_t address, uint8_t value) {
  base[address] = value;
}
}  // namespace

// ---- Start-up ---------------------------------------------------------------------

bool PdHookSkipArcadeLogo() {
  return PdSkipArcadeLogo();
}

// After titleTickLegal's `cmpwi cr6,r11,180`.
void PdHookLegalScreenCompare(PPCRegister& r11, PPCCRRegister& cr6, PPCXERRegister& xer) {
  cr6.compare<int32_t>(r11.s32, PdLegalScreenTicks(), xer);
}

// ---- Game clock and CPU use -------------------------------------------------------

// osGetCount after `lwz r11,92(r1)` (r11 = the counter units since the last call).
// True jumps to 0x8224FC44 with r11 = the new total, as the game's own path does.
bool PdHookOsGetCount(PPCRegister& r9, PPCRegister& r10, PPCRegister& r11) {
  uint8_t* base = HookBase();
  PdNoteGameFrame(base);
  if (!PdSmoothGameClock()) return false;
  r9.s64 = -2107310080;  // 0x82650000
  r11.u64 = PdOsCountStep(r11.u32);
  r10.u64 = LoadU32(base, r9.u32 + 8340);
  r11.u64 = r10.u64 + r11.u64;
  return true;
}

void PdHookTimerR3FromR9(PPCRegister& r3, PPCRegister& r9) {
  r3.u64 = PdTimerMicroseconds(r9.u64);
}
void PdHookTimerR7FromR9(PPCRegister& r7, PPCRegister& r9) {
  r7.u64 = PdTimerMicroseconds(r9.u64);
}
void PdHookTimerR3FromR10(PPCRegister& r3, PPCRegister& r10) {
  r3.u64 = PdTimerMicroseconds(r10.u64);
}

void PdHookGpuWaitBackoff() {
  PdGpuWaitBackoff();
}

// Before `bl Sleep(0)`: true skips the call, with Sleep's result 0.
bool PdHookIdleSleep(PPCRegister& r3) {
  if (!PdIdleSleep()) return false;
  r3.u64 = 0;
  return true;
}

// After the frame wait's `cmplw cr6,r9,r6` (sub_8214D6C8): it goes round again while
// the frames counted (r9) are short of the step the game wants (r6). Only the network
// clock's path waits for real time (g_Vars+0x731 set); the local one never turns.
void PdHookNetFrameWait(PPCRegister& r9, PPCRegister& r6) {
  if (r9.u32 < r6.u32 && HookBase()[0x82648BF8 + 0x731]) PdNetFrameWaitBackoff();
}

// ---- Controls ---------------------------------------------------------------------

// The three Auto-Aim hooks do nothing when pd_default_auto_aim is on: the game's
// own values (Auto-Aim on) stand.
// After `ori r9,r11,2`: the Auto-Aim bit stays off.
void PdHookAutoAimOffR9(PPCRegister& r9, PPCRegister& r11) {
  if (PdDefaultAutoAim()) return;
  r9.u64 = r11.u64 & ~uint64_t(2);
}
// After `li r4,1` (default control settings): Auto-Aim off.
void PdHookAutoAimOffR4(PPCRegister& r4) {
  if (PdDefaultAutoAim()) return;
  r4.s64 = 0;
}
// After `ori r11,r11,9130` (a new profile's controls; 9130 includes Auto-Aim, 2): the
// bit is kept only if the stored flags at r29 already had it, as `| 9128` would.
void PdHookAutoAimOffProfile(PPCRegister& r11, PPCRegister& r29) {
  if (PdDefaultAutoAim()) return;
  const uint32_t stored = LoadU32(HookBase(), r29.u32);
  r11.u64 = (r11.u64 & ~uint64_t(2)) | (stored & 2);
}

// After the storage-device call: X does not open the selector.
void PdHookNoStorageSelector(PPCRegister& r3) {
  r3.u64 = 0;
}
// Replaces `beq cr6,0x82282a70` with an unconditional jump (the config's jump_address).
void PdHookNoStorageLegend() {}

// ---- Menus ------------------------------------------------------------------------

// The dialog tick's start: report which of the dialog's rows the game would skip
// (greyed out or hidden - sub_821C1F38, the test its own focus move uses), so the
// mouse never puts the highlight on one. Rows are the dialog's item array, 20 bytes each.
void PdHookMenuSkipScan(PPCRegister& r3) {
  const uint32_t dialog = r3.u32;
  if (!dialog || !PdMenuSkipScanWanted(dialog)) return;
  uint8_t* base = HookBase();
  const uint32_t def = LoadU32(base, dialog + 0);
  const uint32_t items = def ? LoadU32(base, def + 8) : 0;
  for (uint32_t i = 0; items && i < 40; ++i) {
    const uint32_t item = items + i * 20;
    const uint32_t type = base[item];
    if (type == 0 || type == 29) break;
    PPCContext ctx = HookContext();
    ctx.r3.u64 = item;
    ctx.r4.u64 = dialog;
    sub_821C1F38(ctx, base);
    PdMenuSkipNote(item, ctx.r3.u32 != 0);
  }
}

// mpIsFeatureUnlocked / cheatIsUnlocked: true returns 1 at once while the extra is on.
bool PdHookAllMultiplayer(PPCRegister& r3) {
  if (!PdExtraAllMultiplayer()) return false;
  r3.u64 = 1;
  return true;
}
bool PdHookAllCheats(PPCRegister& r3) {
  if (!PdExtraAllCheats()) return false;
  r3.u64 = 1;
  return true;
}

// ---- Graphics ---------------------------------------------------------------------

// optionsGetScreenRatio: with Aspect Ratio 4:3 it answers 0 (normal), so views get the
// 4:3 field of view; otherwise the game's own answer (16:9).
bool PdHookScreenRatio(PPCRegister& r3) {
  if (!PdAspect43()) return false;
  r3.u64 = 0;
  return true;
}

void PdHookRoomBrightness(PPCRegister& r3) {
  r3.u64 = PdRoomBrightnessFloor(r3.u32);
}

// After `stfs f9,6092(r9)` (FogCol.a): the stored alpha goes through the dark-room
// remap. f12/f11/f10 still hold r, g, b; f9 itself is left as it was.
void PdHookShadeAlpha(PPCRegister& r9, PPCRegister& f9, PPCRegister& f10, PPCRegister& f11,
                      PPCRegister& f12) {
  constexpr float kShadeStart = 0.75f;
  const float r = float(f12.f64), g = float(f11.f64), b = float(f10.f64);
  float a = float(f9.f64);
  if (r <= 0.004f && g <= 0.004f && b <= 0.004f) {
    const float remapped = (a - kShadeStart) / (1.0f - kShadeStart);
    const float darkest = 1.0f - PdDarkRoomObjectVisibility();
    a = remapped < 0.0f ? 0.0f : (remapped > darkest ? darkest : remapped);
  }
  uint32_t bits;
  std::memcpy(&bits, &a, 4);
  bits = __builtin_bswap32(bits);
  std::memcpy(HookBase() + r9.u32 + 6092, &bits, 4);
}

// Before the head update hands on its eye offset (r1+80), look (r1+112) and up (r1+96).
void PdHookViewBobHold(PPCRegister& r1) {
  PdViewBobHold(HookBase(), r1.u32);
}

// ---- Multiplayer ------------------------------------------------------------------

// The lobby's LIVE name check: no check wanted, none in flight, every name fine; the
// function returns without sending the request (config: return).
void PdHookNameVerification(PPCRegister& r3) {
  uint8_t* base = HookBase();
  StoreU8(base, r3.u32 + 172, 0);
  StoreU8(base, r3.u32 + 173, 0);
  StoreU8(base, r3.u32 + 174, 1);
}

void PdHookUpdateRateMinR4(PPCRegister& r4) {
  r4.s64 = PdMpUpdateRateMin();
}
void PdHookUpdateRateR5(PPCRegister& r5) {
  r5.s64 = PdMpUpdateRate();
}
void PdHookUpdateRateMinR8(PPCRegister& r8) {
  r8.s64 = PdMpUpdateRateMin();
}
void PdHookUpdateRateR7(PPCRegister& r7) {
  r7.s64 = PdMpUpdateRate();
}

// After `addi r6,r10,-200`.
void PdHookFacingDelay(PPCRegister& r6, PPCRegister& r10) {
  r6.s64 = r10.s64 - PdMpFacingDelayMs();
}

void PdHookNetLocalInput(PPCRegister& r4) {
  r4.u64 = PdNetLocalInput(r4.u32);
}
void PdHookNetLookReceive(PPCRegister& r3, PPCRegister& r4) {
  PdNetLookReceive(r3.u32, r4.u32);
}
void PdHookNetLookSend(PPCRegister& r4, PPCRegister& r5) {
  PdNetLookSend(r4.u32, r5.u32);
}
void PdHookNetLookApply(PPCRegister& r4) {
  PdNetLookApply(r4.u32 & 0xFF);
}

// In place of the tail call to mpChooseTrack (config: return).
void PdHookChooseTrack() {
  PdMpChooseTrack(HookContext(), HookBase());
}

// setupCreateProps' start, before the stage's props are made: decide this stage's
// weapon slots (pd_mp_weapons, or the host's) and note its weapon pickups.
void PdHookSetupCreateProps() {
  PdSetupCreateProps(HookContext(), HookBase());
}

// mpGetMpWeaponByLocation's start (r3 = a weapon spawn location, 0-15). With extra
// weapon slots, true returns at once with r3 = the weapon table entry it gets.
bool PdHookWeaponByLocation(PPCRegister& r3) {
  uint32_t entry = 0;
  if (!PdWeaponByLocation(HookBase(), r3.s32, &entry)) return false;
  r3.u64 = entry;
  return true;
}

// propEnable's start (r3 = the prop being put on show): a respawning weapon
// pickup may become a different weapon first (pd_mp_weapon_shuffle).
void PdHookPropEnable(PPCRegister& r3) {
  PdPropEnable(HookContext(), HookBase(), r3.u32);
}

// setupCreateProps' ammo-crate case at `cmpwi r10,0` (r10 = the weapon's first ammo
// quantity, r31 = the crate's setup object): 0 skips the crate. A nonzero r10 makes
// it (r10 is reloaded right after, so nothing else sees the change).
void PdHookCrateWithoutAmmo(PPCRegister& r10, PPCRegister& r31) {
  if (r10.s32 <= 0 && PdMakeCrateWithoutAmmo(HookBase(), r31.u32)) r10.u64 = 1;
}

// ---- Solo stages as arenas (pd_stages.h) ------------------------------------------

// setup_load_files, after `bl file_load_to_new` loaded the stage's setup file (r3 =
// the buffer, about to be kept as the stage's setup). A solo stage's multiplayer
// setup is an empty stub: r3 becomes the setup the mod builds for it.
void PdHookSoloStageSetup(PPCRegister& r3) {
  r3.u64 = PdSoloStageSetup(HookContext(), HookBase(), r3.u32);
}

// The arena name lists, before `bl 0x8226c9f0` (text lookup, r4 = text id) with the
// entry's offset in r10 / r8: a stage not in the arena table gets a real name.
void PdHookStageNameTextR10(PPCRegister& r4, PPCRegister& r10) {
  r4.u64 = PdStageNameText(HookBase(), r10.u32, r4.u32);
}
void PdHookStageNameTextR8(PPCRegister& r4, PPCRegister& r8) {
  r4.u64 = PdStageNameText(HookBase(), r8.u32, r4.u32);
}

// The Arena row label (sub_821DA2C0) where it found no g_MpArenas entry for the
// stage (it would return an empty string): a solo stage's name instead.
bool PdHookArenaRowLabel(PPCRegister& r3) {
  const uint32_t label = PdArenaRowLabel(HookContext(), HookBase());
  if (!label) return false;
  r3.u64 = label;
  return true;
}

// The Random arena's pick (sub_821D51A0), at its start: with pd_solo_in_random
// the mod picks, solo stages included, and returns the stage in r3.
bool PdHookRandomArena(PPCRegister& r3) {
  uint32_t stage = 0;
  if (!PdRandomArena(&stage)) return false;
  r3.u64 = stage;
  return true;
}

// Before `bl 0x821d0b28` (set handicap: slot r3, value r4) in the join path, which
// runs again every time the Combat Simulator screen opens: keep the slot's current
// handicap instead of resetting it to 128.
void PdHookHandicapResetJoin(PPCRegister& r3, PPCRegister& r4) {
  const uint32_t slot = r3.u32;
  if (slot >= 4) return;  // the Handicaps dialog has 4 sliders
  r4.u64 = HookBase()[0x82641950 + slot * 216 + 0xCD];
}

// At bondstartnewlife's `sth r30,856(r11)` (r11 = the respawning player's chr;
// it zeroes the poison counter, then the blur). With pd_n64_poison, true jumps past
// both stores, the poison counter is capped (PdN64PoisonRespawnCap) and the
// blur's death count is cleared, so the kept blur fades at its normal speed.
bool PdHookKeepPoisonOnRespawn(PPCRegister& r11) {
  uint8_t* base = HookBase();
  if (!PdN64Poison(base)) return false;
  uint8_t* chr = base + r11.u32;
  const int16_t poison = int16_t((chr[856] << 8) | chr[857]);  // s16, big-endian
  const int16_t cap = PdN64PoisonRespawnCap();
  if (poison > cap) {
    chr[856] = uint8_t(uint16_t(cap) >> 8);
    chr[857] = uint8_t(cap);
  }
  chr[767] = 0;  // blurnumtimesdied: the blur fades (deaths + 1) times faster
  return true;
}

// The player index whose chr this is (g_Vars.players[i]->prop == chr->prop), or -1.
static int PoisonVictimPlayer(uint8_t* base, uint32_t chr) {
  const uint32_t prop = LoadU32(base, chr + 28);
  for (int i = 0; i < 8; ++i) {
    const uint32_t player = LoadU32(base, 0x82648BF8 + 100 + uint32_t(i) * 4);
    if (player && LoadU32(base, player + 188) == prop) return i;
  }
  return -1;
}

// chr_tick_poisoned's MP damage call (`bl 0x8212dd90`, 1.3 every 720 of the
// counter; r31 = the poisoned chr). Every PC ticks every chr's poison, but only the
// victim's own PC knows when that player is dead and holds the counter then; on the
// other PCs a remote player's health always reads full, so their counters run on
// through the death and their ticks kill at other moments. With pd_n64_poison, true
// skips the damage unless the victim is a player on this PC (config +109 = its pad,
// -1 for a remote player).
bool PdHookPoisonTickLocalOnly(PPCRegister& r31) {
  uint8_t* base = HookBase();
  const int player = PoisonVictimPlayer(base, r31.u32);
  bool remote = false;
  if (player >= 0) {
    const uint32_t cfg = LoadU32(base, 0x82648BF8 + 132 + uint32_t(player) * 128 + 112);
    remote = cfg < 8 && int8_t(base[0x82641950 + cfg * 216 + 109]) < 0;
  }
  return remote && PdN64Poison(base);
}

// ---- Detonator crossbow (pd_crossbow.h) -------------------------------------------

// TEMP_XBOW: player_activate_remote_mine_detonator's start (r3 = player number).
void PdHookXbowDetonateLog(PPCRegister& r3) {
  PdXbowDetonateLog(HookBase(), r3.u32);
}

// TEMP_XBOW: prop_explode's start (r3 = prop, r4 = explosion type).
void PdHookXbowExplodeLog(PPCRegister& r3, PPCRegister& r4) {
  PdXbowExplodeLog(HookBase(), r3.u32, r4.u32);
}

// Right after each fire path's weapon_create_projectile_from_weapon_num call for
// a crossbow (r3 = the new bolt, 0 if none could be made): the bolt is tagged a
// Detonator bolt when the Detonator Crossbow fired it.
void PdHookXbowBoltMadeLocal(PPCRegister& r3) { PdXbowBoltCreated(HookBase(), r3.u32, "local"); }
void PdHookXbowBoltMadeReplay(PPCRegister& r3) { PdXbowBoltCreated(HookBase(), r3.u32, "replay"); }

// weapon_tick at the bolt branch's `cmplwi cr6,r11,86` (r11 = the weapon number,
// r27 = the prop, r31 = the object). Every weapon not handled by an earlier branch
// passes here, so it first checks for a bolt. True = the bolt exploded: the config
// jumps to the end of the weapon branches (0x82215678), where the mine branch goes
// after its explosion.
bool PdHookXbowBoltTick(PPCRegister& r11, PPCRegister& r27, PPCRegister& r31) {
  if (r11.u32 != 86) return false;
  return PdXbowBoltTick(HookContext(), HookBase(), r27.u32, r31.u32);
}

// ---- Custom weapons: the left hand (pd_custom_weapons.h, stage 4a) ------------------

// The weapon switch (sub_8217C4A8) right after it set both hands' weapon to the new
// one (0x8217C7FC, before inv_calculate_current_index; r26 = the player). For a
// custom weapon with a left-hand item: dual-wielding on, the left hand in use,
// holding that item (the remote mine for the Detonator, whose left hand the game
// then treats as its detonator).
void PdHookCustomLeftHandEquip(PPCRegister& r26) {
  namespace custom = pd::custom;
  uint8_t* base = HookBase();
  const uint32_t player = r26.u32;
  const custom::Weapon* w = custom::Find(uint8_t(base[player + custom::kPlayerWeapon]));
  if (!w || !w->left_weapon) return;
  if (PdDetSiteOff(11)) return;  // TEMP_DET
  base[player + custom::kPlayerDualWield] |= 0x80;
  base[player + custom::kHandLeft + custom::kHandInUse] = 1;
  base[player + custom::kHandLeft + custom::kHandWeapon] = w->left_weapon;
  PdCustomSiteLog(base, "the weapon switch (left hand set up)", w->number);  // TEMP_DET
}

// The gun loader (sub_8217B268) right after it made both hands' model instances
// from the player's gun model (0x8217B6D0; r31 = the player). For a custom weapon
// with a left-hand model, the left hand's instance and command list are made
// again from that model (model_init sub_82195A70, bgun_create_model_cmd_list
// sub_82172D58), the list in memory of our own.
void PdHookCustomLeftHandModels(PPCRegister& r31) {
  namespace custom = pd::custom;
  uint8_t* base = HookBase();
  const uint32_t player = r31.u32;
  const int i = custom::IndexOf(custom::LoadU32(base, player + custom::kPlayerGunMemNew));
  if (i < 0 || !custom::g_left_model[i] || !custom::g_left_cmds[i]) return;
  if (PdDetSiteOff(12)) return;  // TEMP_DET
  const uint32_t hand = player + custom::kHandLeft;
  const uint32_t model = hand + custom::kHandGunModel;
  PPCContext call = HookContext();
  call.r3.u64 = model;
  call.r4.u64 = custom::g_left_model[i];
  call.r5.u64 = hand + custom::kHandGunModelSave;
  call.r6.u64 = 0;
  sub_82195A70(call, base);  // model_init
  call = HookContext();
  call.r3.u64 = model;
  call.r4.u64 = custom::LoadU32(base, custom::g_left_model[i]);  // the model's root node
  call.r5.u64 = custom::g_left_cmds[i];
  sub_82172D58(call, base);  // bgun_create_model_cmd_list: returns its size
  const uint32_t size = call.r3.u32;
  custom::StoreU32(base, hand + custom::kHandGunModelCmds, custom::g_left_cmds[i]);
  PdCustomLeftHandLog(base, custom::kList[i].number, custom::g_left_model[i], size);  // TEMP_DET
}

// bgun_get_weapon_num (sub_821722D0; r3 = the hand number) right after it read the
// player's weapon (0x821722FC, r11): the game asks it, not the hand's own weapon,
// for almost everything a hand does (its weapon info and definition, animations,
// functions, part visibility, the detonator test). For the left hand of a custom
// weapon with a left-hand item the answer is that item, so the left hand behaves
// as the remote mine's detonator hand (test 4a.3: without this it played the
// crossbow's animations on the mine model and fired).
// The compiler also INLINED this function in about a dozen places (test 4a.3b: the
// hand tick's copy left the detonator frozen in the middle of the screen); each
// copy gets the same hook at its `extsb`, passing whatever register still tells
// the hand: its number (left = 1) or its offset from the player (left = 1956).
namespace {
void LeftHandItem(PPCRegister& weapon) {
  const pd::custom::Weapon* w = pd::custom::Find(uint8_t(weapon.u32));
  if (w && w->left_weapon) weapon.u64 = w->left_weapon;
}
}  // namespace
void PdHookCustomLeftHandWeapon(PPCRegister& hand, PPCRegister& weapon) {
  if (hand.u32 == 1 || hand.u32 == pd::custom::kHandSize) LeftHandItem(weapon);
}
// The same, where the hand is only known by its address next to the player's.
void PdHookCustomLeftHandWeaponAddr(PPCRegister& hand_address, PPCRegister& player, PPCRegister& weapon) {
  if (hand_address.u32 - player.u32 == pd::custom::kHandSize) LeftHandItem(weapon);
}

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

// bgun_tick_gameplay (sub_821807A0) where it decides which hands fire: "both hands
// take turns unless the weapon is the remote mine" (0x82180C68, r10) and "the
// remote mine fires only the right hand" (0x82180D58, r11), each comparing the
// player's weapon with 34. A custom weapon with a left-hand item reads as that item.
void PdHookCustomAsLeftWeapon(PPCRegister& weapon) {
  const pd::custom::Weapon* w = pd::custom::Find(uint8_t(weapon.u32));
  if (w && w->left_weapon) weapon.u64 = w->left_weapon;
}

// bgun_tick_hand (sub_82174EC8; r14 = the hand number) right after it read the
// player's gun model for this hand (`lwz r22,5560(r25)` at 0x821753E4; r25 = the
// player): the left hand of a custom weapon with a left-hand model uses that
// model, when its instance is made from it.
void PdHookCustomLeftHandTick(PPCRegister& r14, PPCRegister& r22, PPCRegister& r25) {
  namespace custom = pd::custom;
  uint8_t* base = HookBase();
  PdDetGiveTick(HookContext(), base);  // TEMP_DET
  if (r14.u32 != 1) return;  // the left hand
  const uint32_t player = r25.u32;
  const int i = custom::IndexOf(uint8_t(base[player + custom::kPlayerWeapon]));
  if (i < 0 || !custom::g_left_model[i]) return;
  const uint32_t model = player + custom::kHandLeft + custom::kHandGunModel;
  if (custom::LoadU32(base, model + custom::kModelDefinition) != custom::g_left_model[i]) return;
  if (!PdDetSiteOff(13)) r22.u64 = custom::g_left_model[i];  // TEMP_DET bit 13
  // While the right hand reloads, the left-hand item is hidden: the crossbow's
  // reload animation brings in a left arm of its own (test 4a.3e: two left hands).
  // The hand stays ticked, only not drawn this frame.
  const uint32_t right = player + custom::kPlayerHands;
  if (!PdDetSiteOff(14) && int32_t(custom::LoadU32(base, right + custom::kHandState)) == custom::kHandStateReload) {
    base[player + custom::kHandLeft + custom::kHandVisible] = 0;
  }
}

// bgun_tick_hand (sub_82174EC8) at 0x821759E8, where a gun that is not animating
// (r30 = N64's a0) has its joints worked out ONCE into a cache, the right hand's
// matrix block (hands[RIGHT].unk0dd8, flag unk0dd4 at player+3580), which BOTH
// hands then reuse. Fine when both hands hold the same model; with a left-hand
// model of our own it mixed the two (test 4a.3h: the idle crossbow drawn with the
// mine model's joints, its body turned). The left hand of a custom weapon with a
// left-hand model skips the cache: true jumps to the per-frame path (0x82175AB0).
bool PdHookCustomLeftHandNoCache(PPCRegister& r14, PPCRegister& r25) {
  namespace custom = pd::custom;
  if (r14.u32 != 1) return false;  // the left hand
  uint8_t* base = HookBase();
  const int i = custom::IndexOf(uint8_t(base[r25.u32 + custom::kPlayerWeapon]));
  return i >= 0 && custom::g_left_model[i] && !PdDetSiteOff(15);  // TEMP_DET bit 15
}

// ---- Custom weapon tints (pd_custom_weapons.h, stage 3) ----------------------------

// The first-person gun renderer (sub_821761F8) at 0x821767D8, where every path has
// stored the gun's render colour at 148(r1) (the room's light on the gun,
// gunshadecol at player+5692, blended towards red for a charging Mauler); r29 =
// the hand (its weapon number in its first byte). For a custom weapon the colour
// is blended towards its tint, as the Mauler's charge does. r28 keeps the colour
// from before the Mauler's blend: the game draws the hands (arms) with it, so it
// is left alone (test 3.2b: tinting it tinted the arms too).
void PdHookCustomGunColour(PPCRegister& r1, PPCRegister& r29) {
  uint8_t* base = HookBase();
  const uint32_t weapon = base[r29.u32];
  const uint32_t tint = pd::custom::TintOf(weapon);
  if (tint == pd::custom::kNoTint) return;
  PdCustomSiteLog(base, "the first-person gun colour (tinted)", weapon);  // TEMP_RECOLOUR
  const uint32_t at = r1.u32 + 148;
  pd::custom::StoreU32(base, at, pd::custom::TintRgba(pd::custom::LoadU32(base, at), tint));
}

// A gun in a player's hand is not drawn by obj_render: chr_render_attached_object
// (sub_8210A070; r31 = the attached prop) draws it with the player's render data
// (model_render sub_82194778 at 0x8210A0AC; r3 = the render data, whose colour is
// fogcolour at +0x38). For a custom weapon that colour is tinted just for this
// model_render call and put back right after it (0x8210A0B0, r30 = the render
// data), so the player's body keeps its own colour.
namespace {
uint32_t held_colour_saved = 0;
bool held_colour_changed = false;
}
void PdHookCustomHeldGunBefore(PPCRegister& r3, PPCRegister& r31) {
  uint8_t* base = HookBase();
  const uint32_t prop = r31.u32;
  if (!prop || base[prop] != 4) return;  // prop type 4 = a weapon
  const uint32_t obj = pd::custom::LoadU32(base, prop + 4);
  if (!obj) return;
  const uint32_t weapon = base[obj + 92];
  const uint32_t tint = pd::custom::TintOf(weapon);
  if (tint == pd::custom::kNoTint) return;
  PdCustomSiteLog(base, "a held gun colour (tinted)", weapon);  // TEMP_RECOLOUR
  const uint32_t at = r3.u32 + 0x38;
  held_colour_saved = pd::custom::LoadU32(base, at);
  held_colour_changed = true;
  pd::custom::StoreU32(base, at, pd::custom::TintRgba(held_colour_saved, tint));
}
void PdHookCustomHeldGunAfter(PPCRegister& r30) {
  if (!held_colour_changed) return;
  held_colour_changed = false;
  pd::custom::StoreU32(HookBase(), r30.u32 + 0x38, held_colour_saved);
}

// scenario_highlight_prop (sub_821E1900; r3 = the prop, r4 = its colour, s32[4] RGBA),
// which obj_render calls in multiplayer before drawing an object: a weapon object
// (a pickup, or a gun in a player's hand) of a custom weapon gets its colour
// blended towards its tint, before the scenario may highlight it.
void PdHookCustomObjectColour(PPCRegister& r3, PPCRegister& r4) {
  uint8_t* base = HookBase();
  const uint32_t prop = r3.u32;
  if (!prop || base[prop] != 4) return;  // prop type 4 = a weapon
  const uint32_t obj = pd::custom::LoadU32(base, prop + 4);
  if (!obj) return;
  const uint32_t weapon = base[obj + 92];
  const uint32_t tint = pd::custom::TintOf(weapon);
  if (tint == pd::custom::kNoTint) return;
  PdCustomSiteLog(base, "an object colour (tinted)", weapon);  // TEMP_RECOLOUR
  // The colour is four s32 channels, 0-255 each (RGBA): blended as one RGBA word.
  uint32_t rgba = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    const int32_t value = int32_t(pd::custom::LoadU32(base, r4.u32 + i * 4));
    rgba |= uint32_t(std::clamp(value, 0, 255)) << (24 - i * 8);
  }
  rgba = pd::custom::TintRgba(rgba, tint);
  for (uint32_t i = 0; i < 4; ++i) pd::custom::StoreU32(base, r4.u32 + i * 4, (rgba >> (24 - i * 8)) & 0xFF);
}

// TEMP_RECOLOUR (step 3.1): right after the gun loader loads a gun file, and right
// after setupLoadModeldef loads a model's file. Log and dump only.
void PdHookRecolourGunLoaded(PPCRegister& r3, PPCRegister& r30, PPCRegister& r31) {
  PdRecolourGunLoaded(HookBase(), r3.u32, r30.u32, r31.u32);
}
void PdHookRecolourModelLoaded(PPCRegister& r3, PPCRegister& r30) {
  PdRecolourModelLoaded(HookBase(), r3.u32, r30.u32);
}

// ---- Custom weapons (pd_custom_weapons.h) ------------------------------------------
// A few generic hooks, each placed at several addresses (perfect_dark_config.toml,
// "Custom weapons"). Each passes one register: the weapon number being checked.

// A check that rejects weapon numbers above 0x2C (or 0x41, or 35), where passing
// means jumping to the code that follows for an accepted weapon: true for a
// custom weapon (jump_address_on_true = that code). No register is changed.
bool PdHookCustomPass(PPCRegister& weapon) {
  if (!pd::custom::IsCustom(weapon.u32)) return false;
  PdCustomSiteLog(HookBase(), "a gun-line check (passed)", weapon.u32);  // TEMP_CUSTOM
  return true;
}

// A check that asks "which weapon is this?" (is it a gun, the crossbow...), where
// the register is not used afterwards for anything else: a custom weapon's number
// becomes its base weapon's.
void PdHookCustomAsBase(PPCRegister& weapon) {
  if (!pd::custom::IsCustom(weapon.u32)) return;
  PdCustomSiteLog(HookBase(), "a weapon-number check (seen as the base weapon)", weapon.u32);  // TEMP_CUSTOM
  weapon.u64 = pd::custom::AsBase(weapon.u32);
}

// The primary/secondary bit of the current weapon, at `srawi rD,rS,3` where rS =
// weapon number - 1 (the bit's position in the player config's gunfuncs): a
// custom weapon gets the position of its spare bit.
void PdHookCustomFuncBit(PPCRegister& weapon_minus_1) {
  const int i = pd::custom::IndexOf(weapon_minus_1.u32 + 1);
  if (i < 0) return;
  PdCustomSiteLog(HookBase(), "a function bit (spare bit used)", weapon_minus_1.u32 + 1);  // TEMP_CUSTOM
  weapon_minus_1.u64 = uint32_t(pd::custom::FuncBitWeapon(i) - 1);
}

// weapon_get_pickup_text (sub_8220CD78) right after it loads the pickup-name table
// into r8: the copy with the custom weapons' rows.
void PdHookCustomPickupNames(PPCRegister& r8) {
  if (pd::custom::g_pickup_names) r8.u64 = pd::custom::g_pickup_names;
}

// The remote shot replay's crossbow check (sub_82171430 at 0x821715E0, r11 = the
// shot's weapon number): logged (TEMP_CUSTOM), then as PdHookCustomAsBase.
void PdHookCustomFireReplay(PPCRegister& r11) {
  PdCustomReplayLog(HookBase(), r11.u32);  // TEMP_CUSTOM
  pd::crossbow::g_firing_weapon = r11.u32;  // the bolt made next is this weapon's
  PdHookCustomAsBase(r11);
}

// The local fire path's crossbow check (sub_8217F5A0 at 0x8217F9B8, r11 = the
// firing weapon's number): remembered for the bolt made next, then as
// PdHookCustomAsBase.
void PdHookCustomFireLocal(PPCRegister& r11) {
  pd::crossbow::g_firing_weapon = r11.u32;
  PdHookCustomAsBase(r11);
}
