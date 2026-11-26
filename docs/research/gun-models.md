# Gun models: unpack, edit, repack (feasibility research)

Status: research done, 2026-11-25 (verdict below). Branch `gun-model-research`.

The question: can a gun's model and its textures be taken out of the game, opened in Blender, changed, and put back so the game uses the changed version? This file collects the answers step by step and ends with a verdict.

## Verdict: yes, with the remastered meshes as the target
- **Unpack:** every model file and texture extracts offline (`pdunpack`), and converts to glTF/PNG for Blender: the N64 models (`model2gltf.py`), and XBLA's remastered meshes, which are what the game draws (`xblamesh.py glb`).
- **Repack:** a changed file or texture placed in `game\DataFiles\override` is used instead of the packed one, on every PC (`pd_file_override`). Proven in game on 2 copies:
  - an edited XBLA mesh draws (test C);
  - an edited N64 display list draws when its node's XBLA link is dropped (test B);
  - a PNG made into the HD texture's format replaces it (T6).
- **For custom guns:** model the gun in Blender, then write it into the XBLA mesh format. That format is a plain skinned vertex/index buffer with sub-meshes per texture, so any topology works; give it a texture of its own, and let the N64 file keep the skeleton, parts and animations. What is still missing:
  - the Blender -> XBLA mesh writer: positions, UVs, normals, bone per vertex; sub-mesh flags still to be understood;
  - new file ids, or reusing a borrowed gun's ids, as the custom weapons registry already does for models;
  - mip levels for replaced textures;
  - the third-person mesh: unskinned, own texture;
  - limits: vertex counts, buffer sizes.

## 1. Where the game's files come from

### PackedSegFile (models, setups, stage files...)
- Layout: `u32 count` (2616), then `count` entries of 16 bytes `{offset, size, compressed size, 0xFFFFFFFF}`. File id N = entry N-1.
- 1627 entries are compressed; **989 are stored as is** (compressed size 0).
- Compressed entries use **XMemCompress "native" framing**: each chunk is `FF uu uu cc cc` + `cc` bytes (`uu` = inflated size of the chunk), or `cc cc` + `cc` bytes for a full 0x8000-byte chunk. The chunk payloads, joined together, form one LZX stream with a 128 KB window (17 bits).
- **Offline unpack works.** libmspack's `lzxd.c` (already in `rexglue-sdk/thirdparty/libmspack`) decodes the joined chunks. The crossbow (0x38D, and its low-detail 0x4BA) and remote mine (0x393) files come out **byte-identical** to the runtime dumps. The old runtime dump of 0x360 (third-person crossbow) differs only because it was taken after the game had relocated it.

### Every file load goes through one archive object
The archive object is at `0x82652968`. Its entry table pointer is at `0x82652B24` (entry i at `table + i*16 - 16`), and its file handle is at `0x82652B2C`.

| Function | What it does |
|---|---|
| `sub_822549A0(archive, id)` | returns the inflated size (entry `+4`) |
| `sub_822549C0(archive, id, dest, 0)` | loads the file into `dest`: first a copy from the 24-slot prefetch cache if the file is there, else a seek + read; `sub_822BD2A0` (XMemDecompress) runs only when the entry is compressed |

Callers:
- `file_get_inflated_size` `sub_821971F0` is a tail call to `sub_822549A0`.
- `file_load_to_new` `sub_82197200` and `file_load_to_addr` `sub_82197290` call both.
- The prefetch functions `sub_82254BE0` and `sub_82254F00` call `sub_822549C0`.
- Third-person models: `sub_82196A58` (model load) calls `file_load_to_new`, then `model_promote_type_to_pointer` `sub_82196928`, `model_promote_offsets_to_pointers` `sub_82195498` (base 0x05000000), and `sub_821967A0` (display-list and texture setup).
- First-person guns: the gun loader `sub_82170488` calls `file_load_to_addr` (the method 17 variant).

**So the edit target is the inflated archive file, in the plain N64 modeldef format.** The XBLA conversions (pointer promotion, display-list setup) all run after the load, on whatever bytes the archive handed over. A loose-file override hooked at `sub_822549A0` and `sub_822549C0` covers first-person guns, third-person models and pickups alike.

### Memory budget for a first-person gun
- `sub_821701F0` returns the gun buffer's capacity: **150 KB** (`0x825CD86C` = 0x25800; the 4 MB/2-player value 0x1E000 at `0x825CD9A0` is not used here).
- `player+5600` and `+5604` are POINTERS to the memory pointer and the bytes remaining (N64 `loadmemptr`/`loadmemremaining`), set in `sub_8217B268`.
- Gun files are about 5-12 KB. The texture pool adds `+0xE00`. Textures themselves go to D3D physical memory, not to the gun buffer. **There is roughly 10x headroom** for edited or bigger guns.

## 2. Model format (N64 modeldef, unchanged)
- Header: `{root node, skeleton, parts, s16 numparts, s16 nummatrices, f32 scale, s16 rwdatalen, s16 numtexconfigs, texconfigs}`. Pointers are `0x05xxxxxx` = file offsets.
- Nodes (N64 `types.h`): the first-person crossbow uses POSITION (0x02), TOGGLE (0x12) and GUNDL (0x04).
- Rodata:
  - POSITION: `{coord pos, u16 part, s16 mtxindexes[3]}`.
  - GUNDL: `{opagdl, xlugdl, baseaddr, vertices, s16 numvertices, s16 rendermode}`.
- Vertices: N64 `Vtx` of 12 bytes (`include/PR/gbi.h`: `s16 x,y,z; u8 flags; u8 colour index; s16 s,t`).
- Display lists use VTX `0x04`, MTX `0x01` (vertices skinned to several matrices), PD's TRI4 `0xB1`, COL `0x07` (colour table), TEXTURE `0xBB`, SETGEOM/CLRGEOM `0xB7`/`0xB6`, SETOTHERH `0xBA`, and NOOP `0xC0` (texture slots filled in at load by `sub_821967A0`).
- Texconfigs, 12 bytes each: `{s32 texture number, w, h, level, format, depth, s, t}`. The crossbow has 13 of them (0x329-0x32F, 0x3BA, 0x3C3, 0x3C4, 0x605-0x607, 0x001).

## 3. Textures (Textures.raw)
- Opened by `sub_82262D60` on the texture manager at `0x82655BE0`. The caller `sub_8224B828` then also loads packed file 0x7E5: 5747 x 8 bytes, mostly zeros, so it is not a mapping; probably usage flags.
- Layout:
  - `u32 count` (5747);
  - `count` x 52-byte **4J records** `{data offset, stored w, stored h, N64 w, N64 h, inflated size, compressed size, -1, ?, 0x32, link or -1, 0, u16 +48 flags, u16 +50 state}`;
  - `count` x 52-byte **D3D texture headers** (fetch constant at +28; the manager keeps a pristine copy);
  - then the texel blobs. Data start = `4 + 104*count` = 0x91EBC.
- **Index = texture number.** The crossbow texconfigs' sizes (44x44, 32x48, 32x64, 32x32) match records 0x329-0x32C exactly.
- Formats (D3D header, fetch constant dword 1, bits 0-5):

  | Format | Textures |
  |---|---|
  | 6 = 8888 ARGB, tiled | 3469 |
  | 18 = DXT1 | 1905 |
  | 20 = DXT4/5 | 344 |
  | 19 = DXT2/3 | 29 |

  Some textures are HD: record 0x3 is stored at 128x128 for an N64 48x48, and 0x296 is 256x256 DXT5 with mips. The crossbow's are N64 size, in 8888.
- Each blob is LZX in the same XMem framing and **decodes offline** with the same tool. 0x296 decodes to a coherent grey metal panel (the Mauler flash is texture x vertex colour, so the green comes from the vertex colours). DXT data is stored 16-bit byte-swapped (Xenos endian mode 1).
- State `u16 +50`: 1 = resident, loaded at startup in `sub_82262D60`, inflated into the physical block `+2880`; 2 = requested; 3 = loaded.
- The streaming loader `sub_82263470` runs when the manager's lock count (`+276`) drops to 0. Model loads lock and unlock it around their work (`sub_82196A58`: `sub_82262A90` / `sub_82263470(mgr, 0)`). For each requested texture it:
  - allocates physical memory (`sub_825976C8`, size = record `+20`);
  - inflates the blob into it (`bl sub_822BD2A0` at `0x82263614`; after the call r29 = texels, r30 = index*52);
  - stores the pointer at `+2868[index]`;
  - copies the pristine D3D header back (`+2876` -> `+2872`);
  - calls `sub_823F4398(header, texels)` (XGOffsetResourceAddress: points the fetch constant at the texels);
  - sets state 3.
- **Replacement hook point:** just after the inflate (`0x82263618`), overwrite the texels at r29 with a same-size, same-format image. The resident path's equivalent is `0x82262FF4` (r28 = texels). The copies run `gpu_plugin = "xenos"`, which reads textures from guest memory, so a guest-side swap is seen whichever renderer draws.

## Consequences for the plan
- Step 2 (unpack) needs **no game code**: an offline tool (`tools/models/pdunpack`, C, libmspack `lzxd.c`, built with the LLVM clang) extracts any PackedSegFile entry or Textures.raw texture.
- Step 5 (texture export) is offline too: decode DXT1/3/5 and tiled 8888 (the `tiled_offset` from `src/pd_native_gpu.cpp`) to PNG in Python.
- Step 4 (loose models) hooks the two archive functions, as planned.
- Step 6 (texture replace) hooks after the inflate. A DXT texture needs a DXT encoder; an 8888 texture only needs tiling.
- A second repack route exists, with no hooks: entries can be stored uncompressed, so a rebuilt PackedSegFile with stored entries would load as is. It is not pursued, because it would change the game's data file on every PC; a loose override folder is cleaner.

## 2. Unpack and view (steps 2, 3 and 5: done)
- `tools/models/pdunpack` extracts all 1590 non-empty packed files and all 5747 textures, in a few seconds. The 9 older runtime dumps match byte for byte.
- `tools/models/gunindex.py` lists each gun's hi/lo first-person files (weapondef `+0`/`+2`) and their textures.
- `tools/models/textures.py` writes PNGs (a Python port of the `pd_native_gpu.cpp` decoder). `tools/models/model2gltf.py` writes `.glb` + `.json`. **Test T2 passed** (2026-11-25, Falcon 2, Mauler, crossbow in Blender).
- Display-list details found on the way:
  - texture placeholders are `0xC0` with any of several w0 variants (`C0080002`, `C0080003`, `C0880002`, `C0A80002`, `C0580002`, `C0580004`) and w1 = texture number;
  - a vertex's colour byte is a byte offset into the table the last `0x07` loaded. Lit entries are s8 normals (length ~127); unlit entries are RGBA;
  - texgen (`0x40000`) surfaces are reflection-mapped;
  - the opaque pass ignores texture alpha.
- Parts: 42 magazine, 43 reload magazine (moved with the hand), 90 muzzle flash (4 quads), Falcon 2 45 scope / 47 silencer. All sit under TOGGLE nodes.
- **Gun textures are N64-resolution in XBLA too** (e.g. 0x323 32x32). Only 1469 of the 5747 textures are stored larger than on N64 (e.g. 0x296: 256x256 for 54x54). Record `+40` links 19 textures to others (0xE93-...), so it is not an HD map.

## 3. Loose files in game, and XBLA's own gun meshes (step 4: done)
- **Override (TEMP_GUNMODEL):**
  - `pd_file_override = true` serves `assets\DataFiles\override\file_XXXX.bin` in place of packed file XXXX (`src/pd_file_override.h`). Every test copy's `assets` is a junction to `game\`, so all PCs share the folder.
  - It writes each loose file's size into the archive's entry table, because `sub_82254BE0` allocates by the table size.
  - It copies the file at `sub_822549C0`'s read path (`0x82254A64` -> exit `0x82254BB4`).
  - Hooks: `0x822549A0`, `0x82254BE0` (table), `0x82254A64` (serve).
  - `Run-TestCopies -ExtraArgs pd_file_override` passes the setting to the copies; scenario `gunmodel`.
- **In a match the game uses each gun's LOW-detail first-person file** (weapondef `+2`; Falcon 2 0x39C). The high-detail one (0x377) was only loaded once, before the match.
- **XBLA draws its own remastered meshes, not the N64 display lists.**
  - In the N64 file, each GUNDL node's u16 at `+2` (padding on N64) holds `sub-mesh << 12 | XBLA file id`; `0xFFFF` = none, and `0x0000` was seen on one node, not traced.
  - The model fix-up `sub_82195030` loads that file and registers it (`sub_8226A598`, 128 slots of 28 bytes).
  - Falcon 2: 0x39C -> 0x9E5 (0x377 -> 0x9E4); Mauler 0x379/0x4CB -> 0xA2D/0xA2E; crossbow 0x38D -> 0x9D2; hands 0x4E5 -> 0x9D0.
  - Third-person models do the same: Falcon 2 model 0xF5 = file 0x348 -> 0x842; Mauler model 0xF7 = file 0x34A.
  - Muzzle flashes (part 90) have `0xFFFF`, so they are N64 display lists. That is why the Carom LX's flash colour could be patched in the N64 data.
- **Tests (2 copies, run by Claude, read from logs and screenshots, 2026-11-25):**

  | Test | What was served | Result |
  |---|---|---|
  | T3 | unchanged 0x377 | served, gun identical |
  | A | 0x39C with N64 vertices edited | **no visible change** (the XBLA mesh draws) |
  | B | 0x39C edited + `patch_model.py --n64-render` (node links set to 0xFFFF) | the edited **N64** low-poly Falcon draws in first person; firing, reloading and switching are stable; the third person is unchanged (other file) |
  | C | the XBLA mesh 0x9E5 with bone 33 scaled 1.6 in z (`xblamesh.py scale`) | **the edited HD Falcon draws** |

- **XBLA mesh format** (`tools/models/xblamesh.py`):
  - header `{u32 vertex count, vertex offset, index offset, sub-mesh count, sub-mesh offset, bone count, f32 1000.0, u32 offset of a small table}`, then bone matrices (48 bytes, 3x4);
  - sub-mesh `{first triangle, triangle count, texture number | flags << 16}`;
  - skinned vertex, 48 bytes: `{xyz, uv, normal, ARGB, 2 weights, 3 bone indices + count}`;
  - u16 indices.
  - Positions are model space, at about 1/10 of N64 units.
  - Textures are XBLA HD ones (Falcon 2: 0x14D1, 512x512 DXT1).
  - The meaning of `flags`, the small table at `+0x1C`, and 36-byte unskinned vertices are not yet known.
- **What this means for custom guns:** the remastered mesh is a plain vertex/index buffer, so any Blender mesh can be written into it: new topology and any vertex count, within limits still to be measured. The N64 file stays as the skeleton (joints, parts, matrices) and points to the new mesh.

## 4. Texture replacement (step 6: done)
- With `pd_file_override`, `override\tex_XXXX.bin` (raw texels in the texture's own Xenos layout) is copied over the texture right after the texture manager inflates it:
  - streamed textures: `0x82263618`, r29 = texels, r30 = number*52;
  - resident textures at start-up: `0x82262FF4`, r28;
  - the size cap is read from the record at `*(0x82655BE0 + 2864) + r30 + 20`.
- Only the base level is replaced; the smaller mips keep the old image (visible only at a distance).
- `tools/models/texpack.py` makes the file from a PNG: DXT1/DXT5 with a simple encoder, or tiled 8888; the PNG reader handles all PNG filters. A PNG -> texpack -> textures.py round trip of the Falcon's atlas 0x14D1 matches closely.
- **T6 passed** (2 copies, read from screenshots): a checkerboard on 0x14D1 shows on the first-person Falcon 2 on both PCs.
  - The third-person Falcon is unchanged: it is a different mesh (0x842, unskinned 36-byte vertices) with its own texture 0xF93.
  - Other HD gun textures: Mauler 0x15D7 (mesh 0xA2E), crossbow 0x148A (mesh 0x9D2).

# Phase 2: towards a real custom gun (2026-11-26)

## 5. Static answers (step 1)
- **Group table** (header `+0x1C`, up to the sub-mesh table): `{u32 first sub-mesh, u32 sub-mesh count, u32 bone}` per group. A GUNDL node's link `group << 12 | file id` draws that group.
  - Falcon 2 0x9E5: 5 groups (bones 33/38/41/40/33). Mauler 0xA2E: 3. Third-person 0x842: 1 group, bone 0, unskinned.
  - The draw loop (`sub_82269470`) passes the group's bone to the shader as a float (constants at device `+1920`).
- **Sub-mesh third word** = `{u8 env-map index, u8 env strength, u16 texture}`:
  - `lhz +10` texture number; bit 15 (`0x8000`) is checked against the pass, probably translucent;
  - `lbz +9` strength: when non-zero, a second texture is bound, number `0xE93 + (lbz +8)` (env/reflection maps 0xE93-...), with the strength as a shader value (`sub_82263850`, `sub_8232BB80`);
  - so the "flags" 0x1E-0x3C are reflection strengths: shiny metal = 60 (Falcon 2), 40 (crossbow), 45 (third-person Falcon), 30 (hands), 0 = matte.
- **Mesh memory** (`sub_8226A598`): 128 slots (28 bytes at manager `+124`); vertex buffer = count x stride (`sub_82333BB0`), plus an index buffer.
  - The mesh FILE is temporary: in the gun loader (global `0x8263029C` set) it goes to `sub_82530EB0` memory via `file_load_to_addr`; otherwise (third person) it goes to the stage heap via `file_load_to_new`.
- **Mips**: the Xenos storage rules are documented in the SDK (`rexglue-sdk/include/rex/graphics/pipeline/texture/util.h`, `GetPackedMipOffset`): base at `base_address`, mips from `mip_address`, 32-block alignment, linear rows aligned to 256 bytes, packed tail for levels <= 16 texels.
- **Free numbers**:
  - 1026 empty PackedSegFile entries (e.g. 0x271-0x276) can hold new files.
  - There are no free texture records, so a custom texture must take over a slot that matches don't use.
  - 0x5B is WEAPON_MPSHIELD and 0x5C WEAPON_DISABLED (not free). The proof weapon borrows the solo-only item number 0x50 (the necklace; its name text is already used by the Detonator Crossbow).
