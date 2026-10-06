"""roadparse.py - A2 Racer II track road files: etappeN.rmp / .pmp / .cmp / .dmp (win3ddp rd_*.cpp).

Layouts (all little endian, no padding; see docs/FORMAT_ROAD.md for the full specification and code citations):

  .rmp  road mesh        u32 unused_total, u32 nchunks,
                         nchunks x { 4 x ( u32 nplanes, nplanes x PLANE ) }   groups in file order: road, land, extra, rail
        PLANE (104 B)    4 x { f32 x, y, z, tu, tv },  6 x i32 triangle corner indices (0..3, two triangles)
  .pmp  chunk data       u32 nchunks,
                         nchunks x { f32 x, y, z, i32 trigger_a, i32 trigger_b,
                                     u32 nrails, nrails x i32 rail,          (.cmp indices, <= 12)
                                     u32 nleft,  nleft  x i32 waypoint,      (.dmp indices, side 0, <= 12)
                                     u32 nright, nright x i32 waypoint }     (.dmp indices, side 1, <= 12)
  .cmp  guard rails      u32 n, n x { f32 px, py, pz, f32 dx, dy, dz }       (24 B: start point + vector to end)
  .dmp  driver map       u32 n, n x { i32 flags, i32 flags2, f32 x, y, z, i32 next, next_inner, next_outer, prev }

usage: python tools/roadparse.py test                 self-test over etappe0..6 (full consumption + consistency)
       python tools/roadparse.py stats  [N]           statistics for one or all tracks
       python tools/roadparse.py obj    [N] [outdir]  export road mesh(es) to out/road/etappeN.obj (+ .mtl)
       python tools/roadparse.py chunk  N C           dump one chunk
"""
import math, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'extract', 'data')

GROUPS = ('road', 'land', 'extra', 'rail')          # file order inside a .rmp chunk (rd_chunk.cpp 0x4431b0)
GROUP_MAX = {'road': 16, 'land': 24, 'extra': 12, 'rail': 12}
DRAW_ORDER = ('road', 'land', 'rail', 'extra')      # draw order in 0x445550
# Raser.ini [MAPS] argument for each group (0x4225d0 parse, 0x424131 -> 0x441de0 set_textures)
GROUP_TEX_ARG = {'road': 0, 'land': 1, 'extra': 2, 'rail': 3}    # index into (weg, gras, gra2, vang)


class Reader:
    def __init__(self, data, name):
        self.d, self.o, self.name = data, 0, name

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.o)[0]; self.o += 4; return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.o)[0]; self.o += 4; return v

    def f32s(self, n):
        v = struct.unpack_from('<%df' % n, self.d, self.o); self.o += 4 * n; return v

    def i32s(self, n):
        v = struct.unpack_from('<%di' % n, self.d, self.o); self.o += 4 * n; return list(v)

    def done(self):
        assert self.o == len(self.d), '%s: consumed %d of %d bytes' % (self.name, self.o, len(self.d))


def _read(track, ext):
    path = os.path.join(DATA, 'etappe%d.%s' % (track, ext))
    return Reader(open(path, 'rb').read(), os.path.basename(path))


# ---------------------------------------------------------------------------------------------------------------- files
def parse_rmp(track):
    """-> (unused_total, [chunk]) with chunk = {group: [plane]}, plane = {'v': [(x,y,z,tu,tv)]*4, 'tri': [6 ints]}"""
    r = _read(track, 'rmp')
    total, n = r.u32(), r.u32()
    chunks = []
    for _ in range(n):
        ch = {}
        for g in GROUPS:
            k = r.u32()
            assert k <= GROUP_MAX[g], (g, k)
            planes = []
            for _ in range(k):
                v = [r.f32s(5) for _ in range(4)]
                tri = r.i32s(6)
                planes.append({'v': v, 'tri': tri})
            ch[g] = planes
        chunks.append(ch)
    r.done()
    return total, chunks


def parse_pmp(track):
    r = _read(track, 'pmp')
    n = r.u32()
    chunks = []
    for _ in range(n):
        pos = r.f32s(3)
        ta, tb = r.i32(), r.i32()
        lists = []
        for _ in range(3):
            k = r.u32()
            assert k <= 12, k
            lists.append(r.i32s(k))
        chunks.append({'pos': pos, 'trigger': (ta, tb), 'rails': lists[0], 'wp_left': lists[1], 'wp_right': lists[2]})
    r.done()
    return chunks


def parse_cmp(track):
    r = _read(track, 'cmp')
    n = r.u32()
    rails = [{'p': r.f32s(3), 'd': r.f32s(3)} for _ in range(n)]
    r.done()
    return rails


def parse_dmp(track):
    r = _read(track, 'dmp')
    n = r.u32()
    wps = []
    for _ in range(n):
        f0, f1 = r.i32(), r.i32()
        pos = r.f32s(3)
        links = r.i32s(4)
        wps.append({'flags': f0, 'flags2': f1, 'pos': pos, 'links': links})
    r.done()
    return wps


def map_textures(track):
    """Raser.ini [MAPS] line for etappe<track> -> (weg, gras, gra2, vang) texture base names."""
    for line in open(os.path.join(DATA, 'Raser.ini'), encoding='latin-1'):
        line = line.split(';')[0].strip()
        if '=' in line and line.split('=')[0].strip().isdigit():
            args = line.split('=', 1)[1].split(',')
            if len(args) >= 6 and args[1].strip().lower() == 'etappe%d' % track:
                return tuple(a.strip() for a in args[2:6])
    return ('weg', 'gras', 'gra2', 'vang')


# ------------------------------------------------------------------------------------------------------ in-memory model
def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(a):
    l = math.sqrt(_dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l) if l else a


class Track:
    """The structures rd_main.cpp builds (0x444cf0): 4 global vertex/index pools and per-chunk index ranges."""

    def __init__(self, track):
        self.track = track
        self.unused_total, rmp = parse_rmp(track)
        self.pmp = parse_pmp(track)
        self.rails = parse_cmp(track)
        self.wps = parse_dmp(track)
        assert len(rmp) == len(self.pmp), 'Data-chunk count does not match draw-chunk count'
        self.n = len(rmp)
        self.tex = map_textures(track)
        # pools: vertices are D3DLVERTEX-like (x, y, z, tu, tv) here; colour 0x00FFFFFF, specular 0 in the original
        self.verts = {g: [] for g in GROUPS}
        self.index = {g: [] for g in GROUPS}       # u16 indices into self.verts[g]
        self.chunks = []
        for c, ch in enumerate(rmp):
            info = dict(self.pmp[c])
            for g in GROUPS:
                start = len(self.index[g])
                for pl in ch[g]:
                    base = len(self.verts[g])
                    self.verts[g].extend(pl['v'])
                    self.index[g].extend(base + t for t in pl['tri'])
                info[g] = (start, len(self.index[g]))   # chunk +0/+4 road, +8/+0xc land, +0x10/+0x14 rail, +0x18/+0x1c extra
            self.chunks.append(info)
        # chunk frames (0x442cb0): fwd = normalize(next.pos - pos), right = normalize(cross(Y, fwd)), up = cross(fwd, right)
        for c in range(self.n):
            nxt = self.chunks[(c + 1) % self.n]['pos']
            ch = self.chunks[c]
            fwd = _norm(_sub(nxt, ch['pos']))
            right = _norm(_cross((0.0, 1.0, 0.0), fwd))
            up = _norm(_cross(fwd, right))
            ch['fwd'], ch['right'], ch['up'] = fwd, right, up
            ch['plane_d'] = _dot(up, ch['pos'])
            # 0x442fe0: angles in 1024ths of a turn (rounded with ftol, i.e. truncated toward zero)
            ch['yaw'] = int((math.atan2(fwd[2], fwd[0]) - math.pi / 2) * 1024.0 / (2 * math.pi))   # chunk +0x58
            ch['pitch'] = int(math.atan2(fwd[1], math.hypot(fwd[0], fwd[2])) * 1024.0 / (2 * math.pi))  # +0x54
        if self.n >= 2:   # 0x445005..0x445195: last chunk copies right/up/fwd/matrix/plane_d of the one before it
            last, prev = self.chunks[-1], self.chunks[-2]
            for k in ('right', 'up', 'fwd', 'plane_d'):
                last[k] = prev[k]

    # 0x445a10: which chunk is (x, z) on, starting the search at chunk `start`
    def _behind(self, c, x, z):
        ch = self.chunks[c]
        p, r = ch['pos'], ch['right']
        return (x - p[0]) * r[2] - (z - p[2]) * r[0]      # > 0: point lies before chunk c's start line

    def find_chunk(self, start, x, z):
        n = self.n
        c = start
        if self._behind(c, x, z) > 0:                       # search backwards
            while c >= 0:
                if self._behind(c, x, z) < 0:
                    return c
                c -= 1
        else:                                               # search forwards
            while c < n:
                if self._behind(c, x, z) > 0:
                    return n - 1 if c - 1 == -1 else c - 1
                c += 1
        if c == 0:                                          # (unreachable for n > 0, kept for fidelity)
            for c in range(n - 1, -1, -1):
                if self._behind(c, x, z) < 0:
                    return c
            return c
        for c in range(n):                                  # full forward scan from chunk 0
            if self._behind(c, x, z) > 0:
                return n - 1 if c - 1 == -1 else c - 1
        return n

    # 0x4252b0 + 0x445cf0: chunk ranges drawn around the camera
    def visible_ranges(self, cam_chunk, view_dir, visible=30, behind=2):
        f = self.chunks[cam_chunk]['fwd']
        d = _dot(_norm(view_dir), f)
        if d > 0.5736:
            start = cam_chunk - behind
        elif d < -0.5736:
            start = cam_chunk + behind - visible
        else:
            start = cam_chunk - visible // 2
        if start < 0:
            start += self.n - 1                             # sic: n - 1, not n
        s = min(max(start, 0), self.n - 1)
        e = max(start + visible, 0)
        if e >= self.n:
            return [(s, self.n - 1), (0, e - self.n)]
        return [(s, e)]

    def waypoint_index(self, chunk, lane, side):
        """0x442150: side 0 = left list (+0x124), 1 = right list (+0x154); lane clamped to count-1."""
        lst = self.chunks[chunk]['wp_left' if side == 0 else 'wp_right']
        return lst[min(lane, len(lst) - 1)]

    def rail_box(self, i):
        """0x44c440 + 0x449c10: guard rail as an oriented box (local x in [-10,0], z in [-1,len+1]) at p, rotated yaw."""
        r = self.rails[i]
        dx, dz = r['d'][0], r['d'][2]
        return {'p': r['p'], 'len': math.hypot(dx, dz),
                'yaw1024': (math.atan2(dz, dx) - math.pi / 2) * 1024.0 / (2 * math.pi)}


# ---------------------------------------------------------------------------------------------------------------- tests
def selftest(verbose=True):
    for t in range(7):
        T = Track(t)
        nr, nd = len(T.rails), len(T.wps)
        rails_seen, wp_seen = [], []
        for c, ch in enumerate(T.chunks):
            rails_seen += ch['rails']; wp_seen += ch['wp_left'] + ch['wp_right']
            for g in GROUPS:
                s, e = ch[g]
                assert (e - s) % 6 == 0
        # rails and waypoints are partitioned over the chunks, in order
        assert rails_seen == list(range(nr)), 'rails not a partition'
        assert sorted(wp_seen) == list(range(nd)), 'waypoints not a partition'
        for g in GROUPS:
            assert len(T.verts[g]) <= T.n * {'road': 16, 'land': 24, 'extra': 12, 'rail': 12}[g] * 4
            assert len(T.verts[g]) < 65536
            assert all(0 <= i < len(T.verts[g]) for i in T.index[g])
        for w in T.wps:
            assert all(-1 <= l < nd for l in w['links'])
        total, rmp = parse_rmp(t)
        # .rmp header dword 0 (never used by the loader) = number of planes whose triangles are not (0,1,2,0,2,3)
        assert total == sum(1 for ch in rmp for g in GROUPS for pl in ch[g] if pl['tri'] != [0, 1, 2, 0, 2, 3])
        for ch in rmp:
            for g in GROUPS:
                for pl in ch[g]:
                    assert all(0 <= k <= 3 for k in pl['tri'])
        # find_chunk: every chunk position must map to itself (from a hint a few chunks away)
        bad = 0
        for c, ch in enumerate(T.chunks[:-1]):
            p, f = ch['pos'], ch['fwd']
            q = (p[0] + f[0] * 1.0, p[2] + f[2] * 1.0)
            got = T.find_chunk(max(c - 3, 0), q[0], q[1])
            bad += got != c
        if verbose:
            print('etappe%d: %4d chunks, planes %s, rails %d, waypoints %d, find_chunk mismatches %d, tex %s' % (
                t, T.n, {g: len(T.verts[g]) // 4 for g in GROUPS}, nr, nd, bad, ','.join(T.tex)))
    print('self-test OK: all 7 tracks fully consumed')


def stats(tracks):
    for t in tracks:
        T = Track(t)
        print('== etappe%d  (%d chunks, rmp header %d = sum of planes? %s)' % (
            t, T.n, T.unused_total, sum(len(T.verts[g]) // 4 for g in GROUPS)))
        for g in GROUPS:
            v = T.verts[g]
            if not v:
                continue
            col = list(zip(*v))
            print('  %-5s %5d planes  x %9.1f..%9.1f  y %7.1f..%7.1f  z %9.1f..%9.1f  tu %6.3f..%6.3f  tv %6.3f..%6.3f' % (
                g, len(v) // 4, min(col[0]), max(col[0]), min(col[1]), max(col[1]), min(col[2]), max(col[2]),
                min(col[3]), max(col[3]), min(col[4]), max(col[4])))
        seg = [math.dist(T.chunks[i]['pos'], T.chunks[i + 1]['pos']) for i in range(T.n - 1)]
        print('  chunk spacing %.1f..%.1f (mean %.1f); closing gap last->first %.1f' % (
            min(seg), max(seg), sum(seg) / len(seg), math.dist(T.chunks[-1]['pos'], T.chunks[0]['pos'])))
        trig = sorted({x for ch in T.chunks for x in ch['trigger'] if x})
        print('  trigger ids used:', trig)
        fl = {}
        for w in T.wps:
            fl[(w['flags'], w['flags2'])] = fl.get((w['flags'], w['flags2']), 0) + 1
        print('  waypoint (flags, flags2) counts:', dict(sorted(fl.items())))
        nl = [len(ch['wp_left']) for ch in T.chunks]; nrr = [len(ch['wp_right']) for ch in T.chunks]
        print('  lanes left %d..%d  right %d..%d  rails/chunk %d..%d' % (min(nl), max(nl), min(nrr), max(nrr),
              min(len(ch['rails']) for ch in T.chunks), max(len(ch['rails']) for ch in T.chunks)))
        # which side of the chunk centre line are the side-0 ("left") waypoints?
        sl = sr = 0
        for ch in T.chunks:
            for w in ch['wp_left']:
                sl += _dot(_sub(T.wps[w]['pos'], ch['pos']), ch['right']) > 0
            for w in ch['wp_right']:
                sr += _dot(_sub(T.wps[w]['pos'], ch['pos']), ch['right']) > 0
        print('  waypoints on +right side of chunk axis: left-list %d/%d, right-list %d/%d' % (
            sl, sum(nl), sr, sum(nrr)))


def export_obj(track, outdir=None, skip_origin=True):
    """skip_origin: drop triangles touching a vertex at exactly (0,0,0) - the unfinished last chunk of the
    point-to-point tracks etappe1/etappe3 (the original loads and draws them as they are)."""
    T = Track(track)
    outdir = outdir or os.path.join(ROOT, 'out', 'road')
    os.makedirs(outdir, exist_ok=True)
    base = os.path.join(outdir, 'etappe%d' % track)
    with open(base + '.mtl', 'w') as m:
        for g in GROUPS:
            tex = T.tex[GROUP_TEX_ARG[g]]
            m.write('newmtl %s\nKd 1 1 1\nmap_Kd %s.tga\n\n' % (tex, tex))
    with open(base + '.obj', 'w') as f:
        f.write('# A2 Racer II etappe%d road mesh (tools/roadparse.py); D3D left-handed coords, z mirrored for OBJ\n'
                % track)
        f.write('mtllib etappe%d.mtl\n' % track)
        off = 1
        for g in DRAW_ORDER:
            v = T.verts[g]
            for x, y, z, tu, tv in v:
                f.write('v %.3f %.3f %.3f\n' % (x, y, -z))
            for x, y, z, tu, tv in v:
                f.write('vt %.5f %.5f\n' % (tu, 1.0 - tv))
            f.write('o %s\nusemtl %s\n' % (g, T.tex[GROUP_TEX_ARG[g]]))
            idx = T.index[g]
            for i in range(0, len(idx), 3):
                if skip_origin and any(v[k][0] == 0 and v[k][1] == 0 and v[k][2] == 0 for k in idx[i:i + 3]):
                    continue
                a, b, c = idx[i] + off, idx[i + 1] + off, idx[i + 2] + off
                # z mirrored -> reverse winding to keep the faces pointing the same way
                f.write('f %d/%d %d/%d %d/%d\n' % (a, a, c, c, b, b))
            off += len(v)
    return base + '.obj'


def dump_chunk(track, c):
    T = Track(track)
    ch = T.chunks[c]
    for k in ('pos', 'trigger', 'fwd', 'right', 'up', 'plane_d', 'yaw', 'pitch', 'rails', 'wp_left', 'wp_right'):
        print('%-8s %s' % (k, ch[k]))
    for g in GROUPS:
        s, e = ch[g]
        print('%-6s idx %d..%d (%d tris)' % (g, s, e, (e - s) // 3))
    for w in ch['wp_left'] + ch['wp_right']:
        print('  wp %d %s' % (w, T.wps[w]))
    for r in ch['rails']:
        print('  rail %d %s %s' % (r, T.rails[r], T.rail_box(r)))


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a or a[0] == 'test':
        selftest()
    elif a[0] == 'stats':
        stats([int(a[1])] if len(a) > 1 else range(7))
    elif a[0] == 'obj':
        for t in ([int(a[1])] if len(a) > 1 else range(7)):
            print(export_obj(t, a[2] if len(a) > 2 else None))
    elif a[0] == 'chunk':
        dump_chunk(int(a[1]), int(a[2]))
    else:
        print(__doc__)
