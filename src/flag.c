/* flag.c - see flag.h; the original is documented in docs/RENDER.md "Finish flag (gfx_flag.cpp)".
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "flag.h"
#include "gfx.h"
#include "image.h"
#include "texture.h"
#include <math.h>
#include <string.h>

#define COLS 17                 /* vertices per row (34 per strip) */
#define ROWS 11                 /* vertex rows: 10 strips (0x4a7dd0, 10 x 0x440 bytes) */
#define UNITS 4096              /* angle units per turn (the run-time sin table) */

static struct {
    int on;                     /* Game.ini finish_flag (g_opt_finish_flag 0x4d9ae4) */
    int active;                 /* 0x4aa858 */
    int period;                 /* 0x4aa850: ftol(2 / frame dt), the frames of one wave period */
    int frame;                  /* 0x4aa854: 0..period-1 */
    float tick_dt;
    unsigned tex;               /* S_SFX with point sampling (the flag draws with its own filter: NEAREST) */
} FL;

int flag_init(int on, float tick_dt)                /* 0x41aea0 */
{
    flag_free();
    FL.on = on;
    FL.tick_dt = tick_dt;
    FL.period = (int)(2.0f / tick_dt + 0.001f);
    if (FL.period < 1) FL.period = 1;
    if (!on) return 1;
    Image img;
    if (!tex_image("s_sfx", &img)) return 0;
    image_key(&img, 255, 0, 127);                   /* the colour key (COLORKEYENABLE) */
    tex_apply_gamma(img.rgba, img.w * img.h);
    FL.tex = gfx_texture(img.rgba, img.w, img.h, TEX_NEAREST | TEX_CLAMP);
    image_free(&img);
    return FL.tex != 0;
}

void flag_free(void)
{
    gfx_texture_free(FL.tex);
    memset(&FL, 0, sizeof FL);
}

void flag_start(int on)                             /* 0x41abc0 */
{
    FL.period = (int)(2.0f / FL.tick_dt + 0.001f);
    if (FL.period < 1) FL.period = 1;
    if (FL.frame >= FL.period) FL.frame = 0;
    FL.active = on;
}

void flag_tick(void)                                /* the frame++ at the end of flag_draw */
{
    if (!FL.on || !FL.active) return;
    if (++FL.frame >= FL.period) FL.frame = 0;
}

void flag_draw(Vec3 eye, Vec3 target, float aspect, float alpha)     /* 0x41aee0 */
{
    if (!FL.on || !FL.active || !FL.tex) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    /* world = the camera's orientation (rows right, up, forward) at the camera's position: camera space */
    Vec3 f = v3_norm(v3_sub(target, eye)), r = v3_norm(v3_cross(v3(0, 1, 0), f)), u = v3_cross(f, r);
    Mat4 m = m4_identity();
    m.m[0] = r.x; m.m[1] = r.y; m.m[2] = r.z;
    m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z;
    m.m[8] = f.x; m.m[9] = f.y; m.m[10] = f.z;
    m.m[12] = eye.x; m.m[13] = eye.y; m.m[14] = eye.z;
    /* port: the original's screen is 4:3, where the flag (x +-100 at z 90..110, FOV 81 deg horizontal) always
     * reaches past the edges; a wider window widens it alike */
    float sx = aspect > 4.0f / 3.0f ? aspect * 0.75f : 1.0f;

    /* flag_animate 0x41ac00: phase = step * frame + 0x40 * row + 0x80 * column (4096 units per turn) */
    float t = (float)(UNITS / FL.period) * (FL.frame + alpha);
    GfxVertex g[ROWS][COLS];
    for (int s = 0; s < ROWS; s++)
        for (int c = 0; c < COLS; c++) {
            float w = 10.0f * sinf((t + 0x40 * s + 0x80 * c) * (6.2831853f / UNITS));
            int k = 256 - (int)((w + 10.0f) * 6.4f);            /* grey 255 (near) .. 128 (far) */
            if (k < 0x80) k = 0x80;
            if (k > 0xff) k = 0xff;
            GfxVertex *v = &g[s][c];
            /* flag_build_mesh 0x41ad40: x = 12.5 c - 100, y = 15 s - 75; uv an 8 x 6 texel chequer of S_SFX */
            v->x = (12.5f * c - 100.0f) * sx;
            v->y = 15.0f * s - 75.0f;
            v->z = 100.0f + w;
            v->u = 0.445313f + c * (0.06249997f * 0.0625f);
            v->v = 0.046875f - 0.046875f * s * 0.1f;
            v->r = v->g = v->b = (uint8_t)k;
            v->a = 0x64;
        }
    /* the 10 strips (row s and s + 1) as triangles */
    GfxVertex tri[(ROWS - 1) * (COLS - 1) * 6];
    int n = 0;
    for (int s = 0; s + 1 < ROWS; s++)
        for (int c = 0; c + 1 < COLS; c++) {
            tri[n++] = g[s][c]; tri[n++] = g[s + 1][c]; tri[n++] = g[s][c + 1];
            tri[n++] = g[s + 1][c]; tri[n++] = g[s + 1][c + 1]; tri[n++] = g[s][c + 1];
        }
    GfxState st = { BLEND_ALPHA, 0, 0, 0, 0, 0 };   /* SRCALPHA / INVSRCALPHA, ZENABLE off, no culling */
    gfx_set_model(&m);
    gfx_set_depth_off(1);
    gfx_draw_tris(tri, n, FL.tex, &st);
    gfx_set_depth_off(0);
    gfx_set_model(NULL);
}
