"""objparse.py - A2 Racer II track object files: etappeN.omp (static objects), etappeN.pth (scripted paths),
etappeN.pmp (chunk centres + waypoint lists) and CARS.DAT. See docs/FORMAT_OBJECTS.md.

.omp  u32 count, count x 68 bytes {i32 chunk; i32 type; f32 x,y,z; f32 rx,ry,rz (degrees); char anim[32];
      i32 variant}                                                    loader rd_main.cpp 0x4451d0
      (etappe2/6: count = records + 1, the game re-uses the last record once more, see the doc)
.pth  i32 version (-1), i32 count, count x {i32 start_chunk, end_chunk, type, lap, time_ms, npoints;
      npoints x {u32 unused; f32 x,y,z}}                              loader pth_main.cpp 0x433710
.pmp  u32 count, count x {f32 x,y,z; i32 a, b; 3 x (i32 n; n x i32)}  loader rd_chunk.cpp 0x442b00
CARS.DAT 10 x 124-byte car records (31 f32) + 10 x 24-byte suspension records (6 f32)   plr_car.cpp 0x4256a0

usage: python tools/objparse.py test                  self-test over all 7 tracks + CARS.DAT
       python tools/objparse.py omp 1                 dump the placements of etappe1.omp
       python tools/objparse.py pth 0                 dump the paths of etappe0.pth
       python tools/objparse.py pmp 1 [n]             dump the first n chunks of etappe1.pmp
       python tools/objparse.py cars                  dump CARS.DAT with field names
       python tools/objparse.py scene 1 [--paths]     all placed objects (+ chunk line, paths) -> out/d3d/etappe1_scene.obj
"""
import math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dparse

ROOT = d3dparse.ROOT
DATA = os.path.join(ROOT, 'extract', 'data')

OMP_REC = struct.Struct('<ii3f3f32si')      # 68 bytes
# object types (static handler table 0x48c2a8 -> 0x4e5dd0, physics switch in 0x4451d0)
OBJ_TYPES = {0: 'scenery (no handler)', 1: 'solid, bounce (0x429380)', 2: 'pylon, knock away (0x4293f0)',
             3: 'barrier/fence, knock over (0x4297d0)', 4: 'traffic light (0x429480)', 5: 'solid + physics (0x429380)',
             6: 'unused (0x4295d0)', 7: 'speed camera / flitspaal (0x428d30)', 8: 'arrow sign + physics (0x4296b0)',
             9: 'police car (car_cops 0x40c920)', 10: 'police car + physics (0x4293a0)'}
# path types (table 0x4966f0: {init, update, release} x 8)
PATH_TYPES = {0: 'none', 1: 'unused (0x432a10)', 2: 'police car MPOLIZEI_ANIM', 3: 'canal boat E00_ROND_ANIM',
              4: 'tram C_TREIN_ANIM', 5: 'helicopter C_HELI_ANIM (+ blades)', 6: 'traffic car (random N_BUS/N_VRTAUT/'
              'N_BESTEL/N_GOLF/N_ASTRA)', 7: 'airplane A_VLIEGT_ANIM'}
# [2][6] tables: first index = Game.ini [PLAYER] versnellingsbak (0x4d9ed0, set at 0x421df7; 0 = stock 5-speed,
# 1 = 6-speed gearbox upgrade), second index = gear 1..6. Names from the in-game "Tweak Menu" (0x412e90 area).
CAR_FIELDS = ['accel[box0][gear1..6]'] * 6 + ['accel[box1][gear1..6]'] * 6 + ['topspeed[box0][gear] m/s'] * 6 + \
             ['topspeed[box1][gear] m/s'] * 6 + ['upshift point', 'downshift point', 'unknown (always 0)',
                                                 'brake force', 'engine braking', 'roll amplification',
                                                 'steering angle']
SUSP_FIELDS = ['roll damping', 'roll factor', 'pitch damping', 'pitch factor', 'height damping', 'height factor']


def path(e, ext):
    return os.path.join(DATA, f'etappe{e}.{ext}')


# ---------------------------------------------------------------- .omp
def parse_omp(data):
    """-> (header count, [dict]); raises unless the records fill the file exactly."""
    count = struct.unpack_from('<I', data)[0]
    if (len(data) - 4) % 68:
        raise ValueError('omp size is not 4 + n*68')
    n = (len(data) - 4) // 68
    if count not in (n, n + 1):
        raise ValueError(f'omp count {count} vs {n} records')
    recs = []
    for i in range(n):
        chunk, typ, x, y, z, rx, ry, rz, raw, variant = OMP_REC.unpack_from(data, 4 + 68 * i)
        name = raw.split(b'\0')[0].decode('latin-1')
        recs.append(dict(i=i, chunk=chunk, type=typ, pos=(x, y, z), rot=(rx, ry, rz), anim=name,
                         variant=variant, raw=raw))
    return count, recs


# ---------------------------------------------------------------- .pth
def parse_pth(data):
    ver, n = struct.unpack_from('<ii', data)
    if ver != -1:
        raise ValueError(f'pth version {ver}')
    o, paths = 8, []
    for i in range(n):
        start, end, typ, lap, time_ms, npts = struct.unpack_from('<6i', data, o); o += 24
        pts = [struct.unpack_from('<I3f', data, o + 16 * k) for k in range(npts)]; o += 16 * npts
        paths.append(dict(i=i, start=start, end=end, type=typ, lap=lap, time_ms=time_ms,
                          points=[p[1:] for p in pts], unused=[p[0] for p in pts]))
    if o != len(data):
        raise ValueError(f'pth ends at {o} of {len(data)}')
    return paths


# ---------------------------------------------------------------- .pmp
def parse_pmp(data):
    n = struct.unpack_from('<I', data)[0]
    o, chunks = 4, []
    for i in range(n):
        x, y, z, a, b = struct.unpack_from('<3f2i', data, o); o += 20
        lists = []
        for k in range(3):
            c = struct.unpack_from('<i', data, o)[0]; o += 4
            if not 0 <= c <= 12:
                raise ValueError(f'pmp chunk {i} list {k} count {c}')
            lists.append(list(struct.unpack_from(f'<{c}i', data, o))); o += 4 * c
        chunks.append(dict(i=i, pos=(x, y, z), a=a, b=b, lists=lists))
    if o != len(data):
        raise ValueError(f'pmp ends at {o} of {len(data)}')
    return chunks


# ---------------------------------------------------------------- CARS.DAT
def parse_cars(data):
    if len(data) != 10 * 124 + 10 * 24:
        raise ValueError('CARS.DAT size')
    cars = [struct.unpack_from('<31f', data, 124 * i) for i in range(10)]
    susp = [struct.unpack_from('<6f', data, 1240 + 24 * i) for i in range(10)]
    return cars, susp


# ---------------------------------------------------------------- transforms
def yaw_matrix(deg):
    """File yaw (degrees) -> world: mesh +z (car front) points along (sin ry, 0, cos ry); verified against the
    road direction of .pmp chunks for parked police cars / petrol stations. x' = x cos + z sin, z' = -x sin + z cos."""
    a = math.radians(deg)
    return math.cos(a), math.sin(a)


def place(v, rec):
    c, s = yaw_matrix(rec['rot'][1])
    x, y, z = v
    px, py, pz = rec['pos']
    return (x * c + z * s + px, y + py, -x * s + z * c + pz)


# ---------------------------------------------------------------- self-test
def selftest():
    sp = d3dparse.Spel()
    files = d3dparse.rcs_files()
    cfg16 = {c[4]: c[1] for c in sp.CONFIGS if c[3] == 16}
    common = set(sp.ilist(cfg16['common']))
    for e in range(7):
        city = e in (0, 2, 4, 6)
        avail = common | set(sp.ilist(cfg16['city (etappe 0,2,4,6)' if city else 'highway (etappe 1,3,5)'])) | \
            set(sp.ilist(cfg16['etappe 0/6' if e in (0, 6) else f'etappe {e}']))
        rmp_count = struct.unpack_from('<2I', open(path(e, 'rmp'), 'rb').read(8))[1]
        chunks = parse_pmp(open(path(e, 'pmp'), 'rb').read())
        assert len(chunks) == rmp_count, (e, len(chunks), rmp_count)
        for c in chunks:
            if e != 0 and e != 6:
                pass
        steps = [math.dist(chunks[i]['pos'], chunks[i + 1]['pos']) for i in range(len(chunks) - 1)]
        assert all(abs(s - 128) < 0.5 for s in steps), (e, min(steps), max(steps))
        count, recs = parse_omp(open(path(e, 'omp'), 'rb').read())
        types, junk = {}, 0
        for r in recs:
            ai = sp.anim_index.get(r['anim'].lower())
            assert ai is not None and ai in avail, (e, r['i'], r['anim'])
            assert 0 <= r['chunk'] < len(chunks), (e, r['i'], r['chunk'])
            assert r['variant'] in (0, 1, 2), r
            for m, t in sp.anim_frames(ai):
                assert m.lower() in files and os.path.splitext(t)[0].lower() + '.tga' in files
            types[r['type']] = types.get(r['type'], 0) + 1
            junk += any(r['raw'][len(r['anim']) + 1:])
        paths = parse_pth(open(path(e, 'pth'), 'rb').read())
        for p in paths:
            assert 0 <= p['type'] < 8 and p['lap'] in (0, 1, 2) and p['time_ms'] > 0 and 0 < len(p['points']) < 50, p
            assert 0 <= p['start'] < len(chunks) and 0 <= p['end'] < len(chunks)
            assert set(p['unused']) == {0}
        print(f'etappe{e}: omp {len(recs)} objects (header {count}{" = n+1!" if count != len(recs) else ""}, '
              f'{junk} names with stale bytes), types {dict(sorted(types.items()))}; pth {len(paths)} paths '
              f'types {sorted({p["type"] for p in paths})}; pmp {len(chunks)} chunks = rmp count, 128 apart')
    cars, susp = parse_cars(open(os.path.join(DATA, 'CARS.DAT'), 'rb').read())
    assert all(not any(c) for c in cars[7:]) and all(not any(s) for s in susp[7:])
    print(f'CARS.DAT: 1480 bytes = 10 x 124 + 10 x 24, cars 0..6 filled, 7..9 zero')
    print('OK')


# ---------------------------------------------------------------- dumps / scene
def dump_omp(e):
    sp = d3dparse.Spel()
    count, recs = parse_omp(open(path(e, 'omp'), 'rb').read())
    print(f'# etappe{e}.omp: header count {count}, {len(recs)} records')
    print('#   i chunk type var         x         y         z      ry  anim -> mesh + texture')
    for r in recs:
        fr = sp.anim_frames(r['anim'])
        rx, ry, rz = r['rot']
        rot = f'{ry:7.1f}' + (f' (rx {rx} rz {rz})' if rx or rz else '')
        print(f"{r['i']:5} {r['chunk']:5} {r['type']:4} {r['variant']:3} {r['pos'][0]:9.1f} {r['pos'][1]:9.1f} "
              f"{r['pos'][2]:9.1f} {rot}  {r['anim']} -> {' | '.join(m + '+' + t for m, t in fr)}")


def dump_pth(e):
    for p in parse_pth(open(path(e, 'pth'), 'rb').read()):
        print(f"path {p['i']}: chunks {p['start']}..{p['end']} type {p['type']} ({PATH_TYPES.get(p['type'])}) "
              f"lap {p['lap']} time {p['time_ms']} ms, {len(p['points'])} points")
        for pt in p['points']:
            print('    (%.1f, %.1f, %.1f)' % pt)


def dump_pmp(e, n=20):
    for c in parse_pmp(open(path(e, 'pmp'), 'rb').read())[:n]:
        print(f"chunk {c['i']:4} pos ({c['pos'][0]:.1f}, {c['pos'][1]:.1f}, {c['pos'][2]:.1f}) a {c['a']} b {c['b']} "
              f"lists {c['lists']}")


def dump_cars():
    cars, susp = parse_cars(open(os.path.join(DATA, 'CARS.DAT'), 'rb').read())
    for i, (c, s) in enumerate(zip(cars, susp)):
        if not any(c):
            print(f'car {i}: (empty)'); continue
        print(f'car {i}:')
        for k in range(4):
            print(f'  {CAR_FIELDS[6 * k]:26} ' + ' '.join(f'{v:7.3f}' for v in c[6 * k:6 * k + 6]))
        for k in range(24, 31):
            print(f'  f[{k}] {CAR_FIELDS[k]:21} {c[k]:.3f}')
        print('  susp ' + ', '.join(f'{n} {v:g}' for n, v in zip(SUSP_FIELDS, s)))


def scene(e, with_paths=False):
    """Write out/d3d/etappe<e>_scene.obj: every .omp object (frame 0 of its anim, yaw applied), the chunk centre
    line from .pmp and optionally the .pth control polygons. OBJ coordinates: (x, y, -z), 1 unit ~ 10 cm."""
    sp = d3dparse.Spel()
    files = d3dparse.rcs_files()
    out = os.path.join(d3dparse.OUT)
    os.makedirs(out, exist_ok=True)
    count, recs = parse_omp(open(path(e, 'omp'), 'rb').read())
    chunks = parse_pmp(open(path(e, 'pmp'), 'rb').read())
    cache, mats, nv = {}, set(), 0
    lines = [f'# etappe{e}: {len(recs)} objects', f'mtllib etappe{e}_scene.mtl']
    for r in recs:
        m, t = sp.anim_frames(r['anim'])[0]
        if m not in cache:
            cache[m] = d3dparse.parse(open(files[m.lower()], 'rb').read(), m)
        mesh = cache[m]
        tb = os.path.splitext(t)[0].lower(); mats.add(t)
        lines.append(f"o {r['i']}_{r['anim']}\nusemtl {tb}")
        for v in mesh.verts:
            x, y, z = place(v[:3], r)
            lines.append(f'v {x:.2f} {y:.2f} {-z:.2f}')
        for v in mesh.verts:
            lines.append(f'vt {v[6]:.5f} {1 - v[7]:.5f}')
        for a, b, c in mesh.faces:
            lines.append(f'f {a+1+nv}/{a+1+nv} {b+1+nv}/{b+1+nv} {c+1+nv}/{c+1+nv}')
        nv += len(mesh.verts)
    lines.append('o chunk_centres')
    for c in chunks:
        lines.append('v %.2f %.2f %.2f' % (c['pos'][0], c['pos'][1], -c['pos'][2]))
    lines.append('l ' + ' '.join(str(nv + 1 + i) for i in range(len(chunks))))
    nv += len(chunks)
    if with_paths:
        for p in parse_pth(open(path(e, 'pth'), 'rb').read()):
            lines.append(f"o path{p['i']}_type{p['type']}")
            for x, y, z in p['points']:
                lines.append(f'v {x:.2f} {y:.2f} {-z:.2f}')
            lines.append('l ' + ' '.join(str(nv + 1 + i) for i in range(len(p['points']))))
            nv += len(p['points'])
    open(os.path.join(out, f'etappe{e}_scene.obj'), 'w').write('\n'.join(lines) + '\n')
    mtl = []
    for t in sorted(mats):
        png = d3dparse.texture_png(t, files=files, outdir=os.path.join(out, 'tex'))
        tb = os.path.splitext(t)[0].lower()
        mtl += [f'newmtl {tb}', 'Kd 1 1 1', f'map_Kd tex/{os.path.basename(png)}', f'map_d tex/{os.path.basename(png)}', '']
    open(os.path.join(out, f'etappe{e}_scene.mtl'), 'w').write('\n'.join(mtl))
    print(os.path.join(out, f'etappe{e}_scene.obj'), f'({len(recs)} objects, {len(cache)} meshes)')


def main(argv):
    if not argv or argv[0] == 'test':
        return selftest()
    cmd = argv[0]
    if cmd == 'omp':
        dump_omp(int(argv[1]))
    elif cmd == 'pth':
        dump_pth(int(argv[1]))
    elif cmd == 'pmp':
        dump_pmp(int(argv[1]), int(argv[2]) if len(argv) > 2 else 20)
    elif cmd == 'cars':
        dump_cars()
    elif cmd == 'scene':
        scene(int(argv[1]), '--paths' in argv)
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
