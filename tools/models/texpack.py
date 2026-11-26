# Make a replacement texture for pd_file_override: a PNG -> tex_XXXX.bin, in the texture's
# own Xenos layout (format, tiling, endian and size from Textures.raw), base level only.
#
#   python -I texpack.py <Textures.raw> <number hex> <image.png | --checker> -o tex_XXXX.bin
#
# The PNG must have the texture's stored size (textures.py / textures.json list it).
# Formats: 18 DXT1, 20 DXT5 (a simple bounding-box encoder: fine for tests and flat art,
# not the best quality), 6 8888 (tiled). This is the inverse of textures.py.
import os, struct, sys, zlib

import importlib.util

# textures.py holds the Xenos details (formats, tiling, endian swaps); this is its inverse.
_spec = importlib.util.spec_from_file_location('textures', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'textures.py'))
textures = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(textures)


def read_png(path):
    """RGBA rows of an 8-bit RGB or RGBA PNG (non-interlaced), with PNG filters undone."""
    d = open(path, 'rb').read()
    i, idat, w, h, ctype = 8, b'', 0, 0, 0
    while i < len(d):
        n = struct.unpack('>I', d[i:i + 4])[0]; t = d[i + 4:i + 8]; c = d[i + 8:i + 8 + n]
        if t == b'IHDR': w, h, depth, ctype = struct.unpack('>IIBB', c[:10]); assert depth == 8 and ctype in (2, 6)
        elif t == b'IDAT': idat += c
        i += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat); stride = w * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]; line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            px = line[x * bpp:(x + 1) * bpp]
            out += px if bpp == 4 else px + b'\xff'
    return w, h, bytes(out)


def checker(w, h):
    """Magenta/yellow 32-texel checkerboard with a white border: easy to spot in game."""
    out = bytearray()
    for y in range(h):
        for x in range(w):
            if x < 4 or y < 4 or x >= w - 4 or y >= h - 4: out += b'\xff\xff\xff\xff'
            elif (x // 32 + y // 32) % 2: out += b'\xff\x00\xff\xff'
            else: out += b'\xff\xff\x00\xff'
    return bytes(out)


def to565(c): return (c[0] * 31 // 255) << 11 | (c[1] * 63 // 255) << 5 | (c[2] * 31 // 255)


def dxt_colour_block(px):
    """8 bytes of DXT colour data (little-endian, as decode_block reads them) for 16 RGBA texels."""
    lum = [p[0] * 3 + p[1] * 6 + p[2] for p in px]
    hi, lo = px[lum.index(max(lum))], px[lum.index(min(lum))]
    c0, c1 = to565(hi), to565(lo)
    if c0 < c1: c0, c1 = c1, c0
    if c0 == c1: return struct.pack('<HHI', c0, c1, 0)
    p0, p1 = textures.rgb565(c0), textures.rgb565(c1)
    pal = [p0, p1, [(2 * p0[k] + p1[k]) // 3 for k in range(3)], [(p0[k] + 2 * p1[k]) // 3 for k in range(3)]]
    bits = 0
    for t, p in enumerate(px):
        best = min(range(4), key=lambda i: sum((p[k] - pal[i][k]) ** 2 for k in range(3)))
        bits |= best << (2 * t)
    return struct.pack('<HHI', c0, c1, bits)


def dxt5_alpha_block(px):
    a = [p[3] for p in px]; a0, a1 = max(a), min(a)
    if a0 == a1: return bytes([a0, a1]) + bytes(6)
    tab = [a0, a1] + [((6 - k) * a0 + (k + 1) * a1) // 7 for k in range(6)]
    bits = 0
    for t, v in enumerate(a):
        bits |= min(range(8), key=lambda i: abs(v - tab[i])) << (3 * t)
    return bytes([a0, a1]) + bits.to_bytes(6, 'little')


def encode(info, rgba):
    w, h, fmt = info['width'], info['height'], info['format']
    bpb, block = textures.FORMATS[fmt]
    bw, bh = (w + block - 1) // block, (h + block - 1) // block
    pitch_blocks = max(info['pitch'] // block, bw)
    # Undo the fetch swizzle: output channel c shows source component sel(c).
    sw = [info['swizzle'] >> 3 * c & 7 for c in range(4)]
    def source(p):
        s = [0, 0, 0, 255]
        for c in range(4):
            if sw[c] < 4: s[sw[c]] = p[c]
        return s
    size = (bh * pitch_blocks if not info['tiled'] else max(textures.tiled_offset(bw - 1, bh - 1, pitch_blocks, bpb) + 1, bw * bh)) * bpb
    out = bytearray(size)
    for by in range(bh):
        for bx in range(bw):
            px = []
            for t in range(block * block):
                x, y = min(bx * block + t % block, w - 1), min(by * block + t // block, h - 1)
                o = (y * w + x) * 4
                px.append(list(rgba[o:o + 4]))
            if fmt == 6: data = bytes(source(px[0]))
            elif fmt == 18: data = dxt_colour_block(px)
            elif fmt == 20: data = dxt5_alpha_block(px) + dxt_colour_block(px)
            else: sys.exit(f'format {fmt} not supported')
            idx = textures.tiled_offset(bx, by, pitch_blocks, bpb) if info['tiled'] else by * pitch_blocks + bx
            out[idx * bpb:idx * bpb + bpb] = textures.swap(data, info['endian'])  # the swaps are their own inverse
    return bytes(out)


def main():
    a = sys.argv[1:]
    raw_path, number, src = a[0], int(a[1], 16), a[2]
    out = a[a.index('-o') + 1]
    with open(raw_path, 'rb') as f:
        n = struct.unpack('>I', f.read(4))[0]; f.seek(0); head = f.read(4 + 104 * n)
    info = textures.header(head, n, number)
    if src == '--checker':
        w, h, rgba = info['width'], info['height'], checker(info['width'], info['height'])
    else:
        w, h, rgba = read_png(src)
    if (w, h) != (info['width'], info['height']):
        sys.exit(f'{src} is {w}x{h}; texture 0x{number:04X} is {info["width"]}x{info["height"]}')
    data = encode(info, rgba)
    open(out, 'wb').write(data)
    print(f'texture 0x{number:04X}: {w}x{h} format {info["format"]} -> {out} ({len(data)} bytes)')


main()
