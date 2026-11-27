# The Carom LX's own model: the Mauler, white with blue details, a red muzzle flash in
# both views, and (with Blender) a front 60% longer. docs/research/gun-models.md,
# "Carom LX skin".
#
#   python -I make_carom.py <work dir> --install <override folder> [--blender <blender.exe>]
#                           [--data <DataFiles folder>] [--details-hue 215] [--white 0.55]
#
# What it does (every file comes from the game's data, so each PC makes its own):
#   1. pdunpack: the Mauler's low-detail first-person file 0x4CB and its mesh 0xA2E, the
#      third-person/pickup file 0x34A and its mesh 0x850, and the textures 0x15D7 (first
#      person), 0xFB5 (third person) and 0x96F (third-person flash).
#   2. recolour.py: metal near-white, green details blue; the flash red.
#   3. xblamesh.py glb, with the materials renamed to the texture slots; Blender (if given)
#      stretches the front; glb2xbla.py writes the meshes and relinked N64 copies:
#        first person 0x277 (N64) -> 0x278 (mesh), third person 0x279 -> 0x27A.
#   4. In the N64 copies: the first-person flash's vertex colour becomes red (and the flash
#      moves with the longer front); the third-person flash (a chrgunfire node) uses the
#      red flash texture, at the new muzzle.
#   5. texpack.py --slot: texture slots 0xD02 (first person, like 0x15D7), 0xD03 (third
#      person, like 0xFB5), 0xD04 (flash, like 0x96F), with mip levels.
# src/pd_custom_weapons.h gives the Carom LX these files (0x277, model 0x116 = file 0x279).
import json, os, re, shutil, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FP_N64, FP_MESH, TP_N64, TP_MESH = 0x4CB, 0xA2E, 0x34A, 0x850
NEW_FP_N64, NEW_FP_MESH, NEW_TP_N64, NEW_TP_MESH = 0x277, 0x278, 0x279, 0x27A
FP_TEX, TP_TEX, FLASH_TEX = 0x15D7, 0xFB5, 0x96F
SLOT_FP, SLOT_TP, SLOT_FLASH = 0xD02, 0xD03, 0xD04
RED = 0xFF3030FF            # the first-person flash's vertex colour (was 0xFEFEFEFF)
# The longer front (Blender): first person, everything in front of z = 14 (mesh units, the
# front fifth) 60% longer; third person (muzzle along -x), the same fraction: x < -113.
STRETCH = 1.6
FP_CUT, TP_CUT = 14.0, -113.0
FP_N64_SCALE = 10.0         # N64 model units per XBLA mesh unit (first person)
FLASH_PART = 90             # the first-person flash part in the N64 file


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def py(script, *args):
    run(sys.executable, '-I', os.path.join(HERE, script), *args)


def rename_materials(src, dst, mapping):
    """A .glb with its materials' texture numbers changed (tex_15D7 -> tex_0D02...)."""
    d = open(src, 'rb').read()
    jl = struct.unpack('<I', d[12:16])[0]
    g = json.loads(d[20:20 + jl])
    rest = d[20 + jl:]
    for m in g.get('materials', []):
        m['name'] = re.sub(r'tex_([0-9A-Fa-f]{4})', lambda x: 'tex_%04X' % mapping.get(int(x.group(1), 16), int(x.group(1), 16)), m['name'])
    js = json.dumps(g).encode()
    js += b' ' * (-len(js) % 4)
    open(dst, 'wb').write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + len(rest)) +
                          struct.pack('<I4s', len(js), b'JSON') + js + rest)


def red_flash(path):
    """First-person N64 file: the flash's one vertex colour {FEFEFEFF, 0, 05xxxxxx} -> red."""
    d = bytearray(open(path, 'rb').read())
    for o in range(0, len(d) - 12, 4):
        if d[o:o + 8] == bytes.fromhex('FEFEFEFF00000000') and d[o + 8] == 0x05:
            struct.pack_into('>I', d, o, RED)
            open(path, 'wb').write(d)
            print(f'flash colour at 0x{o:X} -> {RED:08X}')
            return
    sys.exit('the first-person flash colour was not found')


def third_person_flash(path, slot, stretched):
    """Third-person N64 file: its chrgunfire node (the flash quad) uses texconfig[0]; that
    texture becomes the red flash slot, and with a longer front the flash moves forward."""
    d = bytearray(open(path, 'rb').read())
    base = 0x05000000
    u32 = lambda o: struct.unpack('>I', d[o:o + 4])[0]
    seen, stack, done = set(), [u32(0)], False
    while stack:
        p = stack.pop()
        while p and p not in seen and 0 <= p - base < len(d):
            seen.add(p); o = p - base
            if struct.unpack('>H', d[o:o + 2])[0] & 0xFF == 0x0C:  # chrgunfire
                rod = u32(o + 4) - base
                tc = u32(rod + 24) - base
                old = struct.unpack('>i', d[tc:tc + 4])[0]
                struct.pack_into('>i', d, tc, slot)
                x = struct.unpack('>f', d[rod:rod + 4])[0]
                if stretched and x < TP_CUT:
                    struct.pack_into('>f', d, rod, TP_CUT + (x - TP_CUT) * STRETCH)
                print(f'third-person flash: texture 0x{old:04X} -> 0x{slot:04X}, x {x:.1f} -> '
                      f'{struct.unpack(">f", d[rod:rod + 4])[0]:.1f}')
                done = True
            child = u32(o + 0x14)
            if child: stack.append(child)
            p = u32(o + 0x0C)
    if not done: sys.exit('no chrgunfire node in the third-person file')
    open(path, 'wb').write(d)


def main():
    a = sys.argv[1:]
    out = a[0]
    opt = lambda name, default=None: a[a.index(name) + 1] if name in a else default
    data_dir = opt('--data', os.path.join(HERE, '..', '..', 'game', 'DataFiles'))
    blender = opt('--blender')
    files, tex, png, game = (os.path.join(out, x) for x in ('files', 'tex', 'png', 'game'))
    for f in (files, tex, png, game): os.makedirs(f, exist_ok=True)
    unpack = os.path.join(HERE, 'pdunpack', 'pdunpack.exe')
    if not os.path.exists(unpack): sys.exit(f'{unpack} is missing: run tools/models/pdunpack/build.bat first')
    raw = os.path.join(data_dir, 'Textures.raw')

    # 1-2: templates, textures, recolour
    run(unpack, 'pack', os.path.join(data_dir, 'PackedSegFile'), f'{FP_N64:X},{FP_MESH:X},{TP_N64:X},{TP_MESH:X}', files)
    run(unpack, 'tex', raw, f'{FP_TEX:X},{TP_TEX:X},{FLASH_TEX:X}', tex)
    py('textures.py', raw, tex, png, f'{FP_TEX:X},{TP_TEX:X},{FLASH_TEX:X}')
    skin = ['--skin', '--details-hue', opt('--details-hue', '215'), '--white', opt('--white', '0.55')]
    py('recolour.py', os.path.join(png, f'tex_{FP_TEX:04X}.png'), os.path.join(png, 'carom_fp.png'), *skin)
    py('recolour.py', os.path.join(png, f'tex_{TP_TEX:04X}.png'), os.path.join(png, 'carom_tp.png'), *skin)
    py('recolour.py', os.path.join(png, f'tex_{FLASH_TEX:04X}.png'), os.path.join(png, 'carom_flash.png'), '--flash')

    # 3: meshes (Blender for the longer front)
    f = lambda n: os.path.join(files, f'file_{n:04X}.bin')
    for name, mesh, mapping in (('fp', FP_MESH, {FP_TEX: SLOT_FP}), ('tp', TP_MESH, {TP_TEX: SLOT_TP})):
        py('xblamesh.py', 'glb', f(mesh), '-o', os.path.join(out, f'{name}_mauler.glb'))
        rename_materials(os.path.join(out, f'{name}_mauler.glb'), os.path.join(out, f'{name}.glb'), mapping)
        if blender:
            args = ['z', '+1', str(FP_CUT), str(STRETCH)] if name == 'fp' else ['x', '-1', str(TP_CUT), str(STRETCH)]
            run(blender, '--background', '--factory-startup', '--python', os.path.join(HERE, 'blender', 'roundtrip.py'),
                '--', os.path.join(out, f'{name}.glb'), os.path.join(out, f'{name}_edited.glb'), '--stretch', *args)
    src = lambda name: os.path.join(out, f'{name}_edited.glb' if blender else f'{name}.glb')
    py('glb2xbla.py', src('fp'), '--template-n64', f(FP_N64), '--template-mesh', f(FP_MESH),
       '--mesh-id', f'{NEW_FP_MESH:X}', '--file-id', f'{NEW_FP_N64:X}', '-o', game)
    py('glb2xbla.py', src('tp'), '--template-n64', f(TP_N64), '--template-mesh', f(TP_MESH),
       '--mesh-id', f'{NEW_TP_MESH:X}', '--file-id', f'{NEW_TP_N64:X}', '-o', game)

    # 4: the flashes
    fp_n64 = os.path.join(game, f'file_{NEW_FP_N64:04X}.bin')
    red_flash(fp_n64)
    if blender:  # the first-person flash part moves with the longer front (N64 units)
        shift = (20.8 - FP_CUT) * (STRETCH - 1) * FP_N64_SCALE  # 20.8: the mesh's front end
        py('patch_model.py', fp_n64, fp_n64, '--offset', str(FLASH_PART), '0', '0', f'{shift:.1f}')
    third_person_flash(os.path.join(game, f'file_{NEW_TP_N64:04X}.bin'), SLOT_FLASH, bool(blender))

    # 5: texture slots
    for slot, donor, image in ((SLOT_FP, FP_TEX, 'carom_fp.png'), (SLOT_TP, TP_TEX, 'carom_tp.png'),
                               (SLOT_FLASH, FLASH_TEX, 'carom_flash.png')):
        py('texpack.py', raw, f'{slot:X}', os.path.join(png, image), '--slot', f'{donor:X}',
           '-o', os.path.join(game, f'tex_{slot:04X}.bin'))

    if '--install' in a:
        override = opt('--install')
        os.makedirs(override, exist_ok=True)
        for name in sorted(os.listdir(game)):
            shutil.copy(os.path.join(game, name), os.path.join(override, name))
            print('installed', os.path.join(override, name))


if __name__ == '__main__':
    main()
