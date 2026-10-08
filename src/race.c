/* race.c - see race.h. Function names and addresses refer to spel.dat (docs/PHYSICS.md, INPUT.md, RENDER.md).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "race.h"
#include "ai.h"
#include "car_old.h"
#include "collide.h"
#include "effects.h"
#include "exetab.h"
#include "fog.h"
#include "gfx.h"
#include "hud.h"
#include "lights.h"
#include "texture.h"
#include "ini.h"
#include "model.h"
#include "pad.h"
#include "paths.h"
#include "plat.h"
#include "radar.h"
#include "res.h"
#include "rules.h"
#include "screen.h"
#include "sfx.h"
#include "shadow.h"
#include "touch.h"
#include "txcopy.h"
#include "wheels.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.28319f                 /* the original's 2 pi (0x47f... 6.28319f) */
static float SIN[ANGLE_UNITS], COS[ANGLE_UNITS];   /* math3d_init_tables 0x450280 (4096 entries, track.h) */
#define AMASK (ANGLE_UNITS - 1)
static Race *R;                         /* the race the callbacks work on */
/* the software modes' sky (gfx_sky.cpp): Game.ini [OPTIONS] BACKGROUND (non-zero: none), the image */
static int g_sky_off;
static unsigned g_sky_sw;
static int g_sky_w, g_sky_h;

static int ftol(float f) { return (int)f; }   /* MSVC _ftol: truncation toward zero */
static float frand_sign(void) { return rand() % 10 < 5 ? 1.0f : -1.0f; }

/* ---- road helpers -------------------------------------------------------------------------------------------- */
/* chunk_height_at 0x442f00: the chunk is one plane n.p = d */
float chunk_height_at(const Chunk *c, float x, float z)
{
    if (fabsf(c->up.y) < 1e-6f) return c->pos.y;
    return (c->plane_d - c->up.x * x - c->up.z * z) / c->up.y;
}

static int find_chunk(int start, Vec3 p) { return track_find_chunk(&R->track, start, p); }

/* ---- objects ----------------------------------------------------------------------------------------------------- */
int obj_add(int anim, Vec3 pos, float yaw)
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
            o->ai = o->waypoint = -1;
            o->chunk = o->prev_chunk = find_chunk(0, pos);
            o->draw_pos0 = o->draw_pos1 = pos;
            o->draw_yaw0 = o->draw_yaw1 = yaw;
            if (i >= R->nobj) R->nobj = i + 1;
            return i;
        }
    return -1;
}

/* obj_init_physics 0x4403b0 */
void obj_init_physics(Obj *o, float mass, float w, float l, float restitution)
{
    o->physics = 1;
    o->inv_mass = mass > 1e-6f ? 1.0f / mass : 0;
    o->inv_inertia = mass > 1e-6f ? 1.0f / (mass * (w * w + l * l) * (1.0f / 12.0f)) : 0;
    o->restitution = restitution;
}

float race_sin(int a) { return SIN[a & AMASK]; }
float race_cos(int a) { return COS[a & AMASK]; }

/* obj_integrate 0x4404a0: moves by frac t of the velocity, or by the slide-out vector of a collision (use_resp);
 * the yaw step is skipped while the turn is blocked by an overlap (rotblk) */
static void obj_integrate(Obj *c, float dt, float t, int rotblk, int use_resp, Vec3 resp)
{
    int iy = ftol(floorf(c->yaw));
    Vec3 d = use_resp ? v3_scale(v3_scale(resp, 10), dt) : v3_scale(c->vel, t * 10 * dt);
    int i = iy & AMASK;
    c->pos.z += ftol((SIN[i] * d.x + COS[i] * d.z) * 10000) * 0.0001f;
    c->pos.x += ftol((COS[i] * d.x - SIN[i] * d.z) * 10000) * 0.0001f;
    float dyaw = dt * c->yawrate * ANGLE_UNITS / TWO_PI;
    if (rotblk == 0) c->yaw += dyaw;
    if (!c->no_vel_rotate) {
        int j = ftol(-dyaw) & AMASK;                /* truncated to whole angle units: the drift */
        float vx = c->vel.x, vz = c->vel.z;
        c->vel.x = COS[j] * vx - SIN[j] * vz;
        c->vel.z = SIN[j] * vx + COS[j] * vz;
    }
    c->vel = v3_add(c->vel, v3_scale(c->force, c->inv_mass * dt));
    c->yawrate += c->torque * c->inv_inertia * dt;
}

/* obj_move_all 0x440ea0: objects of the handling window (ai.c) are taken off their waypoint's obstacle list, moved
 * with the swept collision test of docs/PHYSICS.md 8.6 (collide.c), and put on the list of their new waypoint */
static void obj_move_all(float dt0)
{
    for (int o = 0; o < R->nobj; o++) {
        Obj *c = &R->obj[o];
        if (!c->used || !c->active || c->physics != 1) continue;
        c->vel.x = ftol(c->vel.x * 10000) * 0.0001f;
        c->vel.z = ftol(c->vel.z * 10000) * 0.0001f;
        if (!ai_obj_in_window(o)) continue;
        int n = o == R->plr_obj ? 8 : 1;
        float h = o == R->plr_obj ? dt0 * 0.125f : dt0;
        ai_wp_remove(o);
        for (int k = 0; k < n; k++) {
            int rotblk = 0, hit = 0;
            float t = 1.0f;
            Vec3 resp = v3(0, 0, 0);
            if (!R->collision_off) {
                t = bbox_sweep_object(o, &rotblk, h);
                if (obj_slide_out(o, h, &resp, t, &rotblk)) hit = 1;
            }
            obj_integrate(c, h, t, rotblk, hit, resp);
            coll_after_step(o);
        }
        c->prev_chunk = c->chunk;
        c->chunk = find_chunk(c->chunk, c->pos);
        ai_wp_add(o);
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

/* body motion, also of the AI cars, always with the player's CarsBody [quirk]: car_body_roll 0x40ebd0 */
void car_body_roll(int o)
{
    Obj *c = &R->obj[o];
    const CarsBody *b = &R->body[R->plr_car];
    float dt = R->dt;
    float acc = -(b->roll_damp * c->roll_vel) - b->roll_factor * c->roll;
    c->roll_vel += dt * acc;
    c->roll += dt * c->roll_vel;
    c->roll_vel += frand_sign() * 4 * c->vel.z * dt;
}

void car_body_pitch(int o)                                                     /* 0x40ece0 */
{
    Obj *c = &R->obj[o];
    const CarsBody *b = &R->body[R->plr_car];
    float dt = R->dt;
    float acc = -(b->pitch_damp * c->pitch_vel) - b->roll_factor * c->pitch;   /* sic: roll_factor */
    c->pitch_vel += dt * acc;
    c->pitch += dt * c->pitch_vel;
    c->pitch_vel += frand_sign() * 4 * c->vel.z * dt;
}

/* car_road_height_reset 0x40ede0 */
float car_road_height_reset(int o)
{
    Obj *c = &R->obj[o];
    float h = chunk_height_at(&R->track.chunks[c->chunk], c->pos.x, c->pos.z);
    if (h - 1 > c->pos.y) { c->pos.y = h - 1; c->vel.y = -c->vel.y; }
    return h;
}

void car_body_height(int o)                                                    /* 0x40ee80 */
{
    Obj *c = &R->obj[o];
    const CarsBody *b = &R->body[R->plr_car];
    float dt = R->dt;
    Vec3 p = c->pos, v = c->vel;
    int i = ftol(c->yaw) & AMASK;
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
    car_body_roll(o);
    car_body_pitch(o);
    car_body_height(o);
}

/* ---- input: plr_input_keyboard 0x4265b0 (new model), plus the game controllers (PORT, pad.h): their buttons count
 * as the keys (as the original's joypad, DEVICE 2), their sticks / triggers / wheel axes are analogue (DEVICE 3).
 * The touch controls (touch.h) arrive the same way: their buttons digital, drag and tilt steering analogue ------- */
static int g_pad_steering;                           /* the analogue steering had the wheel last tick */

static void plr_input(int o)
{
    (void)o;
    float dt = R->dt;
    const int *k = R->input_keys;
    int left = plat_key(k[0]) || pad_digital(PA_STEER_LEFT), right = plat_key(k[1]) || pad_digital(PA_STEER_RIGHT);
    int up = plat_key(k[2]) || pad_digital(PA_GAS), down = plat_key(k[3]) || pad_digital(PA_BRAKE);
    int turbo = plat_key(k[4]) || pad_held(PA_TURBO), hand = plat_key(k[5]) || pad_held(PA_HANDBRAKE);
    R->handbrake = hand ? 1 : 0;
    R->turbo_active = 0;
    if (turbo && R->turbo > 0) {                     /* plr_handle_turbo 0x425cf0 (simplified) */
        static unsigned last;
        if (R->tick - last >= 3) { last = R->tick; R->turbo--; }        /* one charge per 100 ms */
        R->turbo_active = 1;
        pad_rumble(0.12f, 150);                      /* PORT: a light buzz (the FF stick of DEVICE 4 got a jolt) */
    }
    float pad_gas = pad_analog(PA_GAS) * 100, pad_brake = pad_analog(PA_BRAKE) * 100;
    if (up) { R->gas = R->gas + 70 * dt; if (R->gas > 100) R->gas = 100; }
    else R->gas = 0;
    if (down) { R->brake = ftol((float)R->brake + 40 * dt); if (R->brake > 100) R->brake = 100; }
    else R->brake = 0;
    if (R->test_gas > 0) { R->gas = R->test_gas; R->steer = R->test_steer; return; }
    if (pad_gas > R->gas) R->gas = pad_gas;
    if (pad_brake > R->brake) R->brake = (int)pad_brake;
    float st = pad_steer();
    if (st != 0 && !left && !right) {
        /* analogue: as DEVICE 3 does, steer = position (dead zone and curve from controller.ini) */
        R->steer = st * 100;
        g_pad_steering = 1;
        return;
    }
    if (g_pad_steering && !left && !right) R->steer = 0;   /* the stick let go: straight, as a centred stick */
    g_pad_steering = 0;
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
    R->plr_pos = c->pos;
    if (c->chunk != c->prev_chunk) {
        int d = c->chunk - c->prev_chunk, n = R->track.nchunks;
        if (d > n / 2) d -= n;
        if (d < -n / 2) d += n;
        R->progress += d;
    }
    R->speed_kmh = ftol(c->vel.z * 3.6f);
    c->force = v3(0, 0, 0);
    c->torque = 0;
    if (ai_plr_autopilot(o)) { R->rpm = 0; R->skid = 0; return 1; }   /* the old cop model drives the player (ai.c) */
    if (!R->race_started) { R->rpm = 0; R->skid = 0; c->vel = v3(0, 0, 0); c->yawrate = 0; return 1; }
    if (!R->control) return 1;
    if (R->stuur_model == 1) {                       /* the old model: per-frame steps, forces for obj_integrate */
        const int *k = R->input_keys;
        int ev = R->game_event, stop = ev == 8 || ev == 6 || ev == 3;
        int left = plat_key(k[0]) || pad_digital(PA_STEER_LEFT);
        int right = plat_key(k[1]) || pad_digital(PA_STEER_RIGHT);
        int up = !stop && (plat_key(k[2]) || pad_digital(PA_GAS));
        int down = stop || plat_key(k[3]) || pad_digital(PA_BRAKE);
        if (R->test_gas > 0) { up = 1; left = R->test_steer < 0; right = R->test_steer > 0; }
        R->handbrake = plat_key(k[5]) || pad_held(PA_HANDBRAKE) ? 1 : 0;
        car_old_input(R, left, right, up, down);
        if (R->test_gas <= 0) {
            /* PORT: the analogue elements in the old model's ranges, as DEVICE 3 there: steer 30 +- 30, gas 0..40
             * around the neutral 20 (below 20 brakes / reverses) */
            float st = pad_steer(), g = pad_analog(PA_GAS) - pad_analog(PA_BRAKE);
            if (st != 0 && !left && !right) R->steer = (float)ftol(30 + st * 30 + (st > 0 ? 0.5f : -0.5f));
            if (g != 0 && !up && !down) R->gas = 20 + 20 * g;
        }
        car_old_drive(R, o, R->input_device);
        return 1;
    }
    plr_input(o);
    car_drive_new(o, R->steer, R->gas, (float)R->brake);
    if (getenv("A2_TRACE") && R->tick % 15 == 0)
        fprintf(stderr, "t%u gas %.0f brake %d vz %.2f vx %.2f y %.1f h %.1f gear %d yaw %.1f chunk %d\n", R->tick, R->gas, R->brake,
                c->vel.z, c->vel.x, c->pos.y, chunk_height_at(&R->track.chunks[c->chunk], c->pos.x, c->pos.z), R->gear, c->yaw, c->chunk);
    return 1;
}

/* ---- cameras: c_camera::update 0x447ab0 with the player views of set_player_cam_mode 0x414b70 ------------------ */
void car_axes(const Obj *c, Vec3 *right, Vec3 *up, Vec3 *fwd)
{
    int i = ftol(c->yaw) & AMASK;
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

/* the arrest camera: cops_event_camera 0x4151f0 (still, above the chunk after the player's, looking at a point 20
 * above the car) */
static void event_camera(const Obj *c)
{
    if (!R->ev_cam) {
        const Chunk *ch = &R->track.chunks[track_wrap(&R->track, c->chunk + 1)];
        R->ev_cam = 1;
        R->cam_pos = v3(ch->pos.x, ch->pos.y + 20, ch->pos.z);
    }
    R->cam_prev = R->cam_pos;
    R->cam_target = v3_add(c->pos, v3(0, 20, 0));
    R->cam_chunk = find_chunk(R->cam_chunk, R->cam_pos);
}

static void camera_update(void)
{
    static const Vec3 OFF[11] = { { 0, 25, -100 }, { 0, 25, -60 }, { 0, 38, -55 }, { 0, 15.5f, -1 }, { 25, 15, -50 },
                                  { 0, 23, 40 }, { -30, 15, 60 }, { 25, 15, -50 }, { -20, 9, -30 }, { -25, 55, -30 },
                                  { 0, 45, -55 } };
    const Obj *c = &R->obj[R->plr_obj];
    if (R->game_event == 8) { event_camera(c); return; }
    R->ev_cam = 0;
    Vec3 eye, tgt;
    if (rules_camera(R, &eye, &tgt)) {                  /* start fly-in, finish and game-over orbits (rules.c) */
        R->cam_pos = R->cam_prev = eye;
        R->cam_target = tgt;
        R->cam_chunk = find_chunk(R->cam_chunk, eye);
        return;
    }
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
    /* PORT: the touch controls first (touch.h): what they show follows the rules' state, and they feed pad.h */
    int pause = rules_pause_kind();
    touch_frame(pause == 1 ? TOUCH_PAUSE : pause ? TOUCH_QUIT : rules_hud_on() ? TOUCH_RACE : TOUCH_END,
                rules_hud_on() && !R->event_running && R->detail >= 2);
    if (plat_key_pressed('1')) R->cam_mode = 1;
    if (plat_key_pressed('2')) R->cam_mode = 3;
    if (plat_key_pressed('4')) R->cam_mode = 0;
    if (pad_pressed(PA_CAMERA)) R->cam_mode = R->cam_mode == 1 ? 3 : R->cam_mode == 3 ? 0 : 1;
    int back = plat_key('3') || pad_held(PA_LOOK_BACK);
    pad_race_active(!rules_paused() && rules_hud_on());  /* a force-feedback wheel's centring spring */
    if (back && R->cam_mode != 5) { R->cam_saved_mode = R->cam_mode; R->cam_mode = 5; }
    if (!back && R->cam_mode == 5) R->cam_mode = R->cam_saved_mode;
    if (plat_key_pressed('D')) R->detail = (R->detail + 1) & 3;
    rules_keys(R);                                      /* pause, quit question, horn, cheats, continue */
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
    res_user_path(p, sizeof p, "Game.ini");
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
    r->overlay_off = ini_int(g, "GFX OPTIONS", "disable_overlay", 0) != 0;
    r->collision_off = ini_int(g, "OPTIONS", "collisionoff", 0);
    r->stuur_model = ini_int(g, "OPTIONS", "stuur_model", 0);
    r->input_device = ini_int(g, "INPUT", "DEVICE", 1);
    /* SENSITIVITY (the menu's Gevoeligheid, shown for the wheel / joystick devices, which the menu writes as DEVICE
     * 3 - the Sidewinder too): the original's analogue steering curve. The original used it only in the old model
     * (DEVICE 3) and for DEVICE 4; PORT: with DEVICE 3 it shapes the controllers' analogue steering in both models,
     * else controller.ini's steer_linearity does */
    pad_set_steer_curve(r->input_device >= 3 ? ini_int(g, "INPUT", "SENSITIVITY", 50) : -1);
    tex_set_gamma(ini_int(g, "GFX OPTIONS", "gamma", 1) == 1);
    /* cloud_init 0x4201a0: no smoke without g_hw_accel (clouds = 0) */
    fx_init(ini_int(g, "GFX OPTIONS", "skidmarks", 1), ini_int(g, "GFX OPTIONS", "fade_skmarks", 1),
            ini_int(g, "GFX OPTIONS", "clouds", 1) && scr_hw3d(), ini_int(g, "GFX OPTIONS", "alpha_blending", 1));
    g_sky_off = ini_int(g, "OPTIONS", "BACKGROUND", 0);    /* g_opt_background 0x4d9a80 (ini_load_all 0x421c05) */
    ini_free(g);
}

/* the AI's sounds (ai.h): an object's looping sample follows the object (sndfx_play_object_sample, every tick while
 * it sounds); objects that stop asking are silenced at the end of the tick */
static unsigned char g_snd_on[MAX_OBJS], g_snd_was[MAX_OBJS];

static void snd_object(int o, int sample, int on)
{
    if (o < 0 || o >= MAX_OBJS) return;
    if (on && sample > 0 && sfx_object(o, sample, R->obj[o].pos, 1)) g_snd_on[o] = 1;
    else sfx_object_stop(o);
}

static void snd_objects_done(void)
{
    for (int o = 0; o < MAX_OBJS; o++) {
        if (g_snd_was[o] && !g_snd_on[o]) sfx_object_stop(o);
        g_snd_was[o] = g_snd_on[o];
        g_snd_on[o] = 0;
    }
}

int race_init(Race *r, int etappe, int car)
{
    memset(r, 0, sizeof *r);
    R = r;
    scr_init(etappe);                                  /* set_game_display: the mode of Raser.ini RESOLUTION (screen.h) */
    ai_snd_object = snd_object;
    ai_snd_play = sfx_play;
    ai_snd_playing = sfx_playing;
    memset(g_snd_on, 0, sizeof g_snd_on);
    memset(g_snd_was, 0, sizeof g_snd_was);
    for (int i = 0; i < ANGLE_UNITS; i++) { SIN[i] = sinf(TWO_PI * i / ANGLE_UNITS); COS[i] = cosf(TWO_PI * i / ANGLE_UNITS); }
    r->etappe = etappe;
    if (!track_load(&r->track, etappe)) return 0;
    objects_load(&r->objects, &r->track);
    load_cars_dat(r);
    load_car_types(r);
    wheels_init();
    read_ini(r);
    r->plr_car = car >= 0 && car < 7 ? car : 0;
    static int hud_ok;
    if (!hud_ok) hud_ok = hud_init();
    if (scr_hw3d()) {   /* the sky: one of back1..7.pcx at random (0x4234e0), loaded without gamma */
        char n[16];
        snprintf(n, sizeof n, "back%d", rand() % 7 + 1);
        const Texture *t = tex_get(n);
        r->sky_tex = t ? t->id : 0;
    } else if (!g_sky_off) {
        /* sky_init_moving_sky 0x41e740 (software modes, BACKGROUND 0): [MAPS] arg 6 (SCRL_LO 480x200) at 320x200, arg 7
         * (SCRL_HI 960x320) at 640x480, through the screen's palette */
        Image img;
        if (tex_pixels(r->track.sky[scr_res_index() ? 1 : 0], &img)) {
            g_sky_sw = gfx_texture(img.rgba, img.w, img.h, TEX_NEAREST | TEX_CLAMP);
            g_sky_w = img.w;
            g_sky_h = img.h;
            image_free(&img);
        }
    }
    r->turbo = r->upg_turbo ? 100 : 0;
    r->plr_obj = -1;
    r->hud_message = r->hud_msg_shown = -1;
    objects_race_init(r);                              /* road_load_omp's bodies (types 5, 8, 10), objects.c */
    ai_init_cops(r);                                   /* road_load_omp's type-9 objects come first in the object list */
    /* init_player_car 0x4256a0: add_car at g_startchunk - [PLAYER] position, lane 4, the right-hand side (ai.c) */
    int o = ai_init_player(r->plr_car, handle_player_car, plr_collision_cb);
    r->plr_obj = o;
    r->start_chunk = ai_start_chunk();
    Obj *c = &r->obj[o];
    c->pos.y = chunk_height_at(&r->track.chunks[c->chunk], c->pos.x, c->pos.z);
    c->draw_pos0 = c->draw_pos1 = c->pos;
    if (r->stuur_model == 1) car_old_init(r, ai_difficulty() == 0);   /* also the neutral steer 30 / gas 20 */
    r->cam_mode = 1;
    r->cam_prev = r->cam_pos = car_point(c, v3(0, 25, -60));
    r->cam_target = car_point(c, v3(0, 10, 0));
    r->cam_chunk = c->chunk;
    r->cam_draw0 = r->cam_draw1 = r->cam_pos;
    r->tgt_draw0 = r->tgt_draw1 = r->cam_target;
    r->race_started = 1;
    r->control = 1;
    r->dt = 1.0f / RACE_HZ;
    ai_init_cars(r);                                   /* the rest of init_all_cars 0x410b60 and plr_init 0x427fd0 */
    collide_init(r);
    txcopy_init(r);                                    /* sub_42b060, init_radar 0x41cf20 */
    radar_init(r);
    paths_load(etappe);
    fx_reset();
    shadow_init(); lights_init(r); fog_init();         /* init_game / init_player_car: shadows, brake lights, fog */
    sfx_init();
    static const RulesHooks hooks = { ai_player_position, ai_save_racers };
    rules_init(r, &hooks);                             /* state 1: the start camera and the countdown */
    r->race_ms = 0;
    plat_touch_capture(1);                             /* fingers drive the touch controls, not the emulated mouse */
    return 1;
}

void race_free(Race *r)
{
    pad_race_active(0);
    touch_end();
    plat_touch_capture(0);
    g_pad_steering = 0;
    gfx_texture_free(g_sky_sw);
    g_sky_sw = 0;
    sfx_stop_all();
    radar_free();
    txcopy_free();
    rules_free();
    paths_free();
    fx_free();
    ai_free();
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
    lights_tick(R);             /* the brake lights, at the start of handle_player_car (last tick's brake_light) */
    objects_tick();             /* the handlers of the .omp objects without a body (objects.c) */
    for (int o = 0; o < R->nobj; o++)
        if (R->obj[o].used && R->obj[o].active && R->obj[o].update && ai_obj_in_window(o)) R->obj[o].update(o);
    wheels_tick(R);             /* car_update_wheels(_ai), run by the cars' handlers in the original */
    obj_move_all(R->dt);
    txcopy_brake_lights(R);     /* handle_player_car 0x425f0b */
    Obj *p = &R->obj[R->plr_obj];
    if (p->chunk != p->prev_chunk) rules_player_chunk_changed(R, p->prev_chunk, p->chunk);   /* the track triggers */
    ai_tick();                  /* traffic recycling, game actions (arrest, game over), the racer and cop FSMs */
    rules_tick(R);
    if (R->race_started && getenv("A2_TEST_CAM")) R->cam_mode = atoi(getenv("A2_TEST_CAM")) % 11;   /* tests: a view */
    R->race_ms = (float)rules_race_ms();
    paths_update(p->chunk, rules_lap(), R->dt);
    {   /* the player's sounds and effects */
        Vec3 rt, up, fw;
        car_axes(p, &rt, &up, &fw);
        sfx_listener(p->pos, rt);
        sfx_engine(R->rpm, R->upg_motor);
        sfx_squeal(R->skid);
        sfx_rattle(R->damage);
        sfx_turbo(R->turbo_active);
        snd_objects_done();
        FxCar fc = { p->pos, rt, up, fw, p->vel, p->car_type >= 0 ? R->car_types[p->car_type].cg_rear : 1.3f };
        fx_skid(&fc, R->skid, R->dt);
        fx_smoke(&fc, R->skid, R->dt);
        fx_tick(R->dt);
    }
    camera_update();
    for (int o = 0; o < R->nobj; o++) {
        Obj *c = &R->obj[o];
        c->draw_pos1 = c->pos;
        c->draw_yaw1 = c->yaw;
    }
    R->cam_draw1 = R->cam_pos;
    R->tgt_draw1 = R->cam_target;
}

/* the sky backdrop (gfx_bkg.cpp, draw_bkg 0x4190d0 / bkg_update_colors 0x418f00): two bands over the screen, the
 * image scrolls with the car's heading, the horizon colour changes along the track */
static void draw_sky(void)
{
    const Obj *c = &R->obj[R->plr_obj];
    float u0 = 1.0f - (float)(ftol(c->yaw) & AMASK) / ANGLE_UNITS, u1 = u0 + 0.15f;
    int n = R->track.nchunks;
    int dc = abs(c->chunk - R->start_chunk) * 2;
    float t = (float)(dc % (2 * n)) / n;
    float a = fabsf(sinf(t * PI_F / 2)), b = fabsf(sinf(t * PI_F)), cc = fabsf(sinf(t * PI_F / 4));
    fog_frame(a);                                       /* bkg_set_fog_params 0x418830 (fog.c) */
    float br = 200 + 25 * cc, bg = 55 + 175 * cc, bb = 55;
    uint8_t hr = (uint8_t)(br + (255 - br) * b), hg = (uint8_t)(bg + (255 - bg) * b), hb = (uint8_t)(bb + (255 - bb) * b);
    float H = 480, y1 = H / 3;
    GfxVertex v[12] = {
        { 0, 0, 0, u0, 0, 255, 255, 255, 255 }, { 0, y1, 0, u0, 1.0f / 3, hr, hg, hb, 255 }, { 640, y1, 0, u1, 1.0f / 3, hr, hg, hb, 255 },
        { 0, 0, 0, u0, 0, 255, 255, 255, 255 }, { 640, y1, 0, u1, 1.0f / 3, hr, hg, hb, 255 }, { 640, 0, 0, u1, 0, 255, 255, 255, 255 },
        { 0, y1, 0, u0, 1.0f / 3, hr, hg, hb, 255 }, { 0, H, 0, u0, 1, 255, 255, 255, 255 }, { 640, H, 0, u1, 1, 255, 255, 255, 255 },
        { 0, y1, 0, u0, 1.0f / 3, hr, hg, hb, 255 }, { 640, H, 0, u1, 1, 255, 255, 255, 255 }, { 640, y1, 0, u1, 1.0f / 3, hr, hg, hb, 255 } };
    /* stretch over the whole window (the original's screen is the 640x480 viewport) */
    int w, h;
    scr_frame_size(&w, &h);
    for (int i = 0; i < 12; i++) { v[i].x = v[i].x * w / 640; v[i].y = v[i].y * h / 480; }
    Mat4 p = m4_ortho(0, (float)w, (float)h, 0, -1, 1), id = m4_identity();
    gfx_set_matrices(&p, &id);
    GfxState st = { BLEND_NONE, 0, 0, 0, 0, 0 };
    gfx_draw_tris(v, 12, R->sky_tex, &st);
    fog_sky(v, 12, a);
}

/* the software modes' sky (gfx_sky.cpp): render_game 0x4250b1 blits it before the scene (sky_blit 0x41e850), a
 * plain copy into the top H - H/3 rows (sky_init_moving_sky 0x41e7e0), the image's rows from 0 (no pitch), at the
 * x offset of gfx_sky_set_heading 0x41eac0, which handle_game_cameras 0x4147d3 sets every frame from the active
 * camera: c = cos(atan2(dz, dx) - pi/2) of its view (= dz / |d|, in whole angle units), x = ftol(-(c + 1) * (W / 2)),
 * wrapped into 0..W-1 (W = image width); past the image's right edge the copy is split in two (sky_build_rects
 * 0x41e9d0). So looking east or west shows the same part [quirk]. (handle_player_car 0x4261dd also sets it from the
 * car's heading, but the camera's call comes later in the frame.) */
static void draw_sky_sw(Vec3 view, int w, int h)
{
    if (!g_sky_sw || g_sky_w <= 0) return;
    float ang = atan2f(view.z, view.x) - PI_F / 2;
    float c = race_cos(ftol(ang * ANGLE_UNITS / TWO_PI));
    int W = g_sky_w, x = ftol((c + 1) * (float)-(W / 2));
    if (x < 0) x += W;
    if (x >= W) x -= W;
    int sh = h - h / 3, sw = w, hh = sh < g_sky_h ? sh : g_sky_h;
    Mat4 p = m4_ortho(0, (float)w, (float)h, 0, -1, 1), id = m4_identity();
    gfx_set_matrices(&p, &id);
    GfxState st = { BLEND_NONE, 0, 0, 0, 0, 0 };
    float v1 = (float)hh / g_sky_h;
    int part = W - x < sw ? W - x : sw;          /* the first copy, then the rest from the image's left edge */
    for (int k = 0; k < 2; k++) {
        int dx0 = k ? part : 0, dx1 = k ? sw : part, sx0 = k ? 0 : x;
        if (dx1 <= dx0) continue;
        float u0 = (float)sx0 / W, u1 = (float)(sx0 + dx1 - dx0) / W;
        GfxVertex q[6] = {
            { (float)dx0, 0, 0, u0, 0, 255, 255, 255, 255 }, { (float)dx0, (float)hh, 0, u0, v1, 255, 255, 255, 255 },
            { (float)dx1, (float)hh, 0, u1, v1, 255, 255, 255, 255 }, { (float)dx0, 0, 0, u0, 0, 255, 255, 255, 255 },
            { (float)dx1, (float)hh, 0, u1, v1, 255, 255, 255, 255 }, { (float)dx1, 0, 0, u1, 0, 255, 255, 255, 255 } };
        gfx_draw_tris(q, 6, g_sky_sw, &st);
    }
}

static void fmt_time(char *b, size_t n, int ms)
{
    if (ms >= 0x0fffffff) snprintf(b, n, "--:--:-");                  /* no record */
    else snprintf(b, n, "%02d:%02d:%d", ms / 60000, ms / 1000 % 60, ms / 100 % 10);
}

/* draw_dashboard: the panels of docs/HUD.md 4 in the mode's layout (hud.h); then the rules' part (messages,
 * checkpoint box, results, game over, pause) */
static void draw_hud(void)
{
    char b[64], pos[8];
    scr_2d_begin();
    /* handle_game 0x424c22: draw_2d_display (the whole dashboard, the pause / quit dialog too) only without
     * disable_overlay; the results and the game-over texts are game actions and stay */
    if (rules_hud_on() && !R->event_running && !R->overlay_off) {
        int D = R->detail;
        char race[16];
        fmt_time(race, sizeof race, rules_race_ms());
        snprintf(pos, sizeof pos, "%d/4", ai_player_position() + 1);     /* POSITIE: [0x4d9e60] + 1 */
        if (D >= 3) {
            if (!R->track.city) {                                         /* 4.1 highway info panel */
                hud_item(57, 0);
                hud_item_text(38, 10, 0, hud_text(33));
                hud_item_text(39, 11, 0, race);
                hud_item_text(58, 10, 0, hud_text(52));
                fmt_time(b, sizeof b, rules_race_record());
                hud_item_text(59, 11, 0, b);
                hud_item_text(60, 10, 0, hud_text(38));
                hud_item_text(61, 11, 0, pos);
            } else {                                                      /* 4.2 city info panel */
                hud_item(51, 0);
                hud_item_text(38, 10, 0, hud_text(33));
                hud_item_text(39, 11, 0, race);
                hud_item_text(40, 10, 0, hud_text(34));
                hud_item_text(41, 13, 0, hud_text(35));
                fmt_time(b, sizeof b, rules_lap_ms());
                hud_item_text(42, 12, 0, b);
                hud_item_text(43, 13, 0, hud_text(36));
                fmt_time(b, sizeof b, rules_lap_record());
                hud_item_text(44, 12, 0, b);
                hud_item_text(45, 13, 0, hud_text(37));
                fmt_time(b, sizeof b, rules_last_lap());
                hud_item_text(46, 12, 0, b);
                hud_item_text(47, 10, 0, hud_text(38));
                hud_item_text(48, 11, 0, pos);
                hud_item_text(49, 10, 0, hud_text(39));
                int n = rules_nrlaps() < 9 ? rules_nrlaps() : 9, l = rules_lap() + 1;
                snprintf(b, sizeof b, "%d/%d", l < n ? l : n, n);
                hud_item_text(50, 11, 0, b);
            }
        }
        if (D >= 2) {                                                     /* 4.3 speed panel */
            hud_item(52, 0);
            hud_item_text(53, 10, 0, hud_text(53));
            int n = 11, f = ftol(R->turbo * 0.990099f) * n / 100;
            hud_item(54, f < 0 ? 0 : f > n - 1 ? n - 1 : f);
            hud_item_text(55, 10, 0, hud_text(54));
            snprintf(b, sizeof b, "%03d", abs(R->speed_kmh));
            hud_item_text(56, 0, 0, b);
            hud_item_text(7, 10, 0, hud_text(55));
            int dn = hud_item_frames(3), df = (int)(R->damage / (100.0f / (dn - 1)));   /* 0x41aabc: 10 / 11 frames */
            if (R->damage < 80 || !((int)(R->tick * 1000 / RACE_HZ / 200) & 1)) hud_item(3, df < 0 ? 0 : df > dn - 1 ? dn - 1 : df);
        }
    }
    rules_draw_hud(R);
    gfx_2d_end();
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
    while (d > ANGLE_UNITS / 2) d -= ANGLE_UNITS;
    while (d < -ANGLE_UNITS / 2) d += ANGLE_UNITS;
    return a + d * t;
}

static void race_draw(float alpha)
{
    int w, h;
    Vec3 eye = v3_lerp(R->cam_draw0, R->cam_draw1, alpha), tgt = v3_lerp(R->tgt_draw0, R->tgt_draw1, alpha);
    /* the 8-bit modes: into their 320x200 / 640x480 target (screen.h); the colour is cleared there by the sky blit
     * alone (render_game 0x4250b1, Z only by d3dlib_clear), here to black first */
    scr_frame_begin(scr_hw3d() ? 0.55f : 0, scr_hw3d() ? 0.7f : 0, scr_hw3d() ? 0.9f : 0, &w, &h);
    if (scr_hw3d()) draw_sky();
    else {
        draw_sky_sw(v3_sub(tgt, eye), w, h);
        fog_states();                                   /* TEXTUREMAPBLEND COPY (set_game_render_states 0x423e94) */
    }
    /* 81 degrees across the screen (RENDER.md 2): the 8-bit modes' viewport has dvClipY = h / w (320x200: 16:10);
     * PORT: the 16-bit mode keeps the 4:3 height and widens with the window */
    float fovy = 2 * atanf(tanf(40.5f * PI_F / 180) * (scr_8bit() ? (float)h / w : 0.75f));
    Mat4 proj = m4_perspective(fovy, (float)w / h, 10, 12500), view = view_lh(eye, tgt);
    gfx_set_matrices(&proj, &view);
    int vis = ai_visible_chunks();                      /* g_visible_chunks 0x4d9a8c (render_game 0x42518b) */
    int start = track_visible_start(&R->track, R->cam_chunk, v3_sub(tgt, eye), 2, vis);
    track_draw(&R->track, start, vis, 0);
    objects_draw(&R->objects, &R->track, start, vis);
    paths_draw();
    for (int o = 0; o < R->nobj; o++) {
        const Obj *c = &R->obj[o];
        if (!c->used || !c->visible || !c->active) continue;      /* a car that is off is not drawn */
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
        /* the visual body pitch and roll (ANGLE_UNITS) */
        Mat4 body = m4_mul(m4_rot_x(-c->pitch * TWO_PI / ANGLE_UNITS), m4_rot_z(c->roll * TWO_PI / ANGLE_UNITS));
        m = m4_mul(m, body);
        shadow_draw(R, o, &m);                          /* the shadow callback, just before the body (shadow.c) */
        lights_attach(o, &m);
        model_draw_matrix(anim_model(c->anim, c->frame), &m);
        wheels_draw(R, o, &m, alpha);
    }
    gfx_scene_ao(&proj, 10, 12500);                     /* PORT EXTRA: graphics.ini ao, on the opaque picture */
    lights_draw();                                      /* render_game: object lights after the objects (lights.c) */
    gfx_set_model(NULL);
    {   /* the effects' billboards copy the camera's orientation */
        Vec3 f = v3_norm(v3_sub(tgt, eye)), rt = v3_norm(v3_cross(v3(0, 1, 0), f)), up = v3_cross(f, rt);
        fx_draw(eye, rt, up);
    }
    rules_draw_3d(R);                                   /* after the effects as in render_game: flag, 3D letters */
    gfx_set_model(NULL);
    radar_draw(R, v3_sub(tgt, eye));                    /* 0x4251ed (in the 3D scene there, on top) */
    draw_hud();
    scr_frame_end();                                    /* PORT: an 8-bit mode's target scaled up into the window */
    touch_draw();                                       /* PORT: the touch controls on top */
}

int race_frame(Race *r, double now)
{
    R = r;
    if (r->last_time == 0) r->last_time = now;
    double el = now - r->last_time;
    r->last_time = now;
    if (el > 0.25) el = 0.25;
    ai_frame_rate_governor(el, r->game_event == 0);    /* handle_game: below 10 fps visiblechunks-- */
    game_keys();
    if (rules_paused()) el = 0;                         /* the pause dialog stops the game */
    r->accum += el;
    while (r->accum >= 1.0 / RACE_HZ && !rules_done()) {
        r->accum -= 1.0 / RACE_HZ;
        race_tick();
    }
    race_draw((float)(r->accum * RACE_HZ));
    return !rules_done();
}
