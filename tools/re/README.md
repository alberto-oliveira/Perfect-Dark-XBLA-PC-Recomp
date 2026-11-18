# Reverse-engineering helpers

Small Python scripts for finding things in the game's code and data. They only
read your own files and write where you tell them; keep their output in
`tools/re/out/` (ignored by git), because it is derived from the game.

Needs Python 3 and, for `unxex.py`, the `cryptography` package
(`sudo apt install python3-cryptography` or `pip install cryptography`).

## Setup (once, and again after a new codegen)

```bash
cd tools/re && mkdir -p out
# The game's memory image (code + data), from your extracted default.xex:
python3 -I unxex.py /mnt/f/Workspace/git/pd/game/default.xex out/image.bin
# Every recompiled function's PowerPC instructions in one searchable file:
python3 -I asm_index.py /mnt/f/Workspace/git/pd/generated/default out/asm.txt
```

## The scripts

| Script | What it does |
|---|---|
| `unxex.py <default.xex> <image.bin>` | Decrypts and unpacks a retail XEX into the memory image the game runs from, so guest address `0x82xxxxxx` is at file offset `address - 0x82000000`. Handles the encryption + "basic" compression this game uses (not LZX-compressed XEXs). |
| `asm_index.py <generated/default> <asm.txt>` | Pulls the original instruction comments out of the generated C++: one `== sub_XXXXXXXX` header per function, then its instructions and `loc_` labels. Hook calls show as `[HOOK]`. |
| `fnfind.py <asm.txt> [--max N] <regex>...` | Lists functions (and their length in lines) whose instructions match every regex. `--max` skips longer functions. |
| `img.py` | Imported by your own scripts: `u8/u16/u32/f32(address)`, `find(bytes)` and `cstr(address)` on the image. Set `PD_IMAGE` to `out/image.bin`. |

## Examples

```bash
# Functions that compare an argument with 127 and do float maths (how the handicap curve was found):
python3 -I fnfind.py out/asm.txt --max 80 'cmplwi cr6,r\d+,127$' 'fmuls'

# Everything that calls a function:
grep -n 'bl 0x821cb7f8$' out/asm.txt

# Read the multiplayer weapon table (10 bytes an entry):
PD_IMAGE=out/image.bin python3 -I -c "
import sys; sys.path.insert(0, '.'); from img import *
for i in range(0x27): print(hex(i), d[0x825DC8E8 - B + i*10:0x825DC8E8 - B + i*10 + 10].hex())"
```

Reading the instructions: `lis rX,-32155` loads the high half of an address
(`-32155` is `0x8265` as an unsigned 16-bit value, so `0x82650000`), and the
following `addi`/`lwz` adds a signed low half. `bl 0x...` is a call, and a
function's result comes back in `r3` (`f1` for floats).
