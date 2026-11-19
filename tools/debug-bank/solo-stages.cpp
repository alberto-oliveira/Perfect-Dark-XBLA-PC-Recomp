// Debug code of the solo stages mod (branch port-solo-stages): the solo mission
// stages as online Combat Simulator arenas. Not built; kept only so the code can
// be recovered.
//
// ============================================================================
// TEMP_STAGES: network session property log
// ============================================================================
//
// What for: before solo stages could be played online we did not know whether
// the host could send a solo stage number to the joined PCs. The match settings
// travel as session properties; their setter (sub_822F0850, r3 = the property,
// r4 = the new value) refuses a value outside the property's range, a pair of
// bytes at +88/+89 of the descriptor at property+24. This logged every change of
// every property with its range. Test 1 (logs host 077, client 037) showed the
// stage property going 50 -> 48 (Defection) with range 0-255, so any stage number
// is accepted and the game's own property carries a solo stage.
//
// Why removed: during a match it logged the live per-player properties tens of
// thousands of times (the host's log rotated eight times in test 2).
//
// Hook / toml entry: PdHookNetPropertyLog at 0x822F0850 (registers r3, r4).
//
// Removed in: the commit that added this file ("Move the session property log
// to the debug bank").

// ---- perfect_dark_config.toml -----------------------------------------------
//
// # TEMP_STAGES: the network session's property setter (sub_822F0850, r3 property,
// # r4 value) logs each change and the property's allowed range.
// [[midasm_hook]]
// address = 0x822F0850
// name = "PdHookNetPropertyLog"
// registers = ["r3", "r4"]

// ---- src/perfect_dark_hooks.cpp ---------------------------------------------

void PdNetPropertyLog(const uint8_t* base, uint32_t property, uint32_t value);

// TEMP_STAGES: the network session's property setter (sub_822F0850), at its start.
void PdHookNetPropertyLog(PPCRegister& r3, PPCRegister& r4) {
  PdNetPropertyLog(HookBase(), r3.u32, r4.u32);
}

// ---- src/main.cpp -----------------------------------------------------------

// TEMP_STAGES: the network session's property setter (sub_822F0850: r3 the
// property, r4 the new value). It refuses a value outside the property's range
// (its descriptor's +88/+89); this logs every change, to learn whether the stage
// property takes the solo stage numbers.
void PdNetPropertyLog(const uint8_t* base, uint32_t property, uint32_t value) {
  static std::map<uint32_t, uint32_t> last;  // property -> value last logged
  const uint32_t desc = pd::stages::U32(base + property + 24);
  const uint32_t current = base[property + 12];
  auto it = last.find(property);
  if (it != last.end() && it->second == (value & 0xFF)) return;
  last[property] = value & 0xFF;
  REXLOG_INFO("[stages] TEMP property 0x{:08X}: {} -> {} (range {}-{})", property, current, value & 0xFF,
              desc ? base[desc + 88] : -1, desc ? base[desc + 89] : -1);
}

// ============================================================================
// TEMP_STAGES: pd_solo_force_stage (play a solo stage before the Arena menu offered it)
// ============================================================================
//
// What for: steps 1-3 tested Defection before the Arena menu listed the solo
// stages. With pd_solo_force_stage = "defection" in the host's toml, the host (or a
// local game) kept g_MpSetup+17 (0x826418A9, the arena's stage) set to that stage
// outside matches, so Finished Setup sent it to the joined PCs.
//
// Why removed: step 5 added the solo stages to Game Setup > Arena.
//
// Hook / toml entry: none (called each frame from PdNoteGameFrame, after
// PdWeaponsFrame: `PdSoloStagesFrame(base);`).
//
// Removed in: the commit that added this section ("Offer the solo stages in the
// Arena menu").

// ---- src/pd_stages.h ---------------------------------------------------------

// Short names for the pd_solo_force_stage setting (the game's own names have spaces
// and capitals). Stage numbers as in g_SoloStages.
struct StageKey {
  const char* key;
  uint8_t stagenum;
};
inline constexpr StageKey kStageKeys[] = {
    {"defection", 0x30},  {"investigation", 0x33}, {"extraction", 0x22}, {"villa", 0x2C},
    {"chicago", 0x1D},    {"g5", 0x1E},            {"infiltration", 0x2F}, {"rescue", 0x35},
    {"escape", 0x19},     {"airbase", 0x27},       {"airforceone", 0x31}, {"crashsite", 0x1C},
    {"pelagic", 0x21},    {"deepsea", 0x38},       {"defense", 0x2D},    {"attackship", 0x34},
    {"skedarruins", 0x2A}, {"mbr", 0x37},          {"maiansos", 0x09},   {"war", 0x16},
    {"duel", 0x4F},
};

// A stage number from a setting: one of the names above or a number ("48", "0x30").
// 0 if it is neither.
inline uint8_t ParseStage(std::string text) {
  text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }),
             text.end());
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (text.empty()) return 0;
  for (const StageKey& k : kStageKeys) {
    if (text == k.key) return k.stagenum;
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 0);
  return (end && *end == 0 && value > 0 && value < 256) ? uint8_t(value) : 0;
}


// ---- src/main.cpp -----------------------------------------------------------

// TEMP_STAGES: until the Arena menu offers the solo stages, this makes the host's
// (or a local) match use one: a name from pd_stages.h (kStageKeys) or a stage number.
REXCVAR_DEFINE_STRING(pd_solo_force_stage, "", "PerfectDark",
                      "Testing: every match you host is played on this solo stage (e.g. "
                      "\"defection\"); empty = the arena chosen in the menu");

// Each frame (TEMP_STAGES): with pd_solo_force_stage, a PC that is not a joined
// one keeps its arena set to that stage outside matches, so the host sends it to
// the joined PCs with the rest of the match settings.
void PdSoloStagesFrame(uint8_t* base) {
  namespace netplay = rex::kernel::xam::netplay;
  namespace stages = pd::stages;
  static std::string last_text;
  static uint8_t forced = 0;
  const std::string text = REXCVAR_GET(pd_solo_force_stage);
  if (text != last_text) {
    last_text = text;
    forced = stages::ParseStage(text);
    if (!text.empty()) {
      if (forced) REXLOG_INFO("[stages] TEMP forcing stage 0x{:02X} ({})", forced, text);
      else REXLOG_WARN("[stages] pd_solo_force_stage \"{}\" is not a stage name or number", text);
    }
  }
  if (!forced || netplay::IsJoinedSession()) return;
  if (stages::U32(base + stages::kMatchRunning) != 0) return;
  base[stages::kMpSetupStage] = forced;
}
