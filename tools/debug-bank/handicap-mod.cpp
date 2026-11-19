// Debug code of the handicap mod (branch handicap-mod): online handicaps and the
// N64 poison. Not built; kept only so the code can be recovered.
//
// ============================================================================
// TEMP_POISON
// ============================================================================
//
// What for: following the N64 poison online. A thrown knife's poison sets a
// counter on the victim's chr; it ticks 1.3 damage every 720 of the counter. The
// logs showed when the counter was set, what bondstartnewlife did to it on
// respawn, and which PC applied or skipped each tick. They found that the
// thrower's PC ticked the poison too, which made the number of extra deaths
// inconsistent (1 or 2). Each line carries the chr address and g_Vars' frame
// counter, so the host's and client's logs can be compared.
//
// Hooks / toml entries:
//   PdHookPoisonSetLog at 0x82109448 (chr_set_poisoned's MP store
//   `sth r10,856(r31)`), its own toml entry (below).
//   The other two log lines sat inside the mod's real hooks, which stay:
//   PdHookKeepPoisonOnRespawn (0x820FDB9C) and PdHookPoisonTickLocalOnly (0x8210960C).
//
// Removed in: the cleanup commit that added this file
// ("Move the handicap mod debug code to the debug bank").

// ---- perfect_dark_config.toml -----------------------------------------------
//
// # TEMP_POISON: log poison set (chr_set_poisoned's MP store).
// [[midasm_hook]]
// address = 0x82109448
// name = "PdHookPoisonSetLog"
// registers = ["r10", "r31"]

// ---- src/perfect_dark_hooks.cpp ---------------------------------------------

// TEMP_POISON: the victim's bondhealth, or -99 if it is not a player.
// (PoisonVictimPlayer stays in perfect_dark_hooks.cpp: PdHookPoisonTickLocalOnly uses it.)
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

// In PdHookKeepPoisonOnRespawn, after the cap and before `chr[767] = 0;`:
REXLOG_INFO("[poison] respawn chr 0x{:08X} frame {}: counter {} -> {}", r11.u32,  // TEMP_POISON
            LoadU32(base, 0x82648BF8 + 0x0C), poison, poison > cap ? cap : poison);

// In PdHookPoisonTickLocalOnly, after `const bool skip = ...;` and before `return skip;`:
const int16_t counter = int16_t((base[r31.u32 + 856] << 8) | base[r31.u32 + 857]);
REXLOG_INFO("[poison] tick chr 0x{:08X} frame {}: counter {}, health before {:.3f}, {}",  // TEMP_POISON
            r31.u32, LoadU32(base, 0x82648BF8 + 0x0C), counter, PoisonVictimHealth(base, r31.u32),
            skip ? "skipped (remote player)" : "applied");

// ============================================================================
// TEMP_NET_HANDICAP
// ============================================================================
//
// What for: finding how online players map to the player configs (0x82641950,
// 216 bytes each: name at +0, pad at +109 with -1 = remote, handicap at +0xCD),
// whether the host's handicap reaches the clients, where gamertags are stored,
// and what resets the handicap to 128.
//
// Hooks / toml entries:
//   PdHookHandicapResetProfile at 0x821CFE68 (r27 = slot * 216)
//   PdHookHandicapResetDefaults at 0x821D0E7C (r30 = slot * 216)
// The two dump functions ran from the F3 dump in src/perfect_dark_app.h.
//
// Removed in: 4b340ce ("Add pd_handicaps setting and remove handicap debug code";
// 2b9af68 before the rebase onto dev). Banked later, in the poison cleanup commit.

// ---- perfect_dark_config.toml -----------------------------------------------
//
// # Debug Hooks for Player Settings Reset
// [[midasm_hook]]
// address = 0x821CFE68
// name = "PdHookHandicapResetProfile"
// registers = ["r27"]
//
// [[midasm_hook]]
// address = 0x821D0E7C
// name = "PdHookHandicapResetDefaults"
// registers = ["r30"]

// ---- src/perfect_dark_hooks.cpp ---------------------------------------------

void PdHookHandicapResetProfile(PPCRegister& r27)  { REXLOG_INFO("[handicap] profile reset, slot {}", r27.u32 / 216); }
void PdHookHandicapResetDefaults(PPCRegister& r30) { REXLOG_INFO("[handicap] defaults reset, slot {}", r30.u32 / 216); }

// ---- src/perfect_dark_app.h (PerfectDarkApp static members) -----------------

// TEMP_NET_HANDICAP: what is the config index of each online player on each machine
// TEMP_NET_HANDICAP: if the host's handicap value reaches the client
static void DebugDumpPlayerSlots() {
  PerfectDarkApp* app = current_instance_;
  if (!app || !app->runtime() || !app->runtime()->memory()) return;
  auto* mem = app->runtime()->memory();
  const uint16_t mask = uint16_t(ReadGuestU32(mem, 0x82641898 + 20) & 0xFFFF);  // +22
  REXLOG_INFO("[handicap] slot mask 0x{:04X}", mask);
  for (uint32_t i = 0; i < 8; ++i) {
    const uint32_t cfg = 0x82641950 + i * 216;
    std::string name;
    for (uint32_t k = 0; k < 15; ++k) {
      const uint8_t c = ReadGuestU8(mem, cfg + k);
      if (!c || c == '\n') break;
      name += char(c);
    }
    REXLOG_INFO("[handicap] cfg {} id {} handicap {} name '{}'", i,
                int(int8_t(ReadGuestU8(mem, cfg + 109))), ReadGuestU8(mem, cfg + 0xCD), name);
  }

  const uint32_t g_vars = 0x82648BF8;
  for (uint32_t i = 0; i < 8; ++i) {
    const uint32_t player = ReadGuestU32(mem, g_vars + 100 + i * 4);
    if (!player) continue;
    const uint32_t cfg_index = ReadGuestU32(mem, g_vars + 132 + i * 128 + 112);
    REXLOG_INFO("[handicap] player {} at 0x{:08X} uses cfg {}", i, player, cfg_index);
  }
}

// TEMP_NET_HANDICAP: every place a player name is stored in the game's data,
// as ASCII and as UTF-16 (the Xbox stores gamertags both ways).
static void DebugFindName(const std::string& name) {
  PerfectDarkApp* app = current_instance_;
  if (!app || !app->runtime() || !app->runtime()->memory()) return;
  auto* mem = app->runtime()->memory();
  constexpr uint32_t kStart = 0x82000000, kSize = 0x6D8000;  // the game's image + data
  const auto* base = mem->TranslateVirtual<const uint8_t*>(kStart);
  std::string wide;                                          // UTF-16 big-endian
  for (char c : name) { wide += '\0'; wide += c; }
  const std::string* patterns[] = {&name, &wide};
  for (const std::string* pat : patterns) {
    for (uint32_t i = 0; i + pat->size() <= kSize; ++i) {
      if (std::memcmp(base + i, pat->data(), pat->size()) == 0)
        REXLOG_INFO("[handicap] '{}' {} at 0x{:08X}", name,
                    pat == &name ? "ascii" : "utf16", kStart + i);
    }
  }
}

// The calls, in the F3 dump:
PerfectDarkApp::DebugDumpPlayerSlots();
PerfectDarkApp::DebugFindName("Zqhost");
PerfectDarkApp::DebugFindName("Zqclient");
