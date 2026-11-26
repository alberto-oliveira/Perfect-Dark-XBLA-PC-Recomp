# Turn textures extracted by pdunpack (tex_XXXX.bin) into PNG files.
#
#   python -I textures.py <Textures.raw> <dir with tex_XXXX.bin> <out dir> [ids|all] [--opaque]
#
# Only the headers of Textures.raw are read (for each texture's format and size). The texel
# data is in the Xenos GPU layout; this is a Python port of the decoder in
# src/pd_native_gpu.cpp (tiled_offset, decode_block, swizzle). Only the first (largest)
# mip level is written. --opaque writes alpha 255 (handy to see colours of cut-out textures).
#
# Each texture's D3D header (second header array, 52 bytes) has its fetch constant at +28:
#   d0: bit 31 tiled, bits 22-30 pitch / 32 texels
#   d1: bits 0-5 format, bits 6-7 endian swap
#   d2: bits 0-12 width-1, bits 13-25 height-1
#   d3: bits 1-12 swizzle (3 bits per output channel: 0-3 = source x,y,z,w, 4 = 0, 5 = 1)
import json, os, struct, sys, zlib

FORMATS = {2: (1, 1), 3: (2, 1), 4: (2, 1), 10: (2, 1), 15: (2, 1), 6: (4, 1), 18: (8, 4), 19: (16, 4), 20: (16, 4)}


def tiled_offset(x, y, pitch, bpb):
    """XGAddress2DTiledOffset: block offset of (x, y) in a tiled surface (port of pd_native_gpu.cpp)."""
    aligned = (pitch + 31) & ~31
    log_bpp = (bpb >> 2) + ((bpb >> 1) >> (bpb >> 2))
    macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    off = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return (((off & ~511) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp


def swap(raw, endian):
    """Undo the Xenos endian mode on one block (1 = 8in16, 2 = 8in32, 3 = 16in32)."""
    out = bytearray(raw)
    swap_pairs = endian == 1 or (endian == 2 and len(raw) < 4)  # 16-bit texels: 8in32 acts as 8in16
    if swap_pairs:
        for i in range(0, len(out) - 1, 2): out[i], out[i + 1] = out[i + 1], out[i]
    elif endian == 2:
        for i in range(0, len(out) - 3, 4): out[i:i + 4] = out[i:i + 4][::-1]
    elif endian == 3:
        for i in range(0, len(out) - 3, 4): out[i:i + 4] = out[i + 2:i + 4] + out[i:i + 2]
    return bytes(out)


def rgb565(c): return [(c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31, 255]


def decode_block(fmt, b):
    """One block (already endian-swapped) -> list of [x, y, z, w] texels."""
    if fmt == 6: return [list(b[:4])]
    if fmt == 2: return [[b[0]] * 4]
    if fmt == 10: return [[b[0], b[1], 0, 255]]
    c = b[0] | b[1] << 8
    if fmt == 4: return [[(c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31, 255]]
    if fmt == 3: return [[(c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31, 255 if c >> 15 else 0]]
    if fmt == 15: return [[(c >> 4 * k & 15) * 17 for k in range(4)]]
    # DXT1 (18), DXT2/3 (19), DXT4/5 (20)
    cb = b if fmt == 18 else b[8:]
    c0, c1 = cb[0] | cb[1] << 8, cb[2] | cb[3] << 8
    p0, p1 = rgb565(c0), rgb565(c1)
    if fmt != 18 or c0 > c1:
        pal = [p0, p1, [(2 * p0[k] + p1[k]) // 3 for k in range(3)] + [255], [(p0[k] + 2 * p1[k]) // 3 for k in range(3)] + [255]]
    else:
        pal = [p0, p1, [(p0[k] + p1[k]) // 2 for k in range(3)] + [255], [0, 0, 0, 0]]
    bits = int.from_bytes(cb[4:8], 'little')
    out = [list(pal[bits >> 2 * t & 3]) for t in range(16)]
    if fmt == 19:
        for t in range(16): out[t][3] = (b[t // 2] >> 4 * (t & 1) & 15) * 17
    elif fmt == 20:
        a = [b[0], b[1]]
        if a[0] > a[1]: a += [((8 - k) * a[0] + (k - 1) * a[1]) // 7 for k in range(2, 8)]
        else: a += [((6 - k) * a[0] + (k - 1) * a[1]) // 5 for k in range(2, 6)] + [0, 255]
        ab = int.from_bytes(b[2:8], 'little')
        for t in range(16): out[t][3] = a[ab >> 3 * t & 7]
    return out


def header(raw_head, n, number):
    """Format details of texture `number` from Textures.raw's two header arrays."""
    rec = raw_head[4 + number * 52:4 + number * 52 + 52]
    fc = struct.unpack('>6I', raw_head[4 + 52 * n + number * 52 + 28:4 + 52 * n + number * 52 + 52])
    return {'number': number, 'width': (fc[2] & 0x1FFF) + 1, 'height': (fc[2] >> 13 & 0x1FFF) + 1,
            'n64_width': struct.unpack('>I', rec[12:16])[0], 'n64_height': struct.unpack('>I', rec[16:20])[0],
            'format': fc[1] & 0x3F, 'endian': fc[1] >> 6 & 3, 'tiled': fc[0] >> 31,
            'pitch': (fc[0] >> 22 & 0x1FF) * 32, 'swizzle': fc[3] >> 1 & 0xFFF}


def decode(info, data):
    bpb, block = FORMATS[info['format']]
    w, h = info['width'], info['height']
    bw, bh = (w + block - 1) // block, (h + block - 1) // block
    pitch_blocks = max(info['pitch'] // block, bw)
    rgba = bytearray(w * h * 4)
    for by in range(bh):
        for bx in range(bw):
            idx = tiled_offset(bx, by, pitch_blocks, bpb) if info['tiled'] else by * pitch_blocks + bx
            texels = decode_block(info['format'], swap(data[idx * bpb:idx * bpb + bpb], info['endian']))
            for t, src in enumerate(texels):
                x, y = bx * block + t % block, by * block + t // block
                if x >= w or y >= h: continue
                o = (y * w + x) * 4
                for c in range(4):
                    sel = info['swizzle'] >> 3 * c & 7
                    rgba[o + c] = src[sel] if sel < 4 else (0 if sel == 4 else 255)
    return bytes(rgba)


def write_png(path, w, h, rgba):
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def selected(spec, number):
    if spec == 'all': return True
    for part in spec.split(','):
        lo, _, hi = part.partition('-')
        if int(lo, 16) <= number <= int(hi or lo, 16): return True
    return False


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opaque = '--opaque' in sys.argv
    raw_path, in_dir, out_dir = args[:3]
    spec = args[3] if len(args) > 3 else 'all'
    with open(raw_path, 'rb') as f:
        n = struct.unpack('>I', f.read(4))[0]
        f.seek(0)
        raw_head = f.read(4 + 104 * n)
    os.makedirs(out_dir, exist_ok=True)
    index = []
    for number in range(n):
        path = os.path.join(in_dir, f'tex_{number:04X}.bin')
        if not selected(spec, number) or not os.path.exists(path): continue
        info = header(raw_head, n, number)
        if info['format'] not in FORMATS:
            print(f'tex 0x{number:04X}: format {info["format"]} not handled'); continue
        rgba = decode(info, open(path, 'rb').read())
        if opaque: rgba = bytes(v if i % 4 != 3 else 255 for i, v in enumerate(rgba))
        write_png(os.path.join(out_dir, f'tex_{number:04X}.png'), info['width'], info['height'], rgba)
        index.append(info)
    json.dump(index, open(os.path.join(out_dir, 'textures.json'), 'w'), indent=1)
    print(f'{len(index)} textures written to {out_dir}')


main()
