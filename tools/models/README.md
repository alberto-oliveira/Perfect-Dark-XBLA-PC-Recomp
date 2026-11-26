# tools/models

Offline tools for getting gun models and textures out of the game's data files (and, later, back in). Background and addresses: `docs/research/gun-models.md`.

| Tool | What it does |
|---|---|
| `pdunpack\build.bat` | Builds `pdunpack.exe` with the LLVM clang. The LZX decoder comes from the SDK's copy of libmspack. |
| `pdunpack.exe pack <PackedSegFile> <ids\|all> <dir>` | Extracts packed files as `file_XXXX.bin`, inflated and byte-identical to what the game loads (N64 modeldef for models). |
| `pdunpack.exe tex <Textures.raw> <ids\|all> <dir>` | Extracts texture texels as `tex_XXXX.bin`, in the Xenos layout the GPU uses: tiled 8888 or DXT, 16-bit byte-swapped. |
| `gunindex.py <image.bin> <files dir> [-o models.json]` | Lists every gun's high- and low-detail first-person files and their texture numbers. |

`<ids>` is `all` or a list of hex ids and ranges, e.g. `377,38D` or `329-32F`. Packed file ids start at 1 (file N = entry N-1); texture numbers start at 0. The game image is `tools/re/out/image.bin` (made by `tools/re/unxex.py`).

Example (everything takes a few seconds):

```
tools\models\pdunpack\build.bat
mkdir C:\pd-unpack\files C:\pd-unpack\tex
tools\models\pdunpack\pdunpack.exe pack game\DataFiles\PackedSegFile all C:\pd-unpack\files
tools\models\pdunpack\pdunpack.exe tex game\DataFiles\Textures.raw all C:\pd-unpack\tex
python -I tools\models\gunindex.py tools\re\out\image.bin C:\pd-unpack\files -o C:\pd-unpack\models.json
python -I tools\re\modeldump.py C:\pd-unpack\files\file_0377.bin 05000000
```

Keep the extracted files outside the repository: they are the game's data.
