// TEMP_WEAP: debug code of the additional-weapons mod (branch additional-weapons).
//
// What for: checking that every PC of a netplay match makes the same weapon
// choices (extra slots 7-16, shuffled respawns, ammo crates), by comparing the
// logs of the host and the joined PCs.
//
// Hooks / toml entries: none of its own. The log lines sat in the mod's own
// functions in src/main.cpp, which the mod's hooks call:
//   PdWeaponByLocation  <- PdHookWeaponByLocation at 0x821CC458 (mpGetMpWeaponByLocation)
//   PdPropEnable        <- PdHookPropEnable at 0x821F7518 (propEnable)
//   SetPickupCrates     <- called from PdPropEnable
// The dump ran from the Home key in PerfectDarkInputTracker::OnDraw
// (src/perfect_dark_app.h, inside the pd_dev_hotkeys block; F7 is taken by the
// SDK achievements overlay).
//
// Removed in: the cleanup commit that added this file
// ("Move the weapons mod debug code to the debug bank").

// ---- src/perfect_dark_app.h -------------------------------------------------

// near the other forward declarations:
// TEMP_WEAP: logs the multiplayer weapon slots and the stage's pickups (main.cpp).
void PdWeaponsDebugDump();

// PerfectDarkInputTracker member:
bool home_was_down_ = false;  // TEMP_WEAP

// PerfectDarkInputTracker::OnDraw, with the other dev hotkeys:
// TEMP_WEAP: Home logs the multiplayer weapon slots and the stage's weapon pickups.
bool home_down = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
if (home_down && !home_was_down_) {
  PdWeaponsDebugDump();
}
home_was_down_ = home_down;

// ---- src/main.cpp -----------------------------------------------------------

// SetPickupCrates: one line per crate a respawn changed.
for (const weapons::Crate& crate : weapons::g_crates) {
  if (crate.weapon_obj != obj) continue;
  weapons::SetCrateAmmo(base, crate, index);
  std::string filled;
  for (int s = 0; s < weapons::kCrateSlotCount; ++s) {
    const uint32_t slot = crate.obj + weapons::kCrateSlots + uint32_t(s) * 4;
    const uint16_t model = weapons::LoadU16(base, slot);
    const uint16_t quantity = weapons::LoadU16(base, slot + 2);
    if (quantity == 0) continue;
    filled += fmt::format(" type {} x{}", s + 1, quantity);
    // ... (load the model)
  }
  REXLOG_INFO("[weapons] crate 0x{:08X} now holds:{}", crate.obj, filled.empty() ? " nothing" : filled);
}

// PdPropEnable: one line per pickup respawn (frame numbers should match on all PCs).
REXLOG_INFO("[weapons] pickup {} respawn #{} (frame {}): index {} -> {}{}", i, respawn,
            weapons::LoadU32(base, weapons::kLevelFrames), pickup.index, next,
            changed ? "" : " (not changed)");

// PdWeaponByLocation: one line per placed pickup / crate (3 per location).
REXLOG_INFO("[weapons] location {} -> index {} (weapon #{})", location, index,
            weapons::WeaponNumber(base, index));

// SetPickupWeapon, after rescaling the pickup's matrix and moving it to the new
// model's floor height (added and removed in the fix for oversized/floating
// respawned pickups, 2026-11-13; it checked the scale ratio and the floor lift).
REXLOG_INFO("[weapons] pickup 0x{:08X} -> index {}: scale {:.4f} -> {:.4f}, lift {:.2f} -> {:.2f}{}",
            obj, index, old_scale, scale, old_lift, new_lift, floor_placed ? "" : " (not floor-placed)");

// The Home dump.
void PdWeaponsDebugDump() {
  namespace weapons = pd::weapons;
  const uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const std::vector<uint8_t> game(base + weapons::kGameSlots, base + weapons::kGameSlots + weapons::kGameSlotCount);
  std::string text = fmt::format(
      "[weapons] dump: game slots {}, weapon set {}, settings '{}' shuffle {} seed {}\n"
      "  this stage: extra slots {}, respawn shuffle {} (seed {}), host choice used #{}",
      weapons::DescribeSlots(base, game), base[0x82648BF8 + 1839], REXCVAR_GET(pd_mp_weapons),
      REXCVAR_GET(pd_mp_weapon_shuffle), REXCVAR_GET(pd_mp_weapon_shuffle_seed),
      weapons::DescribeSlots(base, weapons::g_match.extra), weapons::g_match.shuffle,
      weapons::g_match.seed, weapons_used_seq);
  text += fmt::format("\n  selection {}", weapons::DescribeSlots(base, weapons::g_selection));
  // After the props are made, a pickup's location byte holds its weapon number.
  for (size_t i = 0; i < weapons::g_pickups.size(); ++i) {
    const weapons::Pickup& pickup = weapons::g_pickups[i];
    const int16_t pad = int16_t((base[pickup.obj + 6] << 8) | base[pickup.obj + 7]);
    text += fmt::format("\n  pickup {} 0x{:08X}: pad {}, weapon #{} (index {}), {} respawns, model 0x{:08X}", i,
                        pickup.obj, pad, base[pickup.obj + weapons::kObjWeaponNum], pickup.index,
                        pickup.respawns, weapons::LoadU32(base, pickup.obj + weapons::kObjModel));
  }
  for (const weapons::Crate& crate : weapons::g_crates) {
    text += fmt::format("\n  crate 0x{:08X} (ammo of pickup 0x{:08X}):", crate.obj, crate.weapon_obj);
    for (int s = 0; s < weapons::kCrateSlotCount; ++s) {
      const uint16_t quantity = weapons::LoadU16(base, crate.obj + weapons::kCrateSlots + uint32_t(s) * 4 + 2);
      if (quantity) text += fmt::format(" type {} x{}", s + 1, quantity);
    }
  }
  REXLOG_INFO("{}", text);
}
