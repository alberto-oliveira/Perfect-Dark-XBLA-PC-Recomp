# XBLA's remastered gun meshes: what the game actually draws for a gun
# (docs/research/gun-models.md; the format is in xbla_format.py).
#
#   python -I xblamesh.py info  <file_XXXX.bin>
#   python -I xblamesh.py glb   <file_XXXX.bin> -o mesh.glb [--textures <png dir>]
#   python -I xblamesh.py scale <in.bin> <out.bin> <bone> <sx> <sy> <sz>   (a test edit)
#
# The .glb is laid out for Blender and for glb2xbla.py (the way back):
#   - an armature: one joint per N64 matrix, named bone_<n> (the mesh's bone matrices are
#     its inverse bind matrices); the vertices keep their skin weights (up to 3 bones);
#   - one object per group, named g<group>_bone<bone> (a GUNDL node draws one group);
#   - one primitive (material) per sub-mesh, its material named
#     tex_<XXXX>[_e<strength>_m<env map>][_xlu]: the texture number, the reflection strength
#     and env map (texture 0xE93 + m), and the translucent bit;
#   - unskinned meshes (third person) have no armature.
import importlib.util, json, os, struct, sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('xbla_format', os.path.join(_here, 'xbla_format.py'))
xf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xf)


def info(path):
    m = xf.read_mesh(open(path, 'rb').read())
    print(f"{len(m['verts'])} vertices ({'skinned' if m['skinned'] else 'unskinned'}), {len(m['indices']) // 3} "
          f"triangles, {len(m['bones'])} bones, {len(m['groups'])} groups, {len(m['subs'])} sub-meshes")
    for g, (first, count, bone) in enumerate(m['groups']):
        print(f'  group {g}: sub-meshes {first}-{first + count - 1}, bone {bone}')
    for i, s in enumerate(m['subs']):
        print(f"  sub-mesh {i}: triangles {s['first']}-{s['first'] + s['count'] - 1}, texture 0x{s['texture']:04X}, "
              f"reflection {s['env']} (env map 0x{0xE93 + s['env_map']:04X})")


def material_name(s):
    name = f"tex_{s['texture'] & 0x7FFF:04X}"
    if s['env']: name += f"_e{s['env']}_m{s['env_map']}"
    if s['texture'] & 0x8000: name += '_xlu'
    return name


def invert_affine(m):
    """Inverse of a 3x4 row-major affine matrix (rotation/scale + translation)."""
    a = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
    t = [m[3], m[7], m[11]]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    inv = [[(a[(j + 1) % 3][(i + 1) % 3] * a[(j + 2) % 3][(i + 2) % 3] - a[(j + 1) % 3][(i + 2) % 3] * a[(j + 2) % 3][(i + 1) % 3]) / det
            for j in range(3)] for i in range(3)]
    ti = [-sum(inv[r][c] * t[c] for c in range(3)) for r in range(3)]
    return inv, ti


def to_glb(path, out, textures_dir):
    m = xf.read_mesh(open(path, 'rb').read())
    blob, views, accessors = bytearray(), [], []
    def view(data):
        while len(blob) % 4: blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)})
        blob.extend(data)
        return len(views) - 1
    def accessor(data, ctype, count, typ, **extra):
        accessors.append({'bufferView': view(data), 'componentType': ctype, 'count': count, 'type': typ, **extra})
        return len(accessors) - 1

    nodes, meshes, materials, images, textures, material_index = [], [], [], [], [], {}
    def material(s):
        name = material_name(s)
        if name in material_index: return material_index[name]
        mat = {'name': name, 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.6}, 'doubleSided': True}
        png = os.path.join(textures_dir, f"tex_{s['texture'] & 0x7FFF:04X}.png") if textures_dir else None
        if png and os.path.exists(png):
            images.append({'bufferView': view(open(png, 'rb').read()), 'mimeType': 'image/png'})
            textures.append({'source': len(images) - 1, 'sampler': 0})
            mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(textures) - 1}
        if s['texture'] & 0x8000: mat['alphaMode'] = 'BLEND'
        materials.append(mat)
        material_index[name] = len(materials) - 1
        return material_index[name]

    # The armature: joint nodes at the bones' bind positions (inverse of the inverse-bind matrices).
    skin = None
    if m['skinned']:
        joint_nodes = []
        for b, mat in enumerate(m['bones']):
            inv, t = invert_affine(mat)
            matrix = [inv[0][0], inv[1][0], inv[2][0], 0, inv[0][1], inv[1][1], inv[2][1], 0,
                      inv[0][2], inv[1][2], inv[2][2], 0, t[0], t[1], t[2], 1]  # glTF: column-major
            nodes.append({'name': f'bone_{b}', 'matrix': matrix})
            joint_nodes.append(len(nodes) - 1)
        ibm = b''.join(struct.pack('<16f', mt[0], mt[4], mt[8], 0, mt[1], mt[5], mt[9], 0, mt[2], mt[6], mt[10], 0,
                                   mt[3], mt[7], mt[11], 1) for mt in m['bones'])
        skin = {'name': 'armature', 'joints': joint_nodes, 'inverseBindMatrices': accessor(ibm, 5126, len(m['bones']), 'MAT4')}
        nodes.append({'name': 'armature', 'children': joint_nodes})
        armature_node = len(nodes) - 1

    mesh_nodes = []
    for g, (first, count, bone) in enumerate(m['groups']):
        prims = []
        for s in m['subs'][first:first + count]:
            src = m['indices'][s['first'] * 3:(s['first'] + s['count']) * 3]
            local, order = {}, []
            for i in src:
                if i not in local: local[i] = len(order); order.append(i)
            vs = [m['verts'][i] for i in order]
            lo = [min(v['pos'][k] for v in vs) for k in range(3)]; hi = [max(v['pos'][k] for v in vs) for k in range(3)]
            attrs = {
                'POSITION': accessor(b''.join(struct.pack('<3f', *v['pos']) for v in vs), 5126, len(vs), 'VEC3', min=lo, max=hi),
                'NORMAL': accessor(b''.join(struct.pack('<3f', *v['normal']) for v in vs), 5126, len(vs), 'VEC3'),
                'TEXCOORD_0': accessor(b''.join(struct.pack('<2f', *v['uv']) for v in vs), 5126, len(vs), 'VEC2'),
                # ARGB in the file -> RGBA, normalized u8
                'COLOR_0': accessor(b''.join(bytes((v['argb'][1], v['argb'][2], v['argb'][3], v['argb'][0])) for v in vs),
                                    5121, len(vs), 'VEC4', normalized=True),
            }
            if m['skinned']:
                joints, weights = bytearray(), bytearray()
                for v in vs:
                    n = max(1, min(3, v['bone_count']))
                    js, ws = list(v['bones'][:n]) + [0] * (4 - n), list(v['weights'][:n]) + [0.0] * (4 - n)
                    total = sum(ws) or 1.0
                    joints += bytes(js); weights += struct.pack('<4f', *(w / total for w in ws))
                attrs['JOINTS_0'] = accessor(bytes(joints), 5121, len(vs), 'VEC4')
                attrs['WEIGHTS_0'] = accessor(bytes(weights), 5126, len(vs), 'VEC4')
            idx = b''.join(struct.pack('<H', local[i]) for i in src)
            prims.append({'attributes': attrs, 'indices': accessor(idx, 5123, len(src), 'SCALAR'), 'material': material(s)})
        meshes.append({'name': f'g{g}_bone{bone}', 'primitives': prims})
        node = {'name': f'g{g}_bone{bone}', 'mesh': len(meshes) - 1}
        if skin: node['skin'] = 0
        nodes.append(node)
        mesh_nodes.append(len(nodes) - 1)

    scene_nodes = mesh_nodes + ([armature_node] if skin else [])
    gltf = {'asset': {'version': '2.0', 'generator': 'pd xblamesh'}, 'scene': 0, 'scenes': [{'nodes': scene_nodes}],
            'nodes': nodes, 'meshes': meshes, 'materials': materials, 'accessors': accessors, 'bufferViews': views,
            'buffers': [{'byteLength': len(blob)}]}
    if skin: gltf['skins'] = [skin]
    if images: gltf.update(images=images, textures=textures, samplers=[{}])
    js = json.dumps(gltf).encode(); js += b' ' * (-len(js) % 4)
    while len(blob) % 4: blob.append(0)
    with open(out, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(blob)))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(blob), b'BIN\0') + blob)
    print(f"{len(m['groups'])} groups, {len(m['subs'])} sub-meshes, {len(m['indices']) // 3} triangles, "
          f"{len(m['bones'])} bones -> {out}")


def scale(src, dst, bone, sx, sy, sz):
    m = xf.read_mesh(open(src, 'rb').read())
    n = 0
    for v in m['verts']:
        if m['skinned'] and v['bones'][0] != bone: continue
        x, y, z = v['pos']; v['pos'] = (x * sx, y * sy, z * sz); n += 1
    open(dst, 'wb').write(xf.write_mesh(m))
    print(f'scaled {n} vertices of bone {bone} by ({sx}, {sy}, {sz}) -> {dst}')


def env(src, dst, strength):
    """A test edit: every sub-mesh's reflection strength set to `strength` (0 = matte)."""
    m = xf.read_mesh(open(src, 'rb').read())
    for s in m['subs']: s['env'] = strength
    open(dst, 'wb').write(xf.write_mesh(m))
    print(f"{len(m['subs'])} sub-meshes: reflection strength {strength} -> {dst}")


if __name__ == '__main__':
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == 'info': info(a[0])
    elif cmd == 'glb': to_glb(a[0], a[a.index('-o') + 1], a[a.index('--textures') + 1] if '--textures' in a else None)
    elif cmd == 'scale': scale(a[0], a[1], int(a[2]), *map(float, a[3:6]))
    elif cmd == 'env': env(a[0], a[1], int(a[2]))
    else: sys.exit('commands: info, glb, scale, env')
