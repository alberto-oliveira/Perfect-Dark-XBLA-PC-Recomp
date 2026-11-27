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
#include "pd_ricochet.h"        // the Ricochet Laser
#include "pd_turret.h"          // the Remote Turret
#include "pd_file_override.h"   // custom gun models: loose files over packed ones

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

void PdXbowBoltCreated(uint8_t* base, uint32_t obj);
bool PdXbowBoltTick(PPCContext& ctx, uint8_t* base, uint32_t prop, uint32_t obj);

void PdTestPlayerTick(PPCContext& ctx, uint8_t* base);
void PdRicoShot(PPCContext& ctx, uint8_t* base);
uint32_t PdRicoRenderBeams(PPCContext& ctx, uint8_t* base, uint32_t gdl);
void PdTurretDeployed(uint8_t* base, uint32_t obj);
void PdTurretGoals(uint8_t* base, uint32_t r1, uint32_t obj);
void PdTurretFiring(uint8_t* base, uint32_t obj);

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

// ---- Custom gun models: loose files over packed ones (pd_file_override.h) ---------

// Entry of the archive's size function (sub_822549A0) and of its load-to-new-buffer
// function (sub_82254BE0): before the game reads any size from the entry table, the
// loose files' sizes are written into it.
void PdHookFileOverrideTable() {
  pd::fileoverride::PatchTable(HookBase());
}

// sub_822549C0's read path (0x82254A64; r31 = file id, r27 = destination; its lock is
// held): true when the loose file was copied, and the guest jumps to the function's
// "unlock, return 1" exit.
bool PdHookFileOverrideServe(PPCRegister& r27, PPCRegister& r31) {
  return pd::fileoverride::Serve(HookBase(), r31.u32, r27.u32);
}

// sub_8224B828 right after `bl sub_82262D60` opened Textures.raw: texture slots from
// tex_XXXX.hdr files get their new size and D3D header.
void PdHookTextureSlots() {
  pd::fileoverride::PatchTextureSlots(HookBase());
}

// Right after the texture manager inflated a texture into GPU memory: streamed
// textures (sub_82263470, r29 = texels) and resident ones at start-up (sub_82262D60,
// r28 = texels); r30 = texture number * 52 in both. Loose texels go over it.
void PdHookTextureOverrideStreamed(PPCRegister& r29, PPCRegister& r30) {
  pd::fileoverride::ServeTexture(HookBase(), r30.u32, r29.u32);
}
void PdHookTextureOverrideResident(PPCRegister& r28, PPCRegister& r30) {
  pd::fileoverride::ServeTexture(HookBase(), r30.u32, r28.u32);
}

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

// Right after each fire path's weapon_create_projectile_from_weapon_num call for
// a crossbow (r3 = the new bolt, 0 if none could be made): the bolt is tagged a
// Detonator bolt when the Detonator Crossbow fired it.
void PdHookXbowBoltMadeLocal(PPCRegister& r3) { PdXbowBoltCreated(HookBase(), r3.u32); }
void PdHookXbowBoltMadeReplay(PPCRegister& r3) { PdXbowBoltCreated(HookBase(), r3.u32); }

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
  base[player + custom::kPlayerDualWield] |= 0x80;
  base[player + custom::kHandLeft + custom::kHandInUse] = 1;
  base[player + custom::kHandLeft + custom::kHandWeapon] = w->left_weapon;
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
  sub_82172D58(call, base);  // bgun_create_model_cmd_list, into our kLeftCmdsSize bytes
  custom::StoreU32(base, hand + custom::kHandGunModelCmds, custom::g_left_cmds[i]);
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

// bgun_tick_gameplay (sub_821807A0) where it decides which hands fire: "both hands
// take turns unless the weapon is the remote mine" (0x82180C68, r10) and "the
// remote mine fires only the right hand" (0x82180D58, r11), each comparing the
// player's weapon with 34. A custom weapon with a left-hand item reads as that item.
void PdHookCustomAsLeftWeapon(PPCRegister& weapon) {
  const pd::custom::Weapon* w = pd::custom::Find(uint8_t(weapon.u32));
  if (w && w->left_weapon) weapon.u64 = w->left_weapon;
}

// The remote mine's detonate shortcut (stage 5): the input function sub_820F2988
// detonates when Use and Reload are held together (N64 A+B, recomp E+R), only if
// the current weapon is the remote mine (`cmpwi r10,34` at 0x820F3DE8, r10 = the
// weapon). If so it cancels the right hand's pending reload and calls the
// detonator. A custom weapon whose secondary function is the mine's Detonate reads
// as 34 there, so the Detonator Crossbow detonates with E+R, in either function.
void PdHookCustomDetonateShortcut(PPCRegister& r10) {
  const pd::custom::Weapon* w = pd::custom::Find(uint8_t(r10.u32));
  if (w && w->secondary == pd::custom::kFuncMineDetonate) r10.u64 = 34;
}

// bgun_tick_hand (sub_82174EC8; r14 = the hand number) right after it read the
// player's gun model for this hand (`lwz r22,5560(r25)` at 0x821753E4; r25 = the
// player): the left hand of a custom weapon with a left-hand model uses that
// model, when its instance is made from it.
void PdHookCustomLeftHandTick(PPCRegister& r14, PPCRegister& r22, PPCRegister& r25) {
  namespace custom = pd::custom;
  uint8_t* base = HookBase();
  if (r14.u32 != 1) return;  // the left hand
  const uint32_t player = r25.u32;
  const int i = custom::IndexOf(uint8_t(base[player + custom::kPlayerWeapon]));
  if (i < 0 || !custom::g_left_model[i]) return;
  const uint32_t model = player + custom::kHandLeft + custom::kHandGunModel;
  if (custom::LoadU32(base, model + custom::kModelDefinition) != custom::g_left_model[i]) return;
  r22.u64 = custom::g_left_model[i];
  // While the right hand reloads, the left-hand item is hidden: the crossbow's
  // reload animation brings in a left arm of its own (test 4a.3e: two left hands).
  // The hand stays ticked, only not drawn this frame.
  const uint32_t right = player + custom::kPlayerHands;
  if (int32_t(custom::LoadU32(base, right + custom::kHandState)) == custom::kHandStateReload) {
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
  return i >= 0 && custom::g_left_model[i];
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
  const uint32_t at = r1.u32 + 148;
  pd::custom::StoreU32(base, at, pd::custom::TintRgba(pd::custom::LoadU32(base, at), tint));
}

// The gun loader (sub_82170488) right after it loaded the first-person gun file
// into the player's gun memory (file_load_to_addr returned at 0x82170524; r30 =
// the file, still as on disc; r31 = the player, +5544 the weapon being
// equipped). For a custom weapon with a muzzle flash colour, the flash's vertex
// colour in this copy becomes that colour, before the game prepares the model
// for drawing (pd_custom_weapons.h, "Muzzle flash colour"). The copy is this
// equip's own: a later equip of the real Mauler loads the file again.
void PdHookCustomGunFileLoaded(PPCRegister& r30, PPCRegister& r31) {
  namespace custom = pd::custom;
  uint8_t* base = HookBase();
  const uint32_t rgba = custom::FlashColourOf(base[r31.u32 + custom::kPlayerWeapon]);
  if (!rgba) return;
  const uint32_t entry = custom::FindFlashColour(base, r30.u32);
  if (entry) custom::StoreU32(base, entry, rgba);
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

// The overlay for weapon object `obj` (a pickup, a gun in a hand, or a projectile
// such as a bolt), shared by the two object hooks below. A custom weapon gets its
// tint; a Detonator bolt (weapon 86, tagged when fired: pd_crossbow.h) gets the
// bolt colour, pulsing with the level frame counter. Returns false for anything else.
bool ObjectOverlay(uint8_t* base, uint32_t obj, uint32_t& tint, uint32_t& strength) {
  const uint32_t weapon = base[obj + 92];
  tint = pd::custom::TintOf(weapon);
  if (tint != pd::custom::kNoTint) {
    strength = pd::custom::g_tint_strength;
    return true;
  }
  if (weapon == 86 && pd::crossbow::g_bolt_tint != pd::custom::kNoTint &&
      pd::crossbow::g_detonator_bolts.count(obj)) {
    tint = pd::crossbow::g_bolt_tint;
    strength = pd::crossbow::BoltTintStrength(pd::custom::LoadU32(base, pd::crossbow::kLevelFrames));
    return true;
  }
  return false;
}
}
void PdHookCustomHeldGunBefore(PPCRegister& r3, PPCRegister& r31) {
  uint8_t* base = HookBase();
  const uint32_t prop = r31.u32;
  if (!prop || base[prop] != 4) return;  // prop type 4 = a weapon
  const uint32_t obj = pd::custom::LoadU32(base, prop + 4);
  if (!obj) return;
  const uint32_t at = r3.u32 + 0x38;
  uint32_t tint, strength;
  if (!ObjectOverlay(base, obj, tint, strength)) return;
  held_colour_saved = pd::custom::LoadU32(base, at);
  held_colour_changed = true;
  pd::custom::StoreU32(base, at, pd::custom::TintRgba(held_colour_saved, tint, strength));
}
void PdHookCustomHeldGunAfter(PPCRegister& r30) {
  if (!held_colour_changed) return;
  held_colour_changed = false;
  pd::custom::StoreU32(HookBase(), r30.u32 + 0x38, held_colour_saved);
}

// scenario_highlight_prop (sub_821E1900; r3 = the prop, r4 = its colour, s32[4] RGBA),
// which obj_render calls in multiplayer before drawing an object: a weapon object
// (a pickup, or a gun in a player's hand) of a custom weapon gets its colour
// blended towards its tint, before the scenario may highlight it. Projectiles are
// weapon objects too: a Detonator bolt, flying or lodged, gets the bolt colour.
void PdHookCustomObjectColour(PPCRegister& r3, PPCRegister& r4) {
  uint8_t* base = HookBase();
  const uint32_t prop = r3.u32;
  if (!prop || base[prop] != 4) return;  // prop type 4 = a weapon
  const uint32_t obj = pd::custom::LoadU32(base, prop + 4);
  if (!obj) return;
  uint32_t tint, strength;
  if (!ObjectOverlay(base, obj, tint, strength)) return;
  // The colour is four s32 channels, 0-255 each (RGBA): blended as one RGBA word.
  uint32_t rgba = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    const int32_t value = int32_t(pd::custom::LoadU32(base, r4.u32 + i * 4));
    rgba |= uint32_t(std::clamp(value, 0, 255)) << (24 - i * 8);
  }
  rgba = pd::custom::TintRgba(rgba, tint, strength);
  for (uint32_t i = 0; i < 4; ++i) pd::custom::StoreU32(base, r4.u32 + i * 4, (rgba >> (24 - i * 8)) & 0xFF);
}

// ---- The Ricochet Laser (pd_ricochet.h) ---------------------------------------------

// beam_create_for_hand (sub_821823A8) at 0x82182440, just before it fills the
// hand's beam: r25 = the weapon number the beams get (the player's weapon, or -2
// for the Laser's Stream), r28 = the player. For a Ricochet Laser the beams are
// noted (PdHookRicoBeamCreate) and get the Laser's number, so beam_render draws
// them as Laser beams.
void PdHookRicoHandBeams(PPCRegister& r25, PPCRegister& r28) {
  namespace rico = pd::ricochet;
  uint8_t* base = HookBase();
  rico::g_filling = int8_t(base[r28.u32 + rico::kPlayerWeapon]) == int8_t(rico::kWeaponRicochet);
  if (r25.u32 == rico::kWeaponRicochet) r25.u64 = rico::kWeaponLaser;
}

// beam_create_for_hand's exit (loc_82182618): its beams are filled.
void PdHookRicoHandBeamsDone() { pd::ricochet::g_filling = false; }

// beam_create (sub_82182130) after its stack frame (r3 = the beam being filled):
// noted as a Ricochet Laser beam, or no longer one.
void PdHookRicoBeamCreate(PPCRegister& r3) {
  namespace rico = pd::ricochet;
  if (rico::g_filling) {
    rico::g_beams.insert(r3.u32);
  } else {
    rico::g_beams.erase(r3.u32);
  }
}

// beam_render (sub_82182A30) where its colour branches join (0x82182C90):
// r25 = the address of the beam's colour (RGBA) in the display list, r29 = the
// beam. A Ricochet Laser beam gets its own colour; its alpha is kept (the Stream
// flickers through it).
// f29 = the beam's width (50 for a pulse, 10 for the Stream): widened by
// pd_rico_beam_width percent, since a red beam keeps only the Laser texture's
// bright core (its glow is blue, which the red takes out) and looks thin (the
// user's check; the game's other beam textures were thinner still).
void PdHookRicoBeamColour(PPCRegister& r25, PPCRegister& r29, PPCRegister& f29) {
  namespace rico = pd::ricochet;
  if (!rico::IsRicochetBeam(r29.u32)) return;
  uint8_t* base = HookBase();
  f29.f64 = f29.f64 * double(rico::g_beam_width) / 100.0;
  if (rico::g_beam_tint == pd::custom::kNoTint) return;
  base[r25.u32] = uint8_t(rico::g_beam_tint >> 16);
  base[r25.u32 + 1] = uint8_t(rico::g_beam_tint >> 8);
  base[r25.u32 + 2] = uint8_t(rico::g_beam_tint);
}

// shot_create (sub_821FC8C0) right after its shot_calculate_hits: the shot's
// bounces (main.cpp, PdRicoShot).
void PdHookRicoShot() { PdRicoShot(HookContext(), HookBase()); }

// The player's render (sub_820FC6E0) after the crossbow bolt trails (r3 = the
// display list): the bounce beams are drawn into it.
void PdHookRicoRenderBeams(PPCRegister& r3) { r3.u64 = PdRicoRenderBeams(HookContext(), HookBase(), r3.u32); }

// ---- The Remote Turret (pd_turret.h) ------------------------------------------------

void PdHookCustomAsBase(PPCRegister& weapon);  // (custom weapons, further down)

// The thrown-projectile maker sub_821709E8 at its Laptop check (`cmplwi r11,14` at
// 0x82170B28, r11 = the throw's weapon; used only for the check): the weapon is noted
// for the sentry made next (PdHookTurretDeployed), then the check sees the base, so a
// Remote Turret's throw makes a Laptop sentry (laptop_deploy).
void PdHookTurretDeployCheck(PPCRegister& r11) {
  pd::turret::g_deploying_weapon = r11.u32;
  PdHookCustomAsBase(r11);
}

// Right after laptop_deploy returned the new sentry (0x82170B34, r3; 0 = none).
void PdHookTurretDeployed(PPCRegister& r3) { PdTurretDeployed(HookBase(), r3.u32); }

// autogun_tick: the goal angles before the sentry turns (r1 frame, r31 = the object)
// and the firing flag before the owner sends both (r31).
void PdHookTurretGoals(PPCRegister& r1, PPCRegister& r31) { PdTurretGoals(HookBase(), r1.u32, r31.u32); }
void PdHookTurretFiring(PPCRegister& r31) { PdTurretFiring(HookBase(), r31.u32); }

// player_tick's normal branch, at its player_move_camera_from_pos_rooms call
// (0x821005E0: r3 = the camera position, a copy on the stack; r6/r7 = the position
// and rooms it is checked from): while the local player controls a turret, the
// camera is at the turret. The look and up directions stay the player's.
void PdHookTurretCamera(PPCRegister& r3, PPCRegister& r6, PPCRegister& r7) {
  uint8_t* base = HookBase();
  namespace tu = pd::turret;
  if (!tu::CurrentPlayerControls(base) || !tu::IsTurret(base, tu::g_control.obj)) return;
  const uint32_t prop = tu::LoadU32(base, tu::g_control.obj + tu::kObjProp);
  pd::player::StoreVec(base, r3.u32, tu::EyeOf(base, tu::g_control.obj));
  r6.u64 = prop + pd::player::kPropPos;
  r7.u64 = prop + pd::player::kPropRooms;
}

// The input step's walking call (bwalk_apply_move_data sub_821044B8 at 0x820F4494;
// true jumps past it to 0x820F449C): while the local player controls a turret, the
// body does not walk. The view still turns (that is done outside this call).
bool PdHookTurretFreeze() {
  uint8_t* base = HookBase();
  namespace tu = pd::turret;
  if (!tu::CurrentPlayerControls(base)) return false;
  const uint32_t player = tu::LoadU32(base, pd::player::kGVars + 1188);
  tu::StoreF32(base, player + tu::kPlayerSpeedSideways, 0.0f);
  tu::StoreF32(base, player + tu::kPlayerSpeedStrafe, 0.0f);
  tu::StoreF32(base, player + tu::kPlayerSpeedForwards, 0.0f);
  return true;
}

// bgun_tick_gameplay (sub_821807A0) in a network match, where it stores a human
// player's trigger as XBLA's input exchange handed it back (0x82180B60, r10 = the
// trigger; the value passed to bgun_tick_gameplay is not used then): the turret tick
// gets the trigger from here; the gun sees it released while the player controls a
// turret, and after leaving until it really is.
namespace {
// The gun's view of the local player's trigger (`trigger`, changed in place).
void TurretBlockTrigger(PPCRegister& trigger, const char* where) {
  uint8_t* base = HookBase();
  namespace tu = pd::turret;
  tu::Control& c = tu::g_control;
  if (c.on) c.block_trigger = true;
  if (!c.block_trigger) return;
  c.released_steps = trigger.u32 == 0 ? c.released_steps + 1 : 0;
  if (c.released_steps >= tu::kReleaseSteps && !c.on) {
    c.block_trigger = false;
  } else if (trigger.u32 != 0) {
    trigger.u64 = 0;
    // TEMP_TURRET: which of the two places held the trigger off.
    static uint32_t last = 0;
    const uint32_t frame = tu::LoadU32(base, pd::player::kLevelFrames);
    if (frame / 30 != last / 30) REXLOG_INFO("[turret] TEMP_TURRET frame {}: trigger held off at {}", frame, where);
    last = frame;
  }
}
bool TurretLocalTurn(uint8_t* base) {
  return pd::turret::LoadU32(base, pd::player::kGVars + 1188) == pd::player::Player(base, 0);
}
}  // namespace

void PdHookTurretTrigger(PPCRegister& r3) {
  if (!TurretLocalTurn(HookBase())) return;
  {  // TEMP_TURRET: the trigger each step from 1 s before to 2 s after control ends
    static uint32_t ended = 0;
    uint8_t* base = HookBase();
    const uint32_t frame = pd::turret::LoadU32(base, pd::player::kLevelFrames);
    const uint32_t player = pd::player::Player(base, 0);
    if (pd::turret::g_control.on) ended = frame;
    if (ended && frame - ended < 120 && frame >= ended)
      REXLOG_INFO("[turret] TEMP_TURRET step frame {}: trigger {} on {} block {} func {} hand state {}", frame,
                  r3.u32, pd::turret::g_control.on, pd::turret::g_control.block_trigger,
                  base[player + pd::turret::kRightHand + pd::turret::kHandFunc],
                  int32_t(pd::turret::LoadU32(base, player + pd::turret::kRightHand + 1540)));
  }
  pd::turret::g_control.trigger = r3.u32 != 0;
  TurretBlockTrigger(r3, "the input step");
}
void PdHookTurretTriggerNet(PPCRegister& r10) {
  if (!TurretLocalTurn(HookBase())) return;
  TurretBlockTrigger(r10, "the network trigger");
}

// The first-person gun renderer sub_821761F8 at its start (return_on_true; it takes
// the display list pointer in r3 and writes it back): no gun while the view is at
// the turret.
bool PdHookTurretHideGun() { return pd::turret::CurrentPlayerControls(HookBase()); }

// Using one's own thrown Laptop sentry picks it up (sub_822211E8: inv_give_single_weapon,
// then the pickup message, with `li r3,14` before each; r28 = the sentry): a Remote
// Turret gives back the Remote Turret. Right after each `li`.
void PdHookTurretPickup(PPCRegister& r3, PPCRegister& r28) {
  for (const uint32_t t : pd::turret::g_turrets) {
    if (t && t == r28.u32) r3.u64 = pd::turret::kWeaponTurret;
  }
}

// ---- Test tools (pd_player.h) ------------------------------------------------------

// The player input step sub_820F2988, after its stack frame is made: the test
// pipe's requests for the local player run here, on the game thread.
void PdHookTestPlayerTick() {
  PdTestPlayerTick(HookContext(), HookBase());
}

// ---- Custom weapons (pd_custom_weapons.h) ------------------------------------------
// A few generic hooks, each placed at several addresses (perfect_dark_config.toml,
// "Custom weapons"). Each passes one register: the weapon number being checked.

// A check that rejects weapon numbers above 0x2C (or 0x41, or 35), where passing
// means jumping to the code that follows for an accepted weapon: true for a
// custom weapon (jump_address_on_true = that code). No register is changed.
bool PdHookCustomPass(PPCRegister& weapon) {
  return pd::custom::IsCustom(weapon.u32);
}

// A check that asks "which weapon is this?" (is it a gun, the crossbow...), where
// the register is not used afterwards for anything else: a custom weapon's number
// becomes its base weapon's.
void PdHookCustomAsBase(PPCRegister& weapon) {
  if (!pd::custom::IsCustom(weapon.u32)) return;
  weapon.u64 = pd::custom::AsBase(weapon.u32);
}

// "Which model is this weapon drawn with?" (the third-person model, sub_821F4C68,
// which returns a model number in r3): a custom weapon with a model of its own
// answers with it at once (true: the function returns); any other custom weapon's
// number becomes its look weapon's (the Ricochet Laser is drawn as a Mauler), or
// its base weapon's, and the game's table answers.
bool PdHookCustomAsLook(PPCRegister& weapon) {
  if (!pd::custom::IsCustom(weapon.u32)) return false;
  if (const uint32_t model = pd::custom::OwnModelNumber(weapon.u32)) {
    weapon.u64 = model;
    return true;
  }
  weapon.u64 = pd::custom::AsLook(weapon.u32);
  return false;
}

// The primary/secondary bit of the current weapon, at `srawi rD,rS,3` where rS =
// weapon number - 1 (the bit's position in the player config's gunfuncs): a
// custom weapon gets the position of its spare bit.
void PdHookCustomFuncBit(PPCRegister& weapon_minus_1) {
  const int i = pd::custom::IndexOf(weapon_minus_1.u32 + 1);
  if (i < 0) return;
  weapon_minus_1.u64 = uint32_t(pd::custom::FuncBitWeapon(i) - 1);
}

// weapon_get_pickup_text (sub_8220CD78) right after it loads the pickup-name table
// into r8: the copy with the custom weapons' rows.
void PdHookCustomPickupNames(PPCRegister& r8) {
  if (pd::custom::g_pickup_names) r8.u64 = pd::custom::g_pickup_names;
}

// The remote shot replay's crossbow check (sub_82171430 at 0x821715E0, r11 = the
// shot's weapon number): remembered for the bolt made next, then as
// PdHookCustomAsBase.
void PdHookCustomFireReplay(PPCRegister& r11) {
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
