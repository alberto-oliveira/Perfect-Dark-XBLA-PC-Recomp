# Per-function PPC asm (from the "// insn" comments) of generated code. Usage: asm_index.py generated/default asm.txt
import re, sys, glob, os
gen = sys.argv[1]; out = open(sys.argv[2], 'w')
fn_re = re.compile(r'^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?(sub_[0-9A-F]+)\)')
loc_re = re.compile(r'^(loc_[0-9A-F]+):')
for path in sorted(glob.glob(os.path.join(gen, 'perfect_dark_recomp.*.cpp'))):
    for line in open(path, errors='replace'):
        m = fn_re.match(line)
        if m: out.write(f'\n== {m.group(1)}\n'); continue
        m = loc_re.match(line)
        if m: out.write(f'{m.group(1)}:\n'); continue
        s = line.strip()
        if s.startswith('// '): out.write('  ' + s[3:] + '\n')
        elif s.startswith('if (Pd') or s.startswith('Pd'): out.write('  [HOOK] ' + s + '\n')
