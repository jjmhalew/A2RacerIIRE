"""imgpack.py - A2 Racer II resource archives: <name>.IND (index) + <name>.IMG (data).

IND layout (little endian), two kinds, told apart by the file size:
  named   (RCS, Mrcs):    u16 count, count x { char name[20] (NUL-terminated, rest is junk), u32 offset }
  unnamed (RC, MENURC):   u16 count, count x { u32 offset }
An entry's size is the next entry's offset (or the IMG size) minus its own offset. See docs/FORMAT_IMG.md.

usage: python tools/imgpack.py list  extract/data/RCS
       python tools/imgpack.py unpack extract/data/RCS out/rcs
"""
import os, struct, sys


def read_index(base):
    ind = _find(base, ('.IND', '.ind'))
    img = _find(base, ('.IMG', '.img'))
    data = open(ind, 'rb').read()
    count = struct.unpack_from('<H', data)[0]
    entries = []
    if len(data) == 2 + count * 24:
        for i in range(count):
            raw, off = struct.unpack_from('<20sI', data, 2 + i * 24)
            entries.append([raw.split(b'\0')[0].decode('latin-1'), off])
    elif len(data) == 2 + count * 4:
        for i in range(count):
            entries.append(['%04d' % i, struct.unpack_from('<I', data, 2 + i * 4)[0]])
    else:
        raise ValueError(f'{ind}: unknown index layout ({len(data)} bytes, count {count})')
    total = os.path.getsize(img)
    for i, e in enumerate(entries):
        nxt = entries[i + 1][1] if i + 1 < len(entries) else total
        e.append(nxt - e[1])
    return img, entries


def _find(base, exts):
    for x in exts:
        if os.path.exists(base + x):
            return base + x
    raise FileNotFoundError(base + exts[0])


if __name__ == '__main__':
    cmd, base = sys.argv[1], sys.argv[2]
    img, entries = read_index(base)
    if cmd == 'list':
        for i, (name, off, size) in enumerate(entries):
            print(f'{i:4} {off:#10x} {size:9} {name}')
    elif cmd == 'unpack':
        out = sys.argv[3]
        os.makedirs(out, exist_ok=True)
        with open(img, 'rb') as f:
            for i, (name, off, size) in enumerate(entries):
                f.seek(off)
                open(os.path.join(out, name if not name.isdigit() else name + '.bin'), 'wb').write(f.read(size))
        print(f'{len(entries)} files -> {out}')
