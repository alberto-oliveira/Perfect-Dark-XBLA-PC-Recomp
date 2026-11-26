# XBLA's remastered gun meshes (gun model research; docs/research/gun-models.md, "XBLA meshes").
#
#   python -I xblamesh.py info  <file_XXXX.bin>
#   python -I xblamesh.py glb   <file_XXXX.bin> -o mesh.glb [--textures <png dir>]
#   python -I xblamesh.py scale <in.bin> <out.bin> <bone> <sx> <sy> <sz>
#
# Each N64 gun model's GUNDL node links one of these files in node +2 (u16: sub-mesh << 12 |
# file id; 0xFFFF = none), and the game draws this mesh instead of the N64 display list.
# Layout (big-endian; offsets from the file start):
#   +0x00 u32 vertex count        +0x04 u32 vertex data offset   +0x08 u32 index data offset
#   +0x0C u32 sub-mesh count      +0x10 u32 sub-mesh table offset
#   +0x14 u32 bone count (the N64 model's matrix count; 0 = unskinned, 36-byte vertices)
#   +0x18 f32 (1000.0)            +0x1C u32 offset of a small table (not decoded yet)
#   +0x20 bone matrices, 48 bytes each (3x4 f32, row-major, translation in the 4th column)
#   sub-mesh, 12 bytes: {u32 first triangle, u32 triangle count, u32 texture number | flags << 16}
#   vertex, 48 bytes (skinned): {f32 x, y, z; f32 u, v; f32 nx, ny, nz; u8 a, r, g, b;
#                                f32 weight0, weight1; u8 bone0, bone1, bone2, count}
#   indices: u16, three per triangle, to the end of the file
# Units are about 1/10 of the N64 model's.
import json, os, struct, sys


def read(path):
    d = open(path, 'rb').read()
    nv, vo, io, ns, so, nb = struct.unpack('>6I', d[:24])
    stride = 48 if nb else 36
    subs = [struct.unpack('>3I', d[so + i * 12:so + i * 12 + 12]) for i in range(ns)]
    return d, nv, vo, io, subs, nb, stride


def info(path):
    d, nv, vo, io, subs, nb, stride = read(path)
    print(f'{nv} vertices ({stride} bytes) at 0x{vo:X}, {(len(d) - io) // 6} triangles at 0x{io:X}, {nb} bones')
    for i, (first, count, tex) in enumerate(subs):
        print(f'  sub-mesh {i}: triangles {first}-{first + count - 1}, texture 0x{tex & 0xFFFF:04X} flags 0x{tex >> 16:X}')
    bones = {}
    for i in range(nv):
        b = d[vo + i * stride + 44] if stride == 48 else None
        bones[b] = bones.get(b, 0) + 1
    print('  vertices per bone:', bones)


def to_glb(path, out, textures_dir):
    d, nv, vo, io, subs, nb, stride = read(path)
    verts = []
    for i in range(nv):
        e = vo + i * stride
        x, y, z, u, v, nx, ny, nz = struct.unpack('>8f', d[e:e + 32])
        verts.append((x, y, z, u, v, nx, ny, nz))
    idx = struct.unpack(f'>{(len(d) - io) // 2}H', d[io:])
    blob, views, accessors, meshes, nodes, materials, images, textures = bytearray(), [], [], [], [], [], [], []
    def view(data):
        while len(blob) % 4: blob.append(0)
        views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)}); blob.extend(data)
        return len(views) - 1
    tex_material = {}
    for n, (first, count, tex) in enumerate(subs):
        tex &= 0xFFFF
        if tex not in tex_material:
            mat = {'name': f'tex_{tex:04X}', 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.7}, 'doubleSided': True}
            png = os.path.join(textures_dir, f'tex_{tex:04X}.png') if textures_dir else None
            if png and os.path.exists(png):
                images.append({'bufferView': view(open(png, 'rb').read()), 'mimeType': 'image/png'})
                textures.append({'source': len(images) - 1, 'sampler': 0})
                mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': len(textures) - 1}
            materials.append(mat); tex_material[tex] = len(materials) - 1
        tri = [verts[i] for i in idx[first * 3:(first + count) * 3]]
        pos = b''.join(struct.pack('<3f', *t[:3]) for t in tri)
        lo = [min(t[k] for t in tri) for k in range(3)]; hi = [max(t[k] for t in tri) for k in range(3)]
        accessors.append({'bufferView': view(pos), 'componentType': 5126, 'count': len(tri), 'type': 'VEC3', 'min': lo, 'max': hi})
        accessors.append({'bufferView': view(b''.join(struct.pack('<2f', *t[3:5]) for t in tri)), 'componentType': 5126, 'count': len(tri), 'type': 'VEC2'})
        accessors.append({'bufferView': view(b''.join(struct.pack('<3f', *t[5:8]) for t in tri)), 'componentType': 5126, 'count': len(tri), 'type': 'VEC3'})
        a = len(accessors)
        meshes.append({'name': f'sub{n}', 'primitives': [{'attributes': {'POSITION': a - 3, 'TEXCOORD_0': a - 2, 'NORMAL': a - 1}, 'material': tex_material[tex]}]})
        nodes.append({'name': f'sub{n}_tex{tex:04X}', 'mesh': len(meshes) - 1})
    gltf = {'asset': {'version': '2.0', 'generator': 'pd xblamesh'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}],
            'nodes': nodes, 'meshes': meshes, 'materials': materials, 'accessors': accessors, 'bufferViews': views,
            'buffers': [{'byteLength': len(blob)}]}
    if images: gltf.update(images=images, textures=textures, samplers=[{}])
    js = json.dumps(gltf).encode(); js += b' ' * (-len(js) % 4)
    while len(blob) % 4: blob.append(0)
    with open(out, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(blob)))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(blob), b'BIN\0') + blob)
    print(f'{len(subs)} sub-meshes, {len(idx) // 3} triangles -> {out}')


def scale(src, dst, bone, sx, sy, sz):
    d, nv, vo, io, subs, nb, stride = read(src)
    d = bytearray(d); n = 0
    for i in range(nv):
        e = vo + i * stride
        if stride == 48 and d[e + 44] != bone: continue
        x, y, z = struct.unpack('>3f', d[e:e + 12])
        struct.pack_into('>3f', d, e, x * sx, y * sy, z * sz); n += 1
    open(dst, 'wb').write(d)
    print(f'scaled {n} vertices of bone {bone} by ({sx}, {sy}, {sz}) -> {dst}')


if __name__ == '__main__':
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd == 'info': info(a[0])
    elif cmd == 'glb': to_glb(a[0], a[a.index('-o') + 1], a[a.index('--textures') + 1] if '--textures' in a else None)
    elif cmd == 'scale': scale(a[0], a[1], int(a[2]), *map(float, a[3:6]))
    else: sys.exit('commands: info, glb, scale')
