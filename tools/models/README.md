# tools/models

Tools for getting gun models and textures out of the game's data files, and changed ones back in (with `pd_file_override`, see the research file).

**Important:** on XBLA, the N64 model files hold the skeleton and parts, but the game draws a remastered mesh that each GUNDL node links to (`xblamesh.py`). Edit that mesh to change how a gun looks; edits to the N64 display lists show only with `patch_model.py --n64-render`. Background and addresses: `docs/research/gun-models.md`.

| Tool | What it does |
|---|---|
| `pdunpack\build.bat` | Builds `pdunpack.exe` with the LLVM clang. The LZX decoder comes from the SDK's copy of libmspack. |
| `pdunpack.exe pack <PackedSegFile> <ids\|all> <dir>` | Extracts packed files as `file_XXXX.bin`, inflated and byte-identical to what the game loads (N64 modeldef for models). |
| `pdunpack.exe tex <Textures.raw> <ids\|all> <dir>` | Extracts texture texels as `tex_XXXX.bin`, in the Xenos layout the GPU uses: tiled 8888 or DXT, 16-bit byte-swapped. |
| `gunindex.py <image.bin> <files dir> [-o models.json]` | Lists every gun's high- and low-detail first-person files and their texture numbers. |
| `textures.py <Textures.raw> <tex dir> <png dir> [ids\|all] [--opaque]` | Turns `tex_XXXX.bin` into `tex_XXXX.png` (largest mip only), plus `textures.json` with each texture's format and sizes. |
| `model2gltf.py <file_XXXX.bin> -o gun.glb [--textures <png dir>]` | Converts a gun model to glTF for Blender: one object per GUNDL node (named with its part number), one material per texture, and empties for the joints. Also writes `gun.json` with the parts, and the file offset of every exported vertex (for a later re-import). |
| `patch_model.py <in> <out> [--scale\|--offset PART x y z] [--n64-render] [--pad N]` | Edits an N64 model file in place (test edits; `--n64-render` drops the links to XBLA's meshes so the N64 display lists draw). |
| `xblamesh.py info\|glb\|scale ...` | XBLA's remastered gun meshes (the ones the game actually draws): header and sub-meshes, `.glb` export with the HD texture, and a test edit (scale one bone's vertices). |
| `texpack.py <Textures.raw> <number> <image.png\|--checker> -o tex_XXXX.bin` | A PNG at the texture's stored size -> replacement texels for `override\tex_XXXX.bin` (DXT1, DXT5, tiled 8888; base level only). |
| `texpack.py ... --slot <donor>` | Takes over texture slot `<number>` with the donor's format and size (e.g. `--slot 11`, a 256x256 DXT1): also writes `tex_XXXX.hdr`. Mip levels are made unless `--no-mips`. |
| `xbla_format.py` | Reads and writes XBLA mesh files (used by the tools below). |
| `glb2xbla.py <gun.glb> --template-n64 <file> --template-mesh <file> --mesh-id <hex> --file-id <hex> -o <dir>` | A Blender export -> an XBLA mesh file plus a copy of the template N64 model file linked to it. |
| `make_testgun.py <dir>` | The scripted proof gun (`testgun.glb`, `testgun_tp.glb`, `testgun.png`). |
| `recolour.py <in.png> <out.png> --skin\|--flash` | Recolours a texture by hue (metal near-white, green details blue; or a green flash red). |
| `blender/roundtrip.py` | Run by Blender (`blender --background --factory-startup --python roundtrip.py -- in.glb out.glb [--stretch AXIS SIGN CUT FACTOR]`): imports, optionally stretches, exports glTF for `glb2xbla.py`. |
| `make_carom.py <dir> --install <override> [--blender <exe>]` | The Carom LX's own model (white and blue Mauler, red flash in both views, longer front with Blender). |

Notes on what Blender shows:
- The model is in its rest pose. Part 42 is the magazine; part 43 is the reload magazine, which animations move with the hand (on the Mauler it sits beside the gun).
- Objects ending in `_toggle` are parts the game shows only sometimes. Part 90 is the muzzle flash (quads crossing the barrel or silencer); on the Falcon 2, parts 45 and 47 are the scope and the silencer.
- Shiny metal is reflection-mapped (texgen, `0x60000` geometry mode): its material ends in `_reflect`, and its UVs are made from the normals, as a fixed approximation of what the game does per frame.
- Lit surfaces get the game's normals; unlit ones get its vertex colours (`COLOR_0`, e.g. the flash tint).
- Only the translucent pass (`_xlu` materials) uses alpha; PD's opaque pass ignores texture alpha.
- Units are the game's: the Falcon 2 is about 400 units long, and the gun points along +Z.

`<ids>` is `all` or a list of hex ids and ranges, e.g. `377,38D` or `329-32F`. Packed file ids start at 1 (file N = entry N-1); texture numbers start at 0. The game image is `tools/re/out/image.bin` (made by `tools/re/unxex.py`).

Example (everything takes a few seconds):

```
tools\models\pdunpack\build.bat
mkdir C:\pd-unpack\files C:\pd-unpack\tex
tools\models\pdunpack\pdunpack.exe pack game\DataFiles\PackedSegFile all C:\pd-unpack\files
tools\models\pdunpack\pdunpack.exe tex game\DataFiles\Textures.raw all C:\pd-unpack\tex
python -I tools\models\gunindex.py tools\re\out\image.bin C:\pd-unpack\files -o C:\pd-unpack\models.json
python -I tools\re\modeldump.py C:\pd-unpack\files\file_0377.bin 05000000
python -I tools\models\textures.py game\DataFiles\Textures.raw C:\pd-unpack\tex C:\pd-unpack\png all
python -I tools\models\model2gltf.py C:\pd-unpack\files\file_0377.bin -o C:\pd-unpack\falcon2.glb --textures C:\pd-unpack\png
```

Keep the extracted files outside the repository: they are the game's data.

## Making a gun (the workflow proven by the proof gun)

1. **Start from a gun:** `xblamesh.py glb file_09E5.bin -o falcon2.glb --textures <png dir>` (the Falcon 2's first-person mesh; `gunindex.py` and the GUNDL links of the N64 file name the others). In Blender this is an armature (`bone_<n>`) and one object per group (`g<group>_bone<b>`).
2. **Model:** change or replace the objects. Keep the group number at the start of each object's name; a group is a part the game shows or hides (Falcon 2: g0 frame and slide, g1 magazine, g2 reload magazine held by the hand, g3 slide, g4 silencer). Either keep the armature weights, or use rigid objects named `g<group>_bone<n>`: each one moves with bone n. The first-person view is about 1/10 of the N64's units, with the muzzle along +z.
3. **Texture:** name each material `tex_<XXXX>` (+ `_e<0-255>_m0` for shiny metal, the Falcon's is `_e60_m0`). Use a texture slot nobody needs: `texpack.py Textures.raw <slot> gun.png --slot <donor> -o tex_<slot>.bin`, with a donor of the size and format you want (`textures.json` lists them).
4. **Export** glTF 2.0 (.glb) from Blender, then:
   `glb2xbla.py gun.glb --template-n64 file_039C.bin --template-mesh file_09E5.bin --mesh-id 274 --file-id 273 -o out`
   (free ids: any empty PackedSegFile entry, e.g. 0x271-0x276, below 0x1000). Do the same for the third-person mesh with the templates `file_0348.bin` / `file_0842.bin` (unskinned, one group, muzzle along -x, about 8.5x larger).
5. **Serve:** copy `out\*` and the texture files into `game\DataFiles\override\` on every PC, run with `pd_file_override = true`, and give the custom weapon its files in `src/pd_custom_weapons.h` (`model_hi`, `model_lo`, `model_number`, `model_file`). A joined PC logs whether it has the host's files.
