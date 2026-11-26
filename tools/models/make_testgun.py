# The proof gun for custom gun models (phase 2): a scripted model, built the way a Blender
# export of it would look, so it goes through the same path as a real one (glb2xbla.py).
#
#   python -I make_testgun.py <out dir> [--texture D01]
#   python -I make_testgun.py <out dir> --install <override folder> [--data <DataFiles folder>]
#
# --install also runs the whole pipeline and copies the game files into the override
# folder (e.g. game\DataFiles\override): pdunpack extracts the Falcon 2's templates
# (0x39C, 0x9E5, 0x348, 0x842) from PackedSegFile, glb2xbla.py writes the gun's files
# 0x273-0x276, texpack.py takes over texture slot 0xD01. The files come from the game's
# data, so they are made on each PC rather than shared.
#
# Writes <out dir>/testgun.glb (first person, on the Falcon 2's bones), testgun_tp.glb
# (third person / pickup, one rigid group like the Falcon's 0x842) and testgun.png
# (256x256: orange/teal stripes, "TEST" in pixels, dark grip). Materials are named
# tex_<texture>[_e60_m0], the texture number being a slot taken over with
# texpack.py --slot (see docs/research/gun-models.md).
#
# First-person mesh space (the Falcon 2's mesh 0x9E5): x = side, y = up, z = forward
# (muzzle), about 1/10 of N64 units. The Falcon's groups and bones:
#   g0 bone 33 frame and grip (+ bone 37, a small moving part)   g3 bone 40 slide
#   g1 bone 38 magazine          g2 bone 41 reload magazine (moved with the hand)
#   g4 bone 33 silencer (a part shown only by the silenced Falcon)
# Third-person mesh space (0x842): muzzle along -x, up +z, about 8.5x larger.
import json, os, shutil, struct, subprocess, sys, zlib

# ---- the texture -----------------------------------------------------------------------
FONT = {'T': ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
        'E': ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
        'S': ['01111', '10000', '10000', '01110', '00001', '00001', '11110']}


def texture_rgba():
    size = 256
    img = bytearray(size * size * 4)
    def put(x, y, c):
        o = (y * size + x) * 4; img[o:o + 4] = bytes(c)
    for y in range(size):
        for x in range(size):
            if y < 128:   # metal: orange and teal diagonal stripes
                put(x, y, (240, 140, 30, 255) if ((x + y) // 16) % 2 else (20, 160, 170, 255))
            else:         # grip: dark grey with lighter grooves
                put(x, y, (70, 70, 75, 255) if (y // 8) % 2 else (45, 45, 50, 255))
    # "TEST" on a yellow band, 4x scaled 5x7 letters
    for y in range(40, 88):
        for x in range(20, 236): put(x, y, (250, 220, 40, 255))
    for i, ch in enumerate('TEST'):
        for r, row in enumerate(FONT[ch]):
            for c, bit in enumerate(row):
                if bit == '1':
                    for dy in range(5):
                        for dx in range(5):
                            put(32 + i * 52 + c * 6 + dx, 46 + r * 5 + dy, (20, 20, 20, 255))
    return size, bytes(img)


def write_png(path, size, rgba):
    raw = b''.join(b'\0' + rgba[y * size * 4:(y + 1) * size * 4] for y in range(size))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) + \
        ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b'')
    open(path, 'wb').write(data)
    return data


# ---- shapes: lists of triangles (position, normal, uv) ------------------------------------
METAL, GRIP = (0.0, 0.0, 1.0, 0.5), (0.0, 0.5, 1.0, 1.0)   # uv rectangles in the texture


def box(x0, x1, y0, y1, z0, z1, uv):
    """A box as 12 triangles; each face's texture spans the uv rectangle."""
    u0, v0, u1, v1 = uv
    faces = [((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
             ((-1, 0, 0), [(x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)]),
             ((0, 1, 0), [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)]),
             ((0, -1, 0), [(x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)]),
             ((0, 0, 1), [(x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (x0, y0, z1)]),
             ((0, 0, -1), [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)])]
    tris = []
    for n, q in faces:
        uvs = [(u0, v1), (u0, v0), (u1, v0), (u1, v1)]
        tris.append([(q[0], n, uvs[0]), (q[1], n, uvs[1]), (q[2], n, uvs[2])])
        tris.append([(q[0], n, uvs[0]), (q[2], n, uvs[2]), (q[3], n, uvs[3])])
    return tris


def prism(cx, cy, r, z0, z1, uv, sides=8):
    """A `sides`-sided tube along z (barrel, silencer), capped at both ends."""
    import math
    u0, v0, u1, v1 = uv
    ring = [(cx + r * math.cos(2 * math.pi * i / sides), cy + r * math.sin(2 * math.pi * i / sides)) for i in range(sides)]
    tris = []
    for i in range(sides):
        (ax, ay), (bx, by) = ring[i], ring[(i + 1) % sides]
        mid = 2 * math.pi * (i + 0.5) / sides
        n = (math.cos(mid), math.sin(mid), 0.0)
        ua, ub = u0 + (u1 - u0) * i / sides, u0 + (u1 - u0) * (i + 1) / sides
        tris.append([((ax, ay, z0), n, (ua, v1)), ((bx, by, z0), n, (ub, v1)), ((bx, by, z1), n, (ub, v0))])
        tris.append([((ax, ay, z0), n, (ua, v1)), ((bx, by, z1), n, (ub, v0)), ((ax, ay, z1), n, (ua, v0))])
        for z, nz in ((z0, -1.0), (z1, 1.0)):
            tris.append([((cx, cy, z), (0, 0, nz), (u0, v0)), ((ax, ay, z), (0, 0, nz), (u0, v1)), ((bx, by, z), (0, 0, nz), (u1, v1))])
    return tris


def first_person_parts():
    """{object name: (material suffix, triangles)} in the Falcon 2's first-person mesh space."""
    shiny, matte = '_e60_m0', ''
    return {
        'g0_bone33': (shiny, box(-3.4, 0.4, -9.0, -5.5, -4.0, 17.0, METAL)          # frame
                      + box(-3.2, 0.2, -20.0, -9.0, -4.0, 2.5, GRIP)                # grip
                      + box(-2.0, -1.0, -11.5, -9.0, 2.5, 6.5, METAL)),             # trigger guard
        'g0_bone37': (shiny, box(-2.2, -0.8, -7.0, -4.0, -5.0, -3.5, METAL)),       # hammer
        'g3_bone40': (shiny, box(-3.6, 0.6, -5.5, -0.5, -4.5, 21.0, METAL)          # slide
                      + box(-2.0, -1.0, -0.5, 3.0, 2.0, 15.0, METAL)                # the fin on top
                      + prism(-1.5, -3.0, 1.2, 21.0, 27.0, METAL)),                 # barrel
        'g1_bone38': (matte, box(-2.0, -0.8, -23.0, -9.0, -3.4, 2.0, GRIP)),        # magazine (sticks out)
        'g2_bone41': (matte, box(21.2, 22.4, -25.5, -11.5, 27.2, 32.6, GRIP)),      # reload magazine, at the hand
        'g4_bone33': (shiny, prism(-1.5, -3.0, 1.6, 27.0, 38.0, METAL)),            # silencer (hidden normally)
    }


def to_third_person(tris, k=8.5):
    """First-person mesh space -> the third-person mesh's (muzzle -x, up +z)."""
    out = []
    for tri in tris:
        new = []
        for (x, y, z), (nx, ny, nz), uv in tri:
            new.append(((-(z - 7.9) * k + 11.0, -(x + 1.5) * k + 5.0, (y + 10.55) * k - 28.5), (-nz, -nx, ny), uv))
        out.append(new)
    return out


# ---- glTF writing (plain, unskinned, as a Blender export of rigid objects) ------------------
def write_glb(path, objects, texture, png_bytes):
    blob, views, accessors, meshes, nodes, materials, mat_index = bytearray(), [], [], [], [], [], {}
    def view(data):
        while len(blob) % 4: blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)}); blob.extend(data)
        return len(views) - 1
    image_view = view(png_bytes)
    for name, (suffix, tris) in objects.items():
        mname = f'tex_{texture}{suffix}'
        if mname not in mat_index:
            materials.append({'name': mname, 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0},
                              'metallicFactor': 0.0, 'roughnessFactor': 0.5}, 'doubleSided': True})
            mat_index[mname] = len(materials) - 1
        verts = [v for tri in tris for v in tri]
        pos = b''.join(struct.pack('<3f', *v[0]) for v in verts)
        lo = [min(v[0][i] for v in verts) for i in range(3)]; hi = [max(v[0][i] for v in verts) for i in range(3)]
        accessors.append({'bufferView': view(pos), 'componentType': 5126, 'count': len(verts), 'type': 'VEC3', 'min': lo, 'max': hi})
        accessors.append({'bufferView': view(b''.join(struct.pack('<3f', *v[1]) for v in verts)), 'componentType': 5126, 'count': len(verts), 'type': 'VEC3'})
        accessors.append({'bufferView': view(b''.join(struct.pack('<2f', *v[2]) for v in verts)), 'componentType': 5126, 'count': len(verts), 'type': 'VEC2'})
        accessors.append({'bufferView': view(b''.join(struct.pack('<H', i) for i in range(len(verts)))), 'componentType': 5123, 'count': len(verts), 'type': 'SCALAR'})
        a = len(accessors)
        meshes.append({'name': name, 'primitives': [{'attributes': {'POSITION': a - 4, 'NORMAL': a - 3, 'TEXCOORD_0': a - 2},
                                                      'indices': a - 1, 'material': mat_index[mname]}]})
        nodes.append({'name': name, 'mesh': len(meshes) - 1})
    gltf = {'asset': {'version': '2.0', 'generator': 'pd make_testgun'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
            'nodes': nodes, 'meshes': meshes, 'materials': materials, 'accessors': accessors, 'bufferViews': views,
            'images': [{'bufferView': image_view, 'mimeType': 'image/png'}], 'textures': [{'source': 0, 'sampler': 0}],
            'samplers': [{}], 'buffers': [{'byteLength': len(blob)}]}
    js = json.dumps(gltf).encode(); js += b' ' * (-len(js) % 4)
    while len(blob) % 4: blob.append(0)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(blob)))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(blob), b'BIN\0') + blob)
    print(f'{path}: {len(objects)} objects, {sum(len(t) for _, t in objects.values())} triangles')


def main():
    out = sys.argv[1]
    texture = sys.argv[sys.argv.index('--texture') + 1].upper().rjust(4, '0') if '--texture' in sys.argv else '0D01'
    os.makedirs(out, exist_ok=True)
    size, rgba = texture_rgba()
    png = write_png(os.path.join(out, 'testgun.png'), size, rgba)
    parts = first_person_parts()
    write_glb(os.path.join(out, 'testgun.glb'), parts, texture, png)
    # Third person: everything but the reload magazine and the silencer, as one rigid group.
    tp = [t for name, (_, tris) in parts.items() if name not in ('g2_bone41', 'g4_bone33') for t in tris]
    write_glb(os.path.join(out, 'testgun_tp.glb'), {'g0_bone0': ('_e45_m0', to_third_person(tp))}, texture, png)
    if '--install' in sys.argv:
        install(out, sys.argv[sys.argv.index('--install') + 1], texture,
                sys.argv[sys.argv.index('--data') + 1] if '--data' in sys.argv else None)


def install(out, override, texture, data_dir):
    """The full pipeline: templates out of PackedSegFile, the gun's files, the texture slot."""
    here = os.path.dirname(os.path.abspath(__file__))
    data_dir = data_dir or os.path.join(here, '..', '..', 'game', 'DataFiles')
    run = lambda *args: subprocess.run(list(args), check=True)
    py = lambda script, *args: run(sys.executable, '-I', os.path.join(here, script), *args)
    files = os.path.join(out, 'templates')
    os.makedirs(files, exist_ok=True)
    unpack = os.path.join(here, 'pdunpack', 'pdunpack.exe')
    if not os.path.exists(unpack):
        sys.exit(f'{unpack} is missing: run tools/models/pdunpack/build.bat first')
    run(unpack, 'pack', os.path.join(data_dir, 'PackedSegFile'), '39C,9E5,348,842', files)
    game = os.path.join(out, 'game')
    t = lambda n: os.path.join(files, f'file_{n}.bin')
    py('glb2xbla.py', os.path.join(out, 'testgun.glb'), '--template-n64', t('039C'), '--template-mesh', t('09E5'),
       '--mesh-id', '274', '--file-id', '273', '-o', game)
    py('glb2xbla.py', os.path.join(out, 'testgun_tp.glb'), '--template-n64', t('0348'), '--template-mesh', t('0842'),
       '--mesh-id', '276', '--file-id', '275', '-o', game)
    py('texpack.py', os.path.join(data_dir, 'Textures.raw'), texture, os.path.join(out, 'testgun.png'), '--slot', '11',
       '-o', os.path.join(game, f'tex_{texture}.bin'))
    os.makedirs(override, exist_ok=True)
    for name in sorted(os.listdir(game)):
        shutil.copy(os.path.join(game, name), os.path.join(override, name))
        print('installed', os.path.join(override, name))


main()
