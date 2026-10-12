/* particles.c - the particle module (prt_main.cpp; docs/RENDER.md "Particles / rain").
 *
 * prt_init 0x431f20 (init_game, only with Game.ini particles) allocates one pool of 4000 particles for all emitters
 * (prt_alloc_pool 0x431fb0, 0x54 bytes each, a free list at 0x4e057c), constructs the emitters (prt_emitter_init
 * 0x432130 -> prt_emitter_ctor 0x432000; the array 0x4e0460..0x4e057c holds one of 0x11c bytes) and makes the one a
 * rain emitter (prt_rain_setup 0x4328b0, active = part_rain). prt_frame 0x431f60, from render_game between the clouds
 * and the finish flag, does per active emitter: prt_emitter_update 0x432520 (every particle moved, or freed when its
 * life is 0, then 5..15 new ones), the emitter's update callback (rain: prt_rain_follow_cam 0x432990, the emitter's
 * matrix = identity with the camera's position + (0, 20, 0)) and its draw callback (prt_draw 0x4326e0: each particle a
 * line from its position to last frame's position, LINELIST, Z buffer off). Particle positions are in the emitter's
 * space, so the whole spray moves with the camera.
 *
 * PORT: the update runs once per game tick (one of the original's frames), the drawing at the display's rate around
 * the camera as drawn; the 1-pixel lines are thin camera-facing quads of one of the original's pixels.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "particles.h"
#include "fog.h"
#include "gfx.h"
#include "ini.h"
#include "res.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define POOL 4000                       /* prt_alloc_pool 0x431fb0: malloc(0x52080) = 4000 x 0x54 */

typedef struct {                        /* 0x54 bytes; +0 / +4 the list links */
    Vec3 pos, old;                      /* +0x08 / +0x14: now and last frame, in the emitter's space */
    Vec3 vel;                           /* +0x20: per frame */
    int life;                           /* +0x2c: frames left */
    float col[3], old_col[3], dcol[3];  /* +0x30 / +0x3c / +0x48: RGB 0..1, per frame step */
} Particle;

typedef struct {                        /* 0x11c bytes (+4 the name "Emitter", +0xd4 the matrix) */
    float yaw0, yaw_r;                  /* +0x60 / +0x64: direction = base + rnd * range, radians */
    float pitch0, pitch_r;              /* +0x68 / +0x6c */
    float speed0, speed_r;              /* +0x70 / +0x74: units per frame */
    int max, count;                     /* +0x7c / +0x80 */
    int new0, new_r;                    /* +0x84 / +0x88: new particles per frame */
    int life0, life_r;                  /* +0x8c / +0x90: frames */
    float c0[3], c0_r[3];               /* +0x94 / +0xa0: the start colour */
    float c1[3], c1_r[3];               /* +0xac / +0xb8: the end colour */
    Vec3 accel;                         /* +0xc4: added to the velocity every frame */
    int active;                         /* +0xd0 */
} Emitter;

static int g_on;                        /* g_opt_particles 0x4d9af4 */
static Emitter E;                       /* 0x4e0460 */
static Particle P[POOL];                /* the emitter's list (+0x78); order does not matter (opaque, no Z) */
static int g_n;

static int ftol(float f) { return (int)f; }   /* MSVC _ftol (0x4700e0): truncation toward zero */

/* (rand() - 0x3fff) * [0x47f920] (1/16383): -1 .. 1; the original's rand() is the MSVC CRT one (0..32767) */
static float rnd(void) { return (float)((rand() & 0x7fff) - 0x3fff) * 6.10388815e-05f; }

static void rain_setup(Emitter *e, int active)                      /* prt_rain_setup 0x4328b0 */
{
    memset(e, 0, sizeof *e);
    e->yaw0 = 0; e->yaw_r = 6.28318548f;                            /* every direction */
    e->pitch0 = 0; e->pitch_r = 0;                                  /* horizontal */
    e->speed0 = 2.0f; e->speed_r = 0.05f;
    e->max = 200;
    e->new0 = 10; e->new_r = 5;
    e->life0 = 50; e->life_r = 15;
    for (int i = 0; i < 3; i++) {                                   /* grey 0.5 +- 0.1, each channel on its own */
        e->c0[i] = e->c1[i] = 0.5f;
        e->c0_r[i] = e->c1_r[i] = 0.1f;
    }
    e->accel = v3(0, -0.1f, 0);
    e->active = active;                                             /* g_opt_part_rain 0x4d9af8 */
}

void prt_init(void)
{
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    /* gm_ini_load_gfx_options 0x421fd0 (GetPrivateProfileInt, default 0); here the CD's values when a key is missing */
    g_on = ini_int(g, "GFX OPTIONS", "particles", 0);
    int rain = ini_int(g, "GFX OPTIONS", "part_rain", 1);
    ini_free(g);
    memset(&E, 0, sizeof E);
    g_n = 0;
    if (!g_on) return;
    /* prt_alloc_pool also calls srand(time(NULL)), which reseeds every later rand() of the race (the fog coin, the
     * traffic); PORT: not done, the port seeds nothing */
    rain_setup(&E, rain);
}

void prt_free(void)
{
    memset(&E, 0, sizeof E);
    g_n = 0;
    g_on = 0;
}

static void spawn(Emitter *e)                                       /* prt_spawn 0x432150 */
{
    if (g_n >= POOL || e->count >= e->max) return;                  /* before any rand() */
    Particle *p = &P[g_n++];
    p->pos = p->old = v3(0, 0, 0);
    float yaw = rnd() * e->yaw_r + e->yaw0;
    float pitch = rnd() * e->pitch_r + e->pitch0, cp = cosf(pitch);
    p->vel = v3(-sinf(yaw) * cp, sinf(pitch), cosf(yaw) * cp);
    p->vel = v3_scale(p->vel, rnd() * e->speed_r + e->speed0);
    float c0[3], c1[3];
    for (int i = 0; i < 3; i++) c0[i] = rnd() * e->c0_r[i] + e->c0[i];
    for (int i = 0; i < 3; i++) c1[i] = rnd() * e->c1_r[i] + e->c1[i];
    p->life = ftol(rnd() * (float)e->life_r) + e->life0;
    for (int i = 0; i < 3; i++) {
        p->col[i] = p->old_col[i] = c0[i];                          /* (old_col: left as it was in the original) */
        p->dcol[i] = p->life > 0 ? (c1[i] - c0[i]) / (float)p->life : 0;
    }
    e->count++;
}

void prt_tick(void)
{
    if (!g_on || !E.active) return;                                 /* prt_frame 0x431f60: emitter +0xd0 */
    for (int i = 0; i < g_n;) {
        Particle *p = &P[i];
        if (p->life > 0) {                                          /* prt_particle_update 0x432410 */
            p->old = p->pos;
            p->pos = v3_add(p->pos, p->vel);
            p->vel = v3_add(p->vel, E.accel);
            for (int k = 0; k < 3; k++) {
                p->old_col[k] = p->col[k];
                p->col[k] += p->dcol[k];
            }
            p->life--;
            i++;
        } else {                                                    /* life 0: back to the free list */
            P[i] = P[--g_n];
            E.count--;
        }
    }
    int k = ftol(rnd() * (float)E.new_r) + E.new0;                  /* 0x432544: 5 .. 15 */
    while (k-- > 0) spawn(&E);
}

static uint8_t c255(float c)                                        /* prt_build_lines 0x4325b0: ftol(c * 255) */
{
    int v = ftol(c * 255.0f);
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

void prt_draw(Vec3 eye, Vec3 fwd, float unit_px)
{
    if (!g_on || !E.active || !g_n) return;
    static GfxVertex buf[POOL * 6];
    /* prt_rain_follow_cam 0x432990: the emitter's matrix is the identity with the active camera's position (+8 of the
     * camera, g_cameras 0x4e66a0) plus (0, 20, 0) */
    Vec3 org = v3(eye.x, eye.y + 20.0f, eye.z);
    int nv = 0;
    for (int i = 0; i < g_n; i++) {
        const Particle *p = &P[i];
        /* two D3DLVERTEX: the position with its colour, last frame's position with last frame's colour; alpha and
         * specular 0, tu / tv not written (texture 0 bound: see RENDER.md) */
        Vec3 a = v3_add(org, p->pos), b = v3_add(org, p->old), d = v3_sub(a, b);
        if (v3_dot(d, d) < 1e-8f) continue;                         /* a new particle: both ends at the emitter */
        Vec3 s = v3_cross(d, v3_sub(a, eye));
        float l = v3_len(s);
        if (l < 1e-6f) continue;                                    /* along the line of sight */
        s = v3_scale(s, 0.5f / l);
        float wa = fabsf(v3_dot(v3_sub(a, eye), fwd)) * unit_px, wb = fabsf(v3_dot(v3_sub(b, eye), fwd)) * unit_px;
        Vec3 q[4] = { v3_sub(a, v3_scale(s, wa)), v3_add(a, v3_scale(s, wa)), v3_add(b, v3_scale(s, wb)),
                      v3_sub(b, v3_scale(s, wb)) };
        uint8_t ca[3] = { c255(p->col[0]), c255(p->col[1]), c255(p->col[2]) };
        uint8_t cb[3] = { c255(p->old_col[0]), c255(p->old_col[1]), c255(p->old_col[2]) };
        static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
        for (int t = 0; t < 6; t++) {
            const uint8_t *c = TRI[t] < 2 ? ca : cb;
            GfxVertex v = { q[TRI[t]].x, q[TRI[t]].y, q[TRI[t]].z, 0, 0, c[0], c[1], c[2], 255 };
            buf[nv++] = v;
        }
    }
    if (!nv) return;
    /* prt_draw 0x4326e0: no blending (ALPHABLENDENABLE was reset by the clouds), ZENABLE 0 around the
     * DrawPrimitive(LINELIST, LVERTEX) and 1 after: drawn over everything, writing no depth */
    GfxState st = { BLEND_NONE, 0, 0, 0, 0, 0 };
    gfx_set_model(NULL);
    gfx_set_decal(0);           /* PORT: the vertex colour also under TEXTUREMAPBLEND COPY (fog = 1, the 8-bit modes) */
    gfx_draw_tris(buf, nv, gfx_white(), &st);
    fog_states();               /* the device states of the frame back (COPY) */
}
