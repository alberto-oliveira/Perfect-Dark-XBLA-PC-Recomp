# The Remote Turret's own model: the Laptop Gun in dark gunmetal with hazard-orange parts,
# and two new parts: a radio antenna on top (with a ball tip) and an emitter dish at the
# muzzle. docs/research/gun-models.md has the formats; make_carom.py is the same idea.
#
#   python -I make_turretgun.py <work dir> --install <override folder> [--data <DataFiles folder>]
#
# What it does (every file comes from the game's data, so each PC makes its own):
#   1. pdunpack: the Laptop Gun's low-detail first-person file 0x4C7 and its mesh 0xA22, the
#      third-person/pickup file 0x358 (model 0x105) and its mesh 0x855, and the textures
#      0x1591 (first person) and 0x0FC4 (third person).
#   2. recolour.py --hazard: light grey parts -> hazard orange, the dark case -> gunmetal.
#   3. The new parts are added to the meshes here, in Python (not through Blender: the parts
#      are simple shapes, and a skinned mesh's bone weights are easier to set directly):
#      each part is a set of triangles in the main group's last sub-mesh, rigid on one bone
#      (first person; the third-person mesh has no bones), with all its texture
#      coordinates on one spot of the texture, so it takes that spot's colour.
#   4. The N64 files are copies of the Laptop Gun's that link the new meshes:
#        first person 0x27B (N64) -> 0x27C (mesh), third person 0x27D -> 0x27E.
#   5. texpack.py --slot: texture slots 0xD05 (first person, like 0x1591) and 0xD06 (third
#      person, like 0x0FC4), with mip levels.
# src/pd_custom_weapons.h gives the Remote Turret these files (0x27B, model 0x19F = file 0x27D).
#
# Mesh conventions this relies on (checked on mesh 0xA22, see check_conventions):
#   - texture coordinates: u right, v UP (texel row = (1 - v) * height);
#   - triangles are clockwise seen from the front: cross(b - a, c - a) points INTO the part.
import math, os, shutil, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xbla_format as xf  # noqa: E402
from glb2xbla import relink_n64  # noqa: E402

FP_N64, FP_MESH, TP_N64, TP_MESH = 0x4C7, 0xA22, 0x358, 0x855
NEW_FP_N64, NEW_FP_MESH, NEW_TP_N64, NEW_TP_MESH = 0x27B, 0x27C, 0x27D, 0x27E
FP_TEX, TP_TEX = 0x1591, 0x0FC4
SLOT_FP, SLOT_TP = 0xD05, 0xD06

# Two spots of the (shared) texture layout, as (u, v): the dark laptop case, which becomes
# gunmetal, and a light grey strip, which becomes hazard orange.
UV_DARK = (100 / 1024, 1 - 150 / 1024)
UV_ORANGE = (998 / 1024, 1 - 400 / 1024)
ARGB = (255, 232, 232, 232)  # the vertex colour most of the gun has (a light grey)

# The parts, in each mesh's own space. First person (mesh 0xA22, ~1/10 N64 units): x side,
# y up, z forward (muzzle at z 41.4, centre x 2.05 y -0.25; the top of the front half at
# y ~3, z 12-18, is bone 38's part). Bones: 33 the body and rear, 34 the barrel (the
# muzzle), 38 the top of the front half. The antenna stands there, where the first-person
# view shows it (a first try on the rear top was out of view). Third person (mesh 0x855,
# ~10x larger): forward is -x (muzzle at x -344, centre y -6 z 101), up +z, y the side.
FP_PARTS = [
    # (shape, bone, uv, args)
    ('rod', 38, UV_DARK, dict(base=(3.0, 2.6, 15.0), axis=(0.15, 1.0, -0.12), length=9.0, radius=0.22)),
    ('ball', 38, UV_ORANGE, dict(centre_along=('rod', 0), radius=0.7)),
    ('dish', 34, UV_ORANGE, dict(centre=(2.05, -0.25, 41.0), axis=(0, 0, 1), r0=1.4, r1=4.2, depth=2.6)),
    ('rod', 34, UV_DARK, dict(base=(2.05, -0.25, 41.0), axis=(0, 0, 1), length=3.6, radius=0.35)),
]
TP_PARTS = [
    ('rod', 0, UV_DARK, dict(base=(-80.0, -12.0, 110.0), axis=(0.12, 0.0, 1.0), length=95.0, radius=2.4)),
    ('ball', 0, UV_ORANGE, dict(centre_along=('rod', 0), radius=7.5)),
    ('dish', 0, UV_ORANGE, dict(centre=(-340.0, -6.0, 101.0), axis=(-1, 0, 0), r0=14.0, r1=40.0, depth=26.0)),
    ('rod', 0, UV_DARK, dict(base=(-340.0, -6.0, 101.0), axis=(-1, 0, 0), length=36.0, radius=3.5)),
]
SIDES = 12  # around a rod, a ball or the dish


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def py(script, *args):
    run(sys.executable, '-I', os.path.join(HERE, script), *args)


# ---- small vector helpers ---------------------------------------------------------------
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def mul(a, s): return tuple(x * s for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    n = math.sqrt(dot(a, a))
    return mul(a, 1 / n) if n else a


def frame(axis):
    """Two unit vectors at right angles to `axis` (and to each other)."""
    a = norm(axis)
    helper = (1, 0, 0) if abs(a[0]) < 0.9 else (0, 1, 0)
    u = norm(cross(a, helper))
    return a, u, cross(a, u)


# ---- shapes: lists of triangles, each (points, normals); add_parts winds them -----------
def rod(base, axis, length, radius):
    a, u, w = frame(axis)
    ring = lambda c: [add(c, add(mul(u, radius * math.cos(2 * math.pi * k / SIDES)),
                                 mul(w, radius * math.sin(2 * math.pi * k / SIDES)))) for k in range(SIDES)]
    top_c = add(base, mul(a, length))
    lo, hi = ring(base), ring(top_c)
    tris = []
    for k in range(SIDES):
        k2 = (k + 1) % SIDES
        n1, n2 = norm(sub(lo[k], base)), norm(sub(lo[k2], base))
        tris.append(((lo[k], lo[k2], hi[k2]), (n1, n2, n2)))
        tris.append(((lo[k], hi[k2], hi[k]), (n1, n2, n1)))
        tris.append(((top_c, hi[k], hi[k2]), (a, a, a)))  # the end cap
    return tris, top_c


def ball(centre, radius):
    rings = SIDES // 2
    pt = lambda i, j: (lambda th, ph: add(centre, (radius * math.sin(th) * math.cos(ph), radius * math.cos(th),
                                                   radius * math.sin(th) * math.sin(ph))))(
        math.pi * i / rings, 2 * math.pi * j / SIDES)
    tris = []
    for i in range(rings):
        for j in range(SIDES):
            p00, p01, p10, p11 = pt(i, j), pt(i, j + 1), pt(i + 1, j), pt(i + 1, j + 1)
            nrm = lambda p: norm(sub(p, centre))
            if i > 0: tris.append(((p00, p01, p11), tuple(nrm(p) for p in (p00, p01, p11))))
            if i < rings - 1: tris.append(((p00, p11, p10), tuple(nrm(p) for p in (p00, p11, p10))))
    return tris


def dish(centre, axis, r0, r1, depth):
    """An open cone from radius r0 at `centre` to r1 at `depth` along `axis`, seen from both
    sides (each triangle twice, once each way)."""
    a, u, w = frame(axis)
    far = add(centre, mul(a, depth))
    ring = lambda c, r: [add(c, add(mul(u, r * math.cos(2 * math.pi * k / SIDES)),
                                    mul(w, r * math.sin(2 * math.pi * k / SIDES)))) for k in range(SIDES)]
    lo, hi = ring(centre, r0), ring(far, r1)
    tris = []
    for k in range(SIDES):
        k2 = (k + 1) % SIDES
        for quad in ((lo[k], lo[k2], hi[k2]), (lo[k], hi[k2], hi[k])):
            n = norm(cross(sub(quad[1], quad[0]), sub(quad[2], quad[0])))
            tris.append((quad, (n, n, n)))
            tris.append(((quad[0], quad[2], quad[1]), (mul(n, -1),) * 3))
    return tris


def build_parts(parts):
    out, ends = [], []
    for shape, bone, uv, args in parts:
        if shape == 'rod':
            tris, end = rod(**args)
            ends.append(end)
        elif shape == 'ball':
            tris = ball(ends[args['centre_along'][1]], args['radius'])
        else:
            tris = dish(**args)
        out.append((bone, uv, tris))
    return out


def add_parts(mesh, parts):
    """Appends the parts' triangles to the main group's (group 0's) last sub-mesh."""
    first_sub, count, _ = mesh['groups'][0]
    target = mesh['subs'][first_sub + count - 1]
    insert_at = (target['first'] + target['count']) * 3  # in the index list
    new_indices = []
    added = 0
    for bone, uv, tris in build_parts(parts):
        for points, normals in tris:
            # Wind it counter-clockwise seen from outside (its normals' side) whatever the
            # shape built, then the game's clockwise below.
            outward = add(add(normals[0], normals[1]), normals[2])
            if dot(cross(sub(points[1], points[0]), sub(points[2], points[0])), outward) < 0:
                points, normals = (points[0], points[2], points[1]), (normals[0], normals[2], normals[1])
            base = len(mesh['verts'])
            for p, n in zip(points, normals):
                v = {'pos': p, 'uv': uv, 'normal': n, 'argb': ARGB}
                if mesh['skinned']:
                    v['bones'], v['weights'], v['bone_count'] = (bone, 0, 0), (1.0, 0.0, 0.0), 1
                mesh['verts'].append(v)
            # The game's winding: clockwise seen from outside, so corners 2 and 3 swap.
            new_indices += [base, base + 2, base + 1]
            added += 1
    mesh['indices'][insert_at:insert_at] = new_indices
    target['count'] += added
    for s in mesh['subs']:
        if s is not target and s['first'] * 3 >= insert_at:
            s['first'] += added
    return added


def check_conventions(mesh):
    """The game's own triangles: most must have cross(b - a, c - a) against their normal."""
    V, I = mesh['verts'], mesh['indices']
    against = sum(1 for t in range(0, len(I), 3)
                  if dot(cross(sub(V[I[t + 1]]['pos'], V[I[t]]['pos']), sub(V[I[t + 2]]['pos'], V[I[t]]['pos'])),
                         V[I[t]]['normal']) < 0)
    if against * 2 < len(I) // 3: sys.exit('this mesh is wound the other way: the new parts would show inside out')


def main():
    a = sys.argv[1:]
    out = a[0]
    opt = lambda name, default=None: a[a.index(name) + 1] if name in a else default
    data_dir = opt('--data', os.path.join(HERE, '..', '..', 'game', 'DataFiles'))
    files, tex, png, game = (os.path.join(out, x) for x in ('files', 'tex', 'png', 'game'))
    for f in (files, tex, png, game): os.makedirs(f, exist_ok=True)
    unpack = os.path.join(HERE, 'pdunpack', 'pdunpack.exe')
    if not os.path.exists(unpack): sys.exit(f'{unpack} is missing: run tools/models/pdunpack/build.bat first')
    raw = os.path.join(data_dir, 'Textures.raw')

    # 1-2: templates, textures, recolour
    run(unpack, 'pack', os.path.join(data_dir, 'PackedSegFile'), f'{FP_N64:X},{FP_MESH:X},{TP_N64:X},{TP_MESH:X}', files)
    run(unpack, 'tex', raw, f'{FP_TEX:X},{TP_TEX:X}', tex)
    py('textures.py', raw, tex, png, f'{FP_TEX:X},{TP_TEX:X}')
    py('recolour.py', os.path.join(png, f'tex_{FP_TEX:04X}.png'), os.path.join(png, 'turret_fp.png'), '--hazard')
    py('recolour.py', os.path.join(png, f'tex_{TP_TEX:04X}.png'), os.path.join(png, 'turret_tp.png'), '--hazard')

    # 3-4: meshes with the new parts, their textures in the slots; N64 files relinked
    f = lambda n: os.path.join(files, f'file_{n:04X}.bin')
    for mesh_id, n64_id, new_mesh, new_n64, slot, donor, parts in (
            (FP_MESH, FP_N64, NEW_FP_MESH, NEW_FP_N64, SLOT_FP, FP_TEX, FP_PARTS),
            (TP_MESH, TP_N64, NEW_TP_MESH, NEW_TP_N64, SLOT_TP, TP_TEX, TP_PARTS)):
        mesh = xf.read_mesh(open(f(mesh_id), 'rb').read())
        check_conventions(mesh)
        before = len(mesh['indices']) // 3
        added = add_parts(mesh, parts)
        for s in mesh['subs']:
            if s['texture'] & 0x7FFF == donor: s['texture'] = (s['texture'] & 0x8000) | slot
        if len(mesh['verts']) > 0xFFFF: sys.exit('too many vertices')
        open(os.path.join(game, f'file_{new_mesh:04X}.bin'), 'wb').write(xf.write_mesh(mesh))
        n64, changed = relink_n64(open(f(n64_id), 'rb').read(), new_mesh, mesh_id)
        open(os.path.join(game, f'file_{new_n64:04X}.bin'), 'wb').write(n64)
        print(f'mesh 0x{new_mesh:04X}: {before} + {added} triangles, texture 0x{slot:04X}; '
              f'N64 file 0x{new_n64:04X}: {changed} links 0x{mesh_id:04X} -> 0x{new_mesh:04X}')

    # 5: texture slots
    for slot, donor, image in ((SLOT_FP, FP_TEX, 'turret_fp.png'), (SLOT_TP, TP_TEX, 'turret_tp.png')):
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
