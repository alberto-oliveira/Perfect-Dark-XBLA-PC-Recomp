# A Blender export (glTF) -> an XBLA gun mesh the game draws, plus the N64 model file that
# links to it (docs/research/gun-models.md).
#
#   python -I glb2xbla.py <gun.glb|gun.gltf> --template-n64 <file_XXXX.bin> --template-mesh <file_YYYY.bin>
#                         --mesh-id <hex> --file-id <hex> -o <out dir>
#
# Writes <out dir>/file_<mesh-id>.bin (the mesh) and <out dir>/file_<file-id>.bin (a copy of
# the template N64 file whose GUNDL nodes link the new mesh instead of the old one). Put both
# in the override folder; a custom weapon's definition points at <file-id>.
#
# What the .glb must look like (xblamesh.py glb writes exactly this, so an exported gun can
# be edited and brought back):
#   - objects named g<group>... (e.g. g0_bone33): each group is drawn by the GUNDL node(s)
#     that link it in the template. A group no object names stays empty.
#   - skinning: an armature whose joints are named bone_<n> (N64 matrix n; the template mesh's
#     bone matrices are kept), up to 3 weights per vertex. An object with no skin is rigid:
#     all of it on the bone in its name (_bone<n>), else the template group's bone, with its
#     object transform applied.
#   - materials named tex_<XXXX>[_e<strength>_m<env map>][_xlu]: texture number (hex),
#     reflection strength 0-255 and env map (texture 0xE93 + m), translucent pass.
#     Blender's ".001" suffixes are ignored.
import argparse, importlib.util, json, math, os, re, struct, sys

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('xbla_format', os.path.join(_here, 'xbla_format.py'))
xf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xf)

COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
NODE_GUNDL = 0x04


# ---- glTF reading ----------------------------------------------------------------------

def load_gltf(path):
    data = open(path, 'rb').read()
    if data[:4] == b'glTF':
        jl = struct.unpack('<I', data[12:16])[0]
        gltf = json.loads(data[20:20 + jl])
        rest = data[20 + jl:]
        buffers = [rest[8:8 + struct.unpack('<I', rest[:4])[0]]] if rest else []
    else:
        gltf = json.loads(data)
        buffers = [open(os.path.join(os.path.dirname(path), b['uri']), 'rb').read() for b in gltf.get('buffers', [])]
    return gltf, buffers


def read_accessor(gltf, buffers, index):
    """List of tuples (or floats for SCALAR), normalized integers turned into 0-1 floats."""
    a = gltf['accessors'][index]
    fmt, size = COMPONENT[a['componentType']]
    width = WIDTH[a['type']]
    view = gltf['bufferViews'][a['bufferView']]
    buf = buffers[view.get('buffer', 0)]
    start = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = view.get('byteStride') or size * width
    norm = a.get('normalized') and fmt != 'f'
    scale = {'b': 127.0, 'B': 255.0, 'h': 32767.0, 'H': 65535.0}.get(fmt, 1.0)
    out = []
    for i in range(a['count']):
        vals = struct.unpack_from('<' + fmt * width, buf, start + i * stride)
        if norm: vals = tuple(v / scale for v in vals)
        out.append(vals[0] if width == 1 else vals)
    return out


def node_matrices(gltf):
    """World matrix (column-major 16 floats) of every node."""
    def local(n):
        if 'matrix' in n: return list(n['matrix'])
        t = n.get('translation', [0, 0, 0]); r = n.get('rotation', [0, 0, 0, 1]); s = n.get('scale', [1, 1, 1])
        x, y, z, w = r
        rot = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
               2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
               2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
        return [rot[0] * s[0], rot[1] * s[0], rot[2] * s[0], 0, rot[3] * s[1], rot[4] * s[1], rot[5] * s[1], 0,
                rot[6] * s[2], rot[7] * s[2], rot[8] * s[2], 0, t[0], t[1], t[2], 1]
    def mul(a, b):
        return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]
    nodes = gltf.get('nodes', [])
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []): parent[c] = i
    world = {}
    def get(i):
        if i not in world:
            m = local(nodes[i])
            world[i] = mul(get(parent[i]), m) if i in parent else m
        return world[i]
    return [get(i) for i in range(len(nodes))]


def apply(m, p, w=1.0):
    return tuple(m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * w for r in range(3))


# ---- building the mesh -------------------------------------------------------------------

def parse_material(name):
    name = re.sub(r'\.\d{3}$', '', name or '')
    m = re.match(r'tex_([0-9A-Fa-f]{1,4})', name)
    if not m: return None
    texture = int(m.group(1), 16)
    env = re.search(r'_e(\d+)', name); env_map = re.search(r'_m(\d+)', name)
    return {'texture': texture | (0x8000 if '_xlu' in name else 0), 'env': min(255, int(env.group(1))) if env else 0,
            'env_map': min(255, int(env_map.group(1))) if env_map else 0}


def build(gltf, buffers, template_mesh):
    tm = template_mesh
    worlds = node_matrices(gltf)
    skins = gltf.get('skins', [])
    group_count = len(tm['groups'])
    # Per group: list of (material, triangles of vertex tuples)
    per_group = {g: [] for g in range(group_count)}
    warnings = []
    for ni, node in enumerate(gltf.get('nodes', [])):
        if 'mesh' not in node: continue
        name = re.sub(r'\.\d{3}$', '', node.get('name', ''))
        gm = re.match(r'g(\d+)', name)
        if not gm:
            warnings.append(f'object "{name}" is not named g<group>...: skipped'); continue
        g = int(gm.group(1))
        if g >= group_count:
            warnings.append(f'object "{name}": the template has only {group_count} groups: skipped'); continue
        bm = re.search(r'_bone(\d+)', name)
        rigid_bone = int(bm.group(1)) if bm else tm['groups'][g][2]
        skin = skins[node['skin']] if 'skin' in node else None
        joint_bone = []
        if skin:
            for j in skin['joints']:
                jn = re.search(r'bone_(\d+)', gltf['nodes'][j].get('name', ''))
                joint_bone.append(int(jn.group(1)) if jn else rigid_bone)
        world = worlds[ni]
        for prim in gltf['meshes'][node['mesh']]['primitives']:
            if prim.get('mode', 4) != 4:
                warnings.append(f'object "{name}": a primitive is not triangles: skipped'); continue
            at = prim['attributes']
            pos = read_accessor(gltf, buffers, at['POSITION'])
            nrm = read_accessor(gltf, buffers, at['NORMAL']) if 'NORMAL' in at else None
            uv = read_accessor(gltf, buffers, at['TEXCOORD_0']) if 'TEXCOORD_0' in at else [(0.0, 0.0)] * len(pos)
            col = read_accessor(gltf, buffers, at['COLOR_0']) if 'COLOR_0' in at else None
            joints = read_accessor(gltf, buffers, at['JOINTS_0']) if skin and 'JOINTS_0' in at else None
            weights = read_accessor(gltf, buffers, at['WEIGHTS_0']) if skin and 'WEIGHTS_0' in at else None
            idx = read_accessor(gltf, buffers, prim['indices']) if 'indices' in prim else list(range(len(pos)))
            mat_name = gltf['materials'][prim['material']]['name'] if 'material' in prim else ''
            mat = parse_material(mat_name)
            if mat is None:
                warnings.append(f'object "{name}": material "{mat_name}" is not named tex_XXXX: texture 0 used')
                mat = {'texture': 0, 'env': 0, 'env_map': 0}
            verts = []
            for i in range(len(pos)):
                if skin:  # skinned: positions are already in bind (model) space
                    p, n = pos[i], nrm[i] if nrm else (0.0, 1.0, 0.0)
                else:     # rigid: the object's transform applies
                    p = apply(world, pos[i]); n = apply(world, nrm[i], 0.0) if nrm else (0.0, 1.0, 0.0)
                ln = math.sqrt(sum(c * c for c in n)) or 1.0
                if abs(ln - 1.0) > 1e-3: n = tuple(c / ln for c in n)  # unit normals stay exactly as given
                c = col[i] if col else (1.0, 1.0, 1.0, 1.0)
                if len(c) == 3: c = (*c, 1.0)
                argb = tuple(int(round(max(0.0, min(1.0, v)) * 255)) for v in (c[3], c[0], c[1], c[2]))
                if joints and weights:
                    pairs = sorted(((weights[i][k], joint_bone[int(joints[i][k])]) for k in range(4) if weights[i][k] > 0),
                                   reverse=True)[:3] or [(1.0, rigid_bone)]
                    total = sum(w for w, _ in pairs)
                    pairs = [(w / total, b) for w, b in pairs]
                    bones = [b for _, b in pairs] + [pairs[0][1]] * (3 - len(pairs))
                    ws = [w for w, _ in pairs] + [0.0] * (3 - len(pairs))
                    skin_data = (tuple(bones), (ws[0], ws[1]), len(pairs))
                else:
                    skin_data = ((rigid_bone,) * 3, (1.0, 0.0), 1)
                verts.append((tuple(p), tuple(uv[i]), n, argb, skin_data))
            # glTF triangles are counter-clockwise seen from the front, the game's clockwise
            # (it culls the other side): each triangle's last two corners swap.
            tris = [tuple(verts[idx[k + j]] for j in (0, 2, 1)) for k in range(0, len(idx) - 2, 3)]
            per_group[g].append((mat, tris))
    return per_group, warnings


def make_mesh(per_group, template_mesh, unskinned):
    tm = template_mesh
    mesh = {'bones': [] if unskinned else tm['bones'], 'scale_f': tm['scale_f'], 'groups': [], 'subs': [],
            'verts': [], 'indices': [], 'skinned': not unskinned}
    vindex = {}
    for g in range(len(tm['groups'])):
        first_sub = len(mesh['subs'])
        for mat, tris in per_group[g]:
            sub = {'first': len(mesh['indices']) // 3, 'count': len(tris), **mat}
            for tri in tris:
                for v in tri:
                    key = v if not unskinned else v[:4]
                    if key not in vindex:
                        vindex[key] = len(mesh['verts'])
                        p, uv, n, argb, (bones, ws, count) = v
                        vert = {'pos': p, 'uv': uv, 'normal': n, 'argb': argb}
                        if not unskinned: vert.update(bones=bones, weights=(ws[0], ws[1], 0.0), bone_count=count)
                        mesh['verts'].append(vert)
                    mesh['indices'].append(vindex[key])
            mesh['subs'].append(sub)
        mesh['groups'].append((first_sub, len(mesh['subs']) - first_sub, tm['groups'][g][2]))
    return mesh


def relink_n64(data, mesh_id, old_mesh_id):
    """A copy of the N64 model file whose GUNDL links to old_mesh_id point at mesh_id."""
    out = bytearray(data)
    base = 0x05000000
    u32 = lambda o: struct.unpack('>I', out[o:o + 4])[0]
    seen, changed, stack = set(), 0, [u32(0)]
    while stack:
        p = stack.pop()
        while p and p not in seen and 0 <= p - base < len(out):
            seen.add(p); o = p - base
            typ, link = struct.unpack('>HH', out[o:o + 4])
            if typ & 0xFF == NODE_GUNDL and link not in (0, 0xFFFF) and link & 0xFFF == old_mesh_id:
                struct.pack_into('>H', out, o + 2, (link & 0xF000) | mesh_id); changed += 1
            child = u32(o + 0x14)
            if child: stack.append(child)
            p = u32(o + 0x0C)
    return bytes(out), changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gltf')
    ap.add_argument('--template-n64', required=True, help='the N64 model file to copy (skeleton, parts, links)')
    ap.add_argument('--template-mesh', required=True, help='the XBLA mesh it links (bone matrices, groups)')
    ap.add_argument('--mesh-id', required=True, help='file id (hex) for the new mesh')
    ap.add_argument('--file-id', required=True, help='file id (hex) for the new N64 file')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    mesh_id, file_id = int(a.mesh_id, 16), int(a.file_id, 16)
    if mesh_id > 0xFFF: sys.exit('the mesh id must fit in 12 bits (a GUNDL link is group << 12 | file id)')
    template_mesh = xf.read_mesh(open(a.template_mesh, 'rb').read())
    old_mesh_id = int(re.search(r'file_([0-9A-Fa-f]+)', os.path.basename(a.template_mesh)).group(1), 16)
    gltf, buffers = load_gltf(a.gltf)
    per_group, warnings = build(gltf, buffers, template_mesh)
    mesh = make_mesh(per_group, template_mesh, unskinned=not template_mesh['skinned'])
    if len(mesh['verts']) > 0xFFFF: sys.exit(f"{len(mesh['verts'])} vertices: the limit is 65535 (u16 indices)")
    n64, changed = relink_n64(open(a.template_n64, 'rb').read(), mesh_id, old_mesh_id)
    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, f'file_{mesh_id:04X}.bin'), 'wb').write(xf.write_mesh(mesh))
    open(os.path.join(a.out, f'file_{file_id:04X}.bin'), 'wb').write(n64)
    for w in warnings: print('warning:', w)
    print(f"mesh 0x{mesh_id:04X}: {len(mesh['verts'])} vertices, {len(mesh['indices']) // 3} triangles, "
          f"{len(mesh['subs'])} sub-meshes in {len(mesh['groups'])} groups; N64 file 0x{file_id:04X}: {changed} links "
          f"0x{old_mesh_id:04X} -> 0x{mesh_id:04X}")


if __name__ == '__main__':
    main()
