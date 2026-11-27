# Recolour a texture PNG by hue (for custom weapon skins, e.g. the Carom LX's white-and-blue
# Mauler). Works in HSV, so shading, scratches and wear stay; alpha is kept.
#
#   python -I recolour.py <in.png> <out.png> --skin   (metal -> near-white, green -> blue)
#   python -I recolour.py <in.png> <out.png> --flash  (green muzzle flash -> red)
#   python -I recolour.py <in.png> <out.png> --hazard (the Remote Turret's Laptop Gun: light grey
#            parts -> hazard orange, dark parts -> bluish gunmetal; coloured lights kept)
#   options: --details-hue 215 (degrees, the colour green parts become), --white 0.55 (how
#            light the metal gets: brightness = white + (1 - white) * old brightness),
#            --flash-hue 0
#
# "Green" = hue 50-140 degrees (the Mauler's details are yellow-green, ~75-90; its dirt is teal, ~165), saturation above 0.3 and brightness above 0.12 (darker
# greenish pixels are dirt in the metal, not details). Metal = everything else that
# is not pure black (the empty parts of an atlas stay as they are).
import colorsys, os, struct, sys, zlib

def read_png(path):
    # texpack.py runs main() on import, so take its PNG reader by reading its source.
    path_texpack = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'texpack.py')
    src = open(path_texpack, encoding='utf-8').read()
    ns = {'__file__': path_texpack, '__name__': 'texpack_lib'}
    exec(compile(src.replace('\nmain()\n', '\n'), 'texpack.py', 'exec'), ns)
    return ns['read_png'](path)


def write_png(path, w, h, rgba):
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def is_green(h, s, v=1.0): return 50 / 360 <= h <= 140 / 360 and s > 0.3 and v > 0.12


def skin(r, g, b, details_hue, white):
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if is_green(h, s, v):
        # The details: same saturation and brightness, a blue hue (a little brighter, as
        # blue reads darker than green).
        return colorsys.hsv_to_rgb(details_hue, s, min(1.0, v * 1.15))
    if v < 0.006:
        return r, g, b  # the atlas's empty background (pure black)
    # The metal: much lighter, and one cool grey hue whatever it was (pale greenish edge
    # pixels would otherwise leave an olive fringe around the details).
    return colorsys.hsv_to_rgb(210 / 360, 0.06, white + (1.0 - white) * v)


def flash(r, g, b, flash_hue):
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if is_green(h, s) or (50 / 360 <= h <= 170 / 360 and s > 0.05):
        return colorsys.hsv_to_rgb(flash_hue, s, v)
    return r, g, b


def hazard(r, g, b):
    """The Remote Turret: the Laptop Gun's light grey gun parts become hazard orange, its dark
    laptop case a bluish gunmetal; the shading is kept. Saturated pixels (the green lights,
    the red label) and the atlas's black background stay. Greys between 0.30 and 0.55
    brightness (the shaded edges of light parts) blend from gunmetal to orange, so no hard
    line runs along a part's shading."""
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if s > 0.35 or v < 0.006:
        return r, g, b
    metal = colorsys.hsv_to_rgb(215 / 360, 0.20, min(1.0, 0.06 + 1.15 * v))
    orange = colorsys.hsv_to_rgb(28 / 360, 0.88, min(1.0, 0.30 + 0.70 * v))
    t = max(0.0, min(1.0, (v - 0.30) / 0.25))
    return tuple(m * (1 - t) + o * t for m, o in zip(metal, orange))


def fill_specks(img, w, h):
    """Pure-black pixels inside a part (DXT1's black "transparent" texels: invisible on the
    old dark metal, specks on light metal) take the average of their non-black neighbours,
    when at least 3 of the 8 are non-black (the atlas's empty background stays black;
    a part's border may grow by a pixel, which only helps texture filtering)."""
    black = lambda o: img[o] < 2 and img[o + 1] < 2 and img[o + 2] < 2
    fixes = []
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            o = (y * w + x) * 4
            if not black(o): continue
            near = [((y + dy) * w + x + dx) * 4 for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy]
            lit = [n for n in near if not black(n)]
            if len(lit) >= 3:
                fixes.append((o, [sum(img[n + c] for n in lit) // len(lit) for c in range(3)]))
    for o, rgb in fixes:
        img[o:o + 3] = bytes(rgb)
    return len(fixes)


def main():
    a = sys.argv[1:]
    src, dst = a[0], a[1]
    opt = lambda name, default: float(a[a.index(name) + 1]) if name in a else default
    w, h, rgba = read_png(src)
    out = bytearray(rgba)
    cache = {}
    details_hue, white, flash_hue = opt('--details-hue', 215) / 360, opt('--white', 0.55), opt('--flash-hue', 0) / 360
    for i in range(0, len(rgba), 4):
        key = rgba[i:i + 3]
        if key not in cache:
            r, g, b = (c / 255 for c in key)
            if '--skin' in a:
                nr, ng, nb = skin(r, g, b, details_hue, white)
            elif '--hazard' in a:
                nr, ng, nb = hazard(r, g, b)
            else:
                nr, ng, nb = flash(r, g, b, flash_hue)
            cache[key] = bytes(int(round(max(0.0, min(1.0, c)) * 255)) for c in (nr, ng, nb))
        out[i:i + 3] = cache[key]
    if '--skin' in a:
        fill_specks(out, w, h)
        fill_specks(out, w, h)  # a second pass for specks two pixels wide
    write_png(dst, w, h, bytes(out))
    print(f'{src} -> {dst} ({w}x{h}, {len(cache)} distinct colours)')


if __name__ == '__main__':
    main()
