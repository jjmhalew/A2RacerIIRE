"""srcmap.py - which original source file (from assert/debug strings) each function belongs to: python tools/srcmap.py"""
import sys, re, collections
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from disasm import load
for tag in ('spel','menu'):
    x=load(tag); m=collections.defaultdict(list)
    for f in x.funcs:
        for s in x.strings_of(f):
            if re.search(r'(?i)\.(cpp|c|hpp)$', s.strip()):
                m[re.split(r'[\/]', s)[-1].lower()].append(f); break
    print('==',tag,len(m),'source files')
    for k,v in sorted(m.items(), key=lambda kv: min(kv[1])):
        print(f'  {k:22} {len(v):3} funcs  {min(v):#x}..{max(v):#x}')
