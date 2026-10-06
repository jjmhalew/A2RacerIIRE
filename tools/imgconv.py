"""imgconv.py - convert the A2 Racer II images and fonts to PNG (formats: docs/FORMAT_IMAGES.md).

Reads the archives directly (tools/imgpack.py) from extract/data and writes out/png/<bank>/...:
  RCS   (race)  *.pcx  8-bit RLE PCX, own 256-colour palette   -> <name>.png (index 254 = colour key -> transparent)
                *.tga  uncompressed 24/32-bit TGA                -> <name>.png (RGB 255,0,127 = colour key -> transparent)
                *.fnt  proportional sprite font                  -> <name>.png glyph sheet (16 x 16 cells, char code
                                                                    = row*16+col) + <name>.txt metrics
  Mrcs  (menu)  *.pcx  as above (no colour key)
                *.img  fixed-cell sprite font                    -> <name>.png glyph sheet + <name>.txt
  RC    (race HUD sprites, Menuanim.cpp)  NNNN.bin  {u16 w,h; s16 hotx,hoty; w*h 8-bit pixels}, 255 = transparent
                                                                                         -> NNNN.png
  MENURC(menu sprites, menu.dat)          same layout                                    -> NNNN.png
The 8-bit fonts and sprites carry no palette; they are drawn with the palette that is active at the time:
  race, 8-bit modes:  the track palette  e0N_pal.pcx  (indices 0..160, 254, 255 are the same for all tracks)
  race, 16-bit mode:  e02_pl16.pcx  (Raser.ini [PALET] second column, the same for every track)
  menu:               the palette of the menu screens (hoofd.pcx; all 7 menu screens share it)
RC entries 0..401 are the 16-bit HUD set (drawn with e02_pl16), 402..452 the 8-bit set (track palette).
The race fonts are written twice: <name>.png with e02_pl16 and <name>_pal8.png with e00_pal. Most fonts (also
several of the 8-bit set, e.g. Logeel, lo_vlag, MARC10) are drawn in e02_pl16 colour indices and only look right
with that palette; fonts made of indices 0..160 look the same with both.

usage: python tools/imgconv.py [--data extract/data] [--out out/png] [--gamma]   convert everything
       python tools/imgconv.py --test                                            parse every file, no output
--gamma applies the in-game 'gamma=1' darkening (each channel c -> c*c/256, pformat 0x468380).
"""
import argparse, os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import imgpack

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PCX_KEY = 254        # rcpcx 0x46af40: palette index [0x4a0be0] = 254 is replaced by the colour key (textures)
SPRITE_KEY = 255     # [0x49e8b0] = 255: transparent index of fonts (ascii 0x46a2d0) and sprites (sprite.cpp)
RC_FIRST_8BIT = 402  # RC entries from here on use the 8-bit track palette
TGA_KEY = (255, 0, 127)  # 0x7f00ff, set by sub_464d30 from sub_4234e0 (16-bit texture colour key)

# gfx_asci font table 0x486d08: [2 font sets][17 fonts], 0x94-byte records starting with the file name
FONTSET_8BIT = ['lo_fnt3', 'lomid', 'logeel', 'lo_geld', 'lo_vlag', 'lo_bts', 'lo_fnt1', 'marc10', 'slider13',
                'slider15', 'lo_fnt1', 'lo_fnt2', 'lo_fnt4', 'lo_fnt1', 'lo_fnt8', 'lo_fnt9', 'lo_fnt1']
FONTSET_16BIT = ['olfnt03', 'himid', 'higeel', 'hi_geld', 'hi_vlag', 'hi_bts', 'olfnt07', 'marc10', 'slider13',
                 'slider15', 'olfnt01', 'olfnt02', 'olfnt04', 'olfnt06', 'olfnt08', 'olfnt09', 'lo_fnt1']


# ---------------------------------------------------------------- PNG writer (no PIL needed)
def write_png(path, w, h, rgba):
    """rgba: bytes, w*h*4."""
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(png)


def gamma_lut(on):
    return bytes((c * c) >> 8 if on else c for c in range(256))


def indexed_to_rgba(pix, pal, key=None, lut=None):
    """pix: bytes of indices; pal: 768 bytes; key: index drawn transparent."""
    lut = lut or gamma_lut(False)
    table = []
    for i in range(256):
        r, g, b = pal[i * 3:i * 3 + 3]
        table.append(bytes((lut[r], lut[g], lut[b], 0 if i == key else 255)))
    return b''.join(table[p] for p in pix)


# ---------------------------------------------------------------- PCX (rcpcx.cpp 0x46ae60..0x46bd80)
def read_pcx(data):
    """-> (w, h, pixels bytes w*h, palette 768 bytes). Version 5, RLE, 8 bpp, 1 plane, palette at EOF-768 (0x0c)."""
    man, ver, enc, bpp, x0, y0, x1, y1 = struct.unpack_from('<4B4H', data, 0)
    planes, bpl = data[65], struct.unpack_from('<H', data, 66)[0]
    if man != 10 or enc != 1 or bpp != 8 or planes != 1:
        raise ValueError(f'unsupported PCX (man {man} enc {enc} bpp {bpp} planes {planes})')
    if data[-769] != 0x0c:
        raise ValueError('PCX without 256-colour palette')
    w, h = x1 - x0 + 1, y1 - y0 + 1
    need, out, i, end = bpl * h, bytearray(), 128, len(data) - 769
    while len(out) < need:
        if i >= end:
            raise ValueError('PCX RLE data runs into the palette')
        v = data[i]
        i += 1
        if v >= 0xc0:          # RLE runs may cross scan lines (the encoder does that)
            out += bytes([data[i]]) * (v & 0x3f)
            i += 1
        else:
            out.append(v)
    if i != end:
        raise ValueError(f'PCX: {end - i} unused bytes before the palette')
    pix = b''.join(bytes(out[y * bpl:y * bpl + w]) for y in range(h))
    return w, h, pix, data[-768:]


# ---------------------------------------------------------------- TGA (tga.cpp 0x4636d0)
def read_tga(data):
    """-> (w, h, rgba). Type 2 only, 24 or 32 bpp, bottom-up unless descriptor bit 5. TGA 2.0 footers are ignored."""
    idlen, cmtype, itype = data[0], data[1], data[2]
    w, h, bpp, desc = struct.unpack_from('<HHBB', data, 12)
    if cmtype != 0 or itype != 2 or bpp not in (24, 32):
        raise ValueError(f'unsupported TGA (colormap {cmtype} type {itype} bpp {bpp})')
    n, off = bpp // 8, 18 + idlen
    if len(data) < off + w * h * n:
        raise ValueError('TGA truncated')
    rows = []
    for y in range(h):
        row = bytearray()
        line = data[off + y * w * n: off + (y + 1) * w * n]
        for x in range(w):
            b, g, r = line[x * n:x * n + 3]
            row += bytes((r, g, b, 0 if (r, g, b) == TGA_KEY else 255))   # alpha byte of 32-bit files is skipped
        rows.append(bytes(row))
    if not desc & 0x20:
        rows.reverse()
    return w, h, b''.join(rows)


# ---------------------------------------------------------------- .fnt (ascii.cpp Load_new_ascii_set 0x46a2d0)
def read_fnt(data):
    """-> (header 4 x s16, {code: (w, h, pixels)}). 256 x {s16 w, s16 h, u32 file offset}; w or h <= 0 = no glyph;
    then 4 x s16 (unused by the drawing code); glyph pixels are 8-bit palette indices, 255 transparent."""
    glyphs = {}
    for c in range(256):
        w, h, ofs = struct.unpack_from('<hhI', data, c * 8)
        if w > 0 and h > 0:
            if ofs + w * h > len(data):
                raise ValueError(f'glyph {c} outside file')
            glyphs[c] = (w, h, data[ofs:ofs + w * h])
    hdr = struct.unpack_from('<4h', data, 2048)
    end = max((struct.unpack_from('<I', data, c * 8 + 4)[0] + g[0] * g[1] for c, g in glyphs.items()), default=2056)
    if end != len(data):
        raise ValueError(f'.fnt size {len(data)} != end of last glyph {end}')
    return hdr, glyphs


# ---------------------------------------------------------------- .img fonts (menu ascii.cpp 0x40eec0)
def read_img_font(data):
    """-> (w, h, hdr, {code: pixels}). u8 present[256]; s16 w, h, a, b; then 256 cells of w*h bytes (also for
    absent characters)."""
    present = data[:256]
    w, h, a, b = struct.unpack_from('<4h', data, 256)
    if len(data) != 264 + 256 * w * h:
        raise ValueError(f'.img font size {len(data)} != 264 + 256*{w}*{h}')
    glyphs = {c: data[264 + c * w * h: 264 + (c + 1) * w * h] for c in range(256) if present[c]}
    return w, h, (a, b), glyphs


# ---------------------------------------------------------------- RC / MENURC sprites (sprite.cpp 0x46c1c0)
def read_sprite(data):
    """-> (w, h, hotx, hoty, pixels)."""
    w, h, hx, hy = struct.unpack_from('<HHhh', data, 0)
    if len(data) != 8 + w * h:
        raise ValueError(f'sprite size {len(data)} != 8 + {w}*{h}')
    return w, h, hx, hy, data[8:]


def glyph_sheet(glyphs, pal, lut, key=SPRITE_KEY):
    """glyphs {code: (w, h, pix)} -> (W, H, rgba) 16x16 grid, 1-pixel grey grid lines."""
    cw = max(g[0] for g in glyphs.values()) + 1
    ch = max(g[1] for g in glyphs.values()) + 1
    W, H = 16 * cw + 1, 16 * ch + 1
    img = bytearray(b'\x40\x40\x40\xff' * (W * H))
    for c in range(256):
        x0, y0 = (c % 16) * cw + 1, (c // 16) * ch + 1
        for y in range(ch - 1):          # empty cell = transparent
            img[((y0 + y) * W + x0) * 4:((y0 + y) * W + x0 + cw - 1) * 4] = b'\0' * ((cw - 1) * 4)
        if c in glyphs:
            w, h, pix = glyphs[c]
            rgba = indexed_to_rgba(pix, pal, key, lut)
            for y in range(h):
                o = ((y0 + y) * W + x0) * 4
                img[o:o + w * 4] = rgba[y * w * 4:(y + 1) * w * 4]
    return W, H, bytes(img)


# ---------------------------------------------------------------- driver
def bank(data_dir, name):
    img, entries = imgpack.read_index(os.path.join(data_dir, name))
    with open(img, 'rb') as f:
        for ename, off, size in entries:
            f.seek(off)
            yield ename, f.read(size)


def convert(data_dir, out, test=False, gamma=False):
    lut = gamma_lut(gamma)
    errors, counts = [], {}
    files = {b: dict(bank(data_dir, b)) for b in ('RCS', 'Mrcs', 'RC', 'MENURC')}
    lower = {b: {k.lower(): v for k, v in d.items()} for b, d in files.items()}
    pal_8 = read_pcx(lower['RCS']['e00_pal.pcx'])[3]
    pal_16 = read_pcx(lower['RCS']['e02_pl16.pcx'])[3]
    pal_menu = read_pcx(lower['Mrcs']['hoofd.pcx'])[3]

    def emit(path, w, h, rgba):
        if not test:
            write_png(os.path.join(out, path), w, h, rgba)

    for b, entries in files.items():
        for name, data in entries.items():
            base, ext = os.path.splitext(name)
            ext = ext.lower() or '.bin'
            try:
                if ext == '.pcx':
                    w, h, pix, pal = read_pcx(data)
                    emit(f'{b}/{base}.png', w, h, indexed_to_rgba(pix, pal, PCX_KEY if b == 'RCS' else None, lut))
                elif ext == '.tga':
                    w, h, rgba = read_tga(data)
                    if gamma:
                        rgba = bytes(lut[v] if i % 4 != 3 else v for i, v in enumerate(rgba))
                    emit(f'{b}/{base}_tga.png', w, h, rgba)
                elif ext == '.fnt':
                    hdr, glyphs = read_fnt(data)
                    for suffix, pal in (('', pal_16), ('_pal8', pal_8)):
                        emit(f'{b}/{base}{suffix}.png', *glyph_sheet(glyphs, pal, lut))
                    if not test:
                        with open(os.path.join(out, b, base + '.txt'), 'w') as f:
                            sets = [f'{t}[{i}]' for t, lst in (('8bit', FONTSET_8BIT), ('16bit', FONTSET_16BIT))
                                    for i, n in enumerate(lst) if n == base.lower()]
                            f.write(f'# {name}: header {hdr}; gfx_asci font slots: {" ".join(sets) or "unused"}\n')
                            f.write('# code char w h\n')
                            for c, (w, h, _) in sorted(glyphs.items()):
                                f.write(f'{c:3} {chr(c) if 32 < c < 127 else "."} {w:3} {h:3}\n')
                elif ext == '.img':
                    w, h, hdr, glyphs = read_img_font(data)
                    emit(f'{b}/{base}.png', *glyph_sheet({c: (w, h, p) for c, p in glyphs.items()}, pal_menu, lut))
                    if not test:
                        with open(os.path.join(out, b, base + '.txt'), 'w') as f:
                            f.write(f'# {name}: fixed cell {w}x{h}, header {hdr}; chars present:\n')
                            f.write(''.join(chr(c) if 32 <= c < 127 else f'\\x{c:02x}' for c in sorted(glyphs)) + '\n')
                elif ext == '.bin':
                    w, h, hx, hy, pix = read_sprite(data)
                    pal = pal_menu if b == 'MENURC' else (pal_16 if int(base) < RC_FIRST_8BIT else pal_8)
                    if w and h:
                        emit(f'{b}/{base}.png', w, h, indexed_to_rgba(pix, pal, SPRITE_KEY, lut))
                elif ext in ('.wav', '.d3d'):
                    continue
                else:
                    raise ValueError('unknown extension')
                counts[(b, ext)] = counts.get((b, ext), 0) + 1
            except Exception as e:   # noqa: BLE001 - report every bad file
                errors.append(f'{b}/{name}: {e}')
    for (b, ext), n in sorted(counts.items()):
        print(f'{b:7} {ext:5} {n:4} ok')
    for e in errors:
        print('ERROR', e)
    return not errors


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', default=os.path.join(ROOT, 'extract', 'data'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'png'))
    ap.add_argument('--test', action='store_true', help='only parse every file (self-test)')
    ap.add_argument('--gamma', action='store_true', help="apply the game's gamma=1 darkening")
    a = ap.parse_args()
    ok = convert(a.data, a.out, a.test, a.gamma)
    print('self-test passed' if ok and a.test else ('done' if ok else 'FAILED'))
    sys.exit(0 if ok else 1)
