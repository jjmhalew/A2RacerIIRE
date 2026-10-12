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

usage: python tools/coverage.py [--min N] [--write] [--html [FILE]]
  prints the totals per program, area and source file, then every uncovered function of at least N instructions
  (default 30) with its callers, callees and strings. --write also puts the result into docs/COVERAGE.md, between the
  "generated" markers. --html also writes the coverage map, one page with every function of the three programs by
  address, coloured by class (default out/coverage.html: it holds the original's function names, so it stays out of
  the repository). Needs the CD's files in extract/data, like disasm.py."""
import collections, glob, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from disasm import ROOT, load

min_n = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 30
write = '--write' in sys.argv
html = None
if '--html' in sys.argv:
    k = sys.argv.index('--html')
    html = sys.argv[k + 1] if k + 1 < len(sys.argv) and not sys.argv[k + 1].startswith('--') else 'out/coverage.html'

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
in_src = {p: set() for p in PROGS}                       # functions src/ cites (the port follows them)
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
                if rel.startswith('src/'):
                    in_src[p].add(f)

# ---- callers, and the class of every function (the coverage map)
callers = {p: collections.defaultdict(set) for p in PROGS}
for p in PROGS:
    for f in X[p].funcs:
        for c in X[p].calls_of(f):
            if isinstance(c, int):
                callers[p][c].add(f)
CLASSES = ['ported', 'read', 'helper', 'platform', 'unused', 'open']
WHAT = {'ported': 'cited in the port\'s code (src/)',
        'read': 'cited only in the docs, names files or tools',
        'helper': 'not cited, only called from ported, read or helper code',
        'platform': 'C runtime, or calls Windows / DirectX directly: the port uses SDL, OpenGL and libc',
        'unused': 'nothing calls it or points at it',
        'open': 'reached by the game, nobody has looked at it yet'}
cls = {}
for p in PROGS:
    x = X[p]
    tend = x.text_va + len(x.text)
    for f in x.funcs:
        if f in in_src[p]:
            cls[p, f] = 'ported'
        elif f in cov[p]:
            cls[p, f] = 'read'
        elif area_of(p, f) == 'crt' or any(isinstance(c, str) for c in x.calls_of(f)):
            cls[p, f] = 'platform'
        elif f != x.entry and not x.xrefs.get(f):
            cls[p, f] = 'unused'
        else:
            cls[p, f] = 'open'
    grew = True
    while grew:                                          # helpers of helpers too
        grew = False
        for f in x.funcs:
            if cls[p, f] == 'open' and callers[p][f] and \
                    all(cls[p, c] in ('ported', 'read', 'helper') for c in callers[p][f]):
                cls[p, f] = 'helper'
                grew = True

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

out.append('\n## By class\n')
out.append('Shares of the code bytes, the C runtime included (the coverage map, `--html`, draws the same classes).\n')
out.append('| program | ' + ' | '.join(CLASSES) + ' |')
out.append('|---|' + '---:|' * len(CLASSES))
for p in PROGS:
    by = collections.Counter()
    for f in X[p].funcs:
        a, e = X[p].func_range(f)
        by[cls[p, f]] += e - a
    tot = sum(by.values()) or 1
    out.append(f'| {FILES[p]} | ' + ' | '.join(f'{100.0 * by[c] / tot:.1f} %' for c in CLASSES) + ' |')
for c in CLASSES:
    out.append(f'- **{c}**: {WHAT[c]}')

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
out.append('Not the C runtime or functions that call Windows / DirectX themselves (class "platform").\n')
out.append('| program | function | class | instructions | source file | callers | callees | strings |')
out.append('|---|---|---|---:|---|---|---|---|')
for p in PROGS:
    x = X[p]
    for f in x.funcs:
        if cls[p, f] in ('ported', 'read', 'platform'):
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
        cs_ = callers[p][f]
        cl = ' '.join(f'{c:#x}' for c in sorted(cs_)[:6]) + (' ...' if len(cs_) > 6 else '')
        out.append(f'| {FILES[p]} | `{f:#x}` | {cls[p, f]} | {n} | {src[p, f]} | {cl} | {" ".join(cs[:8])} | {ss} |')

text = '\n'.join(out) + '\n'
print(text)
if write:
    path = os.path.join(ROOT, 'docs', 'COVERAGE.md')
    doc = open(path, encoding='utf-8').read()
    a, b = '<!-- generated by tools/coverage.py -->', '<!-- end of generated -->'
    i, j = doc.index(a) + len(a), doc.index(b)
    open(path, 'w', encoding='utf-8', newline='\n').write(doc[:i] + '\n' + text + doc[j:])
    print('-> docs/COVERAGE.md')

# ---- the coverage map (--html): one canvas per program, one row = 4 KB, each function as wide as it is long
if html:
    import json
    from html import escape
    HTML = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>A2RacerIIRE coverage map</title>
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wght@500;700;800&family=JetBrains+Mono:wght@400;600&display=swap">
<style>
:root{--bg:#f6f7f5;--panel:#fff;--fg:#1d2321;--mut:#5d6763;--line:#dde2de;--hl:#1d2321;@LIGHT@;color-scheme:light;
--display:"Archivo","Helvetica Neue",Arial,sans-serif;--body:system-ui,-apple-system,"Segoe UI",sans-serif;--mono:"JetBrains Mono",ui-monospace,Consolas,monospace}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#141816;--panel:#1b201e;--fg:#e4e8e5;--mut:#98a39e;--line:#2c3431;--hl:#fff;@DARK@;color-scheme:dark}}
:root[data-theme="dark"]{--bg:#141816;--panel:#1b201e;--fg:#e4e8e5;--mut:#98a39e;--line:#2c3431;--hl:#fff;@DARK@;color-scheme:dark}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 var(--body)}
main{max-width:1120px;margin:0 auto;padding:28px 16px 56px;display:grid;gap:28px}main>*{min-width:0}
h1{font:800 clamp(26px,4vw,38px)/1.05 var(--display);letter-spacing:-.01em;margin:0}
h2{font:700 19px/1.2 var(--display);margin:0 0 10px}
.lede{color:var(--mut);max-width:68ch;margin:8px 0 0}
.eyebrow{font:600 12px var(--mono);letter-spacing:.08em;text-transform:uppercase;color:var(--mut);margin:0 0 8px}
.bar{display:flex;height:18px;border-radius:4px;overflow:hidden;gap:1px;background:var(--line);margin-bottom:10px}.bar i{display:block;min-width:2px}
.legend{list-style:none;margin:14px 0 0;padding:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:8px 22px}
.legend li{display:grid;grid-template-columns:14px auto 1fr;column-gap:8px;align-items:baseline;min-width:0}
.sw{width:14px;height:14px;border-radius:3px;display:block;align-self:center}
.nm{font-weight:600}.pc{font:600 13px var(--mono);font-variant-numeric:tabular-nums;justify-self:end}.ds{grid-column:2/4;color:var(--mut);font-size:13px}
.s-ported{background:var(--s-ported)}.s-read{background:var(--s-read)}.s-helper{background:var(--s-helper)}
.s-platform{background:var(--s-platform)}.s-unused{background:var(--s-unused)}.s-open{background:var(--s-open)}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:6px}
.wrap{padding:10px 10px 10px 0;overflow:hidden}canvas{display:block;width:100%;cursor:crosshair;touch-action:manipulation}
#info{position:sticky;bottom:0;font:13px/1.5 var(--mono);padding:10px 12px;min-height:3.2em;overflow-wrap:anywhere;box-shadow:0 -4px 12px rgba(0,0,0,.08)}
#info .st{font-weight:600}#info .m{color:var(--mut)}
.tw{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:14px}
th,td{text-align:left;padding:8px 10px;border-bottom:1px solid var(--line);vertical-align:top}
th{font:600 12px var(--mono);letter-spacing:.06em;text-transform:uppercase;color:var(--mut)}
td.mono{font:12.5px var(--mono);white-space:nowrap}td.num{font:12.5px var(--mono);text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
tbody tr td:first-child{box-shadow:inset 3px 0 0 var(--s-open)}
.how{color:var(--mut);font-size:13px;margin:8px 0 0}code{font:12.5px var(--mono)}
</style></head><body><main>
<header><p class="eyebrow">A2 Racer II &middot; spel.dat, menu.dat, a2_II.exe &middot; @NF@ functions &middot; @KB@ KB of code</p>
<h1>A2RacerIIRE coverage map</h1>
<p class="lede">Every function of the original's three programs, sorted by what the port and its docs do with it. Shares are of the code's bytes.</p>
<ul class="legend">@LEGEND@</ul></header>
@SECTIONS@
<div id="info" class="panel"><span class="m">Point at or tap a function for its address, size, name, source file, class and the files that cite it.</span></div>
<p class="how">One row is 4 KB; each function is as wide as it is long. spel.dat: the game up to 0x435000, the 3D library win3ddp up to 0x464000, the 2D library winlib, the C runtime from 0x471000.</p>
<section><h2>Open</h2>
<div class="panel tw"><table><thead><tr><th>Program</th><th>Function</th><th>Bytes</th><th>Source file</th><th>Strings</th></tr></thead><tbody>@OPEN@</tbody></table></div>
<p class="how">Reached by the game, at least @MIN@ instructions, largest first.</p>@MORE@
<p class="how">Made by <code>python tools/coverage.py --html</code> from the CD's programs (tools/disasm.py) and the addresses <code>docs/</code>, <code>src/</code> and <code>tools/</code> cite. See docs/COVERAGE.md.</p></section>
</main>
<script>
const DATA=@DATA@,N=@N@,ROW=4096,H=7,G=1,LBL=64,info=document.getElementById('info'),HINT=info.innerHTML;let pinned=null;
function css(n){return getComputedStyle(document.documentElement).getPropertyValue(n).trim()}
function esc(t){return String(t).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
const maps=[...document.querySelectorAll('canvas[data-p]')].map(cv=>{const D=DATA[cv.dataset.p];
const rows=[...new Set(D.flatMap(f=>{const a=Math.floor(f[0]/ROW),b=Math.floor((f[0]+Math.max(f[1],1)-1)/ROW),r=[];for(let k=a;k<=b;k++)r.push(k);return r}))].sort((a,b)=>a-b);
return {cv,D,rows,ri:new Map(rows.map((r,k)=>[r,k]))}});
function span(m,f,cb){let a=f[0],n=f[1];while(n>0){const r=Math.floor(a/ROW),o=a-r*ROW,k=Math.min(n,ROW-o);cb(m.ri.get(r),o,k);a+=k;n-=k}}
function draw(m){const {cv,D,rows}=m,w=cv.parentNode.clientWidth-10,dpr=window.devicePixelRatio||1,h=rows.length*(H+G);
cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);cv.style.height=h+'px';const x=cv.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);
const col=N.map(s=>css('--s-'+s));x.font='9px '+css('--mono');x.fillStyle=css('--mut');
rows.forEach((r,k)=>{if(k%8==0||(k>0&&rows[k-1]!=r-1))x.fillText('0x'+(r*ROW).toString(16),6,k*(H+G)+H)});
const sc=(w-LBL)/ROW;for(const f of D){x.fillStyle=col[f[2]];span(m,f,(k,o,n)=>x.fillRect(LBL+o*sc,k*(H+G),Math.max(n*sc-0.35,0.6),H))}
if(pinned&&pinned.m===m){x.strokeStyle=css('--hl');x.lineWidth=1.5;span(m,pinned.f,(k,o,n)=>x.strokeRect(LBL+o*sc-1,k*(H+G)-1,Math.max(n*sc,1)+2,H+2))}}
function drawAll(){maps.forEach(draw)}
function at(m,e){const b=m.cv.getBoundingClientRect(),k=Math.floor((e.clientY-b.top)/(H+G));if(k<0||k>=m.rows.length||e.clientX-b.left<LBL)return null;
const a=m.rows[k]*ROW+(e.clientX-b.left-LBL)/((b.width-LBL)/ROW);let lo=0,hi=m.D.length-1,f=null;
while(lo<=hi){const i=(lo+hi)>>1;if(m.D[i][0]<=a){f=m.D[i];lo=i+1}else hi=i-1}return f&&a<f[0]+f[1]?f:null}
function show(m,f){if(!f){info.innerHTML=HINT;return}
info.innerHTML=esc(m.cv.dataset.n)+' &middot; <span class="st" style="color:var(--s-'+N[f[2]]+')">'+N[f[2]]+'</span> &middot; 0x'+f[0].toString(16)+
' &middot; '+f[1].toLocaleString()+' bytes<br>'+esc(f[3]||'sub_'+f[0].toString(16))+(f[4]?' <span class="m">'+esc(f[4])+'</span>':'')+
(f[5]?'<br><span class="m">cited in '+esc(f[5])+'</span>':'')}
maps.forEach(m=>{m.cv.addEventListener('mousemove',e=>{if(!pinned)show(m,at(m,e))});
m.cv.addEventListener('mouseleave',()=>{if(!pinned)show(m,null)});
m.cv.addEventListener('click',e=>{const f=at(m,e),old=pinned;pinned=f&&!(old&&old.f===f)?{m,f}:null;show(m,pinned&&pinned.f);draw(m);if(old&&old.m!==m)draw(old.m)})});
let rt=0;addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(drawAll,60)});
try{matchMedia('(prefers-color-scheme: dark)').addEventListener('change',drawAll)}catch(e){}
new MutationObserver(drawAll).observe(document.documentElement,{attributes:true,attributeFilter:['data-theme']});
drawAll();if(document.fonts)document.fonts.ready.then(drawAll);
</script></body></html>
"""
    COL = {'ported': ('#1f8a4c', '#38b56a'), 'read': ('#62b47e', '#4f9466'), 'helper': ('#a9d6b6', '#2f5a3e'),
           'platform': ('#7d8794', '#5e6772'), 'unused': ('#d2d3cc', '#3a3f3c'), 'open': ('#d2453c', '#f0625a')}
    light = ';'.join(f'--s-{c}:{COL[c][0]}' for c in CLASSES)
    dark = ';'.join(f'--s-{c}:{COL[c][1]}' for c in CLASSES)
    data, sections, opens = {}, [], []
    allb = collections.Counter()
    nf = 0
    for p in PROGS:
        x = X[p]
        rows = []
        by = collections.Counter()
        for f in x.funcs:
            a, e = x.func_range(f)
            c = cls[p, f]
            by[c] += e - a
            nf += 1
            rows.append([f, e - a, CLASSES.index(c), x.names.get(f, ''), src[p, f] if src[p, f] != '?' else '',
                         ' '.join(sorted(cov[p].get(f, ())))])
            if c == 'open' and ninsn(p, f) >= min_n:
                opens.append((e - a, p, f))
        allb.update(by)
        data[p] = rows
        tot = sum(by.values()) or 1
        bar = ''.join(f'<i class="s-{c}" style="flex:{by[c]}" title="{c} {100.0 * by[c] / tot:.1f} %"></i>'
                      for c in CLASSES if by[c])
        sections.append(f'<section><h2>{FILES[p]}</h2><div class="bar" aria-hidden="true">{bar}</div>'
                        f'<div class="panel map"><div class="wrap"><canvas data-p="{p}" data-n="{FILES[p]}" role="img" aria-label="'
                        f'Map of every function of {FILES[p]} by address, coloured by class"></canvas></div></div></section>')
    tot = sum(allb.values()) or 1
    legend = ''.join(f'<li><i class="sw s-{c}"></i><span class="nm">{c}</span><span class="pc">{100.0 * allb[c] / tot:.1f} %'
                     f'</span><span class="ds">{escape(WHAT[c])} &middot; {sum(1 for k in cls if cls[k] == c):,} functions, '
                     f'{allb[c] / 1024:,.0f} KB</span></li>' for c in CLASSES)
    opens.sort(reverse=True)
    trs = ''.join(f'<tr><td class="mono">{FILES[p]}</td><td class="mono">{f:#x}</td><td class="num">{n:,}</td>'
                  f'<td>{escape(src[p, f])}</td><td>{escape("; ".join(dict.fromkeys(s.strip()[:40] for s in X[p].strings_of(f)))[:160])}</td></tr>'
                  for n, p, f in opens[:80]) or '<tr><td colspan="5">None: every function the game reaches is cited, a helper or platform code.</td></tr>'
    more = f'<p class="how">{len(opens) - 80} more in docs/COVERAGE.md.</p>' if len(opens) > 80 else ''
    page = HTML.replace('@LIGHT@', light).replace('@DARK@', dark).replace('@LEGEND@', legend) \
        .replace('@SECTIONS@', ''.join(sections)).replace('@OPEN@', trs + '').replace('@MORE@', more) \
        .replace('@MIN@', str(min_n)).replace('@NF@', f'{nf:,}').replace('@KB@', f'{tot / 1024:,.0f}') \
        .replace('@N@', json.dumps(CLASSES)).replace('@DATA@', json.dumps(data, separators=(',', ':')))
    path = os.path.join(ROOT, html)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w', encoding='utf-8', newline='\n').write(page)
    print('->', html)
