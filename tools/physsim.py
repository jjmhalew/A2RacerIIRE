"""physsim.py - straight-line simulation of the A2 Racer II player car, NEW steering model (stuur_model=0).

Transcribes the longitudinal part of the original code (see docs/PHYSICS.md):
  handle_player_car 0x425e20 -> keyboard 0x4265b0 (throttle ramp) -> plr_new_model 0x40fc30
  -> plr_new_engine 0x40f0d0 -> plr_new_gear_accel 0x40e9f0, integrator obj_integrate 0x4404a0.
Reads CARS.DAT (extract/data/CARS.DAT) and prints, per player car and upgrade set, the time to 100 km/h,
the speed after 30 s and the asymptotic top speed (vmax of the top gear).

Usage: python tools/physsim.py [--fps 30] [--motor] [--gearbox] [--turbo]
"""
import argparse
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = ['Brabant (Trabant)', 'Opol Astro', 'Wolfswagen', 'BWM Z3.5', 'PARCHE 913', 'Mersedes SLK GTR', 'Lambdaghini']


def load_cars():
    d = open(os.path.join(ROOT, 'extract', 'data', 'CARS.DAT'), 'rb').read()
    cars = []
    for i in range(10):
        f = struct.unpack_from('<31f', d, i * 124)
        cars.append({'accel': [f[0:6], f[6:12]], 'vmax': [f[12:18], f[18:24]], 'shift_up': f[24],
                     'shift_down': f[25], 'f26': f[26], 'brake': f[27], 'engine_brake': f[28],
                     'roll_gain': f[29], 'steer': f[30]})
    return cars


def gear_accel(car, gear, v, motor, gearbox, turbo):
    """plr_new_gear_accel 0x40e9f0: returns (acc per throttle unit, ratio)."""
    vmax = car['vmax'][gearbox][gear] + (4.0 if motor else 0.0)
    ratio = v / vmax
    acc = (1.0 - ratio) * car['accel'][gearbox][gear]
    if turbo:
        acc += 0.05
    return acc, ratio


def simulate(car, fps, motor, gearbox, turbo, t_end=60.0):
    dt = 1.0 / fps
    v = 0.0
    gear = 0
    throttle = 0.0
    maxgear = 5 if gearbox else 4
    t = 0.0
    t100 = None
    v30 = None
    while t < t_end:
        # keyboard handler 0x4265b0, new model: throttle += 70*dt, max 100 (turbo forces 100)
        throttle = min(throttle + 70.0 * dt, 100.0)
        thr = 100.0 if turbo else throttle
        # plr_new_engine 0x40f0d0 (throttle > 0, brake = 0)
        v0 = v
        acc, ratio = gear_accel(car, gear, v, motor, gearbox, turbo)
        if v > 1.0:
            if ratio > car['shift_up'] and gear < maxgear:
                gear += 1
                acc, ratio = gear_accel(car, gear, v, motor, gearbox, turbo)
            if ratio < car['shift_down'] and gear > 0:
                gear -= 1
                acc, ratio = gear_accel(car, gear, v, motor, gearbox, turbo)
        v = v0 + dt * (acc * thr)
        v = int(v * 10000) * 0.0001          # obj_move_all 0x440ea0 quantises velocity to 1e-4
        t += dt
        if t100 is None and v * 3.6 >= 100.0:
            t100 = t
        if v30 is None and t >= 30.0:
            v30 = v
    return t100, v30, v, gear


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--motor', action='store_true')
    ap.add_argument('--gearbox', action='store_true')
    ap.add_argument('--turbo', action='store_true', help='turbo held all the time (ignores the 10 s fuel limit)')
    a = ap.parse_args()
    cars = load_cars()
    print(f'fps={a.fps} motor={a.motor} gearbox={a.gearbox} turbo={a.turbo}')
    print(f'{"car":20} {"0-100 s":>8} {"v@30s km/h":>11} {"v@60s km/h":>11} {"gear":>4} {"top km/h":>9}')
    for i in range(7):
        c = cars[i]
        t100, v30, v60, gear = simulate(c, a.fps, a.motor, int(a.gearbox), a.turbo)
        maxgear = 5 if a.gearbox else 4
        top = (c['vmax'][int(a.gearbox)][maxgear] + (4.0 if a.motor else 0.0)) * 3.6
        print(f'{i} {NAMES[i]:18} {t100 if t100 else float("nan"):8.2f} {v30 * 3.6:11.1f} {v60 * 3.6:11.1f} '
              f'{gear + 1:4} {top:9.1f}')


if __name__ == '__main__':
    main()
