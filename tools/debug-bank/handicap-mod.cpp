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
// TEMP_HCAP
// ============================================================================
//
// What for: checking that the joined PCs decode exactly the list the host sent
// (part 2: the host's pd_handicaps sent with netplay::SetHostBlob, key "HCAP").
// Each log line writes the whole list sorted by name, so the host's and the
// clients' logs can be compared line by line.
//
// Hooks / toml entries: none. The two log lines sat in
// PerfectDarkApp::TickNetHandicaps (src/perfect_dark_app.h), next to the
// "sent N entries" and "host's list #seq: N entries" lines that stay.
//
// Removed in: the commit that moved TEMP_HCAP here ("Move the handicap list
// debug log to the debug bank"), right after ee44185.

// ---- src/pd_handicap.h ------------------------------------------------------

// TEMP_HCAP: the table as "name=byte, name=byte", to compare the host's and the
// joined PCs' logs.
inline std::string Describe(const std::unordered_map<std::string, uint8_t>& table) {
  std::string text;
  for (const auto& [name, value] : Sorted(table)) {
    if (!text.empty()) text += ", ";
    text += name + "=" + std::to_string(value);
  }
  return text.empty() ? "(empty)" : text;
}

// ---- src/perfect_dark_app.h (TickNetHandicaps) --------------------------------

// Joined PC, after a successful DecodeBlob:
REXLOG_INFO("[handicap] host's list: {}", pd::handicap::Describe(from_host));  // TEMP_HCAP
// Host, after SetHostBlob:
REXLOG_INFO("[handicap] sent list: {}", pd::handicap::Describe(own));  // TEMP_HCAP

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

// ============================================================================
// TEMP_LOBBY (research R1-R3b)
// ============================================================================
//
// What for: finding where the online lobby keeps its players (part 3, the
// lobby Handicaps sliders). The vanilla Handicaps dialog only shows the host's
// slider in an online lobby (slot mask g_MpSetup+22 stays 0x0001: joined
// players reach the player configs only when the match starts).
//   R1  (host log 072): every copy of the test gamertags in all guest memory.
//       Found a list of 0x30-byte records (XUID + gamertag) in join order.
//   R2  (log 073): dump around the list + every pointer into it. The list's
//       address changes between runs (heap); counts found next to it were
//       unrelated neighbours.
//   R3  (log 074): pd_watch write watch on 3 words. Showed the list is cleared
//       and rebuilt; pd_watch could not name the writers (no symbol file).
//   R3b (log 075): every byte of the list watched, with the guest function of
//       each write: sub_82284198 (clears the 8 records), sub_82289AA0 (fills
//       them, every frame), sub_825AAB90 (string copy). Reading sub_82289AA0
//       gave the result kept in src/pd_handicap.h (LobbyPlayers): records at
//       *(0x826646D4) + 1032, 48 bytes, +0 in use, +16 XUID, +24 gamertag.
//
// Hooks / toml entries: none. Home ran DebugFindLobbyPlayers and End ran
// pd_watch::DisarmMemoryWriteWatch, in the pd_dev_hotkeys block of
// PerfectDarkInputTracker::OnDraw (src/perfect_dark_app.h), with the members
// `bool home_was_down_ = false;` and `bool end_was_down_ = false;`.
//
// Removed in: the part 3 commit ("Add online lobby Handicaps sliders"); Home
// then got a short dump of the lobby list instead.

// ---- src/perfect_dark_app.h: pd_watch::VectoredHandler, after
// `st.hits_by_symbol[GuestFunctionAt(rip)]++;` -------------------------------
if (exact) {
  // TEMP_LOBBY: a write to one of the exact addresses is always logged,
  // with the guest function it came from (the build has no symbol file,
  // so SymFromAddr below finds nothing).
  REXLOG_INFO("[menu_watch] *** EXACT *** write to guest 0x{:08X} from {}", guest_addr, GuestFunctionAt(rip));
}

// ---- src/perfect_dark_app.h: OnDraw, pd_dev_hotkeys block ---------------------
// TEMP_LOBBY: Home logs every place the test gamertags are stored.
const bool home_down = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
if (home_down && !home_was_down_) {
  PerfectDarkApp::DebugFindLobbyPlayers();
}
home_was_down_ = home_down;
// TEMP_LOBBY: End stops the write watch Home started and lists the writers.
const bool end_down = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
if (end_down && !end_was_down_) {
  pd_watch::DisarmMemoryWriteWatch();
}
end_was_down_ = end_down;

// ---- src/perfect_dark_app.h: PerfectDarkApp static member --------------------
  // TEMP_LOBBY: where does the lobby keep its players? In an online lobby the
  // game has not put the joined players into its player configs yet, but the
  // lobby screen shows their gamertags, so they are somewhere in memory. This
  // logs every copy of the test gamertags (ASCII, UTF-16 big- and little-endian)
  // in all of the game's memory, with the bytes around each one, plus the slot
  // mask that hides the vanilla Handicaps sliders. Pressing it after each join
  // and leave shows which copies follow the lobby.
  static void DebugFindLobbyPlayers() {
    PerfectDarkApp* app = current_instance_;
    if (!app || !app->runtime() || !app->runtime()->memory()) return;
    auto* mem = app->runtime()->memory();
    static int scan = 0;
    ++scan;
    REXLOG_INFO("[lobby] scan {}: hosting {}, joined {}, slot mask (g_MpSetup+22) 0x{:04X}", scan,
                rex::kernel::xam::netplay::IsHostingSession(), rex::kernel::xam::netplay::IsJoinedSession(),
                ReadGuestU32(mem, kMpSetup + 20) & 0xFFFF);
    uint32_t host_record = 0;  // (R2) Zqhost's gamertag in the list of player records
    // "Zqclient" also finds "Zqclient2": the dump shows the byte after it.
    for (const std::string name : {"Zqhost", "Zqclient"}) {
      std::string be, le;  // UTF-16 big-endian (the Xbox's) and little-endian
      for (char c : name) {
        be += '\0'; be += c;
        le += c; le += '\0';
      }
      const std::pair<const char*, const std::string*> patterns[] = {{"ascii", &name}, {"utf16be", &be}, {"utf16le", &le}};
      for (const auto& [kind, pat] : patterns) {
        int hits = 0;
        ForEachCommittedRegion(mem, [&](uint32_t region, uint32_t size) {
          const uint8_t* p = mem->TranslateVirtual<const uint8_t*>(region);
          if (!p || hits >= 40) return;
          for (uint32_t i = 0; i + pat->size() <= size && hits < 40; ++i) {
            if (p[i] != uint8_t((*pat)[0]) || std::memcmp(p + i, pat->data(), pat->size()) != 0) continue;
            ++hits;
            // 16 bytes before the name and 48 from it, as hex.
            const uint32_t from = i >= 16 ? i - 16 : 0;
            std::string hex;
            for (uint32_t k = from; k < from + 64 && k < size; ++k) {
              hex += fmt::format("{:02X}", p[k]);
              if ((k - from) % 4 == 3) hex += ' ';
            }
            REXLOG_INFO("[lobby] '{}' {} at 0x{:08X} (dump from 0x{:08X}): {}", name, kind, region + i,
                        region + from, hex);
            // The list record: the gamertag right after an XUID (0x0009...).
            if (name == "Zqhost" && pat == &name && i >= 8 && p[i - 8] == 0x00 && p[i - 7] == 0x09) {
              host_record = region + i;
            }
          }
        });
        if (hits >= 40) REXLOG_INFO("[lobby] '{}' {}: stopped at 40 hits", name, kind);
      }
    }
    // TEMP_LOBBY (R2): scan 1 found a list of player records at 0x40064FA8,
    // 0x30 bytes each (an 8-byte XUID, then the gamertag), in join order and
    // packed again when a player leaves, but with stale records left behind.
    // To find its count: dump the memory around it, and every word in the
    // game's memory that points into it (a count usually sits next to the
    // pointer).
    // The list is on the heap, so it is found again from Zqhost's record (in
    // the first test, 0x40064FB0): 0x1B0 bytes before it to 0x250 after.
    if (!host_record) {
      REXLOG_INFO("[lobby] scan {} done (no list record for Zqhost found)", scan);
      return;
    }
    const uint32_t kListFrom = (host_record - 0x1B0) & ~0x1Fu, kListTo = kListFrom + 0x400;
    REXLOG_INFO("[lobby] Zqhost's record name at 0x{:08X}", host_record);
    for (uint32_t a = kListFrom; a < kListTo; a += 32) {
      std::string hex;
      for (uint32_t k = 0; k < 32; k += 4) hex += fmt::format("{:08X} ", ReadGuestU32(mem, a + k));
      REXLOG_INFO("[lobby] list 0x{:08X}: {}", a, hex);
    }
    int pointers = 0;
    ForEachCommittedRegion(mem, [&](uint32_t region, uint32_t size) {
      const uint8_t* p = mem->TranslateVirtual<const uint8_t*>(region);
      if (!p || pointers >= 60) return;
      for (uint32_t i = 0; i + 4 <= size && pointers < 60; i += 4) {
        const uint32_t v = (uint32_t(p[i]) << 24) | (uint32_t(p[i + 1]) << 16) | (uint32_t(p[i + 2]) << 8) | p[i + 3];
        if (v < kListFrom || v >= kListTo) continue;
        ++pointers;
        // The 8 words before the pointer and the 8 from it.
        const uint32_t at = region + i;
        std::string hex;
        for (uint32_t k = at - 32; k < at + 32; k += 4) hex += fmt::format("{:08X} ", ReadGuestU32(mem, k));
        REXLOG_INFO("[lobby] pointer 0x{:08X} -> 0x{:08X} (words from 0x{:08X}): {}", at, v, at - 32, hex);
      }
    });
    REXLOG_INFO("[lobby] scan {} done ({} pointers into the list)", scan, pointers);

    // TEMP_LOBBY (R3): R2 found the player count 0x1E0 bytes after Zqhost's
    // gamertag (1, 2, 3, 2, 1 as players joined and left) and a second copy
    // 0x188 bytes before it. The list is on the heap, so the mod needs the
    // game's own way to reach it: watch which game functions write the count
    // and the second record's gamertag while players join and leave (pd_watch
    // logs the writer of every write to these addresses' memory page). End
    // stops the watch and lists the writers.
    // (R3b) The first watch showed the count is not at a fixed distance from
    // the list (the neighbours differ between runs), and that the list is
    // cleared and rebuilt on every join and leave. So every byte from 0x40
    // before the first record to the 8th record's end is watched, and each
    // write logs the game function it came from.
    std::vector<uint32_t> watched;
    for (uint32_t a = host_record - 0x58; a < host_record + 0x1C0; ++a) watched.push_back(a);
    pd_watch::ArmMemoryWriteWatch(mem, watched);
    REXLOG_INFO("[lobby] watching writes to 0x{:08X}-0x{:08X}", watched.front(), watched.back());
  }

// ============================================================================
// TEMP_HCAP and TEMP_LOBBY (part 3: the online lobby Handicaps sliders)
// ============================================================================
//
// What for: checking the sliders in game (host log 076, client logs 035/036
// and client 2's 015). The lobby dump compared the mod's reading of the lobby
// list with the lobby screen; the HCAP logs showed each slider change, Restore
// Defaults, and the full list the host sent and each client decoded, so the
// logs could be compared line by line.
//
// Hooks / toml entries: none. Home ran DebugFindLobbyPlayers from the
// pd_dev_hotkeys block of PerfectDarkInputTracker::OnDraw, with the member
// `bool home_was_down_ = false;  // TEMP_LOBBY`.
//
// Removed in: "Move the lobby Handicaps sliders' debug code to the debug bank",
// right after 2403b86.

// ---- src/pd_handicap.h ------------------------------------------------------
// (Describe: the same function as the TEMP_HCAP entry above, put back for
// part 3.)

// ---- src/perfect_dark_app.h: TickNetHandicaps ---------------------------------
// Joined PC, after a successful DecodeBlob:
REXLOG_INFO("[handicap] host's list: {}", pd::handicap::Describe(from_host));  // TEMP_HCAP
// Host, after SetHostBlob:
REXLOG_INFO("[handicap] sent list: {}", pd::handicap::Describe(own));  // TEMP_HCAP

// ---- src/perfect_dark_app.h: HandicapSliderRow, op 6, after storing the value --
if (value != before) {
  REXLOG_INFO("[handicap] slider '{}': {} -> {} ({:.0f}%)", name, before, value,  // TEMP_HCAP
              pd::handicap::PercentFromByte(value));
}
// (with `const uint8_t before = HostHandicapFor(key);` before the store)

// ---- src/perfect_dark_app.h: PdHandicapRestoreHandler, after clearing ---------
REXLOG_INFO("[handicap] sliders restored to pd_handicaps");  // TEMP_HCAP

// ---- src/perfect_dark_app.h: OnDraw, pd_dev_hotkeys block ---------------------
// TEMP_LOBBY: Home logs the online lobby's player records.
const bool home_down = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
if (home_down && !home_was_down_) {
  PerfectDarkApp::DebugFindLobbyPlayers();
}
home_was_down_ = home_down;

// ---- src/perfect_dark_app.h: PerfectDarkApp static member --------------------
  // TEMP_LOBBY: Home logs the online lobby's player records as the mod reads
  // them (pd::handicap::LobbyPlayers), to check them against the lobby screen.
  static void DebugFindLobbyPlayers() {
    PerfectDarkApp* app = current_instance_;
    if (!app || !app->runtime() || !app->runtime()->memory()) return;
    auto* mem = app->runtime()->memory();
    const uint8_t* base = mem->virtual_membase();
    const uint32_t object = pd::handicap::LoadU32(base, pd::handicap::kNetObjectPtr);
    REXLOG_INFO("[lobby] network object 0x{:08X}, hosting {}, joined {}", object,
                rex::kernel::xam::netplay::IsHostingSession(), rex::kernel::xam::netplay::IsJoinedSession());
    if (object == 0) return;
    for (int i = 0; i < pd::handicap::kLobbyRecordCount; ++i) {
      const uint32_t record = object + pd::handicap::kLobbyRecords + uint32_t(i) * pd::handicap::kLobbyRecordSize;
      std::string name;
      for (uint32_t k = 0; k < pd::handicap::kLobbyNameMax && base[record + pd::handicap::kLobbyRecordName + k]; ++k) {
        name += char(base[record + pd::handicap::kLobbyRecordName + k]);
      }
      REXLOG_INFO("[lobby] record {}: in use {}, '{}'", i, base[record], name);
    }
    const std::vector<std::string> players = pd::handicap::LobbyPlayers(base);
    std::string names;
    for (const std::string& n : players) names += (names.empty() ? "" : ", ") + n;
    REXLOG_INFO("[lobby] {} players: {}", players.size(), names);
  }
