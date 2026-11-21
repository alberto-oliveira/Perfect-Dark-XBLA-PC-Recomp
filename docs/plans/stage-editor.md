# A multiplayer stage editor for the PD recomp: feasibility and plan

Status (2026-11-21): planned only, not started. It replaces an earlier idea of porting GoldenEye's Archives, Bunker and Caverns; those maps could later be rebuilt by hand in the editor. When work starts, check the addresses below against the code as it is then, and choose the editor toolchain.

## Context
You want to make your own multiplayer stages for the PD XBLA recomp and play them online with simulants. The editor doesn't have to run inside the game. It can be a separate application whose output the game picks up.

## Verdict: feasible, high effort; most of it in the exporter, not the editor

The game side is well understood. XBLA PD loads N64-format stage files, checked during the GoldenEye study:

| File | XBLA function | Matches N64 |
|---|---|---|
| bg | `sub_820D6C48` | `bg_reset` |
| tiles | `sub_820E8220` / `sub_820E80D8` | `tiles_reset` / `stage_parse_tiles` |
| pads and setup | known since the solo-stages mod | N64 |

- **The display lists are N64 GBI**, run by 4J's interpreter `sub_822653B8`. Plume only sees the resulting D3D calls, so custom stages need **no renderer work**.
- **bg sections can be stored uncompressed.** The section reader `sub_82234BB8` copies a section as is when it lacks the Rare zip magic `0x1173`, so the exporter needs no compressor.
- **Free stage slots exist:** about 20 test stages with stub files in `g_Stages`, each with its own file ids. Examples: 0x18, 0x1A, 0x23, 0x3A, 0x48–0x4C.
- **Already built by the solo-stages mod:**
  - the Arena menu group, names and Random (`PdArenaListHandler`, `src/perfect_dark_app.h`);
  - the setup builder (`PdSoloStageSetup` in `src/main.cpp`, with `ChooseSpawns`, `ChooseWeaponSpawns` and `SetupWriter` in `src/pd_stages.h`);
  - the hand-picked layout table and the marking keys;
  - online sync and the `STGE` fingerprint check.

So the work splits into three parts:

| Part | What | Effort |
|---|---|---|
| **A. Game-side loader** | Serve custom stage files for free slots; Arena menu "Custom" group; scale, fog/sky, music | Medium (about the size of the solo-stages mod) |
| **B. Exporter** | Editor scene → PD bg (rooms, portals, GBI display lists, vertex colours), tiles, pads (waypoints, waygroups), setup (spawns, weapons, doors, lifts, glass) | **High:** the core of the project and the main risk |
| **C. Editor integration** | Texture library, entity definitions (spawn, weapon, door, light...), room and portal tools, preview | Low to high, depending on the toolchain below |

### Unknowns to settle first
- **Textures:** the editor needs previews of PD's textures. `Textures.raw` is Xbox-format LZX with no offline decoder. Plan: a TEMP debug key dumps every texture to PNG (`tex_<number>.png`, at its N64 size) by having the game decode it, through the Plume `SetTexture` path in `src/pd_native_gpu.cpp` or through the LZX routine `sub_822BD2A0` and the record table. Done once; the PNGs become the editor's texture library.
- **PD bg details:**
  - the room and portal structs;
  - the bg commands (visibility);
  - the vertex format with its separate colour table, and the `0x04`/`0x07` commands;
  - how texture numbers go into `0xFD`;
  - the light section.

  All of this is in the N64 decomp (`F:\Workspace\git\perfect_dark\src\game\bg.c`, `include/types.h`). It is checked against real XBLA bg files dumped from memory at runtime: after inflation, the sections are plain N64 layout.
- **Lighting:** PD bakes lighting into vertex colours. The exporter bakes it from light entities (ambient plus point lights, optionally with ray-cast shadows).
- **Limits:** GBI vertex batches of 16 vertices, triangle and room counts per stage, and the display-list memory budget. The first test stage measures these.

---

## Toolchain options for the editor

| Option | How it works | Pros | Cons | Editor effort |
|---|---|---|---|---|
| **1. TrenchBroom + Python exporter** (recommended) | Quake-style brush editor (free, open source). A game config gives it PD's texture PNGs and an entity file (`pd.fgd`) for `info_spawn`, `item_weapon`, `func_door`, `func_lift`, `func_glass`, `light`, `func_room`, `func_portal`. A Python tool turns the `.map` file into PD files. | Made for FPS arenas: grid, convex brushes (watertight collision by design), texture alignment, entity forms. Brushes map well to PD collision tiles, and rooms and portals become brush entities. Almost no editor code. | Brushes only (no organic shapes, but you can import meshes as models). A new tool to learn. | Low |
| **2. Blender add-on** | You model in Blender. An add-on adds PD properties (room, portal, pad, spawn, weapon, door) and an export button, written in Python with `bpy`. | You have Blender. Free-form modelling, organic shapes, real light baking (Cycles bake to vertex colours). | Free-form meshes can leak or have holes in their collision, so the exporter needs checks. No grid or arena workflow out of the box. You build the PD panels yourself. | Medium |
| **3. Godot 4 editor plugin** | You build levels inside the Godot editor with CSG boxes and meshes, plus custom nodes (`PdSpawn`, `PdWeapon`, `PdRoom`, `PdPortal`, `PdDoor`). An `EditorPlugin` in GDScript or C# exports. TrenchBroom maps can also come in through the func_godot add-on. | You know Godot. Live 3D preview with lighting. You can build a custom "walk the map" preview and a texture browser with tool scripts. | More code than option 1: the plugin, inspectors, and validation. CSG triangulation needs clean-up for collision. | Medium to high |
| **4. Own app (C++ with Dear ImGui, or Rust with egui)** | A standalone editor built for PD. | Full control. It could read PD files back and show existing PD stages for reference. | By far the most work: camera, picking, gizmos, undo, file handling, all before any level gets made. | Very high |

The 2D Tiled editor (installed) is ruled out: a grid of 2D tiles with heights would give only Doom-like layouts, and PD stages need real 3D (rooms over rooms, ramps).

**Recommendation: option 1, TrenchBroom.** It is the shortest way to make good arena levels, its brush geometry is the easiest to turn into clean collision, and the exporter's Python can later also read Blender or Godot output if you want a second editor. Option 2 (Blender) is the runner-up if you want organic geometry or real light baking.

### How the custom stages reach the game (no live injection)
- The exporter writes `game\DataFiles\custom_stages\<name>\`:
  - `stage.json`: name, slot preference, scale, fog/sky, music
  - `bg.bin`, `tiles.bin`, `pads.bin`
  - `layout.json`: spawns, weapon spots, doors, lifts, glass
- At startup the game's loader (`src/pd_custom_stages.h` plus hooks) scans that folder, gives each stage a free test slot, and adds it to the Arena menu.
- **File serving:** hooks on the archive's size and load functions (`sub_822549A0` / `sub_822549C0`) return the custom file for that slot's file ids.
- **The setup** is built at load time by the existing solo-stages builder, from `layout.json`.
- **Online:** each PC needs the same folder (copy it by hand, or send it later as a zip). The `STGE` fingerprint also covers the custom files, so a PC with a different version is warned.
- **Quick iteration:** a hot-reload key re-reads `custom_stages` between matches, so you export, restart the match, and walk it.

---

## Steps (each committable once its test passes)

**0. Texture library and file dumps (TEMP, no gameplay change)**
- A debug key dumps all textures to PNG.
- A dump of inflated bg, tiles and pads files from memory for 2–3 vanilla arenas, as references for the exporter.
- **Test 0.1** (1 player): run the dump. Then I check the PNGs and the reference files.

**1. Loader proven with vanilla data**
- `custom_stages\skedar_copy` holds Skedar's dumped files, loaded into a test slot as the "Custom" arena.
- **Test 1** (host + client): the arena loads, plays and syncs; the fingerprint passes; vanilla arenas are unaffected.

**2. Exporter, hello room**
- `tools/stage-editor/export.py` writes the bg, tiles and pads files for one room with one portal-free box.
- Two PD textures and simple baked light; spawns from `layout.json`.
- **Test 2** (1 player): the room renders with the right textures and lighting; walking, collision and shooting the walls work.

**3. Editor integration** (option 1: the TrenchBroom game config, `pd.fgd` and the texture folder; other options: their plugin or add-on)
- You build a small two-room arena with a portal; the exporter reads it.
- **Test 3** (host + client): rooms and portals cull correctly, there are no collision holes, and both PCs match.

**4. Bots:** pads, waypoints and waygroups. Waypoints come from floor sampling (automatic) plus optional hand-placed `info_waypoint` entities.
- **Test 4** (host + client + 4 simulants): bots move between rooms and pick up weapons.

**5. Props:** doors, lifts, glass and scenery, as PD objects by model number, copied the way the solo-stages mod copies them.
- **Test 5:** doors and lifts work and sync online.

**6. Polish:** fog/sky and music per stage, the hot-reload key, validation reports (leaks, unreachable spawns, waypoints with no links). Then cleanup (TEMP code to `tools/debug-bank/`) and the PR to `dev`.

## Debug hooks planned (TEMP_EDITOR)
- Texture and file dump keys (step 0).
- A served-file log: id, size and fingerprint.
- A `bg_reset` result log: rooms, portals, sections.
- A "where am I" key: room, nearest pad and waypoint. This reuses `pd_solo_mark_keys`/`MarkPad`.
- Exporter reports: triangles per room, GBI batches, tiles, waypoint graph, validation warnings.

## Risks
- **4J interpreter quirks** with display lists not made by Rare's tools: combine and render modes, texture loads. Step 2 finds these early, using the vanilla reference dumps.
- **bg visibility:** if the portal and bg-command logic misbehaves, the fallback is one room per stage with no culling. That is fine on a PC for small arenas.
- **Online file distribution** is manual at first.

## Verification
- **Build:** the usual `cmake --build --preset win-amd64-release`, with codegen first when the toml changes.
- **Export:** `python tools/stage-editor/export.py <map> --out game\DataFiles\custom_stages\<name>`.
- Then run `tools\Copy Test Copies.bat`, which must also copy `custom_stages\`, and do the tests listed per step.
- **Every step:** check that a vanilla arena and a solo stage still work.
