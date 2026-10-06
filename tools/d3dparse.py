"""d3dparse.py - A2 Racer II .d3d meshes and the mesh/texture/anim tables of spel.dat. See docs/FORMAT_D3D.md.

.d3d (little endian), loaded by mesh_load_d3d 0x455b50:
    u32 nverts, u32 nfaces
    nverts x 33 bytes: f32 x,y,z, nx,ny,nz, u,v ; u8 marker (1 or 109, ignored by the game)
    nfaces x  9 bytes: u16 a,b,c ; u16 word ; u8 marker      (word + marker ignored by the game)
A mesh carries no texture: the texture comes from the "frame" that pairs a mesh with a texture
(frame table 0x48e5e8 in spel.dat), frames are grouped into "anims" (0x492050, names 0x490430), and
objects in the .omp files name an anim. This module reads those tables straight out of spel.dat.

usage: python tools/d3dparse.py test                       self-test over out/RCS + spel.dat tables
       python tools/d3dparse.py info  n_golf.d3d           header, bbox, uv range, textures
       python tools/d3dparse.py export n_golf.d3d [--pcx]  -> out/d3d/n_golf.obj/.mtl + tex/*.png
       python tools/d3dparse.py export all [--pcx]         every mesh in out/RCS
       python tools/d3dparse.py anims                      dump the anim -> frames -> (mesh, texture) table
       python tools/d3dparse.py configs                    dump the per-track/per-mode load lists
"""
import glob, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RCS = os.path.join(ROOT, 'out', 'RCS')
SPEL = os.path.join(ROOT, 'extract', 'data', 'spel.dat')
OUT = os.path.join(ROOT, 'out', 'd3d')

VERT = struct.Struct('<8fB')   # 33 bytes
FACE = struct.Struct('<3HHB')  # 9 bytes
COLOR_KEY = (255, 0, 127)      # palette index 254 of every texture PCX; TEXT.CPP colour key 0x49b024 = 0xfe


class Mesh:
    """verts: list of (x, y, z, nx, ny, nz, u, v); faces: list of (a, b, c)."""

    def __init__(self, name, verts, vmark, faces, fword, fmark, prefix=0):
        self.name, self.verts, self.vmark, self.faces = name, verts, vmark, faces
        self.fword, self.fmark, self.prefix = fword, fmark, prefix

    def bbox(self):
        if not self.verts:
            return None
        return ([min(v[k] for v in self.verts) for k in range(3)],
                [max(v[k] for v in self.verts) for k in range(3)])


def parse(data, name=''):
    """Parse a .d3d image. Raises ValueError unless the file is consumed exactly to EOF.
    prefix=24: E04_koor.D3D starts with 24 zero bytes (the game would read nverts = nfaces = 0 there)."""
    for prefix in (0, 24):
        if prefix and (len(data) < prefix + 8 or any(data[:prefix])):
            continue
        nv, nf = struct.unpack_from('<II', data, prefix)
        if prefix + 8 + nv * 33 + nf * 9 == len(data) and (nv or nf or not prefix):
            break
    else:
        raise ValueError(f'{name}: size {len(data)} does not match any header')
    o = prefix + 8
    verts, vmark, faces, fword, fmark = [], [], [], [], []
    for i in range(nv):
        *v, m = VERT.unpack_from(data, o)
        verts.append(tuple(v)); vmark.append(m); o += 33
    for i in range(nf):
        a, b, c, w, m = FACE.unpack_from(data, o)
        faces.append((a, b, c)); fword.append(w); fmark.append(m); o += 9
    assert o == len(data)
    return Mesh(name, verts, vmark, faces, fword, fmark, prefix)


def rcs_files():
    """{lower-case name: path} for out/RCS (unpacked RCS.IMG)."""
    return {os.path.basename(p).lower(): p for p in glob.glob(os.path.join(RCS, '*'))}


def rcs_archive():
    """All entries of extract/data/RCS.IMG as [(name, bytes)] (448 .d3d entries; DEFAULT.d3d is stored twice,
    so the unpacked out/RCS directory holds 447 .d3d files)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from imgpack import read_index
    img, entries = read_index(os.path.join(ROOT, 'extract', 'data', 'RCS'))
    data = open(img, 'rb').read()
    return [(n, data[o:o + sz]) for n, o, sz in entries]


def load(name):
    p = rcs_files().get(name.lower())
    if not p:
        raise FileNotFoundError(name)
    return parse(open(p, 'rb').read(), os.path.basename(p))


# ---------------------------------------------------------------- spel.dat tables
class Spel:
    """Static object tables of spel.dat (addresses: docs/FORMAT_OBJECTS.md, docs/names/spel_objects.txt)."""
    MESH_TABLE, N_MESH = 0x48d0e0, 357      # 12-byte {char *name; int handle=-1; int refcount}, + NULL entry
    TEX_TABLE, N_TEX = 0x48e1a0, 91         # same layout
    FRAME_TABLE, N_FRAME = 0x48e5e8, 386    # 16-byte {int mesh_handle=-1; int tex_handle=-1; int mesh; int tex}
    ANIM_TABLE, N_ANIM = 0x492050, 312      # 9-byte packed {int nframes; int *frames; u8 flag=1}
    ANIM_NAMES = 0x490430                   # 312 x char*  (parallel to ANIM_TABLE, ends at 0x490910)
    # (loader function, anim index list, texture index list, colour mode, what) - called by load_anim_config 0x4236d0
    CONFIGS = [
        (0x42b360, 0x490910, 0x490970, 16, 'common'),
        (0x42b700, 0x4909d0, 0x490a20, 8, 'common, packed'), (0x42baa0, 0x490a70, 0x490a88, 8, 'common'),
        (0x42be40, 0x490aa0, 0x490ba8, 16, 'highway (etappe 1,3,5)'),
        (0x42c1e0, 0x490cb0, 0x490db8, 8, 'highway, packed'), (0x42c580, 0x490ec0, 0x490ec8, 8, 'highway'),
        (0x42c920, 0x490ed0, 0x490fb8, 16, 'city (etappe 0,2,4,6)'),
        (0x42ccc0, 0x4910a0, 0x491180, 8, 'city, packed'), (0x42d060, 0x491260, 0x491270, 8, 'city'),
        (0x42d400, 0x491280, 0x491350, 16, 'etappe 0/6'),
        (0x42d7a0, 0x491420, 0x491428, 8, 'etappe 0/6, packed'), (0x42db40, 0x491430, 0x4914f8, 8, 'etappe 0/6'),
        (0x42dee0, 0x4915c0, 0x491618, 16, 'etappe 1'),
        (0x42e280, 0x491670, 0x491678, 8, 'etappe 1, packed'), (0x42e620, 0x491680, 0x4916d0, 8, 'etappe 1'),
        (0x42e9c0, 0x491720, 0x4917d0, 16, 'etappe 2'),
        (0x42ed60, 0x491880, 0x491888, 8, 'etappe 2, packed'), (0x42f100, 0x491890, 0x491938, 8, 'etappe 2'),
        (0x42f4a0, 0x4919e0, 0x491a10, 16, 'etappe 3'),
        (0x42f840, 0x491a40, 0x491a48, 8, 'etappe 3, packed'), (0x42fbe0, 0x491a50, 0x491a80, 8, 'etappe 3'),
        (0x42ff80, 0x491ab0, 0x491b48, 16, 'etappe 4'),
        (0x430320, 0x491be0, 0x491be8, 8, 'etappe 4, packed'), (0x4306c0, 0x491bf0, 0x491c80, 8, 'etappe 4'),
        (0x430a60, 0x491d10, 0x491d40, 16, 'etappe 5'),
        (0x430e00, 0x491d70, 0x491d78, 8, 'etappe 5, packed'), (0x4311a0, 0x491d80, 0x491da8, 8, 'etappe 5'),
    ]

    def __init__(self, path=SPEL):
        import pefile
        pe = pefile.PE(path, fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        self.secs = [(base + s.VirtualAddress, s.get_data().ljust(max(s.Misc_VirtualSize, s.SizeOfRawData), b'\0'))
                     for s in pe.sections]
        self.meshes = self._names(self.MESH_TABLE, self.N_MESH)
        self.textures = self._names(self.TEX_TABLE, self.N_TEX)
        self.frames = [struct.unpack('<4i', self.read(self.FRAME_TABLE + 16 * i, 16)) for i in range(self.N_FRAME)]
        self.anims = []
        for i in range(self.N_ANIM):
            n, p, flag = struct.unpack('<iIB', self.read(self.ANIM_TABLE + 9 * i, 9))
            name = self.cstr(self.u32(self.ANIM_NAMES + 4 * i))
            # frame lists live at 0x48fe08..0x492050; OBJ_LIGHT01_ANIM points at the zeroed int 0x4dcaac (.bss) = [0]
            fr = list(struct.unpack(f'<{n}i', self.read(p, 4 * n)))
            self.anims.append((name, fr, flag, p))
        self.anim_index = {a[0].lower(): i for i, a in enumerate(self.anims)}

    def read(self, va, n):
        for start, data in self.secs:
            if start <= va < start + len(data):
                return data[va - start:va - start + n]
        raise ValueError(hex(va))

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def cstr(self, va):
        b = self.read(va, 64)
        return b[:b.index(b'\0')].decode('latin-1')

    def _names(self, table, n):
        out = [self.cstr(self.u32(table + 12 * i)) for i in range(n)]
        assert self.u32(table + 12 * n) == 0, 'table not NULL-terminated'
        return out

    def ilist(self, va):
        out = []
        while True:
            v = struct.unpack('<i', self.read(va, 4))[0]
            if v == -1:
                return out
            out.append(v); va += 4

    def anim_frames(self, anim):
        """[(mesh name, texture name)] for an anim index or (case-insensitive) name."""
        if isinstance(anim, str):
            anim = self.anim_index[anim.lower()]
        fr = self.anims[anim][1] or []
        return [(self.meshes[self.frames[f][2]], self.textures[self.frames[f][3]]) for f in fr]

    def texture_only(self, i):
        """Anims that only carry textures (road textures, radar, sfx); their frames use r_porche.d3d as a dummy."""
        n = self.anims[i][0]
        return n.endswith('TEXTURES_ANIM') or n == 'S_SFX_ANIM'

    def mesh_textures(self):
        """{mesh name lower: [texture names]} over all real (non texture-only) anims."""
        out = {}
        for i, (name, fr, flag, p) in enumerate(self.anims):
            if self.texture_only(i):
                continue
            for m, t in self.anim_frames(i):
                lst = out.setdefault(m.lower(), [])
                if t not in lst:
                    lst.append(t)
        return out


# ---------------------------------------------------------------- export
def texture_png(tex, use_pcx=False, files=None, outdir=None):
    """Convert a texture (table name, e.g. 'cars01.pcx') to out/d3d/tex/<base>.png with the colour key as alpha.
    16-bit mode loads <base>.tga, 8-bit mode <base>.pcx (TEXT.CPP 0x45ae00)."""
    from PIL import Image
    files = files or rcs_files()
    outdir = outdir or os.path.join(OUT, 'tex')
    base = os.path.splitext(tex)[0].lower()
    src = files.get(base + ('.pcx' if use_pcx else '.tga')) or files.get(base + '.pcx')
    if not src:
        return None
    os.makedirs(outdir, exist_ok=True)
    dst = os.path.join(outdir, base + ('_8' if use_pcx else '') + '.png')
    if not os.path.exists(dst):
        im = Image.open(src)
        if im.mode == 'P':
            idx = im.getdata() if not hasattr(im, 'get_flattened_data') else im.get_flattened_data()
            rgba = im.convert('RGBA')
            rgba.putalpha(Image.frombytes('L', im.size, bytes(0 if i == 254 else 255 for i in idx)))
        else:
            rgba = im.convert('RGBA')
            px = rgba.load()
            for y in range(rgba.height):
                for x in range(rgba.width):
                    r, g, b, a = px[x, y]
                    # key compare happens after conversion to the 16-bit surface format (5:6:5 assumed)
                    if r >> 3 == 31 and g >> 2 == 0 and b >> 3 == 15:
                        px[x, y] = (r, g, b, 0)
        rgba.save(dst)
    return dst


def export_obj(mesh, textures, outdir=OUT, use_pcx=False, files=None):
    """Write <outdir>/<mesh>.obj + .mtl. D3D left-handed y-up -> OBJ right-handed: z is negated (keeps the
    on-screen winding of D3D's clockwise front faces as OBJ counter-clockwise), v -> 1-v. 1 unit ~ 10 cm."""
    os.makedirs(outdir, exist_ok=True)
    base = os.path.splitext(mesh.name)[0].lower()
    mtl = []
    for t in textures:
        png = texture_png(t, use_pcx, files, os.path.join(outdir, 'tex'))
        tb = os.path.splitext(t)[0].lower()
        mtl += [f'newmtl {tb}', 'Kd 1 1 1', 'Ka 0 0 0', 'illum 1']
        if png:
            rel = os.path.relpath(png, outdir).replace('\\', '/')
            mtl += [f'map_Kd {rel}', f'map_d {rel}']
        mtl.append('')
    with open(os.path.join(outdir, base + '.mtl'), 'w') as f:
        f.write('\n'.join(mtl) or '# no texture known for this mesh\n')
    with open(os.path.join(outdir, base + '.obj'), 'w') as f:
        f.write(f'# {mesh.name}: {len(mesh.verts)} vertices, {len(mesh.faces)} faces; textures {textures}\n')
        f.write(f'mtllib {base}.mtl\no {base}\n')
        for x, y, z, nx, ny, nz, u, v in mesh.verts:
            f.write(f'v {x:.4f} {y:.4f} {-z:.4f}\n')
        for x, y, z, nx, ny, nz, u, v in mesh.verts:
            f.write(f'vt {u:.5f} {1 - v:.5f}\n')
        for x, y, z, nx, ny, nz, u, v in mesh.verts:
            f.write(f'vn {nx:.4f} {ny:.4f} {-nz:.4f}\n')
        if textures:
            f.write(f'usemtl {os.path.splitext(textures[0])[0].lower()}\n')
        for a, b, c in mesh.faces:
            f.write(f'f {a+1}/{a+1}/{a+1} {b+1}/{b+1}/{b+1} {c+1}/{c+1}/{c+1}\n')
    return os.path.join(outdir, base + '.obj')


# ---------------------------------------------------------------- self-test
def selftest():
    files = rcs_files()
    d3d = sorted(n for n in files if n.endswith('.d3d'))
    arch = [(n, b) for n, b in rcs_archive() if n.lower().endswith('.d3d')]
    nverts = nfaces = 0
    prefixed, markers = [], {}
    for n, blob in arch:
        m = parse(blob, n)
        if m.prefix:
            prefixed.append((n, len(m.verts), len(m.faces)))
        for a, b, c in m.faces:
            assert max(a, b, c) < len(m.verts), (n, a, b, c)
        vm, fm, fw = set(m.vmark), set(m.fmark), set(m.fword)
        assert len(vm) <= 1 and len(fm) <= 1 and len(fw) <= 1, (n, vm, fm, fw)   # constant per file
        assert not m.verts or not m.faces or vm == fm, n                          # same marker in both
        mk = (vm or fm).pop()
        assert mk in (1, 109), (n, mk)
        if mk == 1:
            assert fw == {0x0700}, (n, fw)
        else:
            assert all(w & 0xf == 0 for w in fw), (n, fw)
        markers[mk] = markers.get(mk, 0) + 1
        nverts += len(m.verts); nfaces += len(m.faces)
    print(f'{len(arch)} .d3d entries of RCS.IMG ({len(d3d)} distinct names) parse to EOF ({nverts} vertices, {nfaces} faces); markers {markers}; '
          f'24-byte zero prefix: {prefixed}')

    s = Spel()
    assert all(m.lower() in files for m in s.meshes), 'mesh missing in RCS'
    for t in s.textures:
        b = os.path.splitext(t)[0].lower()
        assert b + '.pcx' in files and b + '.tga' in files, t
    for f in s.frames:
        assert f[0] == f[1] == -1 and 0 <= f[2] < s.N_MESH and 0 <= f[3] < s.N_TEX, f
    assert s.ANIM_NAMES + 4 * s.N_ANIM == 0x490910
    assert s.ANIM_TABLE + 9 * s.N_ANIM == 0x492b48 and s.FRAME_TABLE + 16 * s.N_FRAME == 0x48fe08
    used = set()
    for name, fr, flag, p in s.anims:
        assert flag == 1 and name.endswith('_ANIM'), name
        assert all(0 <= f < s.N_FRAME for f in fr), name
        used.update(fr)
        assert 0x48fe08 <= p < s.ANIM_TABLE or (name == 'OBJ_LIGHT01_ANIM' and p == 0x4dcaac), name
    for fn, al, tl, mode, what in s.CONFIGS:
        assert all(0 <= a < s.N_ANIM for a in s.ilist(al)) and all(0 <= t < s.N_TEX for t in s.ilist(tl))
    mt = s.mesh_textures()
    unref = sorted(n for n in d3d if n not in {m.lower() for m in s.meshes})
    print(f'spel.dat: {s.N_MESH} mesh names, {s.N_TEX} textures (.pcx+.tga all present), {s.N_FRAME} frames '
          f'({len(used)} used by anims), {s.N_ANIM} anims, {len(s.CONFIGS)} load lists')
    print(f'{len(mt)} meshes get a texture from an anim; {sum(len(v) > 1 for v in mt.values())} with >1 texture; '
          f'{len(unref)} .d3d files in RCS are not in the mesh table (e.g. {unref[:4]})')
    print('OK')


def main(argv):
    if not argv or argv[0] == 'test':
        return selftest()
    cmd = argv[0]
    use_pcx = '--pcx' in argv
    if cmd == 'info':
        m = load(argv[1])
        s = Spel()
        mn, mx = m.bbox() or ([0] * 3, [0] * 3)
        us = [v[6] for v in m.verts] or [0]; vs = [v[7] for v in m.verts] or [0]
        print(f'{m.name}: {len(m.verts)} verts, {len(m.faces)} faces, prefix {m.prefix}, marker '
              f'{set(m.vmark)}, face word {sorted(set(hex(w) for w in m.fword))}')
        print(f'  bbox {[round(a, 2) for a in mn]} .. {[round(a, 2) for a in mx]}; '
              f'u {min(us):.3f}..{max(us):.3f} v {min(vs):.3f}..{max(vs):.3f}')
        print(f'  textures {s.mesh_textures().get(m.name.lower(), [])}')
        print(f'  anims {[a[0] for i, a in enumerate(s.anims) if any(s.meshes[s.frames[f][2]].lower() == m.name.lower() for f in a[1]) and not s.texture_only(i)]}')
    elif cmd == 'export':
        s = Spel(); mt = s.mesh_textures(); files = rcs_files()
        names = sorted(n for n in files if n.endswith('.d3d')) if argv[1] == 'all' else [argv[1].lower()]
        for n in names:
            p = export_obj(parse(open(files[n], 'rb').read(), os.path.basename(files[n])), mt.get(n, []),
                           use_pcx=use_pcx, files=files)
            if len(names) == 1:
                print(p)
        print(f'{len(names)} mesh(es) -> {OUT}')
    elif cmd == 'anims':
        s = Spel()
        for i, (name, fr, flag, p) in enumerate(s.anims):
            print(f'{i:3} {name:28} ' + (', '.join(f'{f}:{m}+{t}' for f, (m, t) in zip(fr, s.anim_frames(i)))
                                        + (f'  (frame list in .bss at {p:#x})' if p > 0x4a0000 else '')))
    elif cmd == 'configs':
        s = Spel()
        for fn, al, tl, mode, what in s.CONFIGS:
            print(f'{fn:#x} {mode:2}-bit {what:24} anims@{al:#x}: {[s.anims[a][0] for a in s.ilist(al)]} '
                  f'textures@{tl:#x}: {[s.textures[t] for t in s.ilist(tl)]}')
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
