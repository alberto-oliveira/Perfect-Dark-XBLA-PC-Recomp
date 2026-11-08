# Functions whose asm matches all regexes. Usage: fnfind.py asm.txt [--max N] regex1 regex2 ...
import re, sys
args = sys.argv[2:]; mx = 10**9
if args[:1] == ['--max']: mx = int(args[1]); args = args[2:]
pats = [re.compile(a) for a in args]
name, body = None, []
def check():
    if name and len(body) <= mx and all(any(p.search(l) for l in body) for p in pats): print(name, len(body))
for line in open(sys.argv[1]):
    if line.startswith('== '): check(); name, body = line[3:].strip(), []
    else: body.append(line)
check()
