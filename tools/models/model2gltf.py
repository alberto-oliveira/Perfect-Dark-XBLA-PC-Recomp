# Convert a Perfect Dark gun model file (N64 modeldef, as pdunpack extracts it) to glTF
# (.glb) for viewing in Blender, plus a sidecar .json with what glTF cannot hold.
#
#   python -I model2gltf.py <file_XXXX.bin> -o gun.glb [--textures <dir with tex_XXXX.png>]
#
# What the model file looks like (N64 layout; pointers are 0x05000000 + file offset):
#   header   {root node, skeleton, parts, s16 numparts, s16 nummatrices, f32 scale,
#             s16 rwdatalen, s16 numtexconfigs, texconfigs}
#   node     {u16 type, rodata, parent (+8), next (+0x0C), prev (+0x10), child (+0x14)}
#   POSITION (0x02) rodata {f32 pos[3] (relative to the parent joint), u16 part, s16 mtxindex[3]}
#   GUNDL    (0x04) rodata {opa display list, xlu display list, baseaddr, vertices, s16 count}
#   Vtx      12 bytes {s16 x, y, z; u8 flags; u8 colour; s16 s, t}
# Display-list commands used (8 bytes: w0, w1):
#   0x01 MTX   w1 = 0x03000000 + matrix index * 0x40: the following vertices are in that joint's space
#   0x04 VTX   w0 bits 20-23 = count-1, bits 16-19 = first slot; w1 = vertex address
#   0xB1 TRI4  four triangles of 4-bit slot numbers (z in w0, y/x in w1); all-zero = unused
#   0xBF TRI1  one triangle, slots * 10 in w1 bytes 1-3 (N64 F3D)
#   0xC0 NOOP  with w1 != 0: texture placeholder (several w0 variants), w1 = texture number;
#              the game swaps in the real texture load at run time
#   0x07 COL   PD colour table: w1 = address, count = byte 1 / 4 + 1 entries of 4 bytes. A
#              vertex's colour byte is a byte offset into it. With lighting on, the entries
#              are packed normals (s8 x, y, z), else real RGBA vertex colours.
#   0xB7/0xB6  SETGEOM/CLRGEOM: geometry mode bits; 0x20000 lighting, 0x40000 texgen
#              (reflection mapping: the texture follows the view, s/t are not used)
#   0x06 DL    call (or branch) another list; 0xB8 ENDDL
# At rest every joint only translates, so a vertex's model-space position is its value plus
# the absolute position of its joint (the sum of the POSITION offsets up the tree).
import argparse, json, os, struct

BASE = 0x05000000
NODE_POSITION, NODE_GUNDL, NODE_TOGGLE = 0x02, 0x04, 0x12


class Model:
    def __init__(self, data):
        self.d = data

    def u32(self, o): return struct.unpack('>I', self.d[o:o + 4])[0]
    def u16(self, o): return struct.unpack('>H', self.d[o:o + 2])[0]
    def s16(self, o): return struct.unpack('>h', self.d[o:o + 2])[0]
    def off(self, ptr):
        o = ptr - BASE
        return o if 0 <= o < len(self.d) else None


def walk_nodes(m, root):
    """Depth-first list of (node offset, parent offset or None)."""
    out, stack = [], [(m.off(root), None)]
    while stack:
        node, parent = stack.pop()
        while node is not None:
            out.append((node, parent))
            child = m.off(m.u32(node + 0x14))
            nxt = m.off(m.u32(node + 0x0C))
            if child is not None:
                if nxt is not None: stack.append((nxt, parent))
                node, parent = child, node
                continue
            node = nxt
    return out


def convert(path, textures_dir):
    m = Model(open(path, 'rb').read())
    root = m.u32(0)
    numparts = m.s16(12)
    nodes = walk_nodes(m, root)
    parent_of = {n: p for n, p in nodes}
    ntype = {n: m.u16(n) & 0xFF for n, _ in nodes}

    # Part numbers: the parts list is numparts node pointers, then numparts u16 part ids.
    part_of = {}
    parts = m.off(m.u32(8))
    if parts is not None:
        for i in range(numparts):
            node = m.off(m.u32(parts + i * 4))
            part_of[node] = m.u16(parts + numparts * 4 + i * 2)

    # Joints: absolute position of every POSITION node, and which matrix index it owns.
    absolute, joint_of_matrix = {}, {}
    def joint_pos(n):
        if n is None: return (0.0, 0.0, 0.0)
        if n in absolute: return absolute[n]
        base = joint_pos(parent_of[n])
        if ntype[n] == NODE_POSITION:
            r = m.off(m.u32(n + 4))
            x, y, z = struct.unpack('>3f', m.d[r:r + 12])
            absolute[n] = (base[0] + x, base[1] + y, base[2] + z)
            for k in range(3):
                idx = m.s16(r + 14 + k * 2)
                if idx >= 0: joint_of_matrix.setdefault(idx, n)
        else:
            absolute[n] = base
        return absolute[n]
    for n, _ in nodes: joint_pos(n)

    def nearest_joint(n):
        while n is not None and ntype[n] != NODE_POSITION: n = parent_of[n]
        return n

    def nearest_part(n):
        while n is not None and n not in part_of: n = parent_of[n]
        return part_of.get(n)

    # Decode every GUNDL node's display lists into triangles.
    meshes, warnings = [], []
    for n, _ in nodes:
        if ntype[n] != NODE_GUNDL: continue
        r = m.off(m.u32(n + 4))
        opa, xlu = m.u32(r), m.u32(r + 4)
        # One primitive per (texture, pass, lit, texgen): they need different materials/attributes.
        prims = {}
        state = {'joint': nearest_joint(n), 'tex': None, 'slots': [None] * 16, 'geom': 0, 'colours': None,
                 'pass': 'opa'}

        def run(dl, depth=0):
            o = m.off(dl)
            for _ in range(4000):
                if o is None or o + 8 > len(m.d): return
                w0, w1 = m.u32(o), m.u32(o + 4)
                op = w0 >> 24
                if op == 0x01:     # MTX: pick the joint for the next vertices
                    idx = (w1 & 0xFFFFFF) // 0x40
                    if idx in joint_of_matrix: state['joint'] = joint_of_matrix[idx]
                    else: warnings.append(f'matrix {idx} has no joint (node 0x{n:X})')
                elif op == 0x04:   # VTX: load vertices into slots
                    count, first = ((w0 >> 20) & 0xF) + 1, (w0 >> 16) & 0xF
                    vo = m.off(w1)
                    jx, jy, jz = absolute.get(state['joint'], (0.0, 0.0, 0.0))
                    for i in range(count):
                        if vo is None or first + i > 15: break
                        e = vo + i * 12
                        x, y, z = struct.unpack('>3h', m.d[e:e + 6])
                        s, t = struct.unpack('>2h', m.d[e + 8:e + 12])
                        # The colour entry this vertex points at (normal or RGBA, see COL above).
                        colour = None
                        if state['colours'] is not None:
                            c = state['colours'] + m.d[e + 7]
                            if c + 4 <= len(m.d): colour = m.d[c:c + 4]
                        state['slots'][first + i] = ((x + jx, y + jy, z + jz), (s, t), e, colour)
                elif op == 0x07:   # COL: colour table for the next vertex loads
                    state['colours'] = m.off(w1)
                elif op == 0xB7:
                    state['geom'] |= w1
                elif op == 0xB6:
                    state['geom'] &= ~w1
                elif op == 0xC0 and w1:  # texture placeholder
                    state['tex'] = w1
                elif op in (0xB1, 0xBF):
                    if op == 0xB1:
                        tris = [(w1 >> (8 * k) & 0xF, w1 >> (8 * k + 4) & 0xF, w0 >> (4 * k) & 0xF) for k in range(4)]
                        tris = [t for t in tris if t != (0, 0, 0)]
                    else:
                        tris = [((w1 >> 16 & 0xFF) // 10, (w1 >> 8 & 0xFF) // 10, (w1 & 0xFF) // 10)]
                    lit, texgen = bool(state['geom'] & 0x20000), bool(state['geom'] & 0x40000)
                    key = (state['tex'], state['pass'], lit, texgen)
                    p = prims.setdefault(key, {'pos': [], 'uv': [], 'src': [], 'col': []})
                    for tri in tris:
                        verts = [state['slots'][i] for i in tri]
                        if None in verts:
                            warnings.append(f'triangle uses an empty slot (node 0x{n:X})'); continue
                        for v in verts:
                            p['pos'].append(v[0]); p['uv'].append(v[1]); p['src'].append(v[2])
                            p['col'].append(v[3] or b'\x80\x80\x80\xff')
                elif op == 0x06:   # DL: call or branch
                    run(w1, depth + 1)
                    if (w0 >> 16) & 0xFF: return
                elif op == 0xB8:
                    return
                o += 8
        for pass_name, dl in (('opa', opa), ('xlu', xlu)):
            state['pass'] = pass_name
            if dl: run(dl)
        if prims:
            part = nearest_part(n)
            # Under a TOGGLE node = a part the game shows only sometimes (flash, scope, silencer...).
            toggle = any(ntype.get(a) == NODE_TOGGLE for a in ancestors(n, parent_of))
            name = f'gundl_{n:04X}' + (f'_part{part}' if part is not None else '') + ('_toggle' if toggle else '')
            meshes.append({'node': n, 'part': part, 'name': name, 'prims': prims, 'toggle': toggle})

    # Texture sizes (to turn s/t texels into 0-1 UVs) from the texconfigs.
    texsize = {}
    tc = m.off(m.u32(0x18))
    for i in range(max(m.s16(0x16), 0) if tc is not None else 0):
        e = tc + i * 12
        texsize[struct.unpack('>i', m.d[e:e + 4])[0]] = (m.d[e + 4] or 1, m.d[e + 5] or 1)

    joints = [{'node': f'0x{j:04X}', 'part': part_of.get(j), 'position': absolute[j]}
              for j, _ in nodes if ntype[j] == NODE_POSITION]
    return meshes, texsize, joints, warnings


def ancestors(n, parent_of):
    n = parent_of.get(n)
    while n is not None:
        yield n
        n = parent_of.get(n)


def write_glb(out, meshes, texsize, joints, textures_dir):
    """A minimal glTF 2.0 binary: one node per GUNDL mesh, one primitive per texture."""
    blob = bytearray()
    views, accessors, gl_meshes, gl_nodes, materials, images, gl_textures = [], [], [], [], [], [], []
    material_of = {}

    def add_view(data, target=None):
        while len(blob) % 4: blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), **({'target': target} if target else {})})
        blob.extend(data)
        return len(views) - 1

    image_of = {}
    def image(tex):
        """glTF texture index for tex_XXXX.png, or None when there is no PNG."""
        if tex in image_of: return image_of[tex]
        png = os.path.join(textures_dir, f'tex_{tex:04X}.png') if textures_dir and tex is not None else None
        image_of[tex] = None
        if png and os.path.exists(png):
            images.append({'bufferView': add_view(open(png, 'rb').read()), 'mimeType': 'image/png'})
            gl_textures.append({'source': len(images) - 1})
            image_of[tex] = len(gl_textures) - 1
        return image_of[tex]

    def material(tex, pass_name, texgen):
        # The opaque pass ignores texture alpha (PD's combiner decides), so only the
        # translucent (xlu) pass blends. Reflection-mapped surfaces get "_reflect" in the name.
        key = (tex, pass_name, texgen)
        if key in material_of: return material_of[key]
        name = (f'tex_{tex:04X}' if tex is not None else 'untextured') + ('_reflect' if texgen else '') + \
               ('_xlu' if pass_name == 'xlu' else '')
        mat = {'name': name, 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.8},
               'doubleSided': True, 'alphaMode': 'BLEND' if pass_name == 'xlu' else 'OPAQUE'}
        index = image(tex)
        if index is not None: mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': index}
        materials.append(mat)
        material_of[key] = len(materials) - 1
        return material_of[key]

    def normal(c):
        """A lit vertex's colour entry is a packed normal: s8 x, y, z."""
        x, y, z = struct.unpack('3b', bytes(c[:3]))
        length = (x * x + y * y + z * z) ** 0.5
        return (x / length, y / length, z / length) if length else (0.0, 1.0, 0.0)

    for mesh in meshes:
        primitives = []
        for (tex, pass_name, lit, texgen), p in mesh['prims'].items():
            w, h = texsize.get(tex, (32, 32))
            count = len(p['pos'])
            pos = b''.join(struct.pack('<3f', *v) for v in p['pos'])
            normals = [normal(c) for c in p['col']] if lit else None
            if texgen:
                # Reflection mapping: the game derives UVs from the normal and the view. A fixed
                # "looking down -Z" version of it (u, v from the normal's x, y) looks close enough.
                uvs = [(0.5 + nx * 0.5, 0.5 - ny * 0.5) for nx, ny, _ in normals] if normals else [(0.5, 0.5)] * count
            else:
                # s/t are texel coordinates in 10.5 fixed point; glTF wants 0-1 of the texture.
                uvs = [(s / 32.0 / w, t / 32.0 / h) for s, t in p['uv']]
            lo = [min(v[k] for v in p['pos']) for k in range(3)]
            hi = [max(v[k] for v in p['pos']) for k in range(3)]
            accessors.append({'bufferView': add_view(pos, 34962), 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': lo, 'max': hi})
            attributes = {'POSITION': len(accessors) - 1}
            accessors.append({'bufferView': add_view(b''.join(struct.pack('<2f', *uv) for uv in uvs), 34962),
                              'componentType': 5126, 'count': count, 'type': 'VEC2'})
            attributes['TEXCOORD_0'] = len(accessors) - 1
            if normals:
                accessors.append({'bufferView': add_view(b''.join(struct.pack('<3f', *nv) for nv in normals), 34962),
                                  'componentType': 5126, 'count': count, 'type': 'VEC3'})
                attributes['NORMAL'] = len(accessors) - 1
            else:
                # Unlit: the colour entries are real vertex colours (e.g. the Mauler flash's green).
                colours = b''.join(struct.pack('<4f', *(c[k] / 255.0 for k in range(4))) for c in p['col'])
                accessors.append({'bufferView': add_view(colours, 34962), 'componentType': 5126, 'count': count, 'type': 'VEC4'})
                attributes['COLOR_0'] = len(accessors) - 1
            primitives.append({'attributes': attributes, 'material': material(tex, pass_name, texgen)})
        gl_meshes.append({'name': mesh['name'], 'primitives': primitives})
        gl_nodes.append({'name': mesh['name'], 'mesh': len(gl_meshes) - 1})
    # Joints as empties (useful to see where parts and the muzzle sit).
    for j in joints:
        gl_nodes.append({'name': f"joint_{j['node']}" + (f"_part{j['part']}" if j['part'] is not None else ''),
                         'translation': list(j['position'])})

    gltf = {'asset': {'version': '2.0', 'generator': 'pd model2gltf'}, 'scene': 0,
            'scenes': [{'nodes': list(range(len(gl_nodes)))}], 'nodes': gl_nodes, 'meshes': gl_meshes,
            'materials': materials, 'accessors': accessors, 'bufferViews': views,
            'buffers': [{'byteLength': len(blob)}]}
    if images: gltf.update(images=images, textures=gl_textures, samplers=[{}])
    for t in gl_textures: t['sampler'] = 0
    js = json.dumps(gltf).encode()
    js += b' ' * (-len(js) % 4)
    while len(blob) % 4: blob.append(0)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(out, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, total))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js)
        f.write(struct.pack('<I4s', len(blob), b'BIN\0') + blob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--textures', help='folder with tex_XXXX.png (tools/models/textures.py)')
    a = ap.parse_args()
    meshes, texsize, joints, warnings = convert(a.file, a.textures)
    write_glb(a.out, meshes, texsize, joints, a.textures)
    # Sidecar: what glTF can't carry. 'vertex_offsets' gives, per mesh and texture, the file
    # offset of every exported vertex (in glTF order): the key for a later re-import.
    side = {'file': os.path.basename(a.file), 'meshes': [
        {'name': x['name'], 'node': f"0x{x['node']:04X}", 'part': x['part'], 'under_toggle': x['toggle'],
         'primitives': [{'texture': f'0x{k[0]:04X}' if k[0] is not None else None, 'pass': k[1], 'lit': k[2],
                         'texgen': k[3], 'vertex_offsets': p['src']}
                        for k, p in x['prims'].items()]} for x in meshes], 'joints': joints, 'warnings': warnings}
    json.dump(side, open(os.path.splitext(a.out)[0] + '.json', 'w'), indent=1)
    tris = sum(len(p['pos']) // 3 for x in meshes for p in x['prims'].values())
    print(f'{len(meshes)} meshes, {tris} triangles, {len(joints)} joints, {len(warnings)} warnings -> {a.out}')
    for w in sorted(set(warnings))[:10]: print('  warning:', w)


if __name__ == '__main__':
    main()
