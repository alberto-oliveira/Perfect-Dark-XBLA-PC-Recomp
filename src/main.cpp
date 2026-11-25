// perfect_dark - ReXGlue Recompiled Project

#include "generated/default/perfect_dark_init.h"

#include "perfect_dark_app.h"
#include "pd_weapons.h"  // more multiplayer weapons, shuffled weapon spawns
#include "pd_stages.h"   // the solo mission stages as Combat Simulator arenas
#include "pd_stage_layouts.h"  // their hand-picked spawn and weapon pads
#include "pd_crossbow.h"  // the detonator crossbow
#include "pd_custom_weapons.h"  // new weapons under borrowed weapon numbers
#include "pd_player.h"  // the local player, for test scripts

#include <rex/chrono/clock.h>
#include <rex/diag_timing.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>

#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <map>
#include <filesystem>
#include <random>
#include <set>
#include <thread>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

// Developer F-key diagnostics (see PerfectDarkInputTracker::OnDraw). Players
// never need these; set pd_dev_hotkeys = true in perfect_dark.toml to enable.
REXCVAR_DEFINE_BOOL(pd_dev_hotkeys, false, "PerfectDark",
                    "Enable developer diagnostic hotkeys (F1, F2, F5, F6, F8, F10-F12, End)");
REXCVAR_DEFINE_BOOL(pd_frame_stats, false, "PerfectDark",
                    "Log presented frame rate and slow frames every five seconds (developer "
                    "aid for performance work)");

// Setup extras (Install-PerfectDark.ps1). Each is read by patched game code:
// cheatIsUnlocked in generated/default/perfect_dark_recomp.7.cpp, the
// multiplayer feature check, and PerfectDarkApp::TickStoryComplete.
REXCVAR_DEFINE_BOOL(pd_unlock_all_cheats, false, "PerfectDark",
                    "Every cheat is available in the Cheats menu (each still has to be switched "
                    "on)");
REXCVAR_DEFINE_BOOL(pd_story_complete, false, "PerfectDark",
                    "Mark every solo mission complete on every difficulty, unlocking the "
                    "special missions and Perfect Dark difficulty (missions keep any real best "
                    "time)");
REXCVAR_DEFINE_BOOL(pd_unlock_all_multiplayer, false, "PerfectDark",
                    "Unlock every Combat Simulator arena, weapon, character and option");
// The launcher's "Auto-Aim on" extra (HANDOVER 4az). The port starts new and default
// control settings with Auto-Aim off (PdHookAutoAimOff*); this keeps the Xbox's on.
// Settings a player already has are not changed: Player Setup > Control sets those.
REXCVAR_DEFINE_BOOL(pd_default_auto_aim, false, "PerfectDark",
                    "New and default control settings start with Auto-Aim on");

// Settings > Video > Render Scale (PerfectDarkApp::OnPreSetup, HANDOVER 4ai).
REXCVAR_DEFINE_STRING(pd_render_scale, "1", "PerfectDark",
                      "Internal rendering sharpness: 1, 2 or 3 times the Xbox's 1280 x 720; "
                      "applied when the game starts (an old \"auto\" is 1)")
    .allowed({"1", "2", "3", "auto"});

// Settings > Video > Aspect Ratio (PerfectDarkApp::ApplyAspectRatioIndex, HANDOVER 4ay).
// The game still has the N64's screen-ratio switch (optionsGetScreenRatio,
// sub_821DC2E0): every view's aspect is its width over its height times 0.75,
// and times 4/3 again when the ratio is 16:9, which the Xbox version always
// returns. For 4:3 the hook answers "normal", so the 3D view gets the N64's
// 4:3 field of view, and the SDK presenter shows the 16:9 frame squeezed to a
// 4:3 box with bars at the sides (present_aspect_ratio). Both apply at once.
REXCVAR_DEFINE_STRING(pd_aspect_ratio, "16:9", "PerfectDark",
                      "Picture shape: 16:9 (the Xbox version) or 4:3 (the N64's field of view, "
                      "with bars at the sides)")
    .allowed({"16:9", "4:3"});

bool PdAspect43() {
  return REXCVAR_GET(pd_aspect_ratio) == "4:3";
}

// Which one-time updates of an older release's perfect_dark.toml this file
// has had (PerfectDarkApp::MigrateSettings, HANDOVER 4ak).
REXCVAR_DEFINE_INT32(pd_settings_revision, 0, "PerfectDark",
                     "Which one-time updates of older settings this file has had (set by the "
                     "game; leave it alone)");

REXCVAR_DEFINE_DOUBLE(pd_dark_room_visibility, 0.37, "PerfectDark",
                      "How much of a room stays visible with its lights switched off: 0 is "
                      "fully black (original), 0.37 dark but visible")
    .range(0.0, 0.6);
REXCVAR_DEFINE_DOUBLE(pd_dark_room_object_visibility, 0.03, "PerfectDark",
                      "How much of their colour objects, characters and your gun keep in a room "
                      "with its lights switched off: 0.03 leaves them almost black, as the user "
                      "chose, 1 leaves them fully lit")
    .range(0.0, 0.6);

// Dark rooms, kept as two settings because they are two different things in
// the game. Objects, characters and weapons are darkened by the shade alpha
// (PdShadeAlpha, perfect_dark_recomp.13.cpp), capped at 1 - object visibility.
// Walls and floors are lit by lights_set_for_room (sub_821519C8) from the
// room's brightness, 0-255, with lit rooms far above this floor: raised to at
// least room visibility x 255.
float PdDarkRoomVisibility() {
  return float(REXCVAR_GET(pd_dark_room_visibility));
}

float PdDarkRoomObjectVisibility() {
  return float(REXCVAR_GET(pd_dark_room_object_visibility));
}

uint32_t PdRoomBrightnessFloor(uint32_t brightness) {
  const uint32_t value = brightness & 0xFF;
  const uint32_t floor = uint32_t(REXCVAR_GET(pd_dark_room_visibility) * 255.0 + 0.5);
  return value < floor ? floor : value;
}

// Camera Bob (Settings > Display Interface, pd_view_bob). The head update sub_820F0150 (the
// N64's bheadUpdate) builds the eye's offset (r1+80), look (r1+112) and up (r1+96)
// vectors from Joanna's walk animation and idle breathing, then hands them on at
// 0x820F068C (smoothed into player+968/+980/+992). Head Roll only decides whether
// the camera turns by the look and up vectors (sub_820F26D0), so with it off the eye
// still bobs. Off, the eye stays at the resting height (player+1052, its value
// standing still) looking straight ahead with no roll; the offset's x and z stay, as
// they are what moves Joanna (the walk code sub_820EF018 reads player+968/+976).
REXCVAR_DEFINE_BOOL(pd_view_bob, true, "PerfectDark",
                    "Camera bob while walking (off = the camera stays level and steady)");

void PdViewBobHold(uint8_t* base, uint32_t sp) {
  if (REXCVAR_GET(pd_view_bob)) return;
  auto load = [base](uint32_t a) {
    uint32_t v;
    std::memcpy(&v, base + a, 4);
    return __builtin_bswap32(v);
  };
  auto store = [base](uint32_t a, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    v = __builtin_bswap32(v);
    std::memcpy(base + a, &v, 4);
  };
  const uint32_t player = load(0x82648BF8u + 1188);  // g_Vars.currentplayer
  if (!player) return;
  const uint32_t rest = load(player + 1052);
  float height;
  std::memcpy(&height, &rest, 4);
  store(sp + 84, height);
  store(sp + 112, 0.0f);
  store(sp + 116, 0.0f);
  store(sp + 120, 1.0f);
  store(sp + 96, 0.0f);
  store(sp + 100, 1.0f);
  store(sp + 104, 0.0f);
}

// Lighting capture for bug reports (perfect_dark_app.h, F9 or Insert): off by default.
REXCVAR_DEFINE_BOOL(pd_light_capture, false, "PerfectDark",
                    "F9 or Insert logs one frame's draws and lighting constants (for lighting bug reports)");
REXCVAR_DEFINE_STRING(pd_light_watch, "", "PerfectDark",
                      "With pd_light_capture: F9 watches writes to this hex byte pattern in the game's memory, "
                      "F9 again lists the code that made them (lighting investigation)");
REXCVAR_DEFINE_STRING(pd_handicaps, "", "PerfectDark",
                      "Online handicaps by gamertag: \"Name:percent, Name:percent\" (10-1000). "
                      "Every player must use the same list.");

REXCVAR_DEFINE_BOOL(pd_menu_mouse, true, "PerfectDark",
                    "Use the mouse in menus: point at a row to highlight it, left button "
                    "selects, right button goes back");

REXCVAR_DEFINE_INT32(pd_mp_update_rate, 30, "PerfectDark",
                     "Most player updates a second each PC sends in Combat Simulator. The game "
                     "shipped 15 (Co-Operative uses 30); higher is smoother")
    .range(15, 30);

// Combat Simulator's player update cap (sub_822848F8 sets the network rate
// range: lockstep modes 30..30, Combat Simulator 5..15). The network tick
// runs 30 times a second, so 30 is the most that can go out.
int64_t PdMpUpdateRate() {
  return REXCVAR_GET(pd_mp_update_rate);
}

// The game's adaptive send rate (sub_822C8E58, per connection) starts a match
// at about 16 updates a second and climbs 10% every five seconds, takes 20%
// off whenever it thinks the link is congested, and falls to its minimum of
// 5 when more than 8% of packets go missing - a minute and a half back to 30.
// Measured on two copies: the joiner skipped 220 send slots while moving in
// one short match. With the floor on, the minimum is the cap, as the game
// already does for its lockstep modes, so the other players always get the
// full rate. At a few kilobytes a second that costs nothing on any modern link.
REXCVAR_DEFINE_BOOL(pd_mp_rate_floor, true, "PerfectDark",
                    "Keep Combat Simulator sending pd_mp_update_rate updates a second, "
                    "instead of letting the game throttle to as few as 5 after packet loss "
                    "and climb back over a minute");

int64_t PdMpUpdateRateMin() {
  return REXCVAR_GET(pd_mp_rate_floor) ? REXCVAR_GET(pd_mp_update_rate) : 5;
}

// How far behind their newest update the other players' facing and aim are
// drawn. sub_822BA128 interpolates them from the last 15 updates at "now -
// half the round trip - this" (the game shipped 200; generated/default/
// perfect_dark_recomp.16.cpp). Updates come about every 33 ms, so anything
// above that still interpolates smoothly; below it the facing holds and then
// jumps with each update. Their body position is not affected: it is
// predicted ahead, not delayed. Read every frame, so it can change mid-match.
// 50 was the best of 200/100/50/0 in a real match over Radmin (2026-10-04):
// turning shows ~150 ms sooner and still interpolates smoothly. A link with
// more jitter than that (internet play) may look smoother at 100 (F9 panel).
REXCVAR_DEFINE_INT32(pd_mp_facing_delay_ms, 50, "PerfectDark",
                     "How many milliseconds behind their newest update other players' "
                     "facing and aim are drawn, on top of half the ping. The game "
                     "shipped 200; lower shows their turning sooner, and below about 35 "
                     "it turns in steps. 100 can look smoother on a jittery internet link")
    .range(0, 200);

int64_t PdMpFacingDelayMs() {
  return REXCVAR_GET(pd_mp_facing_delay_ms);
}

REXCVAR_DEFINE_DOUBLE(pd_shine, 1.15, "PerfectDark",
                      "Shine on reflective surfaces (suit, guns, signs, ships): 1.0 is the "
                      "original, 1.15 a little more")
    .range(1.0, 3.0);

bool PdExtraAllCheats() {
  return REXCVAR_GET(pd_unlock_all_cheats);
}

bool PdExtraAllMultiplayer() {
  return REXCVAR_GET(pd_unlock_all_multiplayer);
}

bool PdDefaultAutoAim() {
  return REXCVAR_GET(pd_default_auto_aim);
}

// CPU use (HANDOVER 4ah). Three game threads waited by spinning: on the Xbox
// 360 they shared a core with other hardware threads, on a PC each burned a
// whole core (measured: the main thread, a 10 ms timer and a worker, ~100%
// each). Called from the game code in generated/default:
//  - the GPU-completion poll (perfect_dark_recomp.19.cpp sub_82329E08), which
//    all six of the game's GPU waits loop on: yield for the first 0.5 ms of a
//    wait, then sleep in 0.5 ms high-resolution steps;
//  - Sleep(0) in the timer thread's wait for its next 10 ms tick and at the
//    end of each pass of the worker it drives (perfect_dark_recomp.13.cpp
//    sub_8226D308 and sub_8226D520): a 0.5 ms sleep instead.
REXCVAR_DEFINE_BOOL(pd_sleep_in_waits, true, "PerfectDark",
                    "Let the game's busy-waiting threads (GPU waits, a 10 ms timer, a worker) "
                    "sleep in 0.5 ms steps instead of spinning a CPU core each");

namespace {
void SleepHalfMillisecond() {
  struct Timer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr,
                                           0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                           TIMER_ALL_ACCESS);
    ~Timer() {
      if (handle) CloseHandle(handle);
    }
  };
  thread_local Timer timer;
  LARGE_INTEGER due;
  due.QuadPart = -5000;  // 0.5 ms, relative, in 100 ns units
  if (timer.handle && SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
    WaitForSingleObject(timer.handle, INFINITE);
  } else {
    SwitchToThread();
  }
}
}  // namespace

// Crash report. A crash in the PC side of the game used to leave the log
// stopping mid-line with no reason given. Now an unhandled exception writes a
// [crash] line - the exception, and the module and offset it happened at - and
// a small dump (stacks and modules, no game memory) beside the logs, for a
// player to send in with the log. abort() (the runtime's own fatal errors, an
// uncaught C++ exception) gets a line too. The newest five dumps are kept.
namespace {
std::wstring CrashLogsFolder() {
  wchar_t exe[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring dir(exe, n);
  return dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"logs";
}

void KeepNewestDumps(const std::wstring& dir, size_t keep) {
  std::vector<std::filesystem::path> dumps;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const auto name = entry.path().filename().wstring();
    if (name.rfind(L"crash-", 0) == 0 && entry.path().extension() == L".dmp") {
      dumps.push_back(entry.path());
    }
  }
  std::sort(dumps.begin(), dumps.end());  // names carry the date and time
  for (size_t i = 0; i + keep < dumps.size(); ++i) std::filesystem::remove(dumps[i], ec);
}

LONG WINAPI PdCrashFilter(EXCEPTION_POINTERS* info) {
  static std::atomic<bool> once{false};
  if (once.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
  const uintptr_t address = record ? uintptr_t(record->ExceptionAddress) : 0;
  HMODULE module = nullptr;
  wchar_t module_path[MAX_PATH] = L"?";
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(address), &module);
  if (module) GetModuleFileNameW(module, module_path, MAX_PATH);
  std::wstring module_name(module_path);
  module_name = module_name.substr(module_name.find_last_of(L"\\/") + 1);
  REXLOG_CRITICAL("[crash] exception 0x{:08X} at {}+0x{:X} (thread {}){}",
                  record ? uint32_t(record->ExceptionCode) : 0u,
                  std::string(module_name.begin(), module_name.end()),
                  module ? address - uintptr_t(module) : address, GetCurrentThreadId(),
                  record && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                          record->NumberParameters >= 2
                      ? fmt::format(", {} address 0x{:X}",
                                    record->ExceptionInformation[0] ? "writing" : "reading",
                                    record->ExceptionInformation[1])
                      : std::string());
  // A guest thread: its registers and the return addresses up its stack (each frame's
  // back chain at 0(r1), the caller's saved LR 8 bytes below the next frame), read only
  // from committed guest pages.
  if (rex::runtime::ThreadState* ts = rex::runtime::ThreadState::Get(); ts && ts->context()) {
    const PPCContext& c = *ts->context();
    uint8_t* mem = rex::system::kernel_state() && rex::system::kernel_state()->memory()
                       ? rex::system::kernel_state()->memory()->virtual_membase()
                       : nullptr;
    const auto load = [mem](uint32_t address, uint32_t& value) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (!mem || !address || !VirtualQuery(mem + address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
      std::memcpy(&value, mem + address, 4);
      value = __builtin_bswap32(value);
      return true;
    };
    std::string callers;
    uint32_t frame = c.r1.u32, back = 0, lr = 0;
    for (int i = 0; i < 8 && load(frame, back) && back > frame; ++i, frame = back) {
      if (load(back - 8, lr)) callers += fmt::format(" {:08X}", lr);
    }
    REXLOG_CRITICAL("[crash] guest lr {:08X} ctr {:08X} r1 {:08X} r3 {:08X} r4 {:08X} r5 {:08X} r11 {:08X} r30 {:08X} "
                    "r31 {:08X}; callers{}",
                    uint32_t(c.lr), c.ctr.u32, c.r1.u32, c.r3.u32, c.r4.u32, c.r5.u32, c.r11.u32, c.r30.u32, c.r31.u32,
                    callers);
  }
  const std::wstring dir = CrashLogsFolder();
  SYSTEMTIME now;
  GetLocalTime(&now);
  wchar_t name[64];
  swprintf(name, 64, L"\\crash-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond);
  const std::wstring dump_path = dir + name;
  HANDLE file = CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file,
        MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
        info ? &exception : nullptr, nullptr, nullptr);
    CloseHandle(file);
    const std::wstring file_name(name + 1);
    REXLOG_CRITICAL("[crash] {}: logs\\{}", ok ? "crash dump written" : "crash dump failed",
                    std::string(file_name.begin(), file_name.end()));
    KeepNewestDumps(dir, 5);
  }
  rex::FlushLogging();
  return EXCEPTION_CONTINUE_SEARCH;
}

void PdAbortHandler(int) {
  REXLOG_CRITICAL("[crash] the game stopped itself (abort); the lines above say why");
  rex::FlushLogging();
}
}  // namespace

void PdInstallCrashReport() {
  SetUnhandledExceptionFilter(PdCrashFilter);
  std::signal(SIGABRT, PdAbortHandler);
}

// Hybrid CPUs (HANDOVER 4ba). Intel 12th gen and later mix fast performance cores
// with slow efficiency cores, and Windows 10 does not tell them apart: the game's
// threads, which sleep between frames, woke on efficiency cores and missed vblanks
// (an i9-12900K on Windows 10 ran ~46 fps with the GPU 45% busy). The process is
// kept on the cores of the highest efficiency class; on a CPU with one kind of
// core this does nothing.
REXCVAR_DEFINE_BOOL(prefer_performance_cores, true, "PerfectDark",
                    "On a CPU with performance and efficiency cores, keep the game on the "
                    "performance cores");

void PdPreferPerformanceCores() {
  ULONG size = 0;
  GetSystemCpuSetInformation(nullptr, 0, &size, GetCurrentProcess(), 0);
  std::vector<uint8_t> buffer(size);
  if (size == 0 ||
      !GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
                                  size, &size, GetCurrentProcess(), 0)) {
    REXLOG_INFO("[system] CPU sets not available: cores left to Windows");
    return;
  }
  std::vector<std::pair<ULONG, BYTE>> sets;  // id, efficiency class
  BYTE top = 0, bottom = 255;
  for (ULONG offset = 0; offset < size;) {
    const auto* entry = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
    if (entry->Size == 0) break;
    if (entry->Type == CpuSetInformation) {
      const BYTE efficiency = entry->CpuSet.EfficiencyClass;
      sets.emplace_back(entry->CpuSet.Id, efficiency);
      top = std::max(top, efficiency);
      bottom = std::min(bottom, efficiency);
    }
    offset += entry->Size;
  }
  if (sets.empty() || top == bottom) {
    REXLOG_INFO("[system] CPU cores: one kind ({} logical processors), no core preference",
                sets.size());
    return;
  }
  std::vector<ULONG> fast;
  for (const auto& [id, efficiency] : sets) {
    if (efficiency == top) fast.push_back(id);
  }
  if (!REXCVAR_GET(prefer_performance_cores)) {
    REXLOG_INFO("[system] hybrid CPU: {} of {} logical processors are performance cores; "
                "prefer_performance_cores is off, so all are used",
                fast.size(), sets.size());
    return;
  }
  if (SetProcessDefaultCpuSets(GetCurrentProcess(), fast.data(), ULONG(fast.size()))) {
    REXLOG_INFO("[system] hybrid CPU: game kept on the {} performance-core logical processors "
                "of {}",
                fast.size(), sets.size());
  } else {
    REXLOG_WARN("[system] hybrid CPU: keeping the game on the performance cores failed ({})",
                GetLastError());
  }
}

namespace {
// The game thread's time in the GPU-completion poll, for its frame split in the
// stutter log (PdNoteGameFrame). Counted per thread: six waits share the poll.
thread_local int64_t t_gpu_poll_ticks = 0;
thread_local uint64_t t_gpu_poll_cycles = 0;
thread_local uint32_t t_gpu_poll_waits = 0;
}  // namespace

void PdGpuWaitBackoff() {
  if (!REXCVAR_GET(pd_sleep_in_waits)) return;
  thread_local int64_t wait_start = 0;
  thread_local int64_t last_poll = 0;
  thread_local uint64_t last_poll_cycles = 0;
  static const int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const int64_t start = now.QuadPart;
  const bool timing = rex::diag::TimingLogOn();
  const uint64_t cycles = timing ? rex::diag::ThreadCycles() : 0;
  // Polls within one wait come microseconds apart; a gap over 2 ms means a new wait.
  if (now.QuadPart - last_poll > freq / 500) {
    wait_start = now.QuadPart;
    ++t_gpu_poll_waits;
  } else if (timing && last_poll && last_poll_cycles) {
    // The guest's own poll code between two calls is part of the wait too.
    t_gpu_poll_ticks += now.QuadPart - last_poll;
    t_gpu_poll_cycles += cycles - last_poll_cycles;
  }
  if (now.QuadPart - wait_start < freq / 2000) {
    SwitchToThread();
  } else {
    SleepHalfMillisecond();
  }
  QueryPerformanceCounter(&now);
  last_poll = now.QuadPart;
  last_poll_cycles = 0;
  if (timing) {
    last_poll_cycles = rex::diag::ThreadCycles();
    t_gpu_poll_ticks += now.QuadPart - start;
    t_gpu_poll_cycles += last_poll_cycles - cycles;
  }
}

// Game clock (HANDOVER 4aj). The port's osGetCount (perfect_dark_recomp.12.cpp
// sub_8224FB60, called once at the start of every frame) turns the time since
// the previous frame into N64 counter units, but never less than one frame
// (781250 units) - so the frame-wait loop that reads it can never spin forever.
// On the Xbox each frame starts on the vblank, so that floor never mattered. On a
// PC the main loop sometimes resumes a little early; each early start was still
// counted as a whole frame, the game clock ran ~2% fast (measured: 47.7 MHz where
// the N64 count is 46.875) and about once a second a frame advanced two steps
// (g_Vars diffframe60 = 2). Now an early call still advances one frame, but the
// difference is owed and taken off the next call. The debt is capped at one frame,
// so a game that really runs faster than the vblank (the network clock sync) is
// counted exactly as before. Network matches take their time from the network
// clock, not from here.
REXCVAR_DEFINE_BOOL(pd_smooth_game_clock, true, "PerfectDark",
                    "Keep the game's frame clock from running fast when a frame starts a "
                    "little early (removes a double step about once a second)");

bool PdSmoothGameClock() {
  return REXCVAR_GET(pd_smooth_game_clock);
}

uint32_t PdOsCountStep(uint32_t real_step) {
  constexpr int64_t kFrame = 781250;      // one 60 Hz frame of the N64 count
  constexpr int64_t kMaxStep = 3125000;   // the game's own cap: four frames
  static int64_t debt = 0;                // counted ahead of real time
  int64_t step = int64_t(real_step) - debt;
  if (step < kFrame) {
    debt = std::min<int64_t>(kFrame - step, kFrame);
    step = kFrame;
  } else {
    debt = 0;
  }
  return uint32_t(std::min(step, kMaxStep));
}

// The game's timer object (sub_822B7700 seconds, sub_822B7750 milliseconds,
// sub_822B7790 microseconds; perfect_dark_recomp.16.cpp, HANDOVER 4ap) turns
// timebase ticks into time with the Xbox 360's rate built in: ticks * 1250 /
// 62347, 49.8776 ticks a microsecond. The guest timebase here runs at 50 MHz,
// so that time ran 0.245% fast. It is the network clock a match runs on
// (sub_82287538 via sub_822F6930), so in every network match the game stepped
// 0.25% fast with a double step every 6-7 s on every PC (measured in a
// two-PC match, 2026-10-25). Converting with the real guest tick rate makes it
// exact; nothing else in the game has the constant. Every PC in a match needs
// the same setting.
REXCVAR_DEFINE_BOOL(pd_exact_timer, true, "PerfectDark",
                    "Convert the game's own timer (the network match clock) with the real "
                    "guest tick rate instead of the Xbox 360's built-in 49.875 MHz");

uint64_t PdTimerMicroseconds(uint64_t ticks) {
  const uint64_t frequency = rex::chrono::Clock::guest_tick_frequency();
  if (!REXCVAR_GET(pd_exact_timer) || frequency == 0) {
    return ticks * 1250 / 62347;  // the game's own conversion
  }
  return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

// The Xbox LIVE Arcade logo (DataFiles\XBLA\ArcadeWide.ptc / ArcadeNormal.ptc) is
// a loading screen: sub_82251360 (perfect_dark_recomp.12.cpp) fades it in, keeps
// drawing it until the game has loaded (byte 0x82652141), and sub_822511D8 fades
// it out. Skipping its three draws leaves that loop as it is - frames are still
// presented and loading is not touched - with nothing on screen (user, 2026-10-25;
// HANDOVER 4aq).
REXCVAR_DEFINE_BOOL(pd_skip_arcade_logo, true, "PerfectDark",
                    "Show a black screen instead of the Xbox LIVE Arcade logo while the game "
                    "loads");

bool PdSkipArcadeLogo() {
  return REXCVAR_GET(pd_skip_arcade_logo);
}
// The blue "Xbox 360 Product Identification" screen is the N64 game's legal
// screen (titleTickLegal = sub_8224A3E8, perfect_dark_recomp.12.cpp): it moves
// on once the title timer passes 180 ticks of 60 Hz and the game has finished
// loading: about 3.2 s with the fade, which the user chose to keep (2026-10-25;
// HANDOVER 4aq). The setting is there to shorten it.
REXCVAR_DEFINE_INT32(pd_legal_screen_ticks, 180, "PerfectDark",
                     "How long the Product Identification screen at start-up stays up, in "
                     "60ths of a second (the game's own is 180)")
    .range(0, 600);

int32_t PdLegalScreenTicks() {
  return REXCVAR_GET(pd_legal_screen_ticks);
}

// Multiplayer music (HANDOVER 4ar). Every PC picks the Combat Simulator track
// itself: mpChooseTrack (sub_821CE2D0, perfect_dark_recomp.8.cpp) rolls random()
// over its own soundtrack settings and never the track it just played, at the
// start of a match and whenever a track runs out (the music tick, sub_82240598
// in .12.cpp). The game does not send it, so in a network match each player
// heard something different (user, 2026-10-27). Now the host's pick rules:
//  - the host publishes the track slot it chose (netplay::SetHostValue; joined
//    PCs ask for it twice a second);
//  - a joined PC plays the host's pick from the last 10 s instead of its own.
//    At a match's start (both PCs pick as the stage starts, the host's arriving
//    a few hundred ms later) it waits up to 1.5 s for it;
//  - with track changes during the match (the music tick's switch, kMpMusicOn,
//    set by match setup sub_821D28A0; off in the network matches tested, which
//    keep one track all match), a new host pick that arrives after a joined
//    PC's own runs its track out (elapsed = length) so the game picks again,
//    and near the end of a track a joined PC holds it for up to 3 s while it
//    waits for the host's next pick (HANDOVER 4ar, 4bj).
// Both PCs need this build; a host without it never answers, and nothing changes.
REXCVAR_DEFINE_BOOL(pd_mp_music_sync, true, "PerfectDark",
                    "In a network match, play the host's multiplayer music track on every PC "
                    "instead of each PC picking its own");

namespace {
constexpr uint32_t kGVarsMpRunning = 0x82648BF8 + 1336;  // g_Vars normmplayerisrunning
constexpr uint32_t kMpMusicOn = 0x82642078;     // the music tick's MP track switch
constexpr uint32_t kTrackElapsed = 0x82644C38;  // 60ths of a second into the track
constexpr uint32_t kTrackLength = 0x825EA178;   // its length, 60ths
constexpr uint32_t kTrackSlot = 0x82641904;     // slot of the track playing (the last pick)
constexpr uint32_t kTrackTable = 0x825DD138;    // 6 bytes a slot: u16 music id << 9 | seconds
constexpr uint32_t kTrackSlots = 48;            // the soundtrack bitmask holds 48
constexpr auto kHostPickFresh = std::chrono::seconds(10);
constexpr auto kMostHold = std::chrono::seconds(3);

constexpr uint32_t kGVarsLevelFrames = 0x82648BF8 + 0x0C;  // g_Vars lvframenum
// At a match's start the game picks while both PCs start the stage together; the
// host's pick reaches a joined PC within a few hundred ms (it asks twice a second).
constexpr auto kStartWait = std::chrono::milliseconds(1500);
constexpr uint32_t kStartFrames = 600;  // "the start": the first 10 s of the stage

uint32_t mp_music_used_seq = 0;  // the host pick this PC last played
std::chrono::steady_clock::time_point mp_music_hold_since;

uint32_t GuestU32(const uint8_t* base, uint32_t address) {
  return _byteswap_ulong(*reinterpret_cast<const uint32_t*>(base + address));
}
void SetGuestU32(uint8_t* base, uint32_t address, uint32_t value) {
  *reinterpret_cast<uint32_t*>(base + address) = _byteswap_ulong(value);
}

// The host's pick, set within the last few seconds (used before or not).
bool RecentHostPick(rex::kernel::xam::netplay::HostValue* host) {
  if (!rex::kernel::xam::netplay::LatestHostValue(host)) return false;
  return host->value < kTrackSlots && std::chrono::steady_clock::now() - host->set_at < kHostPickFresh;
}

// A recent pick of the host's that this PC has not played yet.
bool NewHostPick(rex::kernel::xam::netplay::HostValue* host) {
  return RecentHostPick(host) && host->seq != mp_music_used_seq;
}
}  // namespace

void PdMpChooseTrack(PPCContext& ctx, uint8_t* base) {
  namespace netplay = rex::kernel::xam::netplay;
  sub_821CE2D0(ctx, base);
  if (!REXCVAR_GET(pd_mp_music_sync)) return;
  const uint32_t own = GuestU32(base, kTrackSlot);
  if (netplay::IsHostingSession()) {
    netplay::SetHostValue(own);
    REXLOG_INFO("[mp_music] host: track slot {} (music {})", own, ctx.r3.u32);
    return;
  }
  if (!netplay::IsJoinedSession()) return;
  netplay::HostValue host;
  bool found = RecentHostPick(&host);
  if (!found && GuestU32(base, kGVarsLevelFrames) < kStartFrames) {
    // The match is starting: the host is picking now too; its pick is on the way.
    const auto until = std::chrono::steady_clock::now() + kStartWait;
    while (!found && std::chrono::steady_clock::now() < until) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      found = RecentHostPick(&host);
    }
  }
  if (!found) {
    REXLOG_INFO("[mp_music] own pick, slot {}, until the host's arrives", own);
    return;
  }
  mp_music_used_seq = host.seq;
  const uint32_t slot = host.value;
  const uint32_t entry = uint32_t(_byteswap_ushort(
      *reinterpret_cast<const uint16_t*>(base + kTrackTable + slot * 6)));
  ctx.r3.u64 = entry >> 9;
  SetGuestU32(base, kTrackLength, (entry & 0x1FF) * 60);
  SetGuestU32(base, kTrackSlot, slot);
  REXLOG_INFO("[mp_music] host's pick #{}: track slot {} (music {}; own pick was {})", host.seq,
              slot, entry >> 9, own);
}

// N64 poison (user, 2026-11-11): a thrown combat knife poisons its target, and
// in a match the poison is kept while dead (chr_tick_poisoned, sub_82109478). On
// the N64 respawning did not clear it, so it kept ticking after respawn (at 10%
// handicap every tick kills). 4J's bondstartnewlife (sub_820FDA68) clears the
// poison and the blur on respawn; this skips both stores in a match.
REXCVAR_DEFINE_BOOL(pd_n64_poison, false, "PerfectDark",
                    "Thrown-knife poison and its blur carry over a respawn, as on the N64 "
                    "(multiplayer only)");

REXCVAR_DEFINE_INT32(pd_n64_poison_kills, 2, "PerfectDark",
                     "With pd_n64_poison: how many more times the poison can kill after a "
                     "respawn (0-4)")
    .range(0, 4);

bool PdN64Poison(const uint8_t* base) {
  return REXCVAR_GET(pd_n64_poison) && GuestU32(base, kGVarsMpRunning) != 0;
}

// The poison counter a respawn keeps at most. A tick of poison damage happens
// each time the counter (counting down, 4 a frame) crosses a nonzero multiple of
// 720, and while dead it is held at up to 3600: so it ticks 5 more times after a
// respawn, the first one at once. Keeping kills * 720 + 719 leaves `kills` ticks,
// the first 3 s after the respawn.
int16_t PdN64PoisonRespawnCap() {
  return int16_t(std::clamp(REXCVAR_GET(pd_n64_poison_kills), 0, 4) * 720 + 719);
}

// More multiplayer weapons and shuffled weapon spawns (pd_weapons.h). The host's
// settings decide; a joined PC uses what the host sends, whatever its own say.
REXCVAR_DEFINE_STRING(pd_mp_weapons, "", "PerfectDark",
                      "Multiplayer weapon slots 7-16 in matches you host: weapon table indices "
                      "separated by commas (list in src/pd_weapons.h; 0 = Nothing, 46 = Disabled). "
                      "Empty = only the game's 6 slots. Every player needs this build");
REXCVAR_DEFINE_BOOL(pd_mp_weapon_shuffle, false, "PerfectDark",
                    "In matches you host, a weapon pickup that respawns comes back as a different "
                    "one of the stage's weapons");
REXCVAR_DEFINE_INT32(pd_mp_weapon_shuffle_seed, 0, "PerfectDark",
                     "With pd_mp_weapon_shuffle: 0 = new choices every match, any other number = "
                     "the same choices every time (for testing)");

namespace {
// A joined PC starts the stage at about the same time as the host, whose choice
// arrives within a second (the joined PC asks twice a second).
constexpr auto kWeaponsWait = std::chrono::milliseconds(3000);
// A choice the host made longer ago than this belongs to an earlier match.
constexpr auto kWeaponsFresh = std::chrono::seconds(10);
uint32_t weapons_used_seq = 0;  // the host's choice this PC last used

// This PC's own settings (a host, or a match that is not a network match).
pd::weapons::MatchConfig OwnWeaponsConfig() {
  pd::weapons::MatchConfig config;
  config.extra = pd::weapons::ParseExtraSlots(REXCVAR_GET(pd_mp_weapons));
  config.shuffle = REXCVAR_GET(pd_mp_weapon_shuffle);
  config.seed = uint32_t(REXCVAR_GET(pd_mp_weapon_shuffle_seed));
  if (config.seed == 0) {
    rex::kernel::xam::netplay::RandomBytes(reinterpret_cast<uint8_t*>(&config.seed), sizeof(config.seed));
  }
  return config;
}

// On a joined PC: the host's choice for this stage. It waits for a new one; if
// none comes (a PC that joined a match already under way) it takes the host's
// latest. False if the host never sent one (a host without this build).
bool HostWeaponsConfig(pd::weapons::MatchConfig* out) {
  namespace netplay = rex::kernel::xam::netplay;
  netplay::HostBlob blob;
  const auto is_new = [&] {
    return netplay::LatestHostBlob(pd::weapons::kBlobKey, &blob) && blob.seq != weapons_used_seq &&
           std::chrono::steady_clock::now() - blob.set_at < kWeaponsFresh;
  };
  const auto start = std::chrono::steady_clock::now();
  bool found = is_new();
  while (!found && std::chrono::steady_clock::now() - start < kWeaponsWait) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    found = is_new();
  }
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  if (!found) {
    if (!netplay::LatestHostBlob(pd::weapons::kBlobKey, &blob)) {
      REXLOG_WARN("[weapons] no weapon choice from the host after {} ms: using the game's 6 slots", waited.count());
      return false;
    }
    REXLOG_WARN("[weapons] no new weapon choice from the host after {} ms: using its last one (#{})",
                waited.count(), blob.seq);
  }
  if (!pd::weapons::DecodeBlob(blob.data, out)) {
    REXLOG_WARN("[weapons] the host's weapon choice #{} is from another version: using the game's 6 slots", blob.seq);
    return false;
  }
  weapons_used_seq = blob.seq;
  REXLOG_INFO("[weapons] host's choice #{} after {} ms", blob.seq, waited.count());
  return true;
}

// On a joined PC that built a solo stage's setup (PdSoloStageSetup): compares it
// with the host's, by fingerprint. A different one means the two PCs run
// different builds, and the match would not play the same on both: it says so
// on screen. The host sent its fingerprint as it built its setup, before the
// weapon choice this PC has just waited for, so it is normally here already.
void CheckHostStageSetup() {
  namespace netplay = rex::kernel::xam::netplay;
  namespace stages = pd::stages;
  const int built = stages::g_built_stage;
  stages::g_built_stage = -1;
  if (built < 0) return;  // not a solo stage

  netplay::HostBlob blob;
  int host_stage = -1;
  uint32_t host_fingerprint = 0;
  const auto has_it = [&] {
    return netplay::LatestHostBlob(stages::kBlobKey, &blob) &&
           stages::DecodeBlob(blob.data, &host_stage, &host_fingerprint) && host_stage == built;
  };
  const auto start = std::chrono::steady_clock::now();
  bool found = has_it();
  while (!found && std::chrono::steady_clock::now() - start < kWeaponsWait) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    found = has_it();
  }
  if (!found) {
    REXLOG_WARN("[stages] stage 0x{:02X}: no setup fingerprint from the host (an older build?)", built);
    netplay::ShowNotice("The host's game may be a different build: this stage may not match theirs");
  } else if (host_fingerprint != stages::g_built_fingerprint) {
    REXLOG_WARN("[stages] stage 0x{:02X}: this PC's setup (fingerprint {:08X}) differs from the host's ({:08X}): "
                "different builds", built, stages::g_built_fingerprint, host_fingerprint);
    netplay::ShowNotice("Your game is a different build from the host's: this stage doesn't match theirs");
  } else {
    REXLOG_INFO("[stages] stage 0x{:02X}: same setup as the host's (fingerprint {:08X})", built, host_fingerprint);
  }
}
}  // namespace

// setupCreateProps' start (sub_821FFCC8): the stage's props are about to be made.
// Decides this stage's weapon slots and shuffle (the host also sends them; a
// joined PC waits for them), then notes the stage's weapon pickups and the
// weapons they start with.
void PdCustomInstall(uint8_t* base);  // custom weapons (further down)
uint32_t PdCustomLoadGunModel(PPCContext& ctx, uint8_t* base, uint32_t id);  // further down

void PdSetupCreateProps(PPCContext& ctx, uint8_t* base) {
  namespace netplay = rex::kernel::xam::netplay;
  namespace weapons = pd::weapons;
  PdCustomInstall(base);  // before the slots are read: they may name a custom weapon's index
  weapons::g_match = {};
  weapons::g_pickups.clear();
  weapons::g_selection.clear();
  weapons::g_crates.clear();
  pd::crossbow::g_pending.clear();  // the detonator crossbow's pending explosions
  pd::crossbow::g_pending_tries.clear();
  pd::crossbow::g_detonator_bolts.clear();
  pd::crossbow::g_firing_weapon = 0;
  if (weapons::LoadU32(base, weapons::kMatchRunning) == 0 && weapons::LoadU32(base, weapons::kMatchRunning2) == 0) {
    return;  // not a multiplayer match: the stage has no weapon spawn locations
  }
  // The custom weapons' left-hand models (stage 4a), for this stage.
  for (int i = 0; i < pd::custom::kCount; ++i) {
    const pd::custom::Weapon& w = pd::custom::kList[i];
    pd::custom::g_left_model[i] = w.left_model_file ? PdCustomLoadGunModel(ctx, base, w.left_model_file) : 0;
    if (w.left_model_file && !pd::custom::g_left_cmds[i]) {
      pd::custom::g_left_cmds[i] = rex::system::kernel_state()->memory()->SystemHeapAlloc(pd::custom::kLeftCmdsSize);
    }
  }

  const char* source = "own settings";
  if (netplay::IsJoinedSession()) {
    source = "host";
    if (!HostWeaponsConfig(&weapons::g_match)) weapons::g_match = {};
    CheckHostStageSetup();
  } else {
    weapons::g_match = OwnWeaponsConfig();
    if (netplay::IsHostingSession()) {
      source = "own settings, sent to the joined PCs";
      const std::vector<uint8_t> blob = weapons::EncodeBlob(weapons::g_match);
      netplay::SetHostBlob(weapons::kBlobKey, blob.data(), blob.size());
    }
  }

  // The size of a setup object, in words: the game's own function.
  const auto object_words = [&](uint32_t obj) {
    PPCContext call = ctx;
    call.r3.u64 = obj;
    sub_821FD198(call, base);
    return call.r3.u32;
  };
  // Each pickup's starting weapon is the one its spawn location gets; together
  // they are the stage's weapon selection (with 16 slots and 10 locations, the
  // weapons of slots 1-10), which respawns choose from.
  const std::vector<uint8_t> slots = weapons::AllSlots(base, weapons::g_match);
  for (const uint32_t obj : weapons::FindPickups(base, object_words, &weapons::g_crates)) {
    const int location = base[obj + weapons::kSetupLocation] - weapons::kFirstLocation;
    const uint8_t index = weapons::IndexForLocation(base, location, slots);
    if (!weapons::CanRespawnAs(index)) continue;  // Nothing, or a Shield: not a weapon pickup
    weapons::g_pickups.push_back({obj, index, 0});
    if (std::find(weapons::g_selection.begin(), weapons::g_selection.end(), index) == weapons::g_selection.end()) {
      weapons::g_selection.push_back(index);
    }
  }

  const weapons::MatchConfig& match = weapons::g_match;
  REXLOG_INFO("[weapons] stage {} ({}): slots {}, respawn shuffle {} (seed {}), {} weapon pickups, {} ammo "
              "crates, selection {}",
              weapons::LoadU32(base, weapons::kStageNumber), source, weapons::DescribeSlots(base, slots),
              match.shuffle ? "on" : "off", match.seed, weapons::g_pickups.size(), weapons::g_crates.size(),
              weapons::DescribeSlots(base, weapons::g_selection));
}

namespace {
float LoadF32(const uint8_t* base, uint32_t address) {
  const uint32_t bits = pd::weapons::LoadU32(base, address);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}
void StoreU32(uint8_t* base, uint32_t address, uint32_t value) {
  base[address] = uint8_t(value >> 24);
  base[address + 1] = uint8_t(value >> 16);
  base[address + 2] = uint8_t(value >> 8);
  base[address + 3] = uint8_t(value);
}
void StoreF32(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  StoreU32(base, address, bits);
}

constexpr uint32_t kObjFlags = 8;    // setup/object flags
constexpr uint32_t kObjMatrix = 28;  // the object's 3x3 matrix (rotation times scale)
// Flags that make setupGenericObject place an object some other way than resting
// on its bounding box (0x2: sub_822128E8 instead of sub_82212550; 0x4, 0x8: other
// cases inside sub_82212550): those objects only get their matrix rescaled.
constexpr uint32_t kObjFlagsNotOnFloor = 0x2 | 0x4 | 0x8;

// How high above the floor the stage's floor placement (sub_82212550, called by
// setupGenericObject) puts an object's origin, for `model`: it takes the axis of
// the object's matrix closest to vertical and lifts the object by the model's
// bounding-box end on that axis times that axis' height component. `rot_y` = the
// vertical components of the object's 3 matrix rows (they include the scale).
// False if the model has no bounding box.
bool FloorLift(PPCContext& ctx, uint8_t* base, uint32_t model, const float rot_y[3], float* lift) {
  PPCContext call = ctx;
  call.r3.u64 = model;
  sub_82202DA8(call, base);  // the model's bounding-box node
  if (!call.r3.u32) return false;
  const uint32_t bbox = pd::weapons::LoadU32(base, call.r3.u32 + 4);  // {?, xmin, xmax, ymin, ymax, zmin, zmax}
  if (!bbox) return false;
  int axis = 0;  // as the game: the first of the largest
  if (std::fabs(rot_y[1]) > std::fabs(rot_y[axis])) axis = 1;
  if (std::fabs(rot_y[2]) > std::fabs(rot_y[axis])) axis = 2;
  const uint32_t low_end = bbox + 4 + uint32_t(axis) * 8;  // that axis' min, then max
  const float low = LoadF32(base, rot_y[axis] < 0 ? low_end + 4 : low_end);
  *lift = -rot_y[axis] * low;
  return true;
}

// Makes a weapon pickup's object hold weapon table entry `index`: a new model
// for it, made as setupPlaceWeapon (sub_821FE370), setupGenericObject
// (sub_821FDCF0) and objInit (sub_82203B50) make one, then the old model freed.
// Only for a pickup that is not on show (it is about to respawn). The weapon's
// models are loaded first if they are not yet: a weapon of the selection whose
// pickup the stage left out (player count or difficulty) was never loaded.
bool SetPickupWeapon(PPCContext& ctx, uint8_t* base, uint32_t prop, uint32_t obj, uint8_t index) {
  namespace weapons = pd::weapons;
  const uint32_t entry = weapons::EntryAddress(index);
  const uint8_t weapon_num = base[entry];
  const uint16_t model_num = weapons::LoadU16(base, entry + 6);
  const uint32_t state = weapons::kModelStates + uint32_t(model_num) * 8;

  PPCContext call = ctx;
  call.r3.u64 = model_num;
  sub_821FD640(call, base);  // setupLoadModeldef: the pickup's model
  call = ctx;
  call.r3.u64 = weapon_num;
  sub_82101298(call, base);  // the models its shots use (bolts, rockets...)
  const uint32_t file = weapons::LoadU32(base, state);
  if (!file) {
    REXLOG_WARN("[weapons] model {} of index {} did not load", model_num, index);
    return false;
  }

  call = ctx;
  call.r3.u64 = file;
  sub_8218E648(call, base);  // modelmgrInstantiateModelWithoutAnim
  const uint32_t model = call.r3.u32;
  if (!model) {
    REXLOG_WARN("[weapons] no free model instance for index {}", index);
    return false;
  }
  // As setupPlaceWeapon: each weapon has its own pickup scale (entry +8, 256 = 1.0).
  weapons::StoreU16(base, obj + weapons::kObjScale, weapons::LoadU16(base, entry + 8));

  // As objInit: the model knows its object, and its scale is the model table's.
  StoreU32(base, model + weapons::kModelObj, obj);
  base[model + 1] = 0;
  float scale = float(weapons::LoadU16(base, state + 6)) * weapons::kModelStateScaleUnit;
  // As setupGenericObject: times the object's own scale (256 = 1.0).
  scale *= float(weapons::LoadU16(base, obj + weapons::kObjScale)) / 256.0f;
  StoreF32(base, model + weapons::kModelScale, scale);

  const uint32_t old_model = weapons::LoadU32(base, obj + weapons::kObjModel);
  const float old_scale = old_model ? LoadF32(base, old_model + weapons::kModelScale) : 0.0f;
  if (old_scale > 0.0f) {
    // The stage placed the pickup for its first model: setupGenericObject scaled the
    // object's matrix (obj+28, 3x3, rows of 3 floats) by that model's scale
    // (sub_822438C8), and its floor placement lifted the object by that model's
    // bounding box. Without this a new weapon was drawn at a size made for the
    // old one, and rested at the old one's height (small ones floated).
    const float ratio = scale / old_scale;
    float rot_y[3];
    for (int row = 0; row < 3; ++row) rot_y[row] = LoadF32(base, obj + kObjMatrix + uint32_t(row * 3 + 1) * 4);
    float old_lift = 0.0f;
    float new_lift = 0.0f;
    const bool floor_placed = (weapons::LoadU32(base, obj + kObjFlags) & kObjFlagsNotOnFloor) == 0;
    if (floor_placed && FloorLift(ctx, base, old_model, rot_y, &old_lift)) {
      const float new_rot_y[3] = {rot_y[0] * ratio, rot_y[1] * ratio, rot_y[2] * ratio};
      if (FloorLift(ctx, base, model, new_rot_y, &new_lift)) {
        // The prop's position (prop+8: x, y, z); the respawn that follows this
        // hook (sub_821FD838) works out its rooms again from it.
        StoreF32(base, prop + 12, LoadF32(base, prop + 12) - old_lift + new_lift);
      }
    }
    for (uint32_t i = 0; i < 9; ++i) {
      StoreF32(base, obj + kObjMatrix + i * 4, LoadF32(base, obj + kObjMatrix + i * 4) * ratio);
    }
  }
  StoreU32(base, obj + weapons::kObjModel, model);
  weapons::StoreU16(base, obj + weapons::kObjModelNum, model_num);
  base[obj + weapons::kObjWeaponNum] = weapon_num;
  if (old_model) {
    call = ctx;
    call.r3.u64 = old_model;
    sub_8218E0D8(call, base);  // modelmgrFreeModel
  }

  // As the game's weapon init (sub_8221E410, after objInit): hide the muzzle flash.
  // A new gun model has its flash parts visible, which showed as a stretched fire
  // texture on the pickup. sub_8220D778 = weaponSetGunfireVisible(prop, visible,
  // room); room -1 = no flash light.
  call = ctx;
  call.r3.u64 = prop;
  call.r4.u64 = 0;
  call.r5.s64 = -1;
  sub_8220D778(call, base);
  return true;
}

// The ammo crates that hold pickup `obj`'s ammo get the ammo of weapon table
// entry `index` instead. setupCreateProps loads the ammo models of a crate's
// filled slots only, so those of newly filled slots are loaded here
// (setupLoadModeldef, sub_821FD640: nothing to do if already loaded).
void SetPickupCrates(PPCContext& ctx, uint8_t* base, uint32_t obj, uint8_t index) {
  namespace weapons = pd::weapons;
  for (const weapons::Crate& crate : weapons::g_crates) {
    if (crate.weapon_obj != obj) continue;
    weapons::SetCrateAmmo(base, crate, index);
    for (int s = 0; s < weapons::kCrateSlotCount; ++s) {
      const uint32_t slot = crate.obj + weapons::kCrateSlots + uint32_t(s) * 4;
      const uint16_t model = weapons::LoadU16(base, slot);
      const uint16_t quantity = weapons::LoadU16(base, slot + 2);
      if (quantity == 0 || model == weapons::kNoModel) continue;
      PPCContext call = ctx;
      call.r3.u64 = model;
      sub_821FD640(call, base);
    }
  }
}
}  // namespace

// propEnable's start (sub_821F7518, r3 = the prop): a prop is put on show. Every
// PC calls it when a weapon pickup respawns: the host from the object tick, a
// joined PC from the host's respawn message. With the respawn shuffle on, a
// stage pickup that respawns first becomes a different weapon.
void PdPropEnable(PPCContext& ctx, uint8_t* base, uint32_t prop) {
  namespace weapons = pd::weapons;
  if (!weapons::g_match.shuffle || weapons::g_pickups.empty() || !prop) return;
  if (weapons::LoadU32(base, weapons::kLevelFrames) == 0) return;  // the stage is being made
  const uint32_t obj = weapons::LoadU32(base, prop + weapons::kPropObj);
  for (size_t i = 0; i < weapons::g_pickups.size(); ++i) {
    weapons::Pickup& pickup = weapons::g_pickups[i];
    if (pickup.obj != obj) continue;
    const uint32_t respawn = ++pickup.respawns;
    const uint8_t next = weapons::RespawnIndex(weapons::g_selection, pickup.index, weapons::g_match.seed,
                                               uint32_t(i), respawn);
    // SetPickupWeapon logs a warning when it fails; the pickup then keeps its weapon.
    if (next != pickup.index && SetPickupWeapon(ctx, base, prop, obj, next)) {
      pickup.index = next;
      SetPickupCrates(ctx, base, obj, next);
    }
    return;
  }
}

// setupCreateProps' ammo-crate case, where it skips the crate when its weapon has
// no ammo (Combat Boost, Cloaking Device...). With the respawn shuffle on, the
// pickup can later become a gun, so the crate is made anyway, empty (as
// SetCrateAmmo leaves it for a weapon without ammo). True = make the crate.
// Every PC has the same slots and shuffle flag, so all of them make the same
// crates in the same order (pickups are matched online by that order).
bool PdMakeCrateWithoutAmmo(uint8_t* base, uint32_t crate_obj) {
  namespace weapons = pd::weapons;
  if (!weapons::g_match.shuffle) return false;
  for (const weapons::Crate& crate : weapons::g_crates) {
    if (crate.obj != crate_obj) continue;
    for (const weapons::Pickup& pickup : weapons::g_pickups) {
      if (pickup.obj != crate.weapon_obj) continue;
      weapons::SetCrateAmmo(base, crate, pickup.index);  // empties it
      return true;
    }
    return false;  // its weapon is not a pickup that respawns (Nothing, Shield)
  }
  return false;
}

// mpGetMpWeaponByLocation (sub_821CC458) for a match with extra slots: the guest
// address of the weapon table entry spawn location `location` gets. False with no
// extra slots (the game's own function answers).
bool PdWeaponByLocation(const uint8_t* base, int32_t location, uint32_t* entry) {
  namespace weapons = pd::weapons;
  if (weapons::g_match.extra.empty()) return false;
  const uint8_t index = weapons::IndexForLocation(base, location, weapons::AllSlots(base, weapons::g_match));
  *entry = weapons::EntryAddress(index);
  return true;
}

// Each frame on a joined PC: asking for the host's weapon choice now starts the
// polling in the lobby, so it is there when the stage loads.
void PdWeaponsFrame() {
  namespace netplay = rex::kernel::xam::netplay;
  if (!netplay::IsJoinedSession()) return;
  netplay::HostBlob blob;
  netplay::LatestHostBlob(pd::weapons::kBlobKey, &blob);
}

// ---- Solo stages as arenas (pd_stages.h) ---------------------------------------------

REXCVAR_DEFINE_BOOL(pd_solo_stages, true, "PerfectDark",
                    "Solo mission stages can be Combat Simulator arenas: their missing multiplayer "
                    "setup (spawn points, ...) is built when the stage loads. Every player needs this "
                    "build");
namespace {
// Calls a game function with up to two arguments; returns its r3.
template <typename Fn>
uint32_t CallGuest(Fn fn, PPCContext& ctx, uint8_t* base, uint32_t r3, uint32_t r4 = 0) {
  PPCContext call = ctx;
  call.r3.u64 = r3;
  call.r4.u64 = r4;
  fn(call, base);
  return call.r3.u32;
}
}  // namespace

// setup_load_files, right after the multiplayer setup file was loaded (r3 = the
// buffer). For a solo stage that file is an empty stub: this builds the stage's
// multiplayer setup and returns it instead (else the buffer it was given).
uint32_t PdSoloStageSetup(PPCContext& ctx, uint8_t* base, uint32_t loaded) {
  namespace stages = pd::stages;
  stages::g_built_stage = -1;  // until a solo stage's setup is built below
  if (!REXCVAR_GET(pd_solo_stages)) return loaded;
  if (stages::U32(base + stages::kMatchRunning) == 0) return loaded;  // the solo setup was loaded
  const uint32_t index = stages::U32(base + stages::kStageIndex);
  if (index >= stages::kStageCount) return loaded;
  const uint8_t* entry = base + stages::kStages + index * stages::kStageEntrySize;
  const int stagenum = stages::S16(entry + stages::kStageId);
  if (stages::SoloStageIndex(base, uint32_t(stagenum)) < 0) return loaded;  // a real arena

  // A scenario that needs hills or case spots, which the solo stages lack, is
  // played as Combat. Every PC has the host's scenario, so all of them switch.
  // The game creates the scenario's objects later, from the setup built below.
  const uint8_t scenario = base[stages::kMpSetupScenario];
  if (!stages::ScenarioPlayableOnSoloStage(scenario)) {
    base[stages::kMpSetupScenario] = stages::kScenarioCombat;
    REXLOG_WARN("[stages] stage 0x{:02X}: scenario {} needs spots solo stages lack; playing Combat", stagenum,
                scenario);
  }

  // Load the stage's pads file and solo setup with the game's own loader
  // (file_load_to_new: a new buffer in the stage's memory, freed with the stage).
  // The game loads the pads file again itself a moment later.
  const auto load = [&](uint16_t file, stages::PadsFile* out) {
    const uint32_t buffer = CallGuest(sub_82197200, ctx, base, file, stages::kLoadMethodDefault);
    out->data = base + buffer;
    out->size = stages::U32(base + stages::kFileInfo + uint32_t(file) * 8);
  };
  stages::PadsFile pads, solo;
  load(stages::U16(entry + stages::kStagePadsFile), &pads);
  load(stages::U16(entry + stages::kStageSetupFile), &solo);

  const int start_pad = stages::SoloStartPad(solo.data, solo.size);
  // Hand-picked spawn and weapon pads (pd_stage_layouts.h), if the stage has any.
  const stages::StageLayout* layout = stages::FindStageLayout(uint32_t(stagenum));
  const std::vector<int> no_pads;
  const std::vector<int>& chosen_spawns = layout ? layout->spawns : no_pads;
  const std::vector<int>& chosen_weapons = layout ? layout->weapons : no_pads;

  const std::vector<stages::Pad> spawns = stages::ChooseSpawns(pads, start_pad, chosen_spawns);
  if (spawns.empty()) {
    REXLOG_ERROR("[stages] stage 0x{:02X}: no pad to spawn on; keeping the game's empty setup", stagenum);
    return loaded;
  }

  // The solo setup's props, one command at a time (each command's length comes
  // from the game's own setup_get_cmd_length, sub_821FD198).
  std::vector<stages::SoloCommand> cmds;
  {
    const uint32_t solo_guest = uint32_t(solo.data - base);
    uint32_t offset = stages::U32(solo.data + 0x10);
    for (int n = 0; n < 10000 && offset + 4 <= solo.size; ++n) {
      if (solo.data[offset + 3] == stages::kTypeEnd) break;
      const uint32_t words = CallGuest(sub_821FD198, ctx, base, solo_guest + offset);
      if (words == 0 || offset + words * 4 > solo.size) break;
      cmds.push_back({offset, words});
      offset += words * 4;
    }
  }
  const std::vector<stages::LiftActivation> solo_lifts =
      stages::SoloLiftActivations(solo.data, solo.size, base + stages::kAiCommandLengths);
  const stages::CopiedProps props = stages::CopySoloProps(solo.data, solo.size, cmds, solo_lifts);

  const std::vector<stages::WeaponSpawn> weapons = stages::ChooseWeaponSpawns(pads, spawns, chosen_weapons);

  stages::SetupWriter writer;
  for (const stages::Pad& pad : spawns) writer.spawn_pads.push_back(pad.num);
  // The weapon spawns first, as in the arenas' own setups; the solo objects after
  // them (their links are relative, so moving the block changes nothing).
  writer.props = stages::WeaponSpawnCommands(weapons);
  writer.props.insert(writer.props.end(), props.bytes.begin(), props.bytes.end());
  writer.lifts = props.lifts;
  const std::vector<uint8_t> setup = writer.Build();

  // The new setup goes in the stage's memory too (the allocator file_load_to_new
  // uses: size, pool 4 = the stage's).
  const uint32_t buffer = CallGuest(sub_8218DB98, ctx, base, uint32_t(setup.size()), 4);
  if (!buffer) {
    REXLOG_ERROR("[stages] stage 0x{:02X}: no memory for the setup ({} bytes)", stagenum, setup.size());
    return loaded;
  }
  std::memcpy(base + buffer, setup.data(), setup.size());

  // For the check that every PC built the same setup (CheckHostStageSetup): the
  // host sends its fingerprint; a joined PC compares it once the props are made.
  stages::g_built_stage = stagenum;
  stages::g_built_fingerprint = stages::Fingerprint(setup);
  namespace netplay = rex::kernel::xam::netplay;
  if (netplay::IsHostingSession()) {
    const std::vector<uint8_t> blob = stages::EncodeBlob(stagenum, stages::g_built_fingerprint);
    netplay::SetHostBlob(stages::kBlobKey, blob.data(), blob.size());
  } else if (netplay::IsJoinedSession()) {
    netplay::HostBlob unused;
    netplay::LatestHostBlob(stages::kBlobKey, &unused);  // starts listening for the host's
  }

  int kept_total = 0;
  for (const auto& [type, count] : props.kept) kept_total += count;
  REXLOG_INFO("[stages] stage 0x{:02X}: built its multiplayer setup ({} bytes at 0x{:08X}): {} spawn points, "
              "{} weapon spawns, {} of the solo setup's {} commands kept, {} doors unlocked, {} lifts switched on",
              stagenum, setup.size(), buffer, spawns.size(), weapons.size(), kept_total, cmds.size(),
              props.unlocked, props.lifts.size());

  return buffer;
}

// The arena name lists (sub_8227E590, sub_82281160) read a stage's name id from a
// 17-entry table, one entry past its end for a stage not in it (`offset` = the
// entry * 4). For such a stage: the solo stage's own name, or "Random".
uint32_t PdStageNameText(const uint8_t* base, uint32_t offset, uint32_t text) {
  namespace stages = pd::stages;
  if (offset < stages::kArenaNameCount * 4) return text;
  const uint16_t name = stages::SoloStageName(base, base[stages::kMpSetupStage]);
  return name ? name : stages::U32(base + stages::kArenaNameTexts + 16 * 4);
}

// The Arena row of Game Setup (its label, sub_821DA2C0) for a stage missing from
// g_MpArenas, where the game would show an empty label: the solo stage's name, as
// a string. 0 when the stage is not a solo stage either (the game's label stays).
uint32_t PdArenaRowLabel(PPCContext& ctx, uint8_t* base) {
  namespace stages = pd::stages;
  const uint16_t name = stages::SoloStageName(base, base[stages::kMpSetupStage]);
  return name ? CallGuest(sub_821886E8, ctx, base, name) : 0;  // text id -> string
}

// The pad marking keys, for hand-picking solo stage layouts (PerfectDarkApp::MarkPad).
REXCVAR_DEFINE_BOOL(pd_solo_mark_keys, false, "PerfectDark",
                    "For hand-picking solo stage layouts (pd_stage_layouts.h): in a match on a solo stage, "
                    "Home / End mark the pad you stand on as a spawn point / weapon spawn, Delete or Page Up "
                    "undoes, Shift+Home or Page Down logs the pads around you");

REXCVAR_DEFINE_BOOL(pd_solo_in_random, true, "PerfectDark",
                    "The Random arena can also pick the solo mission stages offered in the Arena "
                    "menu (only the host's setting counts online)");

// The Random arena's pick (sub_821D51A0, which returns a stage number): the game
// picks one of the 16 arenas. With pd_solo_in_random the solo stages of the Arena
// menu join the draw, all with the same chance. False: the game picks. Online only
// the host picks; the joined PCs get its stage.
bool PdRandomArena(uint32_t* stage) {
  namespace stages = pd::stages;
  if (!REXCVAR_GET(pd_solo_stages) || !REXCVAR_GET(pd_solo_in_random)) return false;
  const uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  static std::mt19937 rng(std::random_device{}());
  const int arenas = stages::kMpArenaRandomIndex;  // the 16 real arenas, not Random itself
  std::uniform_int_distribution<int> pick(0, arenas + stages::kSoloArenaCount - 1);
  const int i = pick(rng);
  *stage = i < arenas ? uint32_t(stages::S16(base + stages::kMpArenas + uint32_t(i) * stages::kMpArenaSize))
                      : stages::kSoloArenas[i - arenas];
  REXLOG_INFO("[stages] random arena: stage 0x{:02X}", *stage);
  return true;
}

// ---- The Detonator Crossbow (pd_crossbow.h) --------------------------------------
// A custom weapon (pd_custom_weapons.h): the crossbow with Detonate as its
// secondary function, which explodes every Detonator bolt its player fired.
REXCVAR_DEFINE_INT32(pd_custom_tint_strength, 60, "PerfectDark",
                     "How strongly custom weapons (the Detonator Crossbow) show their colour: 0-100");
REXCVAR_DEFINE_STRING(pd_xbow_tint, "orange", "PerfectDark",
                      "The Detonator Crossbow's colour: red, orange, yellow, green, cyan, blue, purple, "
                      "pink, #RRGGBB, or off");
REXCVAR_DEFINE_STRING(pd_xbow_bolt_tint, "red", "PerfectDark",
                      "The Detonator Crossbow's bolts' colour: red, orange, yellow, green, cyan, blue, "
                      "purple, pink, #RRGGBB, or off (like the crossbow's bolts)");
REXCVAR_DEFINE_BOOL(pd_xbow_bolt_blink, true, "PerfectDark",
                    "The Detonator Crossbow's bolts blink (true) or keep their colour steady (false)");
REXCVAR_DEFINE_STRING(pd_xbow_explosion, "medium", "PerfectDark",
                      "The Detonator Crossbow's bolt explosions: small (the Phoenix's), medium (a "
                      "SuperDragon grenade's), large (a rocket's), or an explosion type 1-25");

// Each frame (game thread): which explosion this PC's Detonator bolts use. The
// host sends its own whenever it starts hosting or the setting changes; a joined
// PC asks twice a second for the host's latest (medium until it has one); with
// no session, its own setting.
namespace {
bool xbow_hosting = false;               // the host blob went out for this hosting session
uint32_t xbow_sent = 0;                  // the explosion type last sent
int xbow_poll_frames = 0;
}

void PdXbowNetFrame() {
  namespace netplay = rex::kernel::xam::netplay;
  namespace xbow = pd::crossbow;
  // The Detonator Crossbow's tint (cosmetic: each PC uses its own setting).
  pd::custom::g_tint[pd::custom::IndexOf(xbow::kWeaponDetonator)] =
      pd::custom::ParseTint(REXCVAR_GET(pd_xbow_tint));
  pd::custom::g_tint_strength = uint32_t(std::clamp(REXCVAR_GET(pd_custom_tint_strength), 0, 100)) * 255 / 100;
  xbow::g_bolt_tint = pd::custom::ParseTint(REXCVAR_GET(pd_xbow_bolt_tint));
  xbow::g_bolt_blink = REXCVAR_GET(pd_xbow_bolt_blink);
  const uint32_t own = xbow::ParseExplosion(REXCVAR_GET(pd_xbow_explosion));
  if (netplay::IsHostingSession()) {
    if (!xbow_hosting || own != xbow_sent) {
      const std::vector<uint8_t> blob = xbow::EncodeBlob(own);
      netplay::SetHostBlob(xbow::kBlobKey, blob.data(), blob.size());
      REXLOG_INFO("[xbow] hosting: Detonator explosion type {} sent to the joined PCs", own);
      xbow_hosting = true;
      xbow_sent = own;
    }
    xbow::g_explosion = own;
    return;
  }
  xbow_hosting = false;
  if (!netplay::IsJoinedSession()) {
    xbow::g_explosion = own;
    return;
  }
  if (++xbow_poll_frames % 30 != 0) return;
  netplay::HostBlob blob;
  uint32_t host = 0;
  if (!netplay::LatestHostBlob(xbow::kBlobKey, &blob) || !xbow::DecodeBlob(blob.data, &host)) host = xbow::kExplosionMedium;
  if (host != xbow::g_explosion) {
    REXLOG_INFO("[xbow] joined: Detonator explosion type {} ({})", host,
                blob.valid ? "the host's" : "medium, nothing heard from the host yet");
  }
  xbow::g_explosion = host;
}

// A fire path just made a bolt (`obj`, 0 if it could not): it is a Detonator bolt
// when the Detonator Crossbow fired it (its crossbow check saw that weapon).
void PdXbowBoltCreated(uint8_t* base, uint32_t obj) {
  namespace xbow = pd::crossbow;
  const bool detonator = xbow::g_firing_weapon == xbow::kWeaponDetonator;
  xbow::g_firing_weapon = 0;
  if (!obj) return;
  if (detonator) {
    xbow::g_detonator_bolts.insert(obj);
  } else {
    xbow::g_detonator_bolts.erase(obj);  // a vanilla bolt at an address a Detonator bolt had
  }
}

namespace {
// Marks a bolt for deletion: its timer at 1 (= at rest, so nothing in its branch
// runs again before the object is freed), then the DELETING flag, as the game's
// mine branch sets it after an explosion.
void XbowDeleteBolt(uint8_t* base, uint32_t obj) {
  namespace xbow = pd::crossbow;
  base[obj + xbow::kObjTimer240] = 0;
  base[obj + xbow::kObjTimer240 + 1] = 1;
  StoreU32(base, obj + xbow::kObjHidden, xbow::LoadU32(base, obj + xbow::kObjHidden) | xbow::kHiddenDeleting);
}

// Before a bolt stuck in a wall or floor explodes: points its scorch mark the
// way the surface faces. prop_explode puts the scorch of an object stuck to a
// surface (a mine) at the object's position plus its up axis (realrot row 1)
// times the bottom of its model, facing along that up axis. A mine lies flat on
// the surface, so its up axis is the surface's normal; a bolt keeps the line it
// flew along, so its up axis runs along the wall and the scorch stood out of it.
// This casts a ray along the bolt's flight line (realrot row 2, the way its tip
// points) to find the surface and its normal, then turns the bolt's up axis to
// that normal and moves the bolt so the scorch lands on the surface. The bolt is
// deleted right after its explosion, so nothing else sees these changes.
void XbowAlignScorch(PPCContext& ctx, uint8_t* base, uint32_t prop, uint32_t obj) {
  namespace xbow = pd::crossbow;
  if (xbow::LoadU32(base, prop + xbow::kPropParent)) return;  // stuck in a player: no scorch
  const uint32_t model = xbow::LoadU32(base, obj + xbow::kObjModel);
  if (!model) return;
  PPCContext call = ctx;
  call.r3.u64 = model;
  sub_82202DA8(call, base);  // the model's bounding-box node
  if (!call.r3.u32) return;
  const uint32_t bbox = xbow::LoadU32(base, call.r3.u32 + 4);  // {?, xmin, xmax, ymin, ymax, zmin, zmax}
  if (!bbox) return;
  const float ymin = LoadF32(base, bbox + 12);
  const float zmax = LoadF32(base, bbox + 24);

  float pos[3], forward[3];
  for (int i = 0; i < 3; ++i) {
    pos[i] = LoadF32(base, prop + xbow::kPropPos + uint32_t(i) * 4);
    forward[i] = LoadF32(base, obj + xbow::kObjRealRot + uint32_t(6 + i) * 4);  // row 2, times the scale
  }
  // The ray: from 2 bolt lengths behind the bolt's position to 6 lengths ahead.
  // Points in guest memory, because the game's collision test reads them there:
  // the stack below the game's own stack pointer is free scratch space.
  const uint32_t from = ctx.r1.u32 - 0x400;
  const uint32_t to = from + 16;
  const uint32_t hit = from + 32;
  const uint32_t normal = from + 48;
  for (int i = 0; i < 3; ++i) {
    StoreF32(base, from + uint32_t(i) * 4, pos[i] - forward[i] * zmax * 2.0f);
    StoreF32(base, to + uint32_t(i) * 4, pos[i] + forward[i] * zmax * 6.0f);
  }
  call = ctx;
  call.r1.u64 = from - 0x200;  // the game's functions get stack below the scratch points
  call.r3.u64 = from;
  call.r4.u64 = prop + xbow::kPropRooms;
  call.r5.u64 = to;
  call.r6.u64 = xbow::kCdTypeSurfaces;
  sub_820E4E88(call, base);  // cd_test_los_oobok_findclosest_autoflags: 0 = it hit something
  if (call.r3.s32 != 0) {
    // No surface found. The bolt points into whatever it is stuck in, so the
    // reverse of its flight line is a fair guess at that surface's normal: exact
    // for a straight-on shot, tilted for a slanted one, but never standing out
    // of the wall as the scorch did with the bolt's own up axis.
    const float length = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
    if (length < 0.001f) return;
    for (int i = 0; i < 3; ++i) {
      const float n = -forward[i] / length;
      StoreF32(base, obj + xbow::kObjRealRot + uint32_t(3 + i) * 4, n);
      StoreF32(base, prop + xbow::kPropPos + uint32_t(i) * 4, pos[i] - n * ymin);
    }
    return;
  }
  call.r3.u64 = hit;
  call.r4.u64 = 0;
  call.r5.u64 = 0;
  sub_820DDDC0(call, base);  // cd_get_obstacle_pos
  call.r3.u64 = normal;
  sub_820E7070(call, base);  // cd_get_obstacle_normal

  float n[3], at[3];
  for (int i = 0; i < 3; ++i) {
    n[i] = LoadF32(base, normal + uint32_t(i) * 4);
    at[i] = LoadF32(base, hit + uint32_t(i) * 4);
  }
  const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (length < 0.001f) return;
  float facing = 0.0f;  // the normal must face back towards where the bolt came from
  for (int i = 0; i < 3; ++i) {
    n[i] /= length;
    facing += n[i] * forward[i];
  }
  if (facing > 0.0f) {
    for (float& c : n) c = -c;
  }
  // Up axis = the normal; position = the surface point minus normal * ymin, so
  // prop_explode's "position + up * ymin" is the surface point itself.
  for (int i = 0; i < 3; ++i) {
    StoreF32(base, obj + xbow::kObjRealRot + uint32_t(3 + i) * 4, n[i]);
    StoreF32(base, prop + xbow::kPropPos + uint32_t(i) * 4, at[i] - n[i] * ymin);
  }
}
}  // namespace

// weapon_tick's bolt branch (sub_82214A40 at 0x82215080; r27 = prop, r31 = obj),
// on every PC for every bolt each tick. When the bolt's owner presses Detonate
// (their bit in g_PlayersDetonatingMines; every PC sees every player's press):
//   - on the owner's own PC the bolt explodes exactly as the game's remote-mine
//     branch explodes a mine (prop_explode, then the object is deleted); the
//     explosion is sent to the other PCs. If no explosion slot is free it stays
//     pending and is tried again at the next tick.
//   - on every other PC this copy of the bolt is just deleted: the explosion
//     arrives from the owner's PC.
// True = done: the hook jumps past the rest of the bolt branch.
bool PdXbowBoltTick(PPCContext& ctx, uint8_t* base, uint32_t prop, uint32_t obj) {
  namespace xbow = pd::crossbow;
  if (!xbow::g_detonator_bolts.count(obj)) return false;  // only the Detonator Crossbow's bolts

  const int owner = xbow::OwnerPlayer(base, obj);
  const bool detonating = (xbow::LoadU32(base, xbow::kDetonatingMines) >> owner) & 1;
  const auto pending = xbow::g_pending.find(prop);
  const bool was_pending = pending != xbow::g_pending.end() && pending->second == obj;
  if (!detonating && !was_pending) return false;

  if (!xbow::IsLocalPlayer(base, owner)) {
    XbowDeleteBolt(base, obj);
    xbow::g_detonator_bolts.erase(obj);
    return true;
  }

  // A bolt outside every room (it flew out of the level) can never explode: the
  // game makes no explosion without a room. Test 2.1 found such bolts retried
  // every frame for minutes; they are removed instead, as is any bolt still
  // without an explosion slot after 10 seconds.
  int& tries = xbow::g_pending_tries[prop];
  if (!was_pending) tries = 0;
  // (Only a free bolt: one stuck in a player explodes where the player is.)
  const bool outside = !xbow::LoadU32(base, prop + xbow::kPropParent) && xbow::LoadS16(base, prop + xbow::kPropRooms) < 0;
  if (outside || ++tries > xbow::kMaxPendingTries) {
    if (was_pending) xbow::g_pending.erase(pending);
    xbow::g_pending_tries.erase(prop);
    XbowDeleteBolt(base, obj);
    xbow::g_detonator_bolts.erase(obj);
    return true;
  }

  if (!was_pending) XbowAlignScorch(ctx, base, prop, obj);
  PPCContext call = ctx;
  call.r3.u64 = prop;
  call.r4.u64 = xbow::g_explosion;  // this PC's, or the host's online (PdXbowNetFrame)
  sub_82205A18(call, base);  // prop_explode: false if no explosion slot is free
  if (!call.r3.u32) {
    xbow::g_pending[prop] = obj;  // try again next tick, as the game does for a mine
    return false;
  }
  if (was_pending) xbow::g_pending.erase(pending);
  xbow::g_pending_tries.erase(prop);
  XbowDeleteBolt(base, obj);
  xbow::g_detonator_bolts.erase(obj);
  return true;
}

// Each frame on a joined PC (from PdNoteGameFrame).
void PdMpMusicFrame(uint8_t* base) {
  namespace netplay = rex::kernel::xam::netplay;
  if (!REXCVAR_GET(pd_mp_music_sync) || !netplay::IsJoinedSession()) return;
  netplay::HostValue host;
  const bool new_pick = NewHostPick(&host);  // also keeps the polling going in the lobby
  if (!GuestU32(base, kGVarsMpRunning) || !GuestU32(base, kMpMusicOn)) return;
  const uint32_t length = GuestU32(base, kTrackLength);
  const uint32_t elapsed = GuestU32(base, kTrackElapsed);
  const auto now = std::chrono::steady_clock::now();
  if (new_pick) {
    if (GuestU32(base, kTrackSlot) == host.value) {
      mp_music_used_seq = host.seq;  // already playing it
    } else if (int32_t(elapsed) <= int32_t(length)) {
      SetGuestU32(base, kTrackElapsed, length);  // the next music tick changes track
    }
    mp_music_hold_since = {};
    return;
  }
  // Close to the end with no new pick from the host yet: wait for it.
  if (length > 8 && int32_t(elapsed) >= int32_t(length) - 8 && int32_t(elapsed) <= int32_t(length)) {
    if (mp_music_hold_since == std::chrono::steady_clock::time_point{}) mp_music_hold_since = now;
    if (now - mp_music_hold_since < kMostHold) {
      SetGuestU32(base, kTrackElapsed, length - 8);
    }
  } else if (int32_t(elapsed) < int32_t(length) - 8) {
    mp_music_hold_since = {};
  }
}

// Game steps in the stutter log (vblank_timing_log, HANDOVER 4al). Called at the
// start of every frame, where g_Vars diffframe60 still holds the step count of
// the frame that just ended. A frame that took under 20 ms but stepped the game
// twice or more is a double step: the game jumps ahead on screen, which is what
// the 4aj clock fault looked like. Every 10 s one line also gives game time
// against real time (100% = the game runs at its own speed).
// ---- Custom weapons (pd_custom_weapons.h) ----------------------------------------

// Makes the custom weapons once, the first time the game thread gets here.
void PdCustomInstall(uint8_t* base) {
  if (pd::custom::g_installed) return;
  auto* memory = rex::system::kernel_state()->memory();
  pd::custom::Install(base, [memory](uint32_t size) { return memory->SystemHeapAlloc(size); });
}

// ---- Custom weapons: a first-person gun model of our own (stage 4a) -------------------
// Loads a first-person gun model file (e.g. the remote mine's 0x393, which holds the
// mine and the detonator) into memory of our own, with the same calls, in the same
// order, as the game's gun loader (sub_82170488; N64 bgun_tick_gun_load), so a
// custom weapon's left hand can be drawn with it (pd_custom_weapons.h). Done at each
// stage start, as the game reloads its own gun models then. Test 4a.2: every step
// works; the textures do not go into the pool (XBLA keeps them elsewhere).
namespace {
constexpr uint32_t kGunModelBlockSize = 0x60000;    // the model, then its textures (a gun gets ~0x25800)
constexpr uint32_t kGunModelTexPoolSize = 64;       // struct texpool (N64: 4 pointers; room to spare)
constexpr uint32_t kGunModelLoadType = 0x8262BF84;  // g_LoadType byte; 7 = LOADTYPE_GUN
constexpr uint32_t kGunModelFileInfo = 0x826302F0;  // g_FileInfo: per file id {u32 loaded size, u32 alloc size}
constexpr uint32_t kGunModelSegment = 0x05000000;   // a gun file's pointers are offsets from this
constexpr int32_t kGunModelNumTextures = 5747;      // texture numbers below this are loadable (N64 NUM_TEXTURES)
struct GunModelMemory {
  uint32_t block = 0;  // our memory for one model file (allocated once, reused every stage)
  uint32_t pool = 0;   // its texture pool
};
std::map<uint32_t, GunModelMemory> gun_model_memory;  // by file id
}

// Returns the loaded modeldef, or 0.
uint32_t PdCustomLoadGunModel(PPCContext& ctx, uint8_t* base, uint32_t id) {
  auto* memory = rex::system::kernel_state()->memory();
  GunModelMemory& mem = gun_model_memory[id];
  if (!mem.block) mem.block = memory->SystemHeapAlloc(kGunModelBlockSize);
  if (!mem.pool) mem.pool = memory->SystemHeapAlloc(kGunModelTexPoolSize);
  const uint32_t block = mem.block;
  const uint32_t pool = mem.pool;
  if (!block || !pool) {
    REXLOG_WARN("[custom] no memory for gun model file 0x{:03X}", id);
    return 0;
  }
  const uint32_t ptr = (block + 15) & ~15u;
  uint32_t remaining = kGunModelBlockSize - (ptr - block);
  std::memset(base + pool, 0, kGunModelTexPoolSize);

  PPCContext call = ctx;
  call.r3.u64 = id;
  sub_821971F0(call, base);  // file_get_inflated_size
  const uint32_t inflated = call.r3.u32;
  uint32_t load_size = ((inflated + 63) & ~63u) + 0x8000;
  if (load_size > remaining) load_size = remaining;
  base[kGunModelLoadType] = 7;
  call = ctx;
  call.r3.u64 = id;
  call.r4.u64 = 17;  // FILELOADMETHOD_EXTRAMEM, as the gun loader
  call.r5.u64 = ptr;
  call.r6.u64 = load_size;
  sub_82197290(call, base);  // file_load_to_addr
  const uint32_t modeldef = call.r3.u32;
  call = ctx;
  call.r3.u64 = id;
  sub_82197398(call, base);  // file_get_loaded_size
  const uint32_t loaded = call.r3.u32;
  if (!modeldef || !loaded) return 0;

  // As the gun loader: room for textures after the file, then the texture pool.
  const uint32_t alloc_size = loaded + 0xE00;
  StoreU32(base, kGunModelFileInfo + id * 8 + 4, alloc_size);
  const uint32_t end = (ptr + alloc_size + 15) & ~15u;
  remaining -= end - ptr;
  call = ctx;
  call.r3.u64 = pool;
  call.r4.u64 = end;
  call.r5.u64 = remaining;
  sub_8224B5D8(call, base);  // tex_init_pool
  call = ctx;
  call.r3.u64 = modeldef;
  sub_82196928(call, base);  // model_promote_type_to_pointer
  call = ctx;
  call.r3.u64 = modeldef;
  sub_82194F20(call, base);  // an XBLA step of the gun loader (unknown; done as the game does)
  call = ctx;
  call.r3.u64 = modeldef;
  call.r4.u64 = kGunModelSegment;
  call.r5.u64 = modeldef;
  sub_82195498(call, base);  // model_promote_offsets_to_pointers
  call = ctx;
  sub_82194F10(call, base);  // an XBLA step of the gun loader (unknown)
  const uint32_t saved_loaded = pd::weapons::LoadU32(base, kGunModelFileInfo + id * 8);
  const uint32_t saved_alloc = pd::weapons::LoadU32(base, kGunModelFileInfo + id * 8 + 4);

  // The textures (the loader's state 2): tex_load for each config with a real number.
  const int numtex = int16_t(pd::weapons::LoadU16(base, modeldef + 0x16));
  const uint32_t texconfigs = pd::weapons::LoadU32(base, modeldef + 0x18);
  int textures = 0;
  for (int i = 0; i < numtex && texconfigs; ++i) {
    const uint32_t config = texconfigs + uint32_t(i) * 12;
    if (int32_t(pd::weapons::LoadU32(base, config)) >= kGunModelNumTextures) continue;
    call = ctx;
    call.r3.u64 = config;
    call.r4.u64 = pool;
    call.r5.u64 = 1;
    sub_8224B5F0(call, base);  // tex_load
    base[config + 11] = 1;
    ++textures;
  }
  StoreU32(base, kGunModelFileInfo + id * 8, saved_loaded);  // the loader puts the file info back
  StoreU32(base, kGunModelFileInfo + id * 8 + 4, saved_alloc);

  // The display lists (the loader's state 3), then the model's working data.
  call = ctx;
  call.r3.u64 = modeldef;
  call.r4.u64 = id;
  call.r5.u64 = kGunModelSegment;
  call.r6.u64 = modeldef;
  call.r7.u64 = pool;
  call.r8.u64 = 0;
  sub_821967A0(call, base);  // modeldef0f1a7560: links the display lists to the textures
  StoreU32(base, kGunModelFileInfo + id * 8, saved_loaded);
  StoreU32(base, kGunModelFileInfo + id * 8 + 4, saved_alloc);
  call = ctx;
  call.r3.u64 = modeldef;
  sub_821957D0(call, base);  // model_allocate_rw_data
  call = ctx;
  call.r3.u64 = pool;
  sub_823544C8(call, base);  // tex_get_pool_left_pos (the loader calls it; the result is not needed)
  REXLOG_INFO("[custom] gun model file 0x{:03X} loaded ({} bytes, {} textures)", id, loaded, textures);
  return modeldef;
}
// ---- end of the custom weapons' gun model loader ---------------------------------------

// ---- Test tools: the local player (pd_player.h) ------------------------------------------
//
// Requests from the test pipe (give, equip, look, face, tp, pos) are carried out
// here, on the game thread, at the start of the local player's input step, when
// g_Vars.currentplayer is that player (PdHookTestPlayerTick).

namespace {

// Writes the view angles and the fields the game keeps beside them, as the mouse
// look does (PerfectDarkApp::ApplyLookAngles): cos/sin, verta, verta360, the turn
// speeds (zeroed, or the game keeps turning), the previous frame's theta, and the
// bond2 theta vector the walking code uses.
void TestSetLook(uint8_t* base, uint32_t player, float theta, float verta) {
  namespace pl = pd::player;
  const double rad = double(theta) * 3.14159265358979323846 / 180.0;
  pl::StoreF32(base, player + pl::kTheta, theta);
  pl::StoreF32(base, player + pl::kTheta + 4, 0.0f);  // speedtheta
  pl::StoreF32(base, player + pl::kTheta + 8, float(std::cos(rad)));
  pl::StoreF32(base, player + pl::kTheta + 12, float(std::sin(rad)));
  pl::StoreF32(base, player + pl::kTheta + 16, verta);
  pl::StoreF32(base, player + pl::kTheta + 20, verta < 0.0f ? verta + 360.0f : verta);
  pl::StoreF32(base, player + pl::kTheta + 24, 0.0f);  // speedverta
  pl::StoreF32(base, player + pl::kPrevTheta, theta);
  pl::StoreVec(base, player + pl::kBondTheta, {float(-std::sin(rad)), 0.0f, float(std::cos(rad))});
}

// Moves the local player to `want` (its y only needs to be above the floor there),
// the way the respawn places a player (bondstartnewlife sub_820FDA68,
// 0x820FDBCC-0x820FDDD8): find the room and the ground, stand on it at eye
// height, reset bond2 and the camera's copy, and put the prop in its new room.
// False if no room there has a floor below `want`.
//
// `first_room` (if > 0) is tried first: room bounding boxes overlap, so a point
// can be "in" several rooms and only some have the floor under it (test T0: the
// first room found had no ground there and the player fell out of the level).
bool TestTeleport(PPCContext& ctx, uint8_t* base, uint32_t player, pd::player::Vec want, float theta,
                  int16_t first_room) {
  namespace pl = pd::player;
  const uint32_t prop = pl::PropOf(base, player);
  if (!prop) return false;

  // Scratch in guest memory below the game's stack (as XbowAlignScorch does).
  const uint32_t pos = ctx.r1.u32 - 0x400;  // 3 floats
  const uint32_t inrooms = pos + 16;        // 21 s16
  const uint32_t aboverooms = pos + 64;     // 21 s16
  const uint32_t best = pos + 112;          // s16
  const uint32_t rooms = pos + 120;         // {room, -1}
  pl::StoreVec(base, pos, want);

  PPCContext call = ctx;
  call.r1.u64 = pos - 0x200;
  call.r3.u64 = pos;
  call.r4.u64 = inrooms;
  call.r5.u64 = aboverooms;
  call.r6.u64 = 20;
  call.r7.u64 = best;
  sub_820D56C8(call, base);  // bg_find_rooms_by_pos(pos, inrooms, aboverooms, 20, &bestroom)

  // The rooms to try, in order: first_room, the rooms the point is in, the rooms
  // it is above, the closest room.
  std::vector<int16_t> candidates;
  if (first_room > 0) candidates.push_back(first_room);
  for (const uint32_t list : {inrooms, aboverooms}) {
    for (int i = 0; i < 20; ++i) {
      const int16_t r = pl::LoadS16(base, list + uint32_t(i) * 2);
      if (r < 0) break;
      candidates.push_back(r);
    }
  }
  candidates.push_back(pl::LoadS16(base, best));

  // cd_find_ground_at_cyl_ctfril(pos, radius 30, rooms, &floorcol, &floortype,
  // &floorflags, &floorroom, NULL, NULL): the radius is a float in f1 and takes
  // r4's place; the last argument is on the stack at 84(r1), as the respawn passes it.
  // With no floor below the point it returns about -4.29e9 (N64: -4294967296).
  int16_t room = -1;
  float ground = 0.0f;
  for (const int16_t r : candidates) {
    if (r <= 0) continue;
    pl::StoreS16(base, rooms, r);
    pl::StoreS16(base, rooms + 2, -1);
    call = ctx;
    call.r1.u64 = pos - 0x200;
    pl::StoreU32(base, call.r1.u32 + 84, 0);
    call.r3.u64 = pos;
    call.f1.f64 = 30.0;
    call.r5.u64 = rooms;
    call.r6.u64 = player + pl::kFloorCol;
    call.r7.u64 = player + pl::kFloorType;
    call.r8.u64 = player + pl::kFloorFlags;
    call.r9.u64 = player + pl::kFloorRoom;
    call.r10.u64 = 0;
    sub_820E7AF8(call, base);
    const float g = float(call.f1.f64);
    if (g > -100000.0f && g <= want.y + 1.0f) {  // a real floor, below the point
      room = r;
      ground = g;
      break;
    }
  }
  if (room <= 0) return false;
  pl::StoreS16(base, rooms, room);
  pl::StoreS16(base, rooms + 2, -1);

  pl::Vec at = want;
  at.y = ground + pl::LoadF32(base, player + pl::kEyeHeight);
  pl::StoreF32(base, player + pl::kManGround, ground);
  pl::StoreF32(base, player + pl::kGround, ground);
  TestSetLook(base, player, theta, 0.0f);

  // bond2 as player_reset_bond leaves it: radius 30, look (1,0,0), up (0,1,0).
  pl::StoreF32(base, player + pl::kBondRadius, 30.0f);
  pl::StoreVec(base, player + pl::kBondPos, at);
  pl::StoreVec(base, player + pl::kBondLook, {1.0f, 0.0f, 0.0f});
  pl::StoreVec(base, player + pl::kBondUp, {0.0f, 1.0f, 0.0f});

  pl::StoreVec(base, prop + pl::kPropPos, at);
  pl::StoreVec(base, player + pl::kPrevPos, at);
  pl::StoreVec(base, player + pl::kShotSpeed, {});  // no knockback carried over

  call = ctx;
  call.r1.u64 = pos - 0x200;
  call.r3.u64 = prop;
  sub_821FA2F8(call, base);  // prop_deregister_rooms
  pl::StoreS16(base, prop + pl::kPropRooms, room);
  pl::StoreS16(base, prop + pl::kPropRooms + 2, -1);

  // The camera's copy (player_set_cam_properties_in_bounds, inlined in the respawn).
  pl::StoreVec(base, player + pl::kMemCamPos, at);
  pl::StoreU32(base, player + pl::kMemCamRoom, uint32_t(int32_t(room)));
  pl::StoreVec(base, player + pl::kCamPos, at);
  pl::StoreVec(base, player + pl::kCamLook, pl::LoadVec(base, player + pl::kBondLook));
  pl::StoreVec(base, player + pl::kCamUp, pl::LoadVec(base, player + pl::kBondUp));
  pl::StoreU32(base, player + pl::kCamRoom, uint32_t(int32_t(room)));
  return true;
}

}  // namespace

void PdTestPlayerTick(PPCContext& ctx, uint8_t* base) {
  namespace pl = pd::player;
  const uint32_t player = pl::Player(base, 0);
  if (!player || pl::LoadU32(base, 0x82648BF8 + 1188) != player) return;  // not the local player's turn

  if (pl::g_track_frames > 0) {
    const uint32_t frame = pl::LoadU32(base, pl::kLevelFrames);
    static uint32_t last = 0;
    if (frame < last || frame - last >= uint32_t(pl::g_track_frames)) {
      last = frame;
      REXLOG_INFO("[test] track frame {}: {}", frame, pl::Describe(base, 0));
    }
  }

  pl::Request r;
  if (!pl::Take(&r)) return;
  if (!r.who.empty()) {
    r.number = pl::PlayerByName(base, r.who);
    if (r.number < 0) REXLOG_WARN("[test] no player named {}", r.who);
  }
  switch (r.action) {
    case pl::Action::kGive: {
      PPCContext call = ctx;
      call.r3.u64 = uint32_t(r.number);
      sub_82107108(call, base);  // inv_give_single_weapon
      const uint32_t given = call.r3.u32;
      call = ctx;
      call.r3.u64 = 1;
      sub_8217D6D8(call, base);  // bgun_give_max_ammo(force): every ammo type full
      REXLOG_INFO("[test] give {}: inv_give_single_weapon returned {}", r.number, given);
      break;
    }
    case pl::Action::kEquip: {
      PPCContext call = ctx;
      call.r3.u64 = uint32_t(r.number);
      sub_82172258(call, base);  // bgun_equip_weapon: switches to it as a weapon change does
      REXLOG_INFO("[test] equip {}", r.number);
      break;
    }
    case pl::Action::kLook:
      TestSetLook(base, player, r.a, r.b);
      REXLOG_INFO("[test] look {:.1f} {:.1f}", r.a, r.b);
      break;
    case pl::Action::kFace: {
      const uint32_t other = pl::PropOf(base, pl::Player(base, r.number));
      if (!other) {
        REXLOG_WARN("[test] face {}: no such player", r.number);
        break;
      }
      pl::Vec target = pl::LoadVec(base, other + pl::kPropPos);
      target.y -= 40.0f;  // the prop is at eye height: aim at the chest
      float theta, verta;
      pl::AnglesTowards(pl::LoadVec(base, pl::PropOf(base, player) + pl::kPropPos), target, &theta, &verta);
      if (r.c != 0.0f) verta = r.b;  // face <player> <verta>: that player's direction, this vertical angle
      TestSetLook(base, player, theta, verta);
      REXLOG_INFO("[test] face player {}: theta {:.1f} verta {:.1f}", r.number, theta, verta);
      break;
    }
    case pl::Action::kTeleport:
    case pl::Action::kTeleportNear: {
      pl::Vec want{r.a, r.b, r.c};
      float theta = r.d;
      int16_t first_room = -1;
      if (r.action == pl::Action::kTeleportNear) {
        // `distance` in front of the other player, facing it.
        const uint32_t other_player = pl::Player(base, r.number);
        const uint32_t other = pl::PropOf(base, other_player);
        if (!other) {
          REXLOG_WARN("[test] tp near {}: no such player", r.number);
          break;
        }
        const pl::Vec at = pl::LoadVec(base, other + pl::kPropPos);
        const float facing = pl::LoadF32(base, other_player + pl::kTheta);
        // How far the other player can see along `theta` (up to r.a): the
        // collision test (cd_test_los_oobok_findclosest_autoflags, 0 = hit) and
        // where it hit (cd_get_obstacle_pos), as XbowAlignScorch uses them; 60
        // units are kept from the wall.
        auto clear = [&](float theta_deg) {
          const double rad = double(theta_deg) * 3.14159265358979323846 / 180.0;
          const float fx = float(-std::sin(rad)), fz = float(std::cos(rad));
          const uint32_t from = ctx.r1.u32 - 0x400, to = from + 16, hit = from + 32;
          pl::StoreVec(base, from, at);
          pl::StoreVec(base, to, {at.x + fx * r.a, at.y, at.z + fz * r.a});
          PPCContext call = ctx;
          call.r1.u64 = from - 0x200;
          call.r3.u64 = from;
          call.r4.u64 = other + pl::kPropRooms;
          call.r5.u64 = to;
          call.r6.u64 = 0x20 | 0x02 | 0x10;  // background, doors, path blockers
          sub_820E4E88(call, base);
          if (call.r3.s32 != 0) return r.a;
          call.r3.u64 = hit;
          call.r4.u64 = 0;
          call.r5.u64 = 0;
          sub_820DDDC0(call, base);
          const pl::Vec h = pl::LoadVec(base, hit);
          return std::max(0.0f, std::sqrt((h.x - at.x) * (h.x - at.x) + (h.z - at.z) * (h.z - at.z)) - 60.0f);
        };
        // In front of the other player, else behind, left or right: the first
        // direction clear for the whole distance, otherwise the clearest one.
        float best_theta = facing, best_distance = -1.0f;
        for (const float turn : {0.0f, 180.0f, 90.0f, 270.0f}) {
          const float t = std::fmod(facing + turn, 360.0f);
          const float d = clear(t);
          if (d > best_distance) {
            best_distance = d;
            best_theta = t;
          }
          if (d >= r.a) break;
        }
        const double rad = double(best_theta) * 3.14159265358979323846 / 180.0;
        want = {at.x + float(-std::sin(rad)) * best_distance, at.y, at.z + float(std::cos(rad)) * best_distance};
        REXLOG_INFO("[test] tp near player {}: {:.0f} units at theta {:.0f} (it faces {:.0f})", r.number,
                    best_distance, best_theta, facing);
        float verta;
        pl::AnglesTowards(want, at, &theta, &verta);
        first_room = pl::LoadS16(base, other + pl::kPropRooms);  // most likely the same room
      }
      const bool moved = TestTeleport(ctx, base, player, want, theta, first_room);
      REXLOG_INFO("[test] tp {:.1f} {:.1f} {:.1f}: {} - {}", want.x, want.y, want.z, moved ? "moved" : "NOT MOVED (no room there)",
                  pl::Describe(base, 0));
      break;
    }
    case pl::Action::kPos:
      for (int n = 0; n < 8; ++n) {
        if (pl::Player(base, n)) REXLOG_INFO("[test] pos {}: {}", r.tag, pl::Describe(base, n));
      }
      break;
    case pl::Action::kNone:
      break;
  }
  pl::Finish();
}
// ---- end of the test tools ---------------------------------------------------------------

// ---- TEMP_RESEARCH: custom weapons research (logging only) -------------------------------
//
// What for: learning what each PC runs when a player is hit online (chr_damage
// sub_82128200), for the Push Gun, Slow Gun and Ricochet Laser research
// (docs/research/custom-weapons.md). Off unless pd_probe_hits is on.
REXCVAR_DEFINE_BOOL(pd_probe_hits, false, "PerfectDark", "TEMP_RESEARCH: log every chr_damage call");

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
void PdProbeChrDamage(uint8_t* base, uint32_t chr, double damage, uint32_t vector, uint32_t gset, uint32_t aprop,
                      uint32_t hitpart, uint32_t shield) {
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

void PdNoteGameFrame(uint8_t* base) {
  PdCustomInstall(base);
  PdXbowNetFrame();
  PdMpMusicFrame(base);
  PdWeaponsFrame();
  PerfectDarkApp::NoteGameStage(
      _byteswap_ulong(*reinterpret_cast<const uint32_t*>(base + 0x826492E0)),   // stage
      _byteswap_ulong(*reinterpret_cast<const uint32_t*>(base + 0x82648BF8 + 0x0C)));  // level frames
  if (!rex::diag::TimingLogOn()) return;
  constexpr uint32_t kGVars = 0x82648BF8;
  static const int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  static int64_t window_start = 0, last_frame = 0;
  static uint64_t frames = 0, steps = 0, doubles = 0, long_frames = 0, network_frames = 0;
  static uint32_t most = 0;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const uint32_t step = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(base + kGVars));
  const bool network_clock = base[kGVars + 0x731] != 0;
  if (last_frame && now.QuadPart - last_frame < freq) {
    const double ms = double(now.QuadPart - last_frame) * 1000.0 / double(freq);
    ++frames;
    steps += step;
    most = std::max(most, step);
    if (step >= 2) ++(ms < 20.0 ? doubles : long_frames);
    if (network_clock) ++network_frames;
  }
  last_frame = now.QuadPart;
  // The game thread's own split over the window (issue #1): how much of each
  // frame it spent in the GPU-completion poll, and how much it was on a CPU.
  static uint64_t window_tsc = 0, window_cycles = 0;
  if (!window_start) {
    window_start = now.QuadPart;
    window_tsc = rex::diag::TscNow();
    window_cycles = rex::diag::ThreadCycles();
    t_gpu_poll_ticks = 0;
    t_gpu_poll_cycles = 0;
    t_gpu_poll_waits = 0;
  }
  if (now.QuadPart - window_start >= 10 * freq) {
    const double seconds = double(now.QuadPart - window_start) / double(freq);
    REXLOG_INFO("[timing] game steps: {} frames, game time {:.2f}% of real time, {} double "
                "steps (2+ steps in a frame under 20 ms), {} long frames with 2+ steps, most {} "
                "in one frame{}",
                frames, double(steps) / (seconds * 60.0) * 100.0, doubles, long_frames, most,
                network_frames ? fmt::format(", network clock for {} frames", network_frames)
                               : std::string());
    const uint64_t tsc = rex::diag::TscNow();
    const uint64_t cycles = rex::diag::ThreadCycles();
    if (frames && tsc > window_tsc) {
      const double wall_ms = seconds * 1000.0;
      const double tsc_per_ms = double(tsc - window_tsc) / wall_ms;
      const double n = double(frames);
      const double poll_ms = double(t_gpu_poll_ticks) * 1000.0 / double(freq);
      const double cpu_ms = double(cycles - window_cycles) / tsc_per_ms;
      const double poll_cpu_ms = double(t_gpu_poll_cycles) / tsc_per_ms;
      REXLOG_INFO("[timing] game thread per frame: {:.2f} ms = waiting for the GPU {:.2f} ms (on a "
                  "CPU {:.2f} ms, {:.1f} waits a frame), the rest {:.2f} ms (on a CPU {:.2f} ms)",
                  wall_ms / n, poll_ms / n, poll_cpu_ms / n, double(t_gpu_poll_waits) / n,
                  (wall_ms - poll_ms) / n, (cpu_ms - poll_cpu_ms) / n);
    }
    window_tsc = tsc;
    window_cycles = cycles;
    t_gpu_poll_ticks = 0;
    t_gpu_poll_cycles = 0;
    t_gpu_poll_waits = 0;
    window_start = now.QuadPart;
    frames = steps = doubles = long_frames = network_frames = 0;
    most = 0;
  }
}

bool PdIdleSleep() {
  if (!REXCVAR_GET(pd_sleep_in_waits)) return false;
  SleepHalfMillisecond();
  return true;
}

// A network match's frame wait (sub_8214D6C8, the loop at 0x8214D8A8 that reads the
// match clock until the next frame is due), each time it is about to go round again.
// Under Xenos the game thread has spent the frame waiting for the GPU before it gets
// here, so the loop seldom turns; with gpu_native nothing else waits and it spun
// through most of every frame (a native host: 16.6 ms on a CPU per 16.7 ms frame,
// 2026-11-03). Yield for the first 0.5 ms of a wait, then sleep in 0.5 ms steps.
void PdNetFrameWaitBackoff() {
  if (!REXCVAR_GET(pd_sleep_in_waits)) return;
  thread_local int64_t wait_start = 0;
  thread_local int64_t last_poll = 0;
  static const int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (now.QuadPart - last_poll > freq / 500) wait_start = now.QuadPart;  // a new wait
  if (now.QuadPart - wait_start < freq / 2000) {
    SwitchToThread();
  } else {
    SleepHalfMillisecond();
  }
  QueryPerformanceCounter(&now);
  last_poll = now.QuadPart;
}

// Menu rows the game skips (greyed out or hidden): called from the dialog tick in
// generated/default (perfect_dark_recomp.8.cpp sub_821C7198). See
// PerfectDarkApp::MenuItemSkipped.
bool PdMenuSkipScanWanted(uint32_t dialog) {
  return PerfectDarkApp::MenuSkipScanWanted(dialog);
}

void PdMenuSkipNote(uint32_t item, uint32_t skipped) {
  PerfectDarkApp::MenuSkipNote(item, skipped != 0);
}

// Mouse look in networked matches: called from the game's input code in
// generated/default (perfect_dark_recomp.15.cpp sub_82294F00, .13.cpp
// sub_8225D9F8, .6.cpp sub_82187900). See PerfectDarkApp::NetLookSend.
void PdNetLookSend(uint32_t input, uint32_t frame) {
  PerfectDarkApp::NetLookSend(input, frame);
}

void PdNetLookReceive(uint32_t record, uint32_t input) {
  PerfectDarkApp::NetLookReceive(record, input);
}

void PdNetLookApply(uint32_t pad) {
  PerfectDarkApp::NetLookApply(pad);
}

uint32_t PdNetLocalInput(uint32_t input) {
  return PerfectDarkApp::NetLocalInput(input);
}

REX_DEFINE_APP(perfect_dark, PerfectDarkApp::Create)
