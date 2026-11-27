# Edit a gun model file in place, to prove the game accepts a changed file (gun model
# research, step 4). It moves or scales the vertices of chosen parts; the file's layout
# is untouched, so this tests the write side of the format without a Blender import.
#
#   python -I patch_model.py <in.bin> <out.bin> [edits...] [--pad BYTES]
#     --scale  PART sx sy sz   multiply the part's vertex coordinates
#     --offset PART dx dy dz   add to the part's vertex coordinates
#     --pad BYTES              append zero bytes (tests how big a file the game takes)
#     --n64-render             make every GUNDL node draw its N64 display list: XBLA keeps a
#                              link to its remastered mesh in node +2 (u16: sub-mesh << 12 |
#                              file id, 0xFFFF = none); 0xFFFF there drops the link
#                              (docs/research/gun-models.md, "XBLA meshes")
#   PART = a part number (as in model2gltf's object names), or "body" for the meshes
#   that belong to no part (the always-drawn gun body).
#
# Vertices are s16 coordinates relative to their joint (see model2gltf.py), so a scale
# stretches a part away from its joint. Each vertex is changed once, even when several
# triangles use it.
import importlib.util, os, struct, sys

spec = importlib.util.spec_from_file_location('model2gltf', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'model2gltf.py'))
model2gltf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model2gltf)


def main():
    args = sys.argv[1:]
    src, dst = args[0], args[1]
    edits, pad, n64_render, i = [], 0, False, 2
    while i < len(args):
        if args[i] == '--n64-render':
            n64_render = True; i += 1
        elif args[i] in ('--scale', '--offset'):
            part = None if args[i + 1] == 'body' else int(args[i + 1])
            edits.append((args[i], part, [float(v) for v in args[i + 2:i + 5]]))
            i += 5
        elif args[i] == '--pad':
            pad = int(args[i + 1]); i += 2
        else:
            sys.exit(f'unknown argument {args[i]}')

    data = bytearray(open(src, 'rb').read())
    meshes, _, _, _ = model2gltf.convert(src, None)
    for kind, part, (a, b, c) in edits:
        offsets = {off for m in meshes if m['part'] == part for p in m['prims'].values() for off in p['src']}
        for off in offsets:
            x, y, z = struct.unpack('>3h', data[off:off + 6])
            if kind == '--scale': x, y, z = x * a, y * b, z * c
            else: x, y, z = x + a, y + b, z + c
            clamp = lambda v: max(-32768, min(32767, int(round(v))))
            struct.pack_into('>3h', data, off, clamp(x), clamp(y), clamp(z))
        print(f'{kind[2:]} part {part if part is not None else "body"} by ({a}, {b}, {c}): {len(offsets)} vertices')
    if n64_render:
        for m in meshes:
            old = struct.unpack('>H', data[m['node'] + 2:m['node'] + 4])[0]
            if old in (0, 0xFFFF): continue  # no XBLA mesh (0 is a state not yet traced: kept)
            struct.pack_into('>H', data, m['node'] + 2, 0xFFFF)
            print(f"node 0x{m['node']:04X}: XBLA mesh link 0x{old:04X} -> 0xFFFF (N64 display list)")
    data += bytes(pad)
    open(dst, 'wb').write(data)
    print(f'{dst}: {len(data)} bytes ({pad} padding)')


main()
