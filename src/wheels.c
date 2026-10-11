/* wheels.c - the wheels of the player's and the racers' cars (car_whl.cpp; docs/RENDER.md "Car wheels").
 *
 * The original: car_add_wheels 0x4136c0 (from init_player_car and init_racer_cars) adds four 'WIEL_ANIM' sub-objects
 * (sub_441630) at the offsets of the type's row of the table 0x486198 (7 types x 4 wheels x float x, y, z; front
 * left, front right, rear left, rear right), rotation 0. Every game frame car_update_wheels 0x413900 (player) or
 * car_update_wheels_ai 0x413be0 (racers) turns them: pitch -= the rolled distance, front yaw = the steering (player)
 * or the yaw rate (racers), y = 3.5 + the road height under the car minus the body's height (clamped to +-1.05), so
 * the wheels stay on the road while the body bobs. obj_draw_with_damage 0x440270 draws them with
 * child matrix * parent matrix, i.e. they lean and pitch with the body.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "wheels.h"
#include "ai.h"
#include "exetab.h"
#include "model.h"
#include <math.h>
#include <string.h>

#define WHEEL_TYPES 7
#define WHEEL_TABLE 0x486198u           /* car_whl.cpp: float[7][4][3] */

static float g_pos[WHEEL_TYPES][4][3];
static int g_loaded, g_anim = -1;

typedef struct {
    int on;                             /* set up (car_add_wheels) */
    float pitch0, pitch1;               /* spin of all four wheels at the last two ticks (ANGLE_UNITS) */
    float yaw;                          /* the front wheels' steering angle (ANGLE_UNITS) */
    float y;                            /* height of the wheel centres in the body's space */
} Wheels;
static Wheels W[MAX_OBJS];

void wheels_init(void)
{
    memset(W, 0, sizeof W);
    if (g_loaded) return;
    g_loaded = 1;
    PeFile pe;
    if (!pe_open(&pe, "spel.dat")) return;
    const uint8_t *p = pe_ptr(&pe, WHEEL_TABLE, sizeof g_pos);
    if (p) memcpy(g_pos, p, sizeof g_pos);
    pe_close(&pe);
    if (p) g_anim = anim_find("WIEL_ANIM");
}

static int has_wheels(const Race *r, int o)
{
    const Obj *c = &r->obj[o];
    return g_anim >= 0 && c->used && c->car_type >= 0 && c->car_type < WHEEL_TYPES;
}

static Wheels *wheels_of(const Race *r, int o)
{
    Wheels *w = &W[o];
    if (!w->on) {                       /* car_add_wheels 0x4136c0: the table's height, no turn */
        memset(w, 0, sizeof *w);
        w->on = 1;
        w->y = g_pos[r->obj[o].car_type][0][1];
    }
    return w;
}

/* car_update_wheels 0x413900 / car_update_wheels_ai 0x413be0 */
static void wheels_update(const Race *r, int o, Wheels *w)
{
    const Obj *c = &r->obj[o];
    float spin = r->dt * (ANGLE_UNITS * 10 / (3.14159f * 7)) * c->vel.z;     /* a wheel of 0.7 m: one turn per 2.2 m */
    float lim = ANGLE_UNITS * 0.497213f;                                    /* under half a turn per frame */
    if (spin > lim) spin = lim;                                             /* (only forwards) */
    w->pitch0 = w->pitch1;
    w->pitch1 -= spin;
    if (o == r->plr_obj)
        w->yaw = ANGLE_UNITS * 0.0833333f * -0.01f * r->steer;              /* +-100 -> -+30 degrees */
    else {
        float a = c->yawrate * 400;                                         /* rad/s; raw units: +-44 degrees */
        w->yaw = a > 500 ? 500 : a < -500 ? -500 : a;
    }
    float d = chunk_height_at(&r->track.chunks[c->chunk], c->pos.x, c->pos.z) - c->pos.y;
    if (d > 1.05f) d = 1.05f;
    if (d < -1.05f) d = -1.05f;
    w->y = d + 3.5f;
    /* PORT: keep the angle small (the original lets the float grow) */
    float k = floorf(w->pitch1 / ANGLE_UNITS) * ANGLE_UNITS;
    w->pitch0 -= k;
    w->pitch1 -= k;
}

void wheels_tick(const Race *r)
{
    for (int o = 0; o < r->nobj; o++) {
        if (!has_wheels(r, o)) continue;
        Wheels *w = wheels_of(r, o);
        const Obj *c = &r->obj[o];
        w->pitch0 = w->pitch1;
        /* the player's handler turns them once the race runs (countdown: not), a racer's handler whenever it runs */
        if (o == r->plr_obj ? r->race_started : (c->active && c->update && ai_obj_in_window(o)))
            wheels_update(r, o, w);
    }
}

void wheels_draw(const Race *r, int o, const Mat4 *body, float alpha)
{
    if (!has_wheels(r, o)) return;
    const Model *m = anim_model(g_anim, 0);
    if (!m) return;
    Wheels *w = wheels_of(r, o);
    const float u2r = 6.28319f / ANGLE_UNITS;
    /* the original's Rx(p) / Ry(y) (inst_build_matrix 0x459360, row vectors) are m4_rot_x(-p) / m4_rot_y(-y) here */
    Mat4 spin = m4_rot_x(-(w->pitch0 + (w->pitch1 - w->pitch0) * alpha) * u2r);
    Mat4 steer = m4_mul(m4_rot_y(-w->yaw * u2r), spin);
    const float (*p)[3] = g_pos[r->obj[o].car_type];
    for (int i = 0; i < 4; i++) {
        Mat4 local = m4_mul(m4_translate(v3(p[i][0], w->y, p[i][2])), i < 2 ? steer : spin);
        Mat4 world = m4_mul(*body, local);
        model_draw_matrix(m, &world);
    }
}
