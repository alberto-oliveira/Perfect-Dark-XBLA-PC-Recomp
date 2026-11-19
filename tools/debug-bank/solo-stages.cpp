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
