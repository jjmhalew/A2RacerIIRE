"""disasm.py - annotated disassembly of the A2 Racer II executables (spel.dat = the race, menu.dat = the menus).

The binaries are MSVC 5.0 builds without symbols. This module:
  * linearly disassembles .text with capstone and finds functions (call targets + `push ebp; mov ebp, esp` prologues
    + the entry point), each function running to the next function start;
  * names imports (call [__imp_X]) and annotates immediates that point at strings in .rdata/.data;
  * reads names for functions and globals from docs/names/<exe>_<area>.txt (lines "0x401000 name  ; comment"; one
    file per subsystem, e.g. docs/names/spel_road.txt), so the listing improves as the analysis goes on.

Library use:   from disasm import Exe; x = Exe('extract/data/spel.dat'); x.listing(0x401000)
Command line:  python tools/disasm.py spel  0x4123a0            one function (by any address inside it)
               python tools/disasm.py spel  --xref 0x48a440      who references an address (string, global, function)
               python tools/disasm.py spel  --find "etappe%d"    functions referencing strings containing this text
               python tools/disasm.py spel  --funcs              all functions: address, size, name, first string
               python tools/disasm.py spel  --all out/spel.asm   whole program into one file
"""
import bisect, glob, os, re, struct, sys
from collections import defaultdict

import capstone
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXES = {'spel': 'extract/data/spel.dat', 'menu': 'extract/data/menu.dat', 'a2': 'extract/data/a2_II.exe',
        'qrace': 'extract/data/qrace.exe', 'instlng': 'extract/data/instlng.exe'}


class Exe:
    def __init__(self, path, names=None):
        self.path = path
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.entry = self.base + self.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.sections = []
        for s in self.pe.sections:
            va = self.base + s.VirtualAddress
            data = s.get_data().ljust(max(s.Misc_VirtualSize, s.SizeOfRawData), b'\0')
            self.sections.append((s.Name.rstrip(b'\0').decode(), va, data))
        self.imports = {}
        for e in getattr(self.pe, 'DIRECTORY_ENTRY_IMPORT', []):
            dll = e.dll.decode().split('.')[0]
            for i in e.imports:
                self.imports[i.address] = (i.name.decode() if i.name else f'{dll}#{i.ordinal}')
        text = [s for s in self.sections if s[0] == '.text'][0]
        self.text_va, self.text = text[1], text[2]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = False
        self._disasm()
        self.names = {}
        self.comments = {}
        tag = names or os.path.splitext(os.path.basename(path))[0]
        for fn in sorted(glob.glob(os.path.join(ROOT, 'docs', 'names', f'{tag}_*.txt'))):
            self._load_names(fn)

    # ---- memory ----
    def section_of(self, va):
        for name, start, data in self.sections:
            if start <= va < start + len(data):
                return name, start, data
        return None

    def read(self, va, n):
        s = self.section_of(va)
        if not s:
            return None
        off = va - s[1]
        return s[2][off:off + n]

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

    def f32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<f', b)[0] if b and len(b) == 4 else None

    def f64(self, va):
        b = self.read(va, 8)
        return struct.unpack('<d', b)[0] if b and len(b) == 8 else None

    def cstr(self, va, maxlen=200):
        s = self.section_of(va)
        if not s or s[0] == '.text':
            return None
        off = va - s[1]
        end = s[2].find(b'\0', off, off + maxlen)
        if end < 0:
            return None
        raw = s[2][off:end]
        if len(raw) < 2 or not all(32 <= c < 127 or c in (9, 10, 13) or c >= 0xa0 for c in raw):
            return None
        return raw.decode('latin-1')

    # ---- analysis ----
    def _disasm(self):
        self.insns = []  # (address, size, mnemonic, op_str)
        off = 0
        code = self.text
        while off < len(code):
            got = False
            for i in self.md.disasm_lite(code[off:], self.text_va + off):
                self.insns.append(i)
                off = i[0] + i[1] - self.text_va
                got = True
            if not got or off < len(code):
                if off < len(code):
                    self.insns.append((self.text_va + off, 1, 'db', f'{code[off]:#04x}'))
                    off += 1
        self.addrs = [i[0] for i in self.insns]
        starts = {self.entry}
        self.xrefs = defaultdict(list)  # target -> [insn address]
        hexre = re.compile(r'0x[0-9a-f]+')
        end = self.text_va + len(self.text)
        for k, (a, sz, mn, op) in enumerate(self.insns):
            if mn == 'call' and op.startswith('0x'):
                t = int(op, 16)
                if self.text_va <= t < end:
                    starts.add(t)
            if mn == 'push' and op == 'ebp' and k + 1 < len(self.insns) and self.insns[k + 1][2] == 'mov' \
                    and self.insns[k + 1][3] == 'ebp, esp':
                starts.add(a)
            for m in hexre.finditer(op):
                v = int(m.group(), 16)
                if v >= 0x400000:
                    self.xrefs[v].append(a)
        # also function pointers stored in data (vtables, callback tables)
        for name, va, data in self.sections:
            if name in ('.data', '.rdata'):
                for o in range(0, len(data) - 3, 4):
                    v = struct.unpack_from('<I', data, o)[0]
                    if self.text_va <= v < end and v in self._insn_set():
                        prev = self.insns[bisect.bisect_left(self.addrs, v) - 1]
                        if prev[2] in ('ret', 'int3', 'jmp', 'nop') or (prev[2] == 'db'):
                            starts.add(v)
                        self.xrefs[v].append(va + o)
        self.funcs = sorted(s for s in starts if s in self._insn_set())

    def _insn_set(self):
        if not hasattr(self, '_iset'):
            self._iset = set(self.addrs)
        return self._iset

    def _load_names(self, path):
        if not os.path.exists(path):
            return
        for line in open(path, encoding='utf-8'):
            line = line.split('#')[0].rstrip()
            if not line.strip():
                continue
            m = re.match(r'\s*(0x[0-9a-fA-F]+)\s+(\S+)\s*(?:;\s*(.*))?', line)
            if m:
                a = int(m.group(1), 16)
                self.names[a] = m.group(2)
                if m.group(3):
                    self.comments[a] = m.group(3)

    def func_of(self, va):
        i = bisect.bisect_right(self.funcs, va) - 1
        return self.funcs[i] if i >= 0 else None

    def func_range(self, f):
        i = bisect.bisect_left(self.funcs, f)
        end = self.funcs[i + 1] if i + 1 < len(self.funcs) else self.text_va + len(self.text)
        return f, end

    def func_insns(self, f):
        start, end = self.func_range(f)
        i = bisect.bisect_left(self.addrs, start)
        j = bisect.bisect_left(self.addrs, end)
        return self.insns[i:j]

    def name(self, va):
        if va in self.names:
            return self.names[va]
        if va in self.funcs:
            return f'sub_{va:x}'
        return None

    def annotate(self, op):
        notes = []
        m = re.search(r'dword ptr \[(0x[0-9a-f]+)\]', op)
        if m and int(m.group(1), 16) in self.imports:
            return self.imports[int(m.group(1), 16)]
        for h in re.findall(r'0x[0-9a-f]+', op):
            v = int(h, 16)
            if v < 0x400000:
                continue
            n = self.names.get(v) or (f'sub_{v:x}' if v in self.funcs else None)
            if n:
                notes.append(n)
                continue
            s = self.cstr(v)
            if s:
                notes.append(repr(s[:80]))
            elif v in self.comments:
                notes.append(self.comments[v])
            elif 'ptr' in op and self.section_of(v) and self.section_of(v)[0] == '.rdata':
                if 'qword' in op:
                    notes.append(f'= {self.f64(v):g}')
                elif 'dword' in op and self.f32(v) is not None:
                    fv = self.f32(v)
                    if fv == fv and 1e-6 < abs(fv) < 1e9:
                        notes.append(f'= {fv:g}f')
        return ', '.join(notes)

    def listing(self, va, out=None):
        f = self.func_of(va)
        lines = []
        start, end = self.func_range(f)
        title = self.name(f)
        tend = self.text_va + len(self.text)
        callers = sorted({c for c in (self.func_of(x) for x in self.xrefs.get(f, []) if self.text_va <= x < tend) if c})
        lines.append(f'; ===== {title} ({start:#x}..{end:#x}, {end - start} bytes)'
                     + (f'  ; {self.comments[f]}' if f in self.comments else ''))
        if callers:
            lines.append('; callers: ' + ' '.join(self.name(c) or hex(c) for c in callers if c)[:400])
        for a, sz, mn, op in self.func_insns(f):
            note = '' if (mn == 'call' or mn.startswith('j')) and op.startswith('0x') else self.annotate(op)
            if mn == 'call' and op.startswith('0x'):
                op = self.name(int(op, 16)) or op
            elif mn.startswith('j') and op.startswith('0x'):
                t = int(op, 16)
                op = (self.name(t) if t in self.names else f'{op}')
            if a in self.names and a != f:
                lines.append(f'{self.names[a]}:')
            lines.append(f'  {a:08x}  {mn:6} {op}' + (f'    ; {note}' if note else ''))
        text = '\n'.join(lines)
        if out:
            out.write(text + '\n\n')
        return text

    def strings_of(self, f):
        res = []
        for a, sz, mn, op in self.func_insns(f):
            for h in re.findall(r'0x[0-9a-f]+', op):
                s = self.cstr(int(h, 16))
                if s:
                    res.append(s)
        return res

    def calls_of(self, f):
        res = []
        for a, sz, mn, op in self.func_insns(f):
            if mn == 'call':
                if op.startswith('0x'):
                    res.append(int(op, 16))
                else:
                    m = re.search(r'\[(0x[0-9a-f]+)\]', op)
                    if m and int(m.group(1), 16) in self.imports:
                        res.append(self.imports[int(m.group(1), 16)])
        return res


def load(tag):
    return Exe(os.path.join(ROOT, EXES.get(tag, tag)), tag if tag in EXES else None)


if __name__ == '__main__':
    x = load(sys.argv[1])
    arg = sys.argv[2]
    if arg == '--xref':
        v = int(sys.argv[3], 16)
        for a in sorted(set(x.xrefs.get(v, []))):
            f = x.func_of(a) if x.text_va <= a < x.text_va + len(x.text) else None
            print(f'{a:#x}  in {x.name(f) if f else "(data)"}')
    elif arg == '--find':
        pat = sys.argv[3].lower()
        for f in x.funcs:
            ss = [s for s in x.strings_of(f) if pat in s.lower()]
            if ss:
                print(f'{f:#x} {x.name(f)}: {ss[:3]}')
    elif arg == '--funcs':
        for f in x.funcs:
            s, e = x.func_range(f)
            ss = x.strings_of(f)
            print(f'{f:#x} {e - s:6} {x.name(f):28} {repr(ss[0][:60]) if ss else ""}')
    elif arg == '--all':
        with open(sys.argv[3], 'w', encoding='utf-8') as o:
            for f in x.funcs:
                x.listing(f, o)
        print(f'{len(x.funcs)} functions -> {sys.argv[3]}')
    else:
        for a in sys.argv[2:]:
            print(x.listing(int(a, 16)))
