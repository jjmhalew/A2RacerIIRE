/* race.c - see race.h. Function names and addresses refer to spel.dat (docs/PHYSICS.md, INPUT.md, RENDER.md).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "race.h"
#include "exetab.h"
#include "gfx.h"
#include "ini.h"
#include "model.h"
#include "plat.h"
#include "res.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28319f                 /* the original's 2 pi (0x47f... 6.28319f) */
static float SIN[1024], COS[1024];      /* math3d.cpp 0x450280 */
static Race *R;                         /* the race the callbacks work on */

static int ftol(float f) { return (int)f; }   /* MSVC _ftol: truncation toward zero */
static float frand_sign(void) { return rand() % 10 < 5 ? 1.0f : -1.0f; }

/* ---- road helpers -------------------------------------------------------------------------------------------- */
/* chunk_height_at 0x442f00: the chunk is one plane n.p = d */
static float chunk_height_at(const Chunk *c, float x, float z)
{
    if (fabsf(c->up.y) < 1e-6f) return c->pos.y;
    return (c->plane_d - c->up.x * x - c->up.z * z) / c->up.y;
}

static int find_chunk(int start, Vec3 p) { return track_find_chunk(&R->track, start, p); }

/* ---- objects ----------------------------------------------------------------------------------------------------- */
static int obj_add(int anim, Vec3 pos, float yaw)
{
    for (int i = 0; i < MAX_OBJS; i++)
        if (!R->obj[i].used) {
            Obj *o = &R->obj[i];
            memset(o, 0, sizeof *o);
            o->used = o->active = o->visible = 1;
            o->anim = anim;
            o->pos = pos;
            o->yaw = yaw;
            o->restitution = 1.0f;
            o->car_type = -1;
            o->chunk = o->prev_chunk = find_chunk(0, pos);
            o->draw_pos0 = o->draw_pos1 = pos;
            o->draw_yaw0 = o->draw_yaw1 = yaw;
            if (i >= R->nobj) R->nobj = i + 1;
            return i;
        }
    return -1;
}

/* obj_init_physics 0x4403b0 */
static void obj_init_physics(Obj *o, float mass, float w, float l, float restitution)
{
    o->physics = 1;
    o->inv_mass = mass > 1e-6f ? 1.0f / mass : 0;
    o->inv_inertia = mass > 1e-6f ? 1.0f / (mass * (w * w + l * l) * (1.0f / 12.0f)) : 0;
    o->restitution = restitution;
}

/* obj_integrate 0x4404a0 */
static void obj_integrate(Obj *c, float dt, float t, int coll)
{
    int iy = ftol(floorf(c->yaw));
    Vec3 d = v3_scale(c->vel, t * 10 * dt);
    int i = iy & 1023;
    c->pos.z += ftol((SIN[i] * d.x + COS[i] * d.z) * 10000) * 0.0001f;
    c->pos.x += ftol((COS[i] * d.x - SIN[i] * d.z) * 10000) * 0.0001f;
    float dyaw = dt * c->yawrate * 1024 / TWO_PI;
    if (coll == 0) c->yaw += dyaw;
    if (!c->no_vel_rotate) {
        int j = ftol(-dyaw) & 1023;                 /* truncated to whole 1024ths of a turn: the drift */
        float vx = c->vel.x, vz = c->vel.z;
        c->vel.x = COS[j] * vx - SIN[j] * vz;
        c->vel.z = SIN[j] * vx + COS[j] * vz;
    }
    c->vel = v3_add(c->vel, v3_scale(c->force, c->inv_mass * dt));
    c->yawrate += c->torque * c->inv_inertia * dt;
}

/* obj_move_all 0x440ea0 (collisions: docs/PHYSICS.md 8, to come) */
static void obj_move_all(float dt0)
{
    for (int o = 0; o < R->nobj; o++) {
        Obj *c = &R->obj[o];
        if (!c->used || !c->active || c->physics != 1) continue;
        c->vel.x = ftol(c->vel.x * 10000) * 0.0001f;
        c->vel.z = ftol(c->vel.z * 10000) * 0.0001f;
        int n = o == R->plr_obj ? 8 : 1;
        float h = o == R->plr_obj ? dt0 * 0.125f : dt0;
        for (int k = 0; k < n; k++) obj_integrate(c, h, 1.0f, 0);
        c->prev_chunk = c->chunk;
        c->chunk = find_chunk(c->chunk, c->pos);
    }
}

/* ---- the car: new steering model (car_drive_new 0x40fc30) ------------------------------------------------------ */
static float car_gear_accel(Obj *c, int gear, float *ratio)          /* 0x40e9f0 */
{
    const CarsDat *d = &R->cars[R->plr_car];
    int box = R->upg_gearbox != 0;
    float vmax = d->vmax[box][gear] + (R->upg_motor ? 4.0f : 0.0f);
    *ratio = vmax != 0 ? c->vel.z / vmax : 1;
    float acc = (1.0f - *ratio) * d->accel[box][gear];
    R->rpm = 800 + fabsf(*ratio) * 8200;
    if (R->turbo_active) {
        if (c->vel.z > 0) c->pitch_vel += 500 * R->dt;
        return acc + 0.05f;
    }
    return acc;
}

static void car_engine_new(Obj *c, float gas, float brake)            /* 0x40f0d0 */
{
    float dt = R->dt;
    int maxgear = 4 + (R->upg_gearbox != 0);
    const CarsDat *d = &R->cars[R->plr_car];
    if (gas == 0) {
        if (c->vel.z < 0) { c->vel.z += d->engine_brake * dt; if (c->vel.z > 0) c->vel.z = 0; }
        else { c->vel.z -= d->engine_brake * dt; if (c->vel.z < 0) c->vel.z = 0; }
    }
    if (gas > 0) R->stop_latch = 0;
    float v0 = c->vel.z, ratio;
    float acc = car_gear_accel(c, R->gear, &ratio);
    if (c->vel.z > 1.0f) {
        if (ratio > d->shift_up && R->gear < maxgear) acc = car_gear_accel(c, ++R->gear, &ratio);
        if (ratio < d->shift_down && R->gear > 0) acc = car_gear_accel(c, --R->gear, &ratio);
    }
    float brk = d->brake;
    if (brk > 0) brk = -brk;
    if (R->upg_abs) brk -= 0.5f;
    if (R->handbrake) { acc = 0; brk = 0; }
    c->vel.z = v0 + dt * (acc * gas + brk * brake);
    if (brake > 0) {
        R->brake_light = 1;
        if (fabsf(c->vel.z) < 0.4f && !R->stop_latch) { c->vel.z = 0; R->stop_latch = 1; }
        float r = c->vel.z * brake;
        if (r > 1000) r = 1000;
        c->pitch_vel -= dt * r * 0.5f;
    } else R->brake_light = 0;
    if (c->vel.z <= 0) { R->brake_light = 0; R->rpm = 800; }
    if (c->vel.z < -13.333f) c->vel.z = -13.333f;
}

static void wrap2pi(float *a)
{
    if (*a > TWO_PI) *a -= TWO_PI;
    else if (*a < 0) *a += TWO_PI;
}

static void car_steer_new(Obj *c, float steer, float gas)            /* 0x40f3c0 */
{
    float dt = R->dt;
    const CarsDat *d = &R->cars[R->plr_car];
    if (!R->handbrake && R->hb_rev_time == 0) c->yawrate -= c->yawrate * 10 * dt;
    if (R->handbrake && c->vel.z == 0 && gas > 80) {                 /* burn-out wiggle */
        float s = sinf(R->hb_rev_time * TWO_PI);
        float w = R->hb_rev_time > 4 ? s * 0.4f : R->hb_rev_time * 0.1f * s;
        R->hb_rev_time += dt;
        c->yawrate += w;
    } else {
        if (R->hb_rev_time != 0 && gas > 0) {                        /* launch */
            float v2 = d->vmax[R->upg_gearbox != 0][1];
            c->vel.z = R->hb_rev_time * 0.25f * v2 * gas * 0.01f;
            if (c->vel.z > v2) c->vel.z = v2;
        }
        R->hb_rev_time = 0;
    }
    if (!R->handbrake) {
        R->hb_heading = 0;
        R->hb_time = 0;
        c->vel.x -= c->vel.x * (R->upg_banden ? 10 : 5) * dt;
    }
    float k = (fabsf(c->vel.z) < 2 && fabsf(c->vel.x) < 2) ? fabsf(sinf(fabsf(3.14159f * c->vel.z * 0.25f))) * d->steer
                                                           : d->steer;
    float a = steer * 0.5f * TWO_PI * 0.00277778f;
    wrap2pi(&a);
    float dyaw = (k * dt * steer + sinf(a) * (c->vel.z * 50) * dt) * 0.00153398f;
    float dyaw_w = dyaw;
    wrap2pi(&dyaw_w);
    int iy = ftol(c->yaw) % 0xfff;
    if (iy < 0) iy += 0x1000;
    float heading = (float)iy * (1.0f / 4096.f) * TWO_PI;

    if (R->handbrake || R->hb_time != 0) {
        R->hb_sliding = 1;
        if (R->hb_heading == 0) R->hb_heading = heading;
        R->hb_time += dt;
        if (R->hb_time > 1) R->hb_time = 1;
        R->hb_speed = sqrtf(c->vel.x * c->vel.x + c->vel.z * c->vel.z);
        if (R->hb_speed > 1) {
            float w = sinf(R->hb_speed * 3.14159f * 0.004f) * (R->hb_time * steer) * 0.27f * 0.5f;
            if (!(c->vel.z < 0 && R->stop_latch)) w = -w;
            c->yawrate = w;
            if (steer != 0) { c->vel.x -= c->vel.x * dt; c->vel.z -= c->vel.z * dt; }
            else { c->vel.x -= c->vel.x * 2 * dt; c->vel.z -= c->vel.z * 2 * dt; }
            int s = ftol(R->hb_speed * 2);
            R->skid = s > 10 ? 10 : s;
        } else {
            if (R->hb_rev_time == 0) R->skid = 0;
            c->vel.x = 0; c->vel.z = 0; c->yawrate = 0;
        }
        return;
    }
    if (R->hb_sliding && fabsf(c->vel.x) < 3) R->hb_sliding = 0;
    float grip = 10 - fabsf(c->vel.z) * 0.15f;
    if (grip < 2.5f) grip = 2.5f;
    if (dyaw_w > 0 && dyaw_w < TWO_PI) {
        int s = ftol(fabsf(sinf(dyaw_w) * 20));
        R->skid = s > 10 ? 10 : s;
        if (c->vel.z < 10 && R->skid != 0) {
            R->skid = 5;
            c->yawrate += c->vel.z < 0 ? grip * dyaw : -grip * dyaw;
            c->vel.z -= c->vel.z * 2 * dt;
            c->roll += dt * d->roll_gain * steer;
        } else {
            c->yawrate += c->vel.z < 0 ? grip * dyaw : -grip * dyaw;
            if (fabsf(steer) > 20) c->vel.z -= (0.25f - c->vel.z * 0.003f) * c->vel.z * dt;
            c->roll -= dt * d->roll_gain * steer;
        }
    } else {
        R->skid = 0;
        if (c->vel.z < 0) c->yawrate += c->vel.z * -0.05f * grip * dyaw;
        else c->yawrate -= grip * dyaw;
        c->roll += dt * d->roll_gain * steer;
    }
    if (c->vel.z < 1) R->skid = 0;
    {
        float r = c->yawrate * dt;
        float vx = c->vel.x * cosf(r) - c->vel.z * sinf(r);
        c->vel.x = R->upg_banden ? vx : vx * 0.8f;
    }
}

/* body motion: car_body_roll 0x40ebd0, car_body_pitch 0x40ece0, car_body_height 0x40ee80 (the player's CarsBody) */
static void car_body(int o)
{
    Obj *c = &R->obj[o];
    const CarsBody *b = &R->body[R->plr_car];
    float dt = R->dt;
    float acc = -(b->roll_damp * c->roll_vel) - b->roll_factor * c->roll;
    c->roll_vel += dt * acc;
    c->roll += dt * c->roll_vel;
    c->roll_vel += frand_sign() * 4 * c->vel.z * dt;
    acc = -(b->pitch_damp * c->pitch_vel) - b->roll_factor * c->pitch;       /* sic: roll_factor */
    c->pitch_vel += dt * acc;
    c->pitch += dt * c->pitch_vel;
    c->pitch_vel += frand_sign() * 4 * c->vel.z * dt;
    Vec3 p = c->pos, v = c->vel;
    int i = ftol(c->yaw) & 1023;
    float px = p.x + 10 * dt * (COS[i] * v.x - SIN[i] * v.z);
    float pz = p.z + 10 * dt * ((SIN[i] + COS[i]) * v.z);                    /* sic */
    int ch = find_chunk(c->chunk, v3(px, p.y, pz));
    float h = chunk_height_at(&R->track.chunks[ch], px, pz);
    c->vel.y += dt * (-(b->height_damp * c->vel.y) - (p.y - h) * b->height_factor);
    c->pos.y += dt * c->vel.y;
    if (p.y < h - 0.5f) { c->pos.y = h - 0.5f; c->vel.y = -c->vel.y; }
    if (h + 2.0f < p.y) { if (o == R->plr_obj) R->skid = 0; c->vel.y -= 10 * dt; }
}

static void car_drive_new(int o, float steer, float gas, float brake)
{
    Obj *c = &R->obj[o];
    if (R->turbo_active) gas = 100.0f;
    if (c->yaw < 0) c->yaw += 4096;
    if (c->yaw >= 4096) c->yaw -= 4096;
    int ev = R->game_event;
    if (R->handbrake == 1 && R->forced_brake == 1 && ev != 8 && ev != 6 && ev != 3) R->handbrake = R->forced_brake = 0;
    if (ev == 8 || ev == 6 || ev == 3) { R->handbrake = 1; R->gas = 0; gas = 0; R->forced_brake = 1; }
    float h = chunk_height_at(&R->track.chunks[c->chunk], c->pos.x, c->pos.z);
    if (c->pos.y - 5.0f <= h) {
        car_engine_new(c, gas, brake);
        car_steer_new(c, steer, gas);
        if (R->gear <= 1 && gas > 80) R->skid = ftol(gas * 0.1f);
    } else {
        c->vel.x -= R->dt * (c->vel.x * 5);
        c->yawrate -= R->dt * (c->yawrate * 10);
        R->rpm = 4000 + 60 * gas;
    }
    car_body(o);
}

/* ---- input: plr_input_keyboard 0x4265b0 (new model), plus a game controller (PORT) ------------------------------- */
static void plr_input(int o)
{
    (void)o;
    float dt = R->dt;
    const int *k = R->input_keys;
    const PlatPad *pad = plat_pad();
    int left = plat_key(k[0]), right = plat_key(k[1]), up = plat_key(k[2]), down = plat_key(k[3]);
    int turbo = plat_key(k[4]), hand = plat_key(k[5]);
    R->handbrake = hand || (pad->buttons & PAD_B) ? 1 : 0;
    R->turbo_active = 0;
    if ((turbo || (pad->buttons & (PAD_Y | PAD_RB))) && R->turbo > 0) {   /* plr_handle_turbo 0x425cf0 (simplified) */
        static unsigned last;
        if (R->tick - last >= 3) { last = R->tick; R->turbo--; }        /* one charge per 100 ms */
        R->turbo_active = 1;
    }
    float pad_gas = pad->rt > 0.05f ? pad->rt * 100 : (pad->buttons & PAD_A) ? 100 : 0;
    float pad_brake = pad->lt > 0.05f ? pad->lt * 100 : (pad->buttons & PAD_X) ? 100 : 0;
    if (up) { R->gas = R->gas + 70 * dt; if (R->gas > 100) R->gas = 100; }
    else R->gas = 0;
    if (down) { R->brake = ftol((float)R->brake + 40 * dt); if (R->brake > 100) R->brake = 100; }
    else R->brake = 0;
    if (R->test_gas > 0) { R->gas = R->test_gas; R->steer = R->test_steer; return; }
    if (!up && pad_gas > 0) R->gas = pad_gas;
    if (!down && pad_brake > 0) R->brake = (int)pad_brake;
    float lx = pad->lx;
    if (fabsf(lx) > 0.15f || (pad->buttons & (PAD_LEFT | PAD_RIGHT))) {
        /* analogue: as DEVICE 3 does, steer = position (with a dead zone), no curve */
        float v = (pad->buttons & PAD_LEFT) ? -1 : (pad->buttons & PAD_RIGHT) ? 1 : (lx - (lx > 0 ? 0.15f : -0.15f)) / 0.85f;
        R->steer = v * 100;
        return;
    }
    float s = R->steer, kk = 2.0f * (1.0f - fabsf(s) * 0.01f);
    if (left) {
        if (R->steer > 0) { R->steer = 0; return; }
        if (s == 0) s = -15;
        R->steer = s - dt * kk * 200;
        if (R->steer < -100) R->steer = -100;
    } else if (right) {
        if (R->steer < 0) { R->steer = 0; return; }
        if (s == 0) s = 15;
        R->steer = s + dt * kk * 200;
        if (R->steer > 100) R->steer = 100;
    } else {
        if (R->steer > 0) { s = R->steer - 400 * dt; R->steer = s; if (R->steer < 0) R->steer = 0; }
        if (R->steer < 0) { R->steer = s + 400 * dt; if (R->steer > 0) R->steer = 0; }
    }
}

/* handle_player_car 0x425e20 */
static int handle_player_car(int o)
{
    Obj *c = &R->obj[o];
    if (c->chunk != c->prev_chunk) {
        int d = c->chunk - c->prev_chunk, n = R->track.nchunks;
        if (d > n / 2) d -= n;
        if (d < -n / 2) d += n;
        R->progress += d;
    }
    R->speed_kmh = ftol(c->vel.z * 3.6f);
    c->force = v3(0, 0, 0);
    c->torque = 0;
    if (!R->race_started) { R->rpm = 0; R->skid = 0; c->vel = v3(0, 0, 0); c->yawrate = 0; return 1; }
    if (!R->control) return 1;
    plr_input(o);
    car_drive_new(o, R->steer, R->gas, (float)R->brake);
    if (getenv("A2_TRACE") && R->tick % 15 == 0)
        fprintf(stderr, "t%u gas %.0f brake %d vz %.2f vx %.2f y %.1f h %.1f gear %d yaw %.1f chunk %d\n", R->tick, R->gas, R->brake,
                c->vel.z, c->vel.x, c->pos.y, chunk_height_at(&R->track.chunks[c->chunk], c->pos.x, c->pos.z), R->gear, c->yaw, c->chunk);
    return 1;
}

/* ---- cameras: c_camera::update 0x447ab0 with the player views of set_player_cam_mode 0x414b70 ------------------ */
static void car_axes(const Obj *c, Vec3 *right, Vec3 *up, Vec3 *fwd)
{
    int i = ftol(c->yaw) & 1023;
    Vec3 f = v3(-SIN[i], 0, COS[i]);
    Vec3 n = R->track.chunks[c->chunk].up;           /* the car lies on its chunk's plane */
    if (n.y < 0.5f) n = v3(0, 1, 0);
    f = v3_norm(v3_sub(f, v3_scale(n, v3_dot(f, n))));
    *up = n;
    *fwd = f;
    *right = v3_norm(v3_cross(n, f));
}

static Vec3 car_point(const Obj *c, Vec3 off)
{
    Vec3 r, u, f;
    car_axes(c, &r, &u, &f);
    return v3_add(c->pos, v3_add(v3_scale(r, off.x), v3_add(v3_scale(u, off.y), v3_scale(f, off.z))));
}

static void camera_update(void)
{
    static const Vec3 OFF[11] = { { 0, 25, -100 }, { 0, 25, -60 }, { 0, 38, -55 }, { 0, 15.5f, -1 }, { 25, 15, -50 },
                                  { 0, 23, 40 }, { -30, 15, 60 }, { 25, 15, -50 }, { -20, 9, -30 }, { -25, 55, -30 },
                                  { 0, 45, -55 } };
    const Obj *c = &R->obj[R->plr_obj];
    int m = R->cam_mode;
    Vec3 off = OFF[m];
    Vec3 pos = car_point(c, off);
    if (m == 3) {                                       /* cockpit: no damping, looks along the car */
        Vec3 r, u, f;
        car_axes(c, &r, &u, &f);
        R->cam_pos = R->cam_prev = pos;
        R->cam_target = v3_add(pos, f);
    } else {
        pos = v3_add(R->cam_prev, v3_scale(v3_sub(pos, R->cam_prev), 0.245f));
        /* keep the distance behind the car rigid (0x447f97) */
        Vec3 r, u, f;
        car_axes(c, &r, &u, &f);
        Vec3 d = v3_sub(pos, c->pos);
        Vec3 l = v3(v3_dot(d, r), v3_dot(d, u), off.z);
        pos = v3_add(c->pos, v3_add(v3_scale(r, l.x), v3_add(v3_scale(u, l.y), v3_scale(f, l.z))));
        R->cam_prev = R->cam_pos = pos;
        R->cam_target = car_point(c, v3(0, 10, 0));
    }
    R->cam_chunk = find_chunk(R->cam_chunk, R->cam_pos);
}

static void game_keys(void)
{
    const PlatPad *pad = plat_pad();
    if (plat_key_pressed('1')) R->cam_mode = 1;
    if (plat_key_pressed('2')) R->cam_mode = 3;
    if (plat_key_pressed('4')) R->cam_mode = 0;
    if (pad->pressed & PAD_BACK) R->cam_mode = R->cam_mode == 1 ? 3 : R->cam_mode == 3 ? 0 : 1;
    int back = plat_key('3') || (pad->buttons & PAD_LB);
    if (back && R->cam_mode != 5) { R->cam_saved_mode = R->cam_mode; R->cam_mode = 5; }
    if (!back && R->cam_mode == 5) R->cam_mode = R->cam_saved_mode;
    if (plat_key_pressed('D')) R->detail = (R->detail + 1) & 3;
    if (plat_key_pressed(VK_ESCAPE) || (pad->pressed & PAD_START)) R->quit = 1;
}

/* ---- set-up ------------------------------------------------------------------------------------------------------ */
static void load_cars_dat(Race *r)
{
    size_t n = 0;
    uint8_t *d = res_load_file("CARS.DAT", &n);
    if (!d || n < 1480) { free(d); fprintf(stderr, "CARS.DAT missing\n"); return; }
    for (int i = 0; i < 10; i++) memcpy(&r->cars[i], d + i * 124, 124);
    for (int i = 0; i < 10; i++) memcpy(&r->body[i], d + 1240 + i * 24, 24);
    free(d);
}

/* g_car_types 0x484b18 (16 x 0xee bytes): anim name and the dimensions used here */
static void load_car_types(Race *r)
{
    PeFile pe;
    if (!pe_open(&pe, "spel.dat")) return;
    for (int i = 0; i < 16; i++) {
        uint32_t a = 0x484b18 + (uint32_t)i * 0xee;
        const uint8_t *p = pe_ptr(&pe, a, 0xee);
        if (!p) break;
        CarType *t = &r->car_types[i];
        memcpy(t->anim, p, 18);
        t->anim[18] = 0;
        memcpy(&t->mass, p + 0x16, 4);
        memcpy(&t->length, p + 0x1a, 4);
        memcpy(&t->width, p + 0x1e, 4);
        memcpy(&t->cg_front, p + 0x22, 4);
        memcpy(&t->cg_rear, p + 0x26, 4);
    }
    pe_close(&pe);
}

static void read_ini(Race *r)
{
    char p[1024];
    res_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    static const char *keys[6] = { "LEFT", "RIGHT", "UP", "DOWN", "EXTRA1", "EXTRA2" };
    static const int defs[6] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_CONTROL, VK_MENU };
    for (int i = 0; i < 6; i++) r->input_keys[i] = ini_int(g, "INPUT", keys[i], defs[i]);
    r->upg_motor = ini_int(g, "PLAYER", "MOTOR", 0);
    r->upg_banden = ini_int(g, "PLAYER", "BANDEN", 0);
    r->upg_abs = ini_int(g, "PLAYER", "ABS", 0);
    r->upg_turbo = ini_int(g, "PLAYER", "TURBO", 0);
    r->upg_gearbox = ini_int(g, "PLAYER", "VERSNELLINGSBAK", 0);
    r->detail = ini_int(g, "GFX OPTIONS", "overlay_detail", 3) & 3;
    ini_free(g);
}

int race_init(Race *r, int etappe, int car)
{
    memset(r, 0, sizeof *r);
    R = r;
    for (int i = 0; i < 1024; i++) { SIN[i] = sinf(TWO_PI * i / 1024); COS[i] = cosf(TWO_PI * i / 1024); }
    r->etappe = etappe;
    if (!track_load(&r->track, etappe)) return 0;
    objects_load(&r->objects, &r->track);
    load_cars_dat(r);
    load_car_types(r);
    read_ini(r);
    r->plr_car = car >= 0 && car < 7 ? car : 0;
    r->turbo = r->upg_turbo ? 100 : 0;
    r->plr_obj = -1;
    /* start: the chunk of the finish trigger (14), the player in the right-hand lanes (side 1) */
    /* city tracks start at their finish line (trigger 14, trigger_find_start_position 0x429a20); the highways at
     * their beginning (docs/GAMEFLOW.md) */
    int start = 3;
    if (r->track.city)
        for (int i = 0; i < r->track.nchunks; i++)
            if (r->track.chunks[i].trigger[0] == 14 || r->track.chunks[i].trigger[1] == 14) { start = track_wrap(&r->track, i - 2); break; }
    const Chunk *ch = &r->track.chunks[start];
    Vec3 pos = ch->pos;
    if (ch->nwp[1] > 0) pos = r->track.wps[ch->wp[1][ch->nwp[1] > 1 ? 1 : 0]].pos;
    float yaw = atan2f(-ch->fwd.x, ch->fwd.z) * 1024.0f / TWO_PI;
    if (yaw < 0) yaw += 1024;
    char anim[32];
    snprintf(anim, sizeof anim, "%s", r->car_types[r->plr_car].anim[0] ? r->car_types[r->plr_car].anim : "RACER_1_ANIM");
    int o = obj_add(anim_find(anim), pos, yaw);
    r->plr_obj = o;
    Obj *c = &r->obj[o];
    c->car_type = r->plr_car;
    c->update = handle_player_car;
    c->pos.y = chunk_height_at(&r->track.chunks[c->chunk], pos.x, pos.z);
    c->draw_pos0 = c->draw_pos1 = c->pos;
    obj_init_physics(c, r->car_types[r->plr_car].mass > 0 ? r->car_types[r->plr_car].mass : 1000, 1.7f, 4.5f, 0.2f);
    r->steer = 30; r->gas = 20;                        /* init_player_car's old-model neutral values */
    r->cam_mode = 1;
    r->cam_prev = r->cam_pos = car_point(c, v3(0, 25, -60));
    r->cam_target = car_point(c, v3(0, 10, 0));
    r->cam_chunk = c->chunk;
    r->cam_draw0 = r->cam_draw1 = r->cam_pos;
    r->tgt_draw0 = r->tgt_draw1 = r->cam_target;
    r->race_started = 1;
    r->control = 1;
    r->dt = 1.0f / RACE_HZ;
    return 1;
}

void race_free(Race *r)
{
    objects_free(&r->objects);
    track_free(&r->track);
    memset(r, 0, sizeof *r);
}

/* one game tick (handle_game 0x424a70, the parts there are so far) */
static void race_tick(void)
{
    R->tick++;
    for (int o = 0; o < R->nobj; o++) {
        Obj *c = &R->obj[o];
        c->draw_pos0 = c->pos;
        c->draw_yaw0 = c->yaw;
    }
    R->cam_draw0 = R->cam_pos;
    R->tgt_draw0 = R->cam_target;
    for (int o = 0; o < R->nobj; o++)
        if (R->obj[o].used && R->obj[o].active && R->obj[o].update) R->obj[o].update(o);
    obj_move_all(R->dt);
    camera_update();
    for (int o = 0; o < R->nobj; o++) {
        Obj *c = &R->obj[o];
        c->draw_pos1 = c->pos;
        c->draw_yaw1 = c->yaw;
    }
    R->cam_draw1 = R->cam_pos;
    R->tgt_draw1 = R->cam_target;
}

static Mat4 view_lh(Vec3 eye, Vec3 target)
{
    Mat4 flip = m4_scale(v3(1, 1, -1));
    Mat4 v = m4_look_at(v3(eye.x, eye.y, -eye.z), v3(target.x, target.y, -target.z), v3(0, 1, 0));
    return m4_mul(v, flip);
}

static float lerp_angle(float a, float b, float t)
{
    float d = b - a;
    while (d > 512) d -= 1024;
    while (d < -512) d += 1024;
    return a + d * t;
}

static void race_draw(float alpha)
{
    int w, h;
    plat_size(&w, &h);
    gfx_begin(w, h, 0.55f, 0.7f, 0.9f);
    Vec3 eye = v3_lerp(R->cam_draw0, R->cam_draw1, alpha), tgt = v3_lerp(R->tgt_draw0, R->tgt_draw1, alpha);
    float fovy = 2 * atanf(tanf(40.5f * PI_F / 180) * 0.75f);
    Mat4 proj = m4_perspective(fovy, (float)w / h, 10, 12500), view = view_lh(eye, tgt);
    gfx_set_matrices(&proj, &view);
    int start = track_visible_start(&R->track, R->cam_chunk, v3_sub(tgt, eye), 2, 30);
    track_draw(&R->track, start, 30, 0);
    objects_draw(&R->objects, &R->track, start, 30);
    for (int o = 0; o < R->nobj; o++) {
        const Obj *c = &R->obj[o];
        if (!c->used || !c->visible) continue;
        if (o == R->plr_obj && R->cam_mode == 3) continue;          /* cockpit view: the car is not drawn */
        Obj tmp = *c;
        tmp.pos = v3_lerp(c->draw_pos0, c->draw_pos1, alpha);
        tmp.yaw = lerp_angle(c->draw_yaw0, c->draw_yaw1, alpha);
        Vec3 r, u, f;
        car_axes(&tmp, &r, &u, &f);
        Mat4 m = m4_identity();
        m.m[0] = r.x; m.m[1] = r.y; m.m[2] = r.z;
        m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z;
        m.m[8] = f.x; m.m[9] = f.y; m.m[10] = f.z;
        m.m[12] = tmp.pos.x; m.m[13] = tmp.pos.y; m.m[14] = tmp.pos.z;
        /* the visual body pitch and roll (1024 units) */
        Mat4 body = m4_mul(m4_rot_x(-c->pitch * TWO_PI / 1024), m4_rot_z(c->roll * TWO_PI / 1024));
        m = m4_mul(m, body);
        model_draw_matrix(anim_model(c->anim, c->frame), &m);
    }
    gfx_set_model(NULL);
}

int race_frame(Race *r, double now)
{
    R = r;
    if (r->last_time == 0) r->last_time = now;
    double el = now - r->last_time;
    r->last_time = now;
    if (el > 0.25) el = 0.25;
    r->accum += el;
    game_keys();
    while (r->accum >= 1.0 / RACE_HZ) {
        r->accum -= 1.0 / RACE_HZ;
        race_tick();
    }
    race_draw((float)(r->accum * RACE_HZ));
    return !r->quit;
}
