/* car_old.c - see car_old.h. Transcribed from docs/PHYSICS.md 7 with its quirks (marked [quirk]); the per-frame steps
 * stay per frame (one game tick).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "car_old.h"
#include "exetab.h"
#include <math.h>
#include <string.h>

/* the fields of a g_car_types record (0x484b18 + type * 0xee) the old model reads */
typedef struct {
    float mass, a, b, h, Cd, area, roll_max, g, rho, rr_k;
    float fB, fC, fD, rB, rC, rD, rr_f0, rw, max_rpm, max_torque, torque_rpm;
    float ratio[7], fd, up_k, down_k, drive_eff, front_frac;
    float turbo_mul, traction_k, brake_k, lat_grip_k;
} OldCar;

static OldCar T;
static float g_vmax;                     /* g_plr_vmax 0x4d9e68 */
static int g_gear;                       /* g_old_gear 0x4a63d4: never reset, so the race starts in neutral */
static int g_easy, g_abs;                /* 0x4a63d0, 0x4a63c8 */
static float g_prev_latF, g_prev_latR, g_prev_hb_lat;   /* 0x4a63c4, 0x4a5d4c, 0x4a5dc4 */
static int g_steer_curve_k = 3, g_steer_min = 5, g_steer_max = 25;

static float rdf(const uint8_t *p, int off) { float f; memcpy(&f, p + off, 4); return f; }
static int ftol(float f) { return (int)f; }
static float sgn(float v) { return v < 0 ? -1.0f : 1.0f; }

/* car_calc_vmax 0x40ce10 ([quirk] the "top gear" is the final drive at index 7) */
static float calc_vmax(void)
{
    float gt = 0;
    for (int i = 1; i < 8; i++) {
        float r = i < 7 ? T.ratio[i] : T.fd;
        if (r != -1.0f) gt = r;
    }
    float G = gt * T.fd * 2.653927813f / T.rw;
    float b = 2 * T.drive_eff * T.max_torque * G * T.fd / T.torque_rpm * gt / T.rw;
    float a = -(T.drive_eff * T.max_torque * G * G * T.fd / (T.torque_rpm * T.torque_rpm) * gt / T.rw + 0.5f * T.rho * T.Cd * T.area);
    float d = b * b + 1200 * a;
    if (d < 0 || a == 0) return 0;
    float r1 = (sqrtf(d) - b) / (2 * a), r2 = (-b - sqrtf(d)) / (2 * a);
    return r1 > r2 ? r1 : r2;
}

void car_old_init(Race *r, int easy)
{
    memset(&T, 0, sizeof T);
    PeFile pe;
    if (pe_open(&pe, "spel.dat")) {
        const uint8_t *p = pe_ptr(&pe, 0x484b18 + (uint32_t)r->plr_car * 0xee, 0xee);
        if (p) {
            T.mass = rdf(p, 0x16); T.a = rdf(p, 0x22); T.b = rdf(p, 0x26); T.h = rdf(p, 0x2a);
            T.Cd = rdf(p, 0x2e); T.area = rdf(p, 0x32); T.roll_max = rdf(p, 0x36); T.g = rdf(p, 0x3e);
            T.rho = rdf(p, 0x42); T.rr_k = rdf(p, 0x46);
            T.fB = rdf(p, 0x4a); T.fC = rdf(p, 0x4e); T.fD = rdf(p, 0x52);
            T.rB = rdf(p, 0x56); T.rC = rdf(p, 0x5a); T.rD = rdf(p, 0x5e);
            T.rr_f0 = rdf(p, 0x62); T.rw = rdf(p, 0x66); T.max_rpm = rdf(p, 0x6a); T.max_torque = rdf(p, 0x6e);
            T.torque_rpm = rdf(p, 0x72);
            /* plr_apply_upgrades 0x428400: the 6-speed box B (+0xaa..+0xce) over box A (+0x7a..+0x9e) */
            int box = r->upg_gearbox ? 0xaa - 0x7a : 0;
            for (int i = 0; i < 7; i++) T.ratio[i] = rdf(p, 0x7a + box + i * 4);
            T.fd = rdf(p, 0x96 + box); T.up_k = rdf(p, 0x9a + box); T.down_k = rdf(p, 0x9e + box);
            T.drive_eff = rdf(p, 0xa2); T.front_frac = rdf(p, 0xa6);
            T.turbo_mul = 1.0f;                      /* plr_apply_upgrades sets +0xda = 1.0 */
            T.traction_k = rdf(p, 0xde); T.brake_k = rdf(p, 0xe2); T.lat_grip_k = rdf(p, 0xe6);
        }
        pe_close(&pe);
    }
    if (r->upg_motor) { T.max_torque *= 1.3f; T.torque_rpm *= 1.1f; T.max_rpm *= 1.1f; }
    if (r->upg_banden) { T.fB *= 1.2f; T.fD *= 1.2f; T.rB *= 1.2f; T.rD *= 1.2f; }
    g_abs = r->upg_abs != 0;
    g_easy = easy;
    g_vmax = T.rw > 0 ? calc_vmax() : 0;
    g_gear = 0;
    g_prev_latF = g_prev_latR = g_prev_hb_lat = 0;
    r->steer = 30;
    r->gas = 20;
}

int car_old_gear(void) { return g_gear; }

void car_old_input(Race *r, int left, int right, int up, int down)   /* per-frame steps (INPUT.md 5.2) */
{
    if (up) { if (r->gas < 40) r->gas += 5; else r->gas = 40; }
    else if (down) { if (r->gas > 20) r->gas = 20; if (r->gas > 0) r->gas -= 10; else r->gas = 0; }
    else r->gas = 20;
    if (left) { if (r->steer > 30) r->steer = 30; else if (r->steer == 0) r->steer = 0; else r->steer -= 2; }
    else if (right) { if (r->steer < 30) r->steer = 30; else if (r->steer < 60) r->steer += 2; }
    else r->steer = 30;
}

/* ---- the parts (0x40d5d0 .. 0x40e870) ----------------------------------------------------------------------------- */
static void axle_loads(Vec3 *f, Vec3 *rr)                 /* 0x40d5d0, theta always 0 */
{
    float W = T.mass * T.g, L = T.a + T.b;
    f->y = -(W * T.b / L);
    rr->y = -(W * T.a / L);
    f->z = rr->z = -0.0f;
}

static void slip_angles(Race *r, Obj *c, float delta, float yr, float *aF, float *aR, float *vF, float *vR)   /* 0x40e3d0 */
{
    float L = T.a + T.b, vx = c->vel.x, vz = c->vel.z;
    *vF = vx - 0.5f * L * yr;
    *vR = vx + 0.5f * L * yr;
    if (fabsf(vz) != 0) { *aF = delta - atanf(*vF / vz); *aR = -atanf(*vR / vz); }
    else *aF = *aR = 0;
    if (vz < 0) { *aF = -*aF; *aR = -*aR; }
    if (c->vel.z > 0.2f) {
        float beta = -atan2f(c->vel.x, c->vel.z);
        if (!(fabsf(beta) < 0.3f)) beta = 0.3f;
        r->skid = ftol(fabsf(beta) * 3.33333333f * 10.0f);
    } else r->skid = 0;
}

static void lateral_forces(Race *r, Obj *c, const Vec3 *Nf, const Vec3 *Nr, float delta, float aF, float aR, Vec3 *LF, Vec3 *LR)
{                                                          /* 0x40e530 */
    LF->x = -Nf->y * T.fD * T.lat_grip_k * sinf(T.fC * atanf(T.fB * aF));
    LR->x = -Nr->y * T.rD * T.lat_grip_k * sinf(T.rC * atanf(T.rB * aR));
    if (g_easy && r->handbrake == 0) c->vel.x = 0;         /* easy: no side slip */
    float s = fabsf(c->vel.z * (1 / 7.f)) * 3.0f;
    if (fabsf(c->vel.z) < 7.0f) {                          /* below 25 km/h: kinematic yaw, per frame */
        LF->x = 0; LR->x = 0;
        if (delta == 0) c->yawrate *= 0.5f;
        else {
            if (s > fabsf(c->yawrate)) {
                float d = delta;
                if (!(c->vel.z > 0) && r->brake_light) d = -delta;
                c->yawrate -= c->vel.z * (1 / 7.f) * sgn(d) * 0.3f;
            } else c->yawrate -= sgn(c->yawrate) * 0.1f;
            r->skid = 0;
        }
        c->vel.x = 0;
    } else {
        int flipF = !(g_prev_latF != 0 && LF->x / g_prev_latF >= 0) && fabsf(LF->x - g_prev_latF) > 2000;
        int flipR = !flipF && !(g_prev_latR != 0 && LR->x / g_prev_latR >= 0) && fabsf(LR->x - g_prev_latR) > 2000;
        if (flipF || flipR) { c->vel.x = 0; r->skid = 0; }
    }
    LF->x = (LF->x + g_prev_latF) * 0.5f; g_prev_latF = LF->x;
    LR->x = (LR->x + g_prev_latR) * 0.5f; g_prev_latR = LR->x;
}

static void rolling_resistance(const Obj *c, const Vec3 *Nf, const Vec3 *Nr, Vec3 *Rf, Vec3 *Rr)   /* 0x40d6e0 */
{
    float vz = c->vel.z, q = T.rr_f0 + (T.rr_k * vz) * (T.rr_k * vz);   /* [quirk] (k*v)^2 */
    if (vz >= 0.1f) { Rf->z = Nf->y * q; Rr->z = Nr->y * q; }
    else if (vz <= -0.1f) { Rf->z = -(Nf->y * q); Rr->z = -(Nr->y * q); }
}

static void gearbox(Race *r, const Obj *c, float accel, int coast, int braking)   /* 0x40e190 */
{
    int g = g_gear;
    float K = T.ratio[1] * T.fd * 9.554140127f / T.rw, Rcl = (T.down_k - 0.05f) * T.torque_rpm;
    float Vcl = K != 0 ? Rcl / fabsf(K) : 0;
    for (int pass = 0; pass < 2; pass++) {
        float rpm;
        if (g == 0) rpm = (braking == 1 || accel == 0) ? 800 : fabsf(T.max_rpm * accel);
        else if (g == 1 && !(Vcl < fabsf(c->vel.z))) rpm = Rcl;
        else { rpm = fabsf(c->vel.z * T.fd * T.ratio[g] * 9.554140127f / T.rw); if (rpm < 800) rpm = 800; }
        if (pass == 1) { r->rpm = rpm; break; }
        if (rpm < T.down_k * T.torque_rpm) { if (g == 1) { if (coast == 1) g = 0; } else if (g > 1) g--; }
        else if (rpm > T.up_k * T.torque_rpm && g != 6 && T.ratio[g + 1] != -1.0f) g++;
        g_gear = g;
    }
}

static void drive_force(int device, float Tq, const Vec3 *Nf, const Vec3 *Nr, Vec3 *A, Vec3 *B, int *skid)   /* 0x40da90 */
{
    float F = T.rw != 0 ? Tq * T.ratio[g_gear] * T.fd * T.drive_eff / T.rw : 0;
    A->z = T.front_frac * T.turbo_mul * F;
    B->z = (1 - T.front_frac) * T.turbo_mul * F;
    const Vec3 *N[2] = { Nf, Nr };
    Vec3 *O[2] = { A, B };
    for (int i = 0; i < 2; i++) {
        float Fmax = fabsf(T.traction_k * 0.8f * N[i]->y);
        if (Fmax > 0 && fabsf(O[i]->z) > Fmax) {
            float e = (fabsf(O[i]->z) - Fmax) / Fmax;
            if (device >= 1 && device <= 3) O[i]->z = -(T.traction_k * 0.8f * N[i]->y * sgn(O[i]->z));  /* [quirk] not DEVICE 4 */
            *skid = ftol((e < 1 ? e : 1) * 6);
        }
    }
}

static void brakes(const Obj *c, float accel, const Vec3 *Nf, const Vec3 *Nr, Vec3 *C, Vec3 *D, int *skid)   /* 0x40d7d0 */
{
    C->z = -Nf->y * T.brake_k * accel;
    D->z = -Nr->y * T.brake_k * accel;
    const Vec3 *N[2] = { Nf, Nr };
    Vec3 *O[2] = { C, D };
    for (int i = 0; i < 2; i++) {
        float Fmax = fabsf(T.brake_k * 0.8f * N[i]->y);
        if (Fmax > 0 && fabsf(O[i]->z) > Fmax && !g_abs) {
            float e = (fabsf(O[i]->z) - Fmax) * 4 / Fmax;
            if (fabsf(c->vel.z) > 13.89f) {
                float vv = c->vel.z > 13.89f ? 13.89f : c->vel.z;      /* [quirk] vz < -13.89 not clamped */
                *skid = ftol((e < 1 ? e : 1) * (fabsf(vv) / 13.89f * 4));
            }
            O[i]->z = -(T.brake_k * 0.8f * N[i]->y * sgn(O[i]->z));
        }
    }
}

static void drivetrain(Race *r, Obj *c, float accel, const Vec3 *Nf, const Vec3 *Nr, Vec3 *F, Vec3 *R, int device)   /* 0x40dd00 */
{
    float v = fabsf(c->vel.z) < 0.1f ? 0 : c->vel.z;
    int coast;
    if (accel == 0) { r->brake_light = 0; coast = 1; }
    else { r->brake_light = v / accel < 0; coast = 0; }
    gearbox(r, c, accel, coast, r->brake_light);
    float rpm = r->rpm, Tq;
    int engine_brake;
    if (coast) engine_brake = 1;
    else if (fabsf(T.max_rpm * accel) < rpm) engine_brake = 1;
    else if ((c->vel.z > g_vmax || c->vel.z < -13.89f) && !r->turbo_active) engine_brake = 1;
    else engine_brake = 0;
    if (engine_brake && g_gear != 0) Tq = -0.5f * T.max_torque;
    else { float x = rpm / T.torque_rpm; Tq = T.max_torque * x * (2 - x); }
    Vec3 A = { 0, 0, 0 }, B = { 0, 0, 0 };
    int skid = r->skid;
    if (v > 0) {
        if (r->brake_light == 1) brakes(c, accel, Nf, Nr, &A, &B, &skid);
        else drive_force(device, Tq, Nf, Nr, &A, &B, &skid);
    } else if (v == 0) {
        if (accel != 0) {
            drive_force(device, Tq, Nf, Nr, &A, &B, &skid);
            if (accel < 0) { A.z = -A.z; B.z = -B.z; }
        }
    } else {
        if (r->brake_light == 1) brakes(c, accel, Nf, Nr, &A, &B, &skid);
        else if (v > -13.89f) { drive_force(device, Tq, Nf, Nr, &A, &B, &skid); A.z = -A.z; B.z = -B.z; }
    }
    r->skid = skid;
    *F = A;
    *R = B;
}

static void handbrake(Race *r, Obj *c, float aR, const Vec3 *Nr, Vec3 *DrR, Vec3 *LatR, float vlatR)   /* 0x40e870 */
{
    float Fl = fabsf(0.8f * T.rD * Nr->y), Fz = fabsf(0.8f * T.rD * Nr->y);
    DrR->z = -sgn(c->vel.z) * fabsf(cosf(aR) * Fz);
    LatR->x = -sgn(vlatR) * fabsf(sinf(aR) * Fl);
    if (g_prev_hb_lat != 0 && LatR->x / g_prev_hb_lat < 0 && fabsf(LatR->x - g_prev_hb_lat) > 1000) c->vel.x = 0;
    g_prev_hb_lat = LatR->x;
    r->skid = fabsf(c->vel.z) > 13.89f ? 10 : ftol(fabsf(c->vel.z) / 13.89f * 10.0f);
}

static void add_force_at(Obj *c, Vec3 f, Vec3 p)          /* obj_add_force_at 0x4407e0 */
{
    c->force = v3_add(c->force, f);
    c->torque += p.x * f.z - p.z * f.x;
}

void car_old_drive(Race *r, int o, int device)
{
    Obj *c = &r->obj[o];
    /* handle_player_car: steering falloff with speed, gas -1..1 */
    if (device == 3 || device == 4) { g_steer_min = 3; g_steer_max = 20; }
    else { g_steer_min = 5; g_steer_max = 25; }
    float E = g_vmax > 0 ? expf(-(fabsf((float)ftol(c->vel.z)) * g_steer_curve_k / g_vmax)) : 1;
    float delta = (r->steer - 30) * ((g_steer_max - g_steer_min) * E + g_steer_min) * 0.000581776f;
    float accel = (r->gas - 20) * 0.05f;
    T.turbo_mul = r->turbo_active ? 6.0f : 1.0f;

    float L = T.a + T.b;
    Vec3 Pf = { 0, 0, 0.5f * L }, Pr = { 0, 0, -0.5f * L }, Pcg = { 0, 0, 0 };
    Vec3 Nf = { 0, 0, 0 }, Nr = { 0, 0, 0 }, RRf = { 0, 0, 0 }, RRr = { 0, 0, 0 }, LatF = { 0, 0, 0 }, LatR = { 0, 0, 0 };
    Vec3 DrF = { 0, 0, 0 }, DrR = { 0, 0, 0 }, Drag = { 0, 0, 0 };
    float aF = 0, aR = 0, vF = 0, vR = 0, vz = c->vel.z, yr = c->yawrate;
    r->skid = 0;
    axle_loads(&Nf, &Nr);
    if (fabsf(vz) <= 0.05f && fabsf(yr) <= 0.01f) {
        if (r->handbrake == 0) { c->vel.x = 0; c->yawrate = 0; r->skid = 0; }
    } else {
        slip_angles(r, c, delta, yr, &aF, &aR, &vF, &vR);
        lateral_forces(r, c, &Nf, &Nr, delta, aF, aR, &LatF, &LatR);
    }
    vz = c->vel.z;
    Drag.z = T.rho * T.Cd * T.area * vz * vz * (vz > 0 ? -0.5f : 0.5f);
    rolling_resistance(c, &Nf, &Nr, &RRf, &RRr);
    drivetrain(r, c, accel, &Nf, &Nr, &DrF, &DrR, device);
    if (r->handbrake == 1) handbrake(r, c, aR, &Nr, &DrR, &LatR, vR);
    Vec3 Ff = v3_add(v3_add(Nf, RRf), v3_add(LatF, DrF)), Fr = v3_add(v3_add(Nr, RRr), v3_add(LatR, DrR));
    float ay = c->yawrate * c->vel.z;                     /* the visual body roll, set absolutely */
    if (fabsf(ay) > 30) ay = sgn(ay) * 30;
    /* 11.377778 is 1024/90, but the instance angles are 4096 per turn: at most roll_max (13) degrees */
    c->roll = ay * (1 / 30.f) * T.roll_max * 11.377778f;
    add_force_at(c, Ff, Pf);
    add_force_at(c, Fr, Pr);
    add_force_at(c, Drag, Pcg);
    r->gear = g_gear > 0 ? g_gear - 1 : 0;                /* the HUD has no gear; keep the new model's field sane */
}
