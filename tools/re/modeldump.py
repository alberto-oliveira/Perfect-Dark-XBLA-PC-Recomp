# Walk a dumped PD model file (N64 modeldef) and summarise its display lists.
# Usage: python -I modeldump.py <file.bin> <base address of the pointers>
#   unrelocated gun files use base 0x05000000; relocated models the address they were loaded to.
import struct, sys
from collections import Counter

path, base = sys.argv[1], int(sys.argv[2], 16)
d = open(path, 'rb').read()
u32 = lambda o: struct.unpack('>I', d[o:o + 4])[0]
u16 = lambda o: struct.unpack('>H', d[o:o + 2])[0]
s16 = lambda o: struct.unpack('>h', d[o:o + 2])[0]
def off(p):
    o = p - base
    return o if 0 <= o < len(d) else None

root, skel, parts = u32(0), u32(4), u32(8)
numparts, nummat = s16(12), s16(14)
numtex, texcfg = s16(0x16), u32(0x18)
print(f'root {root:08X} skel {skel:08X} parts {parts:08X} numparts {numparts} nummatrices {nummat} texconfigs {numtex} at {texcfg:08X}')
t = off(texcfg)
for i in range(numtex if t is not None else 0):
    e = t + i * 12
    print(f'  tex {i:2}: num 0x{u32(e):04X} {d[e+4]}x{d[e+5]} level {d[e+6]} format {d[e+7]} depth {d[e+8]} s {d[e+9]} t {d[e+10]}')

NAMES = {0x01: 'MTX', 0x04: 'VTX', 0x06: 'DL', 0x07: 'COL(PD)', 0xB1: 'TRI4', 0xBF: 'TRI1', 0xB8: 'ENDDL', 0xB7: 'SETGEOM',
         0xB6: 'CLRGEOM', 0xBB: 'TEXTURE', 0xBA: 'SETOTHERH', 0xB9: 'SETOTHERL', 0xFC: 'SETCOMBINE', 0xFA: 'PRIMCOLOR',
         0xFB: 'ENVCOLOR', 0xFD: 'SETTIMG', 0xF5: 'SETTILE', 0xF3: 'LOADBLOCK', 0xF2: 'SETTILESIZE', 0xE7: 'PIPESYNC',
         0xC0: 'NOOP', 0xE6: 'LOADSYNC', 0xE8: 'TILESYNC', 0xF8: 'FOGCOLOR', 0xF7: 'FILLCOLOR', 0xB4: 'RDPHALF1'}

def walk_dl(p, ops, details, depth=0):
    o = off(p)
    n = 0
    while o is not None and o + 8 <= len(d) and n < 4000:
        w0, w1 = u32(o), u32(o + 4)
        op = w0 >> 24
        ops[op] += 1
        name = NAMES.get(op, f'{op:02X}')
        if op in (0xB7, 0xB6, 0xFC, 0xFA, 0xFB, 0x07, 0xBB, 0xFD) and len(details) < 60:
            details.append(f'{"  " * depth}{name} {w0:08X} {w1:08X}')
        if op == 0x06:
            walk_dl(w1, ops, details, depth + 1)
            if (w0 >> 16) & 0xFF: return  # branch, not call
        if op == 0xB8: return
        o += 8
        n += 1

seen = set()
def walk_node(p, level=0):
    while p and p not in seen:
        seen.add(p)
        o = off(p)
        if o is None:
            print('  ' * level + f'node {p:08X} outside the file'); return
        typ, rod = u16(o), u32(o + 4)
        child, nxt = u32(o + 0x14), u32(o + 0x0C)
        line = '  ' * level + f'node {p:08X} type 0x{typ:02X}'
        r = off(rod)
        if typ & 0xFF in (0x18, 0x04) and r is not None:
            opa, xlu = u32(r), u32(r + 4)
            if typ & 0xFF == 0x18:
                cols, verts, nv, rm, nc = u32(r + 8), u32(r + 12), s16(r + 16), s16(r + 18), u16(r + 22)
                line += f' DL opa {opa:08X} xlu {xlu:08X} colours {cols:08X} ({nc}) vertices {verts:08X} ({nv}) rendermode {rm}'
            else:
                ba, verts, nv = u32(r + 8), u32(r + 12), s16(r + 16)
                line += f' GUNDL opa {opa:08X} xlu {xlu:08X} baseaddr {ba:08X} vertices {verts:08X} ({nv})'
            print(line)
            for g in (opa, xlu):
                if not g: continue
                ops, details = Counter(), []
                walk_dl(g, ops, details)
                print('  ' * level + '   ops: ' + ', '.join(f'{NAMES.get(k, hex(k))}x{v}' for k, v in ops.most_common()))
                for x in details[:14]: print('  ' * level + '     ' + x)
        else:
            print(line)
        if child: walk_node(child, level + 1)
        p = nxt

walk_node(root)

# Colour tables (gSPColor, op 0x07: count in the command, address in w1) by the
# lighting state when vertices are loaded with them: unlit = real vertex colours.
usage = {}
def sim(p, state):
    o = off(p); cur = None; n = 0
    while o is not None and o + 8 <= len(d) and n < 4000:
        w0, w1 = u32(o), u32(o + 4); op = w0 >> 24
        if op == 0xB7: state['geom'] |= w1
        elif op == 0xB6: state['geom'] &= ~w1
        elif op == 0x07: state['col'] = (w1, ((w0 >> 16) & 0xFF) // 4 + 1, w0)
        elif op == 0x04 and state['col']:
            k = state['col'][0]
            u = usage.setdefault(k, {'lit': 0, 'unlit': 0, 'w0': state['col'][2]})
            u['lit' if state['geom'] & 0x20000 else 'unlit'] += 1
        elif op == 0x06:
            sim(w1, state)
            if (w0 >> 16) & 0xFF: return
        elif op == 0xB8: return
        o += 8; n += 1
for p in sorted(seen):
    o = off(p)
    if o is None or u16(o) & 0xFF not in (0x18, 0x04): continue
    r = off(u32(o + 4))
    for g in (u32(r), u32(r + 4)):
        if g: sim(g, {'geom': 0, 'col': None})
print('colour tables:')
for k, u in sorted(usage.items()):
    o = off(k)
    first = ' '.join(d[o + i * 4:o + i * 4 + 4].hex() for i in range(4)) if o is not None else '?'
    print(f'  {k:08X} cmd {u["w0"]:08X}: VTX loads lit {u["lit"]}, unlit {u["unlit"]}; first entries {first}')
