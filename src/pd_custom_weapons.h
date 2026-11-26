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
#include <cstdlib>
#include <cstring>
#include <string>

#include "pd_file_override.h"  // a weapon's own model files
#include "pd_weapons.h"

namespace pd::custom {

constexpr uint32_t kWeapons = 0x825D32C0;     // g_Weapons: u32 weapondef pointers, by weapon number
constexpr int kWeaponsCount = 0x5E;           // its entries (0x00-0x5D)
constexpr uint32_t kWeaponDefCopySize = 0x70; // struct weapondef is 0x50 bytes; a little more in case XBLA added fields
constexpr uint32_t kWeaponDefShortName = 0x44;  // u16 text ids
constexpr uint32_t kWeaponDefName = 0x46;
constexpr uint32_t kWeaponDefSecondary = 0x18;  // functions[1]: the secondary function's definition

// Function definitions a custom weapon can take as its secondary.
constexpr uint32_t kFuncMineDetonate = 0x825D0F60;  // the remote mine's Detonate

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
  uint32_t secondary;  // its secondary function's definition, 0 = the base weapon's
  // Something held in the left hand at the same time (stage 4a), the way the
  // remote mine's left hand holds its detonator: the weapon the left hand gets
  // (0 = none) and the first-person model file it is drawn with.
  uint8_t left_weapon;
  uint16_t left_model_file;
  // The weapon it LOOKS like (0 = its base): its first-person model, the hand's
  // position, the equip animation and which model parts are hidden come from
  // that weapon's definition, its third-person and pickup models from that
  // weapon's too (Install, AsLook). How it works is still the base weapon's.
  uint8_t look;
  // Its own model (custom gun models, docs/research/gun-models.md), served from the
  // override folder (pd_file_override.h); 0 = the look weapon's:
  //   model_hi / model_lo: the first-person N64 model files (definition +0/+2), copies of
  //   the look weapon's that link the gun's own XBLA mesh (tools/models/glb2xbla.py);
  //   model_number: a model number multiplayer never shows, borrowed for the third-person
  //   and pickup model; its g_ModelStates file id becomes model_file.
  uint16_t model_hi = 0;
  uint16_t model_lo = 0;
  uint16_t model_number = 0;
  uint16_t model_file = 0;
};

// The custom weapons. The order gives their virtual table indices (the first is
// 47) and their spare function bits (the first uses weapon 36's).
inline constexpr Weapon kList[] = {
    // base: the crossbow, whose primary (the 1-damage dizzy bolt) it keeps;
    // secondary: Detonate, which explodes this player's Detonator bolts
    // (pd_crossbow.h); name: the Necklace's text (0x4CE6, solo-only)
    // left hand: the remote mine's detonator (weapon 34, which the game turns
    // into the detonator in a left hand) from the mine's model file 0x393
    {0x51, 27, 0x4CE6, "Detonator Crossbow", kFuncMineDetonate, 34, 0x393, 0},
    // The Ricochet Laser, named the Carom LX (a carom: a shot that rebounds, in
    // billiards). base: the Laser (its pulse and Stream, whose shots bounce off
    // walls: pd_ricochet.h); looks like the Mauler (weapon 6), its muzzle flash
    // drawn red (pd_rico_flash_tint); name: the text of solo item 0x4E (0x4CE5)
    {0x52, 29, 0x4CE5, "Carom LX", 0, 0, 0, 6},
    // The proof gun of custom gun models (phase 2, until real custom guns replace it): a
    // Falcon 2 with a model of its own, made by tools/models/make_testgun.py (--install
    // puts its files in the override folder). Number: the necklace's (solo
    // only); name: the text of solo item 0x43 (0x4CE2). Files in the override folder:
    // first person 0x273 (links mesh 0x274), third person 0x275 (links mesh 0x276), shown
    // with the borrowed model number 0xF6 (MODEL_CHRLEEGUN1, a beta gun).
    {0x50, 2, 0x4CE2, "Test Gun", 0, 0, 0, 2, 0x273, 0x273, 0xF6, 0x275},
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

// The weapon number whose models a weapon is drawn with: the look weapon's for a
// custom weapon that has one, else as AsBase.
inline uint32_t AsLook(uint32_t number) {
  const int i = IndexOf(number);
  if (i < 0) return number;
  return kList[i].look ? kList[i].look : kList[i].base;
}

// The custom weapon with weapon number `number`, or nullptr.
inline const Weapon* Find(uint32_t number) {
  const int i = IndexOf(number);
  return i >= 0 ? &kList[i] : nullptr;
}

// ---- The left hand (stage 4a) -----------------------------------------------------
//
// The game gives each player one first-person gun model, used by both hands. The
// remote mine is "dual-wielded": both hands hold weapon 34, and every check for
// "the left hand holds weapon 34" makes that hand its detonator (its position, no
// firing, the press animation on Detonate). A custom weapon with a left_weapon
// gets the same: on equip the left hand is put in use holding left_weapon, and is
// given its own model, loaded from left_model_file into memory of our own with
// the game's gun loader steps (main.cpp, PdCustomLoadGunModel). Hooks: the weapon
// switch, the gun loader's per-hand model setup and the per-hand tick
// (perfect_dark_hooks.cpp, "left hand").

// Player and hand fields (XBLA; the gun control is the N64 gunctrl moved 0x28 bytes).
constexpr uint32_t kPlayerWeapon = 5544;        // s8 gunctrl.weaponnum: the weapon held
constexpr uint32_t kPlayerDualWield = 5547;     // u8: bit 0x80 = gunctrl.dualwielding
constexpr uint32_t kPlayerGunMemNew = 5548;     // s32 gunctrl.gunmemnew: the weapon being loaded
constexpr uint32_t kPlayerHands = 1632;         // hands[2], kHandSize each (right, left)
constexpr uint32_t kHandSize = 1956;
constexpr uint32_t kHandWeapon = 0;             // s8 gset.weaponnum
constexpr uint32_t kHandVisible = 7;            // s8 visible (drawn this frame)
constexpr uint32_t kHandInUse = 8;              // u8 inuse
constexpr uint32_t kHandGunModel = 900;         // struct model gunmodel (definition at +8)
constexpr uint32_t kHandGunModelSave = 1076;    // the gun model's saved data (model_init's 3rd argument)
constexpr uint32_t kHandGunModelCmds = 1940;    // compiledgunmodelcmds
constexpr uint32_t kHandState = 1540;           // s32 state (N64 HANDSTATE_*)
constexpr int32_t kHandStateReload = 1;
constexpr uint32_t kModelDefinition = 8;        // struct model: its modeldef
constexpr uint32_t kHandLeft = kPlayerHands + kHandSize;

// Each custom weapon's left-hand model (0 = none or not loaded yet), loaded at
// each stage start, and the memory its command lists are built in.
inline uint32_t g_left_model[kCount] = {};
inline uint32_t g_left_cmds[kCount] = {};
constexpr uint32_t kLeftCmdsSize = 0x10000;

// The weapon number whose primary/secondary bit a custom weapon uses (a spare one).
inline int FuncBitWeapon(int custom_index) { return kFirstSpareFuncBitWeapon + custom_index; }

// The multiplayer table index of the custom weapon at `custom_index` (47, 48...).
inline uint8_t TableIndex(int custom_index) { return uint8_t(pd::weapons::kWeaponTableCount + custom_index); }

// ---- Tint (stage 3) ---------------------------------------------------------------
//
// A custom weapon can be drawn in a colour of its own. The game already gives a
// whole model one colour at render time: the first-person gun gets the room's
// light (gunshadecol), objects their shade colour. That colour is not a
// multiplier: its RGB is a colour laid over the model and its alpha how much of
// it shows (test 3.2: multiplying the RGB changed nothing). The game's own uses
// show it: a charging Mauler blends its gun colour towards 0xFF00007F (red at
// alpha 0x7F), and Hold the Briefcase highlights the case with green at alpha
// 0x40. A custom weapon's tint is added as such an overlay, at most kTintAlpha,
// scaled by the tint strength (TintRgba), only for that weapon (hooks in
// perfect_dark_hooks.cpp); no model or texture data changes.
// "No tint": a value no 0xRRGGBB colour can have, so that white (0xFFFFFF) can
// be a tint of its own (the Ricochet Laser is tinted white).
constexpr uint32_t kNoTint = 0xFFFFFFFF;

// Each custom weapon's tint (0xRRGGBB), set from its setting every frame.
inline uint32_t g_tint[kCount] = {};

// A tint setting: a colour name, "#RRGGBB", or "off"/"white"/"" for none.
inline uint32_t ParseTint(const std::string& text) {
  static const struct { const char* name; uint32_t rgb; } kNames[] = {
      {"red", 0xFF3030},  {"orange", 0xFF9020}, {"yellow", 0xFFE030}, {"green", 0x40FF40},
      {"cyan", 0x40FFFF}, {"blue", 0x5080FF},   {"purple", 0xB050FF}, {"pink", 0xFF70C0},
      {"white", 0xFFFFFF}, {"off", kNoTint},    {"", kNoTint},
  };
  for (const auto& n : kNames) {
    if (text == n.name) return n.rgb;
  }
  if (text.size() == 7 && text[0] == '#') {
    char* end = nullptr;
    const unsigned long rgb = std::strtoul(text.c_str() + 1, &end, 16);
    if (end && *end == '\0') return uint32_t(rgb);
  }
  return kNoTint;
}

// ---- Muzzle flash colour ---------------------------------------------------------
//
// The Mauler's first-person flash is a part of its gun model (in the dump of
// file 0x379: the GUNDL node at 0x1F8, in the OPAQUE list) drawn as texture x
// vertex colour (combine FC121824 FF33FFFF). Its one vertex colour (a colour
// table of one entry, 0xFEFEFEFF, followed by 0 and its display list's 0x05...
// address) is near-white; the green is in the texture (0x0296). The render
// colour does not reach it. A custom weapon's flash colour is put in that vertex
// colour while its gun is drawn (hooks around the gun renderer's model_render)
// and put back after, so the loaded file stays the Mauler's. The loaded file's
// layout differs from the dump (the entry was 0x4A0 earlier), so the entry is
// found by that pattern once per loaded model. Set every frame (0xRRGGBB, or
// kNoTint = none).
struct FlashTint {
  uint32_t rgb = kNoTint;
};
inline FlashTint g_flash[kCount];
constexpr uint32_t kMaulerFlashColourValue = 0xFEFEFEFF;
constexpr uint32_t kFlashSearchSize = 0x4000;  // the Mauler's gun files are ~12 KB

// Where the flash's colour entry is in the gun model loaded at `def`, or 0: the
// first {0xFEFEFEFF, 0, 0x05xxxxxx} (the entry, an empty word, its display list).
inline uint32_t FindFlashColour(const uint8_t* base, uint32_t def) {
  using pd::weapons::LoadU32;
  for (uint32_t a = def; a < def + kFlashSearchSize; a += 4) {
    if (LoadU32(base, a) == kMaulerFlashColourValue && LoadU32(base, a + 4) == 0 &&
        (LoadU32(base, a + 8) >> 24) == 0x05) {
      return a;
    }
  }
  return 0;
}

// The tint of weapon `number`, kNoTint if it is not a custom weapon.
inline uint32_t TintOf(uint32_t number) {
  const int i = IndexOf(number);
  return i >= 0 ? g_tint[i] : kNoTint;
}

// The muzzle flash colour of weapon `number` as a vertex colour (RGBA), or 0 if
// it has none of its own.
inline uint32_t FlashColourOf(uint32_t number) {
  const int i = IndexOf(number);
  if (i < 0 || g_flash[i].rgb == kNoTint) return 0;
  return (g_flash[i].rgb << 8) | 0xFF;
}

// How much of the tint colour shows at full strength (the Mauler's glow uses 0x7F).
constexpr uint32_t kTintAlpha = 0x7F;
// How strongly the custom weapons' colours move towards their tint: 0-255,
// set from pd_custom_tint_strength every frame.
inline uint32_t g_tint_strength = 153;  // 60%

// A render colour with the tint added (`tint` = 0xRRGGBB).
//
// The renderer lays the colour's RGB over the model with its alpha as the amount:
// shown = model * (1 - A) + RGB * A. The game's own colour is the room's darkness
// (dark RGB, more alpha in darker rooms). Blending it straight towards the tint
// (test 3.2c) replaced that darkness with tint, so the tint grew brighter in dark
// rooms. Instead this works out the one overlay (X, A1) that shows the same as
// the room's darkness first and the tint on top of it:
//   after the room:  m * (1 - A0) + D * A0
//   then the tint:   that * (1 - s) + T' * s
// so A1 = A0 + s - A0 * s and X = (D * A0 * (1 - s) + T' * s) / A1, where s is the
// tint's amount (strength times kTintAlpha) and T' the tint as lit by the room
// (the tint times what the darkness leaves of white: 1 - A0 + A0 * D).
// `strength` (0-255) scales the tint; the custom weapons' own tint uses g_tint_strength.
inline uint32_t TintRgba(uint32_t rgba, uint32_t tint, uint32_t strength = g_tint_strength) {
  const float a0 = float(rgba & 0xFF) / 255.0f;
  const float s = float(strength) / 255.0f * float(kTintAlpha) / 255.0f;
  const float a1 = a0 + s - a0 * s;
  if (a1 <= 0.0f) return rgba;
  uint32_t out = uint32_t(a1 * 255.0f + 0.5f) & 0xFF;
  for (int c = 0; c < 3; ++c) {
    const float d = float((rgba >> (24 - c * 8)) & 0xFF) / 255.0f;  // the room's darkness colour
    const float t = float((tint >> (16 - c * 8)) & 0xFF) / 255.0f;  // the tint
    const float lit = t * (1.0f - a0 + a0 * d);
    float x = (d * a0 * (1.0f - s) + lit * s) / a1;
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    out |= uint32_t(x * 255.0f + 0.5f) << (24 - c * 8);
  }
  return out;
}

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
  // The custom rows go FIRST: the game's lookup (sub_8220C0F0) takes the first row with
  // the weapon's number, and a borrowed number may have a row of its own already (0x50,
  // the necklace, whose text is the Detonator Crossbow's name).
  std::memcpy(base + table + uint32_t(kCount) * kPickupNameRowSize, base + kPickupNames, rows * kPickupNameRowSize);
  for (int i = 0; i < kCount; ++i) {
    const Weapon& w = kList[i];
    const uint32_t row = table + uint32_t(i) * kPickupNameRowSize;
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
  // What the game's first-match lookup will find for each custom weapon (its own text).
  for (int i = 0; i < kCount; ++i) {
    for (uint32_t r = 0; r < rows + uint32_t(kCount); ++r) {
      const uint32_t row = table + r * kPickupNameRowSize;
      if (LoadU32(base, row) != kList[i].number) continue;
      REXLOG_INFO("[custom] weapon 0x{:02X} ({}): pickup message text 0x{:04X}", kList[i].number, kList[i].name,
                  (base[row + 4] << 8) | base[row + 5]);
      break;
    }
  }
  return table;
}

// ---- The look (a custom weapon drawn as another weapon) ---------------------------
//
// A weapon definition (N64 struct weapondef) mixes how a gun looks with how it
// works. What belongs to the look, and is copied from the look weapon:
//   +0x00 u16 hi model file, +0x02 u16 lo model file (first person);
//   +0x04..+0x13 the equip, unequip, primary->secondary and secondary->primary
//         animations (gun scripts that play the MODEL's animations, so they
//         must match the model);
//   +0x28 muzzle z, +0x2C/+0x30/+0x34 the gun's position in the view, +0x38 sway;
//   +0x3C gunviscmds and +0x40 part visibility (which of the model's parts are
//         hidden, e.g. the Mauler's spare magazine).
// What stays the base weapon's: the functions (+0x14/+0x18; their fire
// animations: the Laser has none), ammo (+0x1C/+0x20), aim settings (+0x24),
// names and flags.
constexpr uint32_t kDefLookModels = 0x00, kDefLookModelsSize = 0x14;
constexpr uint32_t kDefLookPlace = 0x28, kDefLookPlaceSize = 0x1C;
// A multiplayer table entry's pickup model (u16) and its scale (u16).
constexpr uint32_t kEntryModel = 6, kEntryModelSize = 4;
// g_ModelStates: 8 bytes per model number {u32 loaded file, u16 file id, u16 scale}.
constexpr uint32_t kModelStates = 0x825F42E8;

// The third-person model number of a custom weapon with a model of its own, else 0.
// Its own models apply only when their files were loaded into the override (with
// pd_file_override off or the files missing, the weapon keeps its look weapon's models:
// a borrowed model number must never point at an empty archive entry).
inline bool HasOwnModels(const Weapon& w) {
  if (!w.model_hi) return false;
  const auto& files = pd::fileoverride::Get().files;
  return files.count(w.model_hi) && files.count(w.model_lo ? w.model_lo : w.model_hi) &&
         (!w.model_number || files.count(w.model_file));
}
inline uint32_t OwnModelNumber(uint32_t number) {
  const int i = IndexOf(number);
  return i >= 0 && HasOwnModels(kList[i]) ? kList[i].model_number : 0;
}

inline void CopyLook(uint8_t* base, uint32_t def, uint8_t look) {
  if (!look) return;
  const uint32_t look_def = LoadU32(base, kWeapons + uint32_t(look) * 4);
  if (!look_def) return;
  std::memcpy(base + def + kDefLookModels, base + look_def + kDefLookModels, kDefLookModelsSize);
  std::memcpy(base + def + kDefLookPlace, base + look_def + kDefLookPlace, kDefLookPlaceSize);
  REXLOG_INFO("[custom] definition 0x{:08X} looks like weapon {} (definition 0x{:08X}): model files 0x{:04X}/0x{:04X}",
              def, look, look_def, (base[def] << 8) | base[def + 1], (base[def + 2] << 8) | base[def + 3]);
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
    const bool own_models = HasOwnModels(w);  // its own model files are loaded (pd_file_override)
    const uint32_t def = alloc(kWeaponDefCopySize);
    const uint32_t base_def = LoadU32(base, kWeapons + uint32_t(w.base) * 4);
    if (g_entries && def && base_def) {
      std::memcpy(base + def, base + base_def, kWeaponDefCopySize);
      pd::weapons::StoreU16(base, def + kWeaponDefShortName, w.text_id);
      pd::weapons::StoreU16(base, def + kWeaponDefName, w.text_id);
      if (w.secondary) StoreU32(base, def + kWeaponDefSecondary, w.secondary);
      CopyLook(base, def, w.look);
      if (w.model_hi && !own_models) {
        REXLOG_WARN("[custom] weapon 0x{:02X} ({}): its model files are not in the override folder: drawn as "
                    "weapon {}", w.number, w.name, w.look);
      }
      if (own_models) {  // its own first-person model files
        pd::weapons::StoreU16(base, def + 0, w.model_hi);
        pd::weapons::StoreU16(base, def + 2, w.model_lo ? w.model_lo : w.model_hi);
      }
      StoreU32(base, kWeapons + uint32_t(w.number) * 4, def);
    }
    const int base_index = TableIndexOf(base, w.base);
    const uint32_t entry = g_entries + uint32_t(i) * pd::weapons::kWeaponEntrySize;
    if (g_entries && base_index >= 0) {
      std::memcpy(base + entry,
                  base + pd::weapons::kWeaponTable + uint32_t(base_index) * pd::weapons::kWeaponEntrySize,
                  pd::weapons::kWeaponEntrySize);
      base[entry] = w.number;
      // The pickup is drawn with the look weapon's model and scale.
      const int look_index = w.look ? TableIndexOf(base, w.look) : -1;
      if (look_index >= 0) {
        std::memcpy(base + entry + kEntryModel,
                    base + pd::weapons::kWeaponTable + uint32_t(look_index) * pd::weapons::kWeaponEntrySize +
                        kEntryModel,
                    kEntryModelSize);
      }
      if (own_models && w.model_number) {  // its own pickup model (the look's scale is kept)
        pd::weapons::StoreU16(base, entry + kEntryModel, w.model_number);
        // The borrowed model number now loads the gun's third-person file.
        pd::weapons::StoreU16(base, kModelStates + uint32_t(w.model_number) * 8 + 4, w.model_file);
        REXLOG_INFO("[custom] weapon 0x{:02X}: own models: first person 0x{:04X}/0x{:04X}, third person and "
                    "pickup model 0x{:03X} = file 0x{:04X}", w.number, w.model_hi, w.model_lo, w.model_number,
                    w.model_file);
      }
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
