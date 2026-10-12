/* effects.c - see effects.h. Lifetimes are counted in frames (the original converts seconds with the frame time at
 * creation); one game tick is one of the original's frames.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "effects.h"
#include "gfx.h"
#include "screen.h"
#include "texture.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- skid marks (gfx_skid.cpp): 20 marks, each two strips (left / right rear wheel) of up to 100 vertices ----- */
#define SKIDS 20
#define SKID_VERTS 100
typedef struct {
    int active, n;              /* vertices per strip */
    int age, life;              /* frames */
    float alpha;
    Vec3 v[2][SKID_VERTS];
} Skid;

typedef struct {
    int active, age, life;
    Vec3 pos, vel;
    float size, alpha;
} Puff;

typedef struct {
    int active, age, life;
    Vec3 pos, vel;
    float amp, y0;
} Spark;

static Skid g_skid[SKIDS];
static int g_cur_skid = -1, g_prev_slip;
static Puff g_puff[70];          /* 0x4d3a18 */
static Spark g_spark[100];       /* 0x4cc088 */
static double g_smoke_clock;
static unsigned g_tex;
static int g_on_skid = 1, g_fade = 1, g_on_smoke = 1;

static float frnd(void) { return (float)rand() / (float)RAND_MAX; }

void fx_init(int skidmarks, int fade, int smoke, int alpha_blending)
{
    const Texture *t = tex_get("s_sfx");
    g_tex = t ? t->id : 0;
    g_on_skid = skidmarks && alpha_blending;
    g_fade = fade;
    g_on_smoke = smoke && alpha_blending;
    fx_reset();
}

void fx_free(void) { fx_reset(); }

void fx_reset(void)
{
    memset(g_skid, 0, sizeof g_skid);
    memset(g_puff, 0, sizeof g_puff);
    memset(g_spark, 0, sizeof g_spark);
    g_cur_skid = -1;
    g_prev_slip = 0;
    g_smoke_clock = 0;
}

static Vec3 car_point(const FxCar *c, float x, float y, float z)
{
    return v3_add(c->pos, v3_add(v3_scale(c->right, x), v3_add(v3_scale(c->up, y), v3_scale(c->fwd, z))));
}

void fx_skid(const FxCar *c, int slip, float dt)
{
    if (!g_on_skid) return;
    if (slip >= 4 && g_prev_slip < 4) {                       /* a new mark: a free record, else the oldest */
        int best = 0;
        for (int i = 0; i < SKIDS; i++) {
            if (!g_skid[i].active) { best = i; break; }
            if (g_skid[i].age > g_skid[best].age) best = i;
        }
        Skid *s = &g_skid[best];
        memset(s, 0, sizeof *s);
        s->active = 1;
        s->life = (int)(20.0f / dt);                         /* 20 s */
        s->alpha = (float)(200 - 16 * (slip - 4));
        if (s->alpha < 0) s->alpha = 0;
        if (s->alpha > 255) s->alpha = 255;
        g_cur_skid = best;
    }
    if (slip < 4) g_cur_skid = -1;
    g_prev_slip = slip;
    if (g_cur_skid < 0) return;
    Skid *s = &g_skid[g_cur_skid];
    if (s->n + 2 > SKID_VERTS) { g_cur_skid = -1; return; }
    /* 0x41d880: the rear wheels at local (+-7, 1, -10*h), the mark 4 wide across the car's direction of travel */
    float vx = c->vel.x, vz = c->vel.z, l = sqrtf(vx * vx + vz * vz);
    float px = l > 1e-4f ? -vz / l : 1, pz = l > 1e-4f ? vx / l : 0;
    float z = -10 * c->cg_rear;
    for (int w = 0; w < 2; w++) {
        float x = w ? 7.0f : -7.0f;
        s->v[w][s->n] = car_point(c, x - 2 * px, 1.0f, z - 2 * pz);
        s->v[w][s->n + 1] = car_point(c, x + 2 * px, 1.0f, z + 2 * pz);
    }
    s->n += 2;
}

static void puff_spawn(Vec3 pos, Vec3 vel, float dt)          /* 0x41fd80 */
{
    for (int i = 0; i < 70; i++)
        if (!g_puff[i].active) {
            Puff *p = &g_puff[i];
            p->active = 1;
            p->age = 0;
            p->life = (int)(4.0f / dt);
            if (p->life < 1) p->life = 1;
            p->pos = pos;
            float k = 0.75f + frnd() * 0.25f;
            p->vel = v3(vel.x * k, (float)(rand() % 35 + 9) / p->life, vel.z * k);
            p->size = 7;
            p->alpha = 0x50;
            return;
        }
}

void fx_smoke(const FxCar *c, int slip, float dt)
{
    if (!g_on_smoke || slip < 4) return;
    g_smoke_clock += dt;
    if (g_smoke_clock < 0.1) return;                          /* at most every 100 ms */
    g_smoke_clock = 0;
    /* 0.6 x the car's velocity, in world units per frame */
    Vec3 v = v3_scale(v3_add(v3_scale(c->right, c->vel.x), v3_scale(c->fwd, c->vel.z)), 0.6f * 10 * dt);
    float z = -10 * c->cg_rear;
    /* tyre_smoke_emitter 0x425a90 calls cloud_emit 0x41fff0 once per rear wheel, and that spawns 2 puffs */
    for (int w = 0; w < 2; w++) {
        Vec3 p = car_point(c, w ? -9.0f : 9.0f, 3, z);
        puff_spawn(p, v, dt);
        puff_spawn(p, v, dt);
    }
}

void fx_cloud(Vec3 pos, Vec3 vel, float dt)                   /* cloud_emit 0x41fff0: two puffs */
{
    if (!g_on_smoke) return;
    puff_spawn(pos, vel, dt);
    puff_spawn(pos, vel, dt);
}

void fx_sparks(Vec3 pos, Vec3 dir, float speed, float dt)
{
    int n = (int)(speed / 10 + 5);
    if (n > 25) n = 25;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < 100; i++)
            if (!g_spark[i].active) {
                Spark *s = &g_spark[i];
                s->active = 1;
                s->age = 0;
                s->life = (int)(2.5f / dt);
                if (s->life < 1) s->life = 1;
                s->pos = v3(pos.x + (frnd() - 0.5f) * 5, pos.y + (frnd() - 0.5f) * 5, pos.z + (frnd() - 0.5f) * 5);
                float m = (frnd() + 0.5f) * speed * 3 / s->life;
                s->vel = v3(dir.x * m, 0, dir.z * m);
                float a = ((rand() % 50) + speed - 25) * 0.02f + 1.5f;
                s->amp = a < 1.5f ? 1.5f : a > 2.3f ? 2.3f : a;
                s->y0 = s->pos.y;
                break;
            }
}

void fx_tick(float dt)
{
    (void)dt;
    for (int i = 0; i < SKIDS; i++) {
        Skid *s = &g_skid[i];
        if (!s->active) continue;
        s->age++;
        if (g_fade) {
            int left = s->life - s->age;
            float step = left > 0 ? (255 - s->alpha) / left : 255;
            s->alpha += step < 1 ? 1 : step;
            if (s->alpha > 255) s->alpha = 255;
        }
        if (s->age >= s->life) { s->active = 0; if (g_cur_skid == i) g_cur_skid = -1; }
    }
    for (int i = 0; i < 70; i++) {                            /* 0x41fae0 */
        Puff *p = &g_puff[i];
        if (!p->active) continue;
        p->vel = v3_scale(p->vel, 0.9f);
        p->pos = v3_add(p->pos, p->vel);
        p->size += 29.0f / p->life;
        p->alpha -= 80.0f / p->life;
        if (++p->age >= p->life || p->alpha <= 0) p->active = 0;
    }
    for (int i = 0; i < 100; i++) {                           /* 0x41eb60 */
        Spark *s = &g_spark[i];
        if (!s->active) continue;
        s->pos = v3_add(s->pos, s->vel);
        s->pos.y = s->y0 + cosf(((2048.0f / s->life * s->age + 300) / 1024.0f) * 6.28319f) * s->amp;
        if (++s->age >= s->life) s->active = 0;
    }
}

static GfxVertex vtx(Vec3 p, float u, float v, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    GfxVertex o = { p.x, p.y, p.z, u, v, r, g, b, a };
    return o;
}

void fx_draw(Vec3 cam_pos, Vec3 cam_right, Vec3 cam_up)
{
    static GfxVertex buf[SKID_VERTS * 3 * 2 * SKIDS];
    gfx_set_model(NULL);
    /* skid marks: colour 0x404040, alpha 255 = invisible, uv u 0.453125 / 0.5390625, v 0.15625 */
    int nv = 0;
    for (int i = 0; i < SKIDS; i++) {
        const Skid *s = &g_skid[i];
        if (!s->active || s->n < 4) continue;
        uint8_t a = (uint8_t)s->alpha;
        for (int w = 0; w < 2; w++)
            for (int k = 0; k + 3 < s->n; k += 2) {
                Vec3 p0 = s->v[w][k], p1 = s->v[w][k + 1], p2 = s->v[w][k + 2], p3 = s->v[w][k + 3];
                buf[nv++] = vtx(p0, 0.453125f, 0.15625f, 64, 64, 64, a);
                buf[nv++] = vtx(p1, 0.5390625f, 0.15625f, 64, 64, 64, a);
                buf[nv++] = vtx(p2, 0.453125f, 0.15625f, 64, 64, 64, a);
                buf[nv++] = vtx(p1, 0.5390625f, 0.15625f, 64, 64, 64, a);
                buf[nv++] = vtx(p3, 0.5390625f, 0.15625f, 64, 64, 64, a);
                buf[nv++] = vtx(p2, 0.453125f, 0.15625f, 64, 64, 64, a);
            }
    }
    /* skid_draw 0x41e430: the blend states only when g_8bit is 0; in the 8-bit modes (COPY) the strips are opaque
     * texture with Z writes (the device defaults) */
    GfxState st = { BLEND_INVALPHA, 0, 1, 0, 0, 0 };
    if (scr_8bit()) { st.blend = BLEND_NONE; st.depth_write = 1; }
    if (nv) gfx_draw_tris(buf, nv, g_tex, &st);

    /* sparks: opaque, colour key, a small triangle facing the camera */
    nv = 0;
    for (int i = 0; i < 100; i++) {
        const Spark *s = &g_spark[i];
        if (!s->active) continue;
        Vec3 a = v3_add(s->pos, v3_scale(cam_up, 0.45f)), b = v3_sub(s->pos, v3_scale(cam_right, 0.45f));
        Vec3 c = v3_add(s->pos, v3_scale(cam_right, 0.45f));
        buf[nv++] = vtx(b, 0.807f, 0.185f, 255, 255, 255, 255);
        buf[nv++] = vtx(a, 0.807f, 0.0f, 255, 255, 255, 255);
        buf[nv++] = vtx(c, 0.992f, 0.0f, 255, 255, 255, 255);
    }
    GfxState ss = { BLEND_NONE, 1, 1, 1, 0, 0 };
    if (nv) gfx_draw_tris(buf, nv, g_tex, &ss);

    /* smoke: additive billboards, far to near, a fan of a bright centre and 4 transparent corners */
    int order[70], n = 0;
    for (int i = 0; i < 70; i++) if (g_puff[i].active) order[n++] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            float da = v3_len(v3_sub(g_puff[order[j - 1]].pos, cam_pos)), db = v3_len(v3_sub(g_puff[order[j]].pos, cam_pos));
            if (da >= db) break;
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    nv = 0;
    for (int k = 0; k < n; k++) {
        const Puff *p = &g_puff[order[k]];
        static const int PING[5] = { 0, 1, 2, 1, 0 };
        int f = PING[(p->age * 5 / (p->life > 0 ? p->life : 1)) % 5];
        float v0 = f / 3.0f, v1 = (f + 1) / 3.0f, u0 = 0, u1 = 0.3954f, h = p->size * 0.5f;
        Vec3 r = v3_scale(cam_right, h), u = v3_scale(cam_up, h);
        Vec3 c[4] = { v3_sub(v3_sub(p->pos, r), u), v3_sub(v3_add(p->pos, r), u), v3_add(v3_add(p->pos, r), u),
                      v3_add(v3_sub(p->pos, r), u) };
        float cu[4] = { u0, u1, u1, u0 }, cv[4] = { v1, v1, v0, v0 };
        uint8_t a = (uint8_t)(p->alpha < 0 ? 0 : p->alpha);
        for (int e = 0; e < 4; e++) {               /* centre 0x508484A4 (alpha fades), corners 0x00646464 */
            int e2 = (e + 1) & 3;
            buf[nv++] = vtx(p->pos, (u0 + u1) / 2, (v0 + v1) / 2, 0x84, 0x84, 0xa4, a);
            buf[nv++] = vtx(c[e], cu[e], cv[e], 0x64, 0x64, 0x64, 0);
            buf[nv++] = vtx(c[e2], cu[e2], cv[e2], 0x64, 0x64, 0x64, 0);
        }
    }
    GfxState sm = { BLEND_ADD, 0, 1, 0, 0, 0 };
    if (nv) gfx_draw_tris(buf, nv, g_tex, &sm);
}
