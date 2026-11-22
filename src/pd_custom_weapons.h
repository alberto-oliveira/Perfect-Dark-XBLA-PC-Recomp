// Custom weapons: new weapons made from existing ones, under weapon numbers the
// game has but never uses in multiplayer.
//
// Why borrowed numbers: the game's weapon table g_Weapons (0x825D32C0) has
// exactly 0x5E entries with other data right after it, and about 40 functions
// hold its address, so it cannot grow. Some of its numbers are free in
// multiplayer: 0x51 (Hammer) and 0x52 (Screwdriver) point at an empty
// placeholder definition, and the keycards etc. are solo-only items. The
// network carries any weapon number (test 2.0: a remote 0x51 shot arrived as 0x51).
//
// What a custom weapon needs besides its definition, all done by hooks that ask
// this registry (perfect_dark_config.toml, "Custom weapons"):
//   - The game treats weapon numbers 0x01-0x2C as guns and everything above as
//     items: weapon cycling, weapon switching, "previous weapon", the weapon
//     wheel (hold Q) and the third-person model skip or mishandle an item. Those
//     checks are passed (jump hooks) or see the base weapon's number instead.
//   - Code that asks "is this the crossbow?" (the fire paths) sees the base.
//   - Each player's primary/secondary choice is one bit per weapon in the
//     player config (cfg+199, 6 bytes, bit = weapon number - 1), used only for
//     weapons 1-35; for any other weapon the switch does nothing. Bits 36-48 are
//     never used, so each custom weapon gets one of them (FuncBitWeapon).
//   - Names: a custom weapon borrows a text id (a solo-only item's name), which
//     its definition and its pickup-message row point at; the text itself is
//     replaced at startup (PerfectDarkApp::OverrideText).
//
// Each custom weapon also gets a multiplayer weapon table entry past the
// game's 47 ("virtual" indices 47, 48, ...), so the weapons mod's slots can
// hold it: pd::weapons::EntryAddress answers those indices.
//
// Header-only, like pd_weapons.h.
#pragma once

#include <cstdint>
#include <cstring>

#include "pd_weapons.h"

namespace pd::custom {

constexpr uint32_t kWeapons = 0x825D32C0;     // g_Weapons: u32 weapondef pointers, by weapon number
constexpr int kWeaponsCount = 0x5E;           // its entries (0x00-0x5D)
constexpr uint32_t kWeaponDefCopySize = 0x70; // struct weapondef is 0x50 bytes; a little more in case XBLA added fields
constexpr uint32_t kWeaponDefShortName = 0x44;  // u16 text ids
constexpr uint32_t kWeaponDefName = 0x46;

// The pickup-message names (weapon_get_pickup_text sub_8220CD78 passes this
// table to its lookup sub_8220C0F0): rows of 16 bytes {u32 weapon number,
// u16 text id, u16 second text id, u8 determiners[5], 3 spare}, ending with a
// row whose weapon number is 0.
constexpr uint32_t kPickupNames = 0x825E29D0;
constexpr uint32_t kPickupNameRowSize = 16;

// The first weapon number whose function bit the game never uses.
constexpr int kFirstSpareFuncBitWeapon = 36;
constexpr int kLastSpareFuncBitWeapon = 48;  // gunfuncs is 6 bytes: weapons 1-48

// One custom weapon.
struct Weapon {
  uint8_t number;    // the borrowed weapon number
  uint8_t base;      // the weapon it is made from
  uint16_t text_id;  // the borrowed text id for its name (a solo-only item's)
  const char* name;  // its name (menus, HUD, pickup message)
};

// The custom weapons. The order gives their virtual table indices (the first is
// 47) and their spare function bits (the first uses weapon 36's).
inline constexpr Weapon kList[] = {
    // base: the crossbow; name: the Necklace's text (0x4CE6, solo-only)
    {0x51, 27, 0x4CE6, "Detonator Crossbow"},
};
constexpr int kCount = int(sizeof(kList) / sizeof(kList[0]));
static_assert(kFirstSpareFuncBitWeapon + kCount - 1 <= kLastSpareFuncBitWeapon, "no spare function bits left");

// The position of the custom weapon with weapon number `number` in kList, or -1.
inline int IndexOf(uint32_t number) {
  for (int i = 0; i < kCount; ++i) {
    if (kList[i].number == number) return i;
  }
  return -1;
}
inline bool IsCustom(uint32_t number) { return IndexOf(number) >= 0; }

// The weapon number the game should see where it asks "which weapon is this?":
// the base weapon's for a custom weapon, `number` itself otherwise.
inline uint32_t AsBase(uint32_t number) {
  const int i = IndexOf(number);
  return i >= 0 ? kList[i].base : number;
}

// The weapon number whose primary/secondary bit a custom weapon uses (a spare one).
inline int FuncBitWeapon(int custom_index) { return kFirstSpareFuncBitWeapon + custom_index; }

// The multiplayer table index of the custom weapon at `custom_index` (47, 48...).
inline uint8_t TableIndex(int custom_index) { return uint8_t(pd::weapons::kWeaponTableCount + custom_index); }

// Set by Install.
inline uint32_t g_entries = 0;       // kCount multiplayer table entries of 10 bytes
inline uint32_t g_pickup_names = 0;  // the pickup-name table with the custom weapons' rows
inline bool g_installed = false;

inline uint32_t LoadU32(const uint8_t* base, uint32_t address) { return pd::weapons::LoadU32(base, address); }
inline void StoreU32(uint8_t* base, uint32_t address, uint32_t value) {
  base[address] = uint8_t(value >> 24);
  base[address + 1] = uint8_t(value >> 16);
  base[address + 2] = uint8_t(value >> 8);
  base[address + 3] = uint8_t(value);
}

// The game's multiplayer table index of weapon `number` (e.g. 26 for the
// crossbow, 27), or -1.
inline int TableIndexOf(const uint8_t* base, uint8_t number) {
  for (int i = 0; i < pd::weapons::kWeaponTableCount; ++i) {
    if (base[pd::weapons::kWeaponTable + uint32_t(i) * pd::weapons::kWeaponEntrySize] == number) return i;
  }
  return -1;
}

// A copy of the pickup-name table with one more row per custom weapon: its base
// weapon's row (same determiners, "a"/"an"...) under its own number and text id.
// The copy keeps the end row last.
template <typename Alloc>
inline uint32_t BuildPickupNames(uint8_t* base, Alloc alloc) {
  uint32_t rows = 0;  // rows before the end row
  while (LoadU32(base, kPickupNames + rows * kPickupNameRowSize) != 0 && rows < 1000) ++rows;
  const uint32_t table = alloc((rows + uint32_t(kCount) + 1) * kPickupNameRowSize);
  if (!table) return 0;
  std::memcpy(base + table, base + kPickupNames, rows * kPickupNameRowSize);
  for (int i = 0; i < kCount; ++i) {
    const Weapon& w = kList[i];
    const uint32_t row = table + (rows + uint32_t(i)) * kPickupNameRowSize;
    std::memset(base + row, 0, kPickupNameRowSize);
    for (uint32_t r = 0; r < rows; ++r) {
      if (LoadU32(base, kPickupNames + r * kPickupNameRowSize) == w.base) {
        std::memcpy(base + row, base + kPickupNames + r * kPickupNameRowSize, kPickupNameRowSize);
        break;
      }
    }
    StoreU32(base, row, w.number);
    pd::weapons::StoreU16(base, row + 4, w.text_id);
    pd::weapons::StoreU16(base, row + 6, 0);
  }
  std::memset(base + table + (rows + uint32_t(kCount)) * kPickupNameRowSize, 0, kPickupNameRowSize);  // the end row
  return table;
}

// Makes each custom weapon, once (game thread): a copy of its base weapon's
// definition at g_Weapons[number], with the borrowed name; a copy of the base's
// multiplayer table entry with the weapon number changed; and the pickup-name
// table. `alloc(size)` returns guest heap memory. The borrowed numbers are
// unused in multiplayer, so they keep their custom weapon for the whole session.
template <typename Alloc>
inline void Install(uint8_t* base, Alloc alloc) {
  if (g_installed) return;
  g_installed = true;
  g_entries = alloc(uint32_t(kCount) * pd::weapons::kWeaponEntrySize);
  for (int i = 0; i < kCount; ++i) {
    const Weapon& w = kList[i];
    const uint32_t def = alloc(kWeaponDefCopySize);
    const uint32_t base_def = LoadU32(base, kWeapons + uint32_t(w.base) * 4);
    if (g_entries && def && base_def) {
      std::memcpy(base + def, base + base_def, kWeaponDefCopySize);
      pd::weapons::StoreU16(base, def + kWeaponDefShortName, w.text_id);
      pd::weapons::StoreU16(base, def + kWeaponDefName, w.text_id);
      StoreU32(base, kWeapons + uint32_t(w.number) * 4, def);
    }
    const int base_index = TableIndexOf(base, w.base);
    const uint32_t entry = g_entries + uint32_t(i) * pd::weapons::kWeaponEntrySize;
    if (g_entries && base_index >= 0) {
      std::memcpy(base + entry,
                  base + pd::weapons::kWeaponTable + uint32_t(base_index) * pd::weapons::kWeaponEntrySize,
                  pd::weapons::kWeaponEntrySize);
      base[entry] = w.number;
    }
    REXLOG_INFO("[custom] weapon 0x{:02X} ({}) from weapon {}: definition 0x{:08X}, table index {} -> entry "
                "0x{:08X}, name text 0x{:04X}, function bit of weapon {}",
                w.number, w.name, w.base, def, TableIndex(i), entry, w.text_id, FuncBitWeapon(i));
  }
  g_pickup_names = BuildPickupNames(base, alloc);
  pd::weapons::g_extra_entries = g_entries;
  pd::weapons::g_extra_count = g_entries ? kCount : 0;
}

}  // namespace pd::custom
