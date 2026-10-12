"""coverage.py - how much of the original's code the docs, the names files and the port name, and what is left.

A function counts as covered ("analysed") when any address inside it (0x4xxxxx) appears in docs/*.md,
docs/names/*.txt, src/*.[ch] or tools/*.py (not in the generated docs/COVERAGE.md and docs/SOURCES_raw.txt). spel.dat and
menu.dat share the image base 0x400000, so every address is credited to one program: to the one a word "menu", "spel" or
"a2_II" right before it names, else to the only program whose names file has it, else to the file's own program
(names/menu_*, MENU.md and src/menu*: menu.dat; names/a2_*: a2_II.exe; everything else: spel.dat).
The functions are those of tools/disasm.py (call targets, prologues, pointers in data). Each one is put in the original
source file of the nearest assert/debug string at or before it (MSVC keeps the functions of one .cpp together). The C
runtime at the end of each program is not counted. "Covered" is not "ported correctly": it only means someone has
looked at the function.

usage: python tools/coverage.py [--min N] [--write]
  prints the totals per program, area and source file, then every uncovered function of at least N instructions
  (default 30) with its callers, callees and strings. --write also puts the result into docs/COVERAGE.md, between the
  "generated" markers. Needs the CD's files in extract/data, like disasm.py."""
import collections, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import ROOT, load

min_n = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 30
write = '--write' in sys.argv

PROGS = ['spel', 'menu', 'a2']
FILES = {'spel': 'spel.dat', 'menu': 'menu.dat', 'a2': 'a2_II.exe'}
# areas by start address (ANALYSE.md "The code"); 'crt' = the C runtime, not counted. menu.dat: from the entry point
AREAS = {'spel': [(0x401000, 'game'), (0x435000, '3D library (win3ddp)'), (0x464000, '2D library (winlib)'),
                  (0x471000, 'crt')],
         'menu': [(0x401000, 'menus + winlib'), (None, 'crt')],
         'a2': [(0x401000, 'launcher'), (0x401520, 'crt')]}
SKIP = {'docs/COVERAGE.md', 'docs/SOURCES_raw.txt', 'tools/coverage.py'}

X = {p: load(p) for p in PROGS}


def area_of(p, a):
    name = None
    for start, n in AREAS[p]:
        if a >= (X[p].entry if start is None else start):
            name = n
    return name


def ninsn(p, f):
    return sum(1 for i in X[p].func_insns(f) if i[2] != 'db')


# ---- the original source file of every function (nearest .cpp/.c string at or before it)
src = {}
for p in PROGS:
    cur = '?'
    for f in X[p].funcs:
        for s in X[p].strings_of(f):
            m = re.search(r'([\w.-]+)\.(cpp|c)\s*$', s.strip(), re.I)
            if m:
                cur = (m.group(1) + '.' + m.group(2)).lower()
                break
        src[p, f] = cur

# ---- who names what
named_by = {p: {} for p in PROGS}            # program -> {address: [names file]}
for p in PROGS:
    for fn in glob.glob(os.path.join(ROOT, 'docs', 'names', f'{p}_*.txt')):
        for m in re.finditer(r'^\s*0x([0-9a-f]+)', open(fn, encoding='utf-8').read(), re.I | re.M):
            named_by[p].setdefault(int(m.group(1), 16), []).append(fn)

word = re.compile(r'(?<!\w)(menu|spel|a2_ii)(?:\.dat|\.exe)?(?!\w)', re.I)
cov = {p: collections.defaultdict(set) for p in PROGS}   # program -> {function: {file}}
files = glob.glob(os.path.join(ROOT, 'docs', '*.md')) + glob.glob(os.path.join(ROOT, 'docs', 'names', '*.txt')) \
    + glob.glob(os.path.join(ROOT, 'src', '*.[ch]')) + glob.glob(os.path.join(ROOT, 'tools', '*.py'))
for fn in files:
    rel = os.path.relpath(fn, ROOT).replace('\\', '/')
    if rel in SKIP:
        continue
    base = os.path.basename(rel).lower()
    own = 'menu' if base.startswith('menu') else 'a2' if base.startswith('a2_') else 'spel'
    text = open(fn, encoding='utf-8', errors='ignore').read()
    for m in re.finditer(r'\b0x(4[0-9a-f]{5})\b', text, re.I):
        a = int(m.group(1), 16)
        w = word.findall(text[max(0, m.start() - 16):m.start()])
        if w:
            p = {'menu': 'menu', 'spel': 'spel'}.get(w[-1].lower(), 'a2')
        else:
            owners = [q for q in PROGS if a in named_by[q]]
            p = owners[0] if len(owners) == 1 else own
        x = X[p]
        if x.text_va <= a < x.text_va + len(x.text):
            f = x.func_of(a)
            if f is not None:
                cov[p][f].add(rel.split('/')[-1])

# ---- totals
out = []
out.append('## Totals\n')
out.append('| program | area | functions | instructions | covered | covered % | functions left |')
out.append('|---|---|---:|---:|---:|---:|---:|')
for p in PROGS:
    x = X[p]
    by = collections.OrderedDict()
    for f in x.funcs:
        ar = area_of(p, f)
        if ar == 'crt':
            continue
        t = by.setdefault(ar, [0, 0, 0, 0])
        n = ninsn(p, f)
        t[0] += 1
        t[1] += n
        if f in cov[p]:
            t[2] += n
        else:
            t[3] += 1
    for ar, (nf, ni, nc, left) in by.items():
        out.append(f'| {FILES[p]} | {ar} | {nf} | {ni} | {nc} | {100.0 * nc / max(ni, 1):.1f} | {left} |')

out.append('\n## Per original source file\n')
out.append('The file of the nearest assert/debug string at or before the function ("?" = before the first one).\n')
out.append('| program | source file | span | functions | instructions | covered % | functions left |')
out.append('|---|---|---|---:|---:|---:|---:|')
for p in PROGS:
    groups = collections.OrderedDict()
    for f in X[p].funcs:
        if area_of(p, f) == 'crt':
            continue
        g = groups.setdefault(src[p, f], [f, f, 0, 0, 0, 0])
        g[1] = f
        n = ninsn(p, f)
        g[2] += 1
        g[3] += n
        if f in cov[p]:
            g[4] += n
        else:
            g[5] += 1
    for s, (lo, hi, nf, ni, nc, left) in groups.items():
        out.append(f'| {FILES[p]} | {s} | `{lo:#x}`..`{hi:#x}` | {nf} | {ni} | {100.0 * nc / max(ni, 1):.1f} | {left} |')

out.append(f'\n## Not analysed yet (at least {min_n} instructions)\n')
out.append('Callees in *italics* are covered, with the files that name them.\n')
out.append('| program | function | instructions | source file | callers | callees | strings |')
out.append('|---|---|---:|---|---|---|---|')
for p in PROGS:
    x = X[p]
    callers = collections.defaultdict(set)
    for f in x.funcs:
        for c in x.calls_of(f):
            if isinstance(c, int):
                callers[c].add(f)
    for f in x.funcs:
        if area_of(p, f) == 'crt' or f in cov[p]:
            continue
        n = ninsn(p, f)
        if n < min_n:
            continue
        cs = []
        for c in dict.fromkeys(x.calls_of(f)):
            if isinstance(c, str):
                cs.append(c)
            elif c in cov[p]:
                cs.append(f'*{c:#x}* ({",".join(sorted(cov[p][c])[:2])})')
            else:
                cs.append(f'{c:#x}')
        ss = '; '.join(s.strip().replace('|', '/')[:40] for s in dict.fromkeys(x.strings_of(f)))[:120]
        cl = ' '.join(f'{c:#x}' for c in sorted(callers[f])[:6]) + (' ...' if len(callers[f]) > 6 else '')
        out.append(f'| {FILES[p]} | `{f:#x}` | {n} | {src[p, f]} | {cl} | {" ".join(cs[:8])} | {ss} |')

text = '\n'.join(out) + '\n'
print(text)
if write:
    path = os.path.join(ROOT, 'docs', 'COVERAGE.md')
    doc = open(path, encoding='utf-8').read()
    a, b = '<!-- generated by tools/coverage.py -->', '<!-- end of generated -->'
    i, j = doc.index(a) + len(a), doc.index(b)
    open(path, 'w', encoding='utf-8', newline='\n').write(doc[:i] + '\n' + text + doc[j:])
    print('-> docs/COVERAGE.md')
