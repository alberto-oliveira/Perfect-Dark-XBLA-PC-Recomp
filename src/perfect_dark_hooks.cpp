// Perfect Dark XBLA PC Recomp - the codegen hooks named in perfect_dark_config.toml.
//
// The recompiled code is generated from the player's own default.xex, so the port's
// changes to the game cannot be edits to it: each is a hook that ReXGlue's codegen
// calls at a guest address. The registers a hook lists in the config are passed by
// reference; a hook that returns bool decides the config's return or jump. The
// behaviour itself lives in main.cpp and perfect_dark_app.h; these are the glue.

#include "generated/default/perfect_dark_init.h"

#include <cstdint>
#include <cstring>

#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

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
  REXLOG_INFO("[poison] respawn chr 0x{:08X} frame {}: counter {} -> {}", r11.u32,  // TEMP_POISON
              LoadU32(base, 0x82648BF8 + 0x0C), poison, poison > cap ? cap : poison);
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

// TEMP_POISON: the victim's bondhealth, or -99 if it is not a player.
static float PoisonVictimHealth(uint8_t* base, uint32_t chr) {
  const int i = PoisonVictimPlayer(base, chr);
  if (i < 0) return -99.0f;
  const uint32_t bits = LoadU32(base, LoadU32(base, 0x82648BF8 + 100 + uint32_t(i) * 4) + 228);
  float health;
  std::memcpy(&health, &bits, 4);
  return health;
}

// TEMP_POISON: chr_set_poisoned's MP store `sth r10,856(r31)` (r10 = the new counter).
void PdHookPoisonSetLog(PPCRegister& r10, PPCRegister& r31) {
  uint8_t* base = HookBase();
  REXLOG_INFO("[poison] set chr 0x{:08X} frame {}: counter -> {}, health {:.3f}", r31.u32,
              LoadU32(base, 0x82648BF8 + 0x0C), int16_t(r10.u16), PoisonVictimHealth(base, r31.u32));
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
  const bool skip = remote && PdN64Poison(base);
  const int16_t counter = int16_t((base[r31.u32 + 856] << 8) | base[r31.u32 + 857]);
  REXLOG_INFO("[poison] tick chr 0x{:08X} frame {}: counter {}, health before {:.3f}, {}",  // TEMP_POISON
              r31.u32, LoadU32(base, 0x82648BF8 + 0x0C), counter, PoisonVictimHealth(base, r31.u32),
              skip ? "skipped (remote player)" : "applied");
  return skip;
}
