# Print functions from tools/re/out/asm.txt with each instruction's address.
# Usage: python -I fnaddr.py <asm.txt> sub_XXXXXXXX [more...] [--grep REGEX] [--ctx N]
import re, sys

args = sys.argv[1:]
asm = args.pop(0)
pattern = None
ctx = 6
if '--grep' in args:
    i = args.index('--grep'); pattern = re.compile(args[i + 1]); del args[i:i + 2]
if '--ctx' in args:
    i = args.index('--ctx'); ctx = int(args[i + 1]); del args[i:i + 2]
wanted = set(args)

funcs = {}
cur = None
for line in open(asm, encoding='latin1'):
    line = line.rstrip('\n')
    if line.startswith('== '):
        cur = line[3:].strip()
        funcs[cur] = []
        continue
    if cur is None or not line.strip():
        continue
    funcs[cur].append(line)

for name in args:
    lines = funcs.get(name)
    if lines is None:
        print('?? no', name); continue
    addr = int(name[4:], 16)
    out = []
    bad = 0
    for line in lines:
        if line.startswith('loc_'):
            want = int(line[4:].rstrip(':'), 16)
            if want != addr: bad += 1
            addr = want  # labels are exact: re-sync (the asm can skip data inside a function)
            out.append((None, line))
            continue
        out.append((addr, line.strip()))
        addr += 4
    print(f'== {name}' + (f'  (label mismatches: {bad})' if bad else ''))
    if pattern:
        hits = [i for i, (_, l) in enumerate(out) if pattern.search(l)]
        shown = set()
        for h in hits:
            for j in range(max(0, h - ctx), min(len(out), h + ctx + 1)):
                if j in shown: continue
                shown.add(j)
                a, l = out[j]
                print(('   ' if a is None else f'{a:08X} ') + l + ('   <==' if j == h else ''))
            print('   ...')
    else:
        for a, l in out:
            print(('   ' if a is None else f'{a:08X} ') + l)
