# The Slow Gun's own model: the DY357 Magnum in deep violet with acid-green details, a fatter
# cylinder (a "canister" drum), a longer barrel, and the Mauler's green muzzle flash in both
# views. docs/research/custom-weapons.md, "Slow Gun"; the method is the Carom LX's
# (make_carom.py), except that the mesh edits are done here in Python, straight on the XBLA
# meshes (xbla_format.py), so no Blender is needed.
#
#   python -I make_slowgun.py <work dir> --install <override folder> [--data <DataFiles folder>]
#                             [--bulge 1.3] [--barrel 1.2]
#
# What it does (every file comes from the game's data, so each PC makes its own):
#   1. pdunpack: the Magnum's low-detail first-person file 0x4BD and its mesh 0x9DA, the
#      third-person/pickup file 0x34B and its mesh 0x83C, and the textures 0x14BE (first
#      person) and 0xF7C (third person).
#   2. recolour.py --slow: violet metal, acid-green grip panels and brass rounds.
#   3. The meshes, edited here:
#        - the cylinder is made fatter: in first person it is the vertices that bone 34 moves
#          (the cylinder swings out on that bone when reloading); in third person (one rigid
#          group) it is the band x 8..58 above the grip. Both are scaled out from the
#          cylinder's axis by --bulge (and their normals turned to match);
#        - the barrel is made longer: everything in front of the cylinder moves --barrel times
#          as far from the cylinder's front;
#        - their texture becomes the new texture slot.
#      First person 0x9DA -> 0x280, third person 0x83C -> 0x282.
#   4. Copies of the N64 files that link the new meshes (glb2xbla.relink_n64): first person
#      0x4BD -> 0x27F, third person 0x34B -> 0x281. In them, the muzzle flashes become the
#      Mauler's green ones and move to the longer barrel's end:
#        - first person: the flash (part 90, drawn from the N64 display list) names its texture
#          in the list (`C0580004` + texture number): the Magnum's 0x39B becomes the Mauler's
#          0x85E; the part moves forward (patch_model.py --offset);
#        - third person: its chrgunfire node (the flash quad) uses texconfig[0], which becomes
#          the Mauler's third-person flash texture 0x96F; it moves forward too.
#   5. texpack.py --slot: texture slots 0xD07 (first person, like 0x14BE) and 0xD08 (third
#      person, like 0xF7C), with mip levels.
# src/pd_custom_weapons.h gives the Slow Gun these files (0x27F, model 0x1A1 = file 0x281).
import os, shutil, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xbla_format as xf          # noqa: E402
from glb2xbla import relink_n64   # noqa: E402

FP_N64, FP_MESH, TP_N64, TP_MESH = 0x4BD, 0x9DA, 0x34B, 0x83C
NEW_FP_N64, NEW_FP_MESH, NEW_TP_N64, NEW_TP_MESH = 0x27F, 0x280, 0x281, 0x282
FP_TEX, TP_TEX = 0x14BE, 0xF7C
SLOT_FP, SLOT_TP = 0xD07, 0xD08
MAULER_FP_FLASH, MAULER_TP_FLASH = 0x85E, 0x96F
MAGNUM_FP_FLASH = 0x39B
FLASH_PART = 90
# The grip panels in the texture atlas (both textures share the layout): made acid green.
GRIP_PANELS = ('411', '121', '477', '246')
FP_N64_SCALE = 10.0  # N64 model units per XBLA mesh unit (first person)

# First person (mesh units; the gun points along +z): the cylinder is bone 34's vertices,
# around the axis x = 0, y = 0.56, from z = -15.3 to -10.1; the barrel runs from the
# cylinder's front to the muzzle at z = 6.5.
FP_CYLINDER_BONE = 34
FP_AXIS = (0.0, 0.56)
FP_BARREL_CUT, FP_MUZZLE = -9.5, 6.5
# Third person (unskinned; the gun points along -x, y = width, z = height): the cylinder is
# the band x 8..58 above the grip, around y = -19.5, z = -14.5; the barrel runs from x = -5
# to the muzzle at x = -120.
TP_BAND, TP_ABOVE_Z, TP_AXIS = (8.0, 58.0), -40.0, (-19.5, -14.5)
TP_BARREL_CUT = -5.0


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def py(script, *args):
    run(sys.executable, '-I', os.path.join(HERE, script), *args)


def normalise(n):
    l = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
    return tuple(c / l for c in n) if l > 1e-9 else n


def edit_fp(mesh, bulge, barrel):
    """First person: the fatter cylinder, the longer barrel, the new texture slot."""
    ax, ay = FP_AXIS
    fat = long_ = 0
    for v in mesh['verts']:
        x, y, z = v['pos']
        nx, ny, nz = v['normal']
        main_bone = v['bones'][0] if v['weights'][0] >= 0.5 else v['bones'][1]
        if main_bone == FP_CYLINDER_BONE:
            # Out from the axis in x and y; a normal scales by the inverse, then is normalised.
            x, y = ax + (x - ax) * bulge, ay + (y - ay) * bulge
            nx, ny, nz = normalise((nx / bulge, ny / bulge, nz))
            fat += 1
        elif z > FP_BARREL_CUT:
            z = FP_BARREL_CUT + (z - FP_BARREL_CUT) * barrel
            long_ += 1
        v['pos'], v['normal'] = (x, y, z), (nx, ny, nz)
    for s in mesh['subs']:
        if s['texture'] & 0x7FFF == FP_TEX: s['texture'] = (s['texture'] & 0x8000) | SLOT_FP
    print(f'first person: {fat} cylinder vertices x{bulge}, {long_} barrel vertices x{barrel}')


def edit_tp(mesh, bulge, barrel):
    """Third person: the same, by position (one rigid group)."""
    ay, az = TP_AXIS
    fat = long_ = 0
    for v in mesh['verts']:
        x, y, z = v['pos']
        nx, ny, nz = v['normal']
        if TP_BAND[0] <= x <= TP_BAND[1] and z > TP_ABOVE_Z:
            y, z = ay + (y - ay) * bulge, az + (z - az) * bulge
            nx, ny, nz = normalise((nx, ny / bulge, nz / bulge))
            fat += 1
        elif x < TP_BARREL_CUT:
            x = TP_BARREL_CUT + (x - TP_BARREL_CUT) * barrel
            long_ += 1
        v['pos'], v['normal'] = (x, y, z), (nx, ny, nz)
    for s in mesh['subs']:
        if s['texture'] & 0x7FFF == TP_TEX: s['texture'] = (s['texture'] & 0x8000) | SLOT_TP
    print(f'third person: {fat} cylinder vertices x{bulge}, {long_} barrel vertices x{barrel}')


def green_fp_flash(path):
    """First-person N64 file: the flash's display list names the Magnum's flash texture
    (`C0580004 0000039B`): the Mauler's green one instead."""
    d = bytearray(open(path, 'rb').read())
    old = bytes.fromhex('C0580004') + struct.pack('>I', MAGNUM_FP_FLASH)
    at = d.find(old)
    if at < 0: sys.exit('the first-person flash texture was not found')
    struct.pack_into('>I', d, at + 4, MAULER_FP_FLASH)
    open(path, 'wb').write(d)
    print(f'first-person flash texture 0x{MAGNUM_FP_FLASH:04X} -> 0x{MAULER_FP_FLASH:04X} at 0x{at:X}')


def green_tp_flash(path, barrel):
    """Third-person N64 file: its chrgunfire node (the flash quad) uses texconfig[0]: the
    Mauler's green flash texture; the flash moves to the longer barrel's end."""
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
                struct.pack_into('>i', d, tc, MAULER_TP_FLASH)
                x = struct.unpack('>f', d[rod:rod + 4])[0]
                if x < TP_BARREL_CUT:
                    struct.pack_into('>f', d, rod, TP_BARREL_CUT + (x - TP_BARREL_CUT) * barrel)
                print(f'third-person flash: texture 0x{old:04X} -> 0x{MAULER_TP_FLASH:04X}, x {x:.1f} -> '
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
    bulge, barrel = float(opt('--bulge', '1.3')), float(opt('--barrel', '1.2'))
    files, tex, png, game = (os.path.join(out, x) for x in ('files', 'tex', 'png', 'game'))
    for f in (files, tex, png, game): os.makedirs(f, exist_ok=True)
    unpack = os.path.join(HERE, 'pdunpack', 'pdunpack.exe')
    if not os.path.exists(unpack): sys.exit(f'{unpack} is missing: run tools/models/pdunpack/build.bat first')
    raw = os.path.join(data_dir, 'Textures.raw')

    # 1-2: the game's files, the textures recoloured
    run(unpack, 'pack', os.path.join(data_dir, 'PackedSegFile'), f'{FP_N64:X},{FP_MESH:X},{TP_N64:X},{TP_MESH:X}', files)
    run(unpack, 'tex', raw, f'{FP_TEX:X},{TP_TEX:X}', tex)
    py('textures.py', raw, tex, png, f'{FP_TEX:X},{TP_TEX:X}')
    for name, texture in (('slow_fp.png', FP_TEX), ('slow_tp.png', TP_TEX)):
        py('recolour.py', os.path.join(png, f'tex_{texture:04X}.png'), os.path.join(png, name), '--slow',
           '--green-rect', *GRIP_PANELS)

    # 3: the meshes
    f = lambda n: os.path.join(files, f'file_{n:04X}.bin')
    fp = xf.read_mesh(open(f(FP_MESH), 'rb').read())
    edit_fp(fp, bulge, barrel)
    open(os.path.join(game, f'file_{NEW_FP_MESH:04X}.bin'), 'wb').write(xf.write_mesh(fp))
    tp = xf.read_mesh(open(f(TP_MESH), 'rb').read())
    edit_tp(tp, bulge, barrel)
    open(os.path.join(game, f'file_{NEW_TP_MESH:04X}.bin'), 'wb').write(xf.write_mesh(tp))

    # 4: the N64 files, linking the new meshes; the flashes
    for n64, new_n64, mesh, new_mesh in ((FP_N64, NEW_FP_N64, FP_MESH, NEW_FP_MESH), (TP_N64, NEW_TP_N64, TP_MESH, NEW_TP_MESH)):
        data, changed = relink_n64(open(f(n64), 'rb').read(), new_mesh, mesh)
        open(os.path.join(game, f'file_{new_n64:04X}.bin'), 'wb').write(data)
        print(f'N64 file 0x{new_n64:04X} (from 0x{n64:04X}): {changed} links 0x{mesh:04X} -> 0x{new_mesh:04X}')
    fp_n64 = os.path.join(game, f'file_{NEW_FP_N64:04X}.bin')
    green_fp_flash(fp_n64)
    shift = (FP_MUZZLE - FP_BARREL_CUT) * (barrel - 1) * FP_N64_SCALE  # the muzzle moves this far (N64 units)
    py('patch_model.py', fp_n64, fp_n64, '--offset', str(FLASH_PART), '0', '0', f'{shift:.1f}')
    green_tp_flash(os.path.join(game, f'file_{NEW_TP_N64:04X}.bin'), barrel)

    # 5: texture slots
    for slot, donor, image in ((SLOT_FP, FP_TEX, 'slow_fp.png'), (SLOT_TP, TP_TEX, 'slow_tp.png')):
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
