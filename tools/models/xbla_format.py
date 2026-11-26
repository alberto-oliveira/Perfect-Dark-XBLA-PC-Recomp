# Read and write XBLA's remastered gun meshes (docs/research/gun-models.md, "XBLA meshes"
# and "Static answers"). Shared by xblamesh.py (export, info, test edits) and glb2xbla.py
# (import from Blender).
#
# File layout (big-endian), as the game's files are laid out (no padding):
#   0x00 header (32 bytes): u32 vertex count, vertex offset, index offset, sub-mesh count,
#        sub-mesh offset, bone count (0 = unskinned), f32 (1000.0, meaning unknown: copied),
#        u32 group-table offset
#   0x20 bone matrices: bone count x 48 bytes (3x4 f32, row-major, translation in column 4)
#   group table: per group {u32 first sub-mesh, u32 sub-mesh count, u32 bone}
#                (a GUNDL node links `group << 12 | file id`)
#   sub-mesh table: per sub-mesh {u32 first triangle, u32 triangle count,
#                   u8 env-map index (texture 0xE93 + n), u8 env strength (0 = none),
#                   u16 texture number (bit 15: likely the translucent pass)}
#   vertices: skinned 48 bytes {f32 x,y,z; f32 u,v; f32 nx,ny,nz; u8 a,r,g,b; f32 w0, w1;
#             u8 bone0, bone1, bone2, bone count}; unskinned 36 bytes (no weights/bones)
#   indices: u16, three per triangle, to the end of the file; triangles are CLOCKWISE seen
#            from the front (the game culls the other side; glTF is counter-clockwise)
import struct


def read_mesh(data):
    nv, vo, io, ns, so, nb = struct.unpack('>6I', data[:24])
    scale_f, go = struct.unpack('>fI', data[24:32])
    stride = 48 if nb else 36
    bones = [struct.unpack('>12f', data[32 + i * 48:32 + i * 48 + 48]) for i in range(nb)]
    groups = [struct.unpack('>3I', data[go + i * 12:go + i * 12 + 12]) for i in range((so - go) // 12)]
    subs = []
    for i in range(ns):
        first, count, word = struct.unpack('>3I', data[so + i * 12:so + i * 12 + 12])
        subs.append({'first': first, 'count': count, 'env_map': word >> 24, 'env': (word >> 16) & 0xFF,
                     'texture': word & 0xFFFF})
    verts = []
    for i in range(nv):
        e = vo + i * stride
        x, y, z, u, v, nx, ny, nz = struct.unpack('>8f', data[e:e + 32])
        vert = {'pos': (x, y, z), 'uv': (u, v), 'normal': (nx, ny, nz), 'argb': tuple(data[e + 32:e + 36])}
        if nb:
            w0, w1 = struct.unpack('>2f', data[e + 36:e + 44])
            b0, b1, b2, n = data[e + 44:e + 48]
            vert['bones'], vert['weights'], vert['bone_count'] = (b0, b1, b2), (w0, w1, max(0.0, 1.0 - w0 - w1)), n
        verts.append(vert)
    indices = list(struct.unpack(f'>{(len(data) - io) // 2}H', data[io:]))
    return {'bones': bones, 'scale_f': scale_f, 'groups': groups, 'subs': subs, 'verts': verts,
            'indices': indices, 'skinned': bool(nb)}


def write_mesh(mesh):
    """Bytes of a mesh file from the dict read_mesh returns (same layout as the game's files)."""
    skinned, bones = mesh['skinned'], mesh['bones'] if mesh['skinned'] else []
    stride = 48 if skinned else 36
    go = 32 + 48 * len(bones)
    so = go + 12 * len(mesh['groups'])
    vo = so + 12 * len(mesh['subs'])
    io = vo + stride * len(mesh['verts'])
    out = bytearray(struct.pack('>6IfI', len(mesh['verts']), vo, io, len(mesh['subs']), so, len(bones),
                                mesh['scale_f'], go))
    for m in bones: out += struct.pack('>12f', *m)
    for g in mesh['groups']: out += struct.pack('>3I', *g)
    for s in mesh['subs']:
        out += struct.pack('>3I', s['first'], s['count'], s['env_map'] << 24 | s['env'] << 16 | s['texture'])
    for v in mesh['verts']:
        out += struct.pack('>8f', *v['pos'], *v['uv'], *v['normal']) + bytes(v['argb'])
        if skinned:
            out += struct.pack('>2f', v['weights'][0], v['weights'][1]) + bytes(v['bones']) + bytes([v['bone_count']])
    out += struct.pack(f'>{len(mesh["indices"])}H', *mesh['indices'])
    return bytes(out)
