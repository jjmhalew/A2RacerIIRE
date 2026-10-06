/* shadow.c - the car shadows (gfx_shdw.cpp; docs/RENDER.md "Car shadows").
 *
 * The original: init_car_shadow_object 0x410df0 (Car_main.cpp, from init_game) creates one 3D instance of
 * S_SHADOW_ANIM (s_shadow.d3d, a 4-vertex quad, + s_shadow.pcx/.tga, a black rounded rectangle in the colour key),
 * instance +0x95 = 2 (no automatic matrix), in g_shadow_instance 0x4d9a90, and rewrites the mesh's vertices once:
 * colour 0x80000000 (black, alpha 128), specular 0, texture coordinates 2 texels inside the edges. add_car 0x410d68 and
 * car_cops_add_static (object_set_shadow 0x4110e0) put that instance in object +0x24 of every car; all other objects
 * have -1 there. shadow_register 0x41d810 (from init_game) stores the callback 0x41d460 in 0x4e5dc8, and the object
 * draw loops 0x4409e0 / 0x440b30 call it (0x441f00) for each visible object with +0x24 != -1, right before
 * obj_draw_with_damage draws the object, i.e. in the opaque pass.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "shadow.h"
#include "gfx.h"
#include "ini.h"
#include "model.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GfxVertex g_v[4];            /* the shared mesh's vertices (mesh +0x80) */
static uint16_t g_idx[6];
static int g_ni;
static unsigned g_tex;
static int g_ok;
static int g_alpha_blending;        /* g_opt_alpha_blending 0x4d9b18, tested per draw */

void shadow_init(void)                                              /* init_car_shadow_object 0x410df0 */
{
    char p[1024];
    res_user_path(p, sizeof p, "Game.ini");
    Ini *g = ini_load(p);
    g_alpha_blending = ini_int(g, "GFX OPTIONS", "alpha_blending", 1);
    ini_free(g);
    if (g_ok) return;                                               /* the mesh is the same for every race */
    GfxVertex *v = NULL;
    uint16_t *idx = NULL;
    int nv = 0, ni = 0, tw = 0;
    if (!anim_mesh_raw(anim_find("S_SHADOW_ANIM"), 0, &v, &nv, &idx, &ni, &g_tex, &tw)) return;
    if (nv != 4 || ni > 6 || tw <= 0) {                             /* fatal in the original */
        fprintf(stderr, "shadow mesh is not square\n");
        free(v);
        free(idx);
        return;
    }
    memcpy(g_v, v, sizeof g_v);
    memcpy(g_idx, idx, sizeof *idx * (size_t)ni);
    g_ni = ni;
    free(v);
    free(idx);
    float e = 2.0f / tw;                                            /* 2 / texture width (g_textures +0x24) */
    for (int i = 0; i < 4; i++) { g_v[i].r = g_v[i].g = g_v[i].b = 0; g_v[i].a = 0x80; }   /* 0x80000000 */
    g_v[0].u = 1 - e; g_v[0].v = e;                                 /* +0x18 / +0x1c */
    g_v[1].u = e;     g_v[1].v = e;                                 /* +0x38 / +0x3c */
    g_v[2].u = e;     g_v[2].v = 1 - e;                             /* +0x58 / +0x5c */
    g_v[3].u = 1 - e; g_v[3].v = 1 - e;                             /* +0x78 / +0x7c */
    g_ok = 1;
}

/* the shadow callback 0x41d460 (gfx_shdw.cpp) */
void shadow_draw(const Race *r, int o, const Mat4 *body)
{
    const Obj *c = &r->obj[o];
    if (!g_ok || c->car_type < 0) return;                           /* object +0x24 == -1: no shadow */
    const Model *m = anim_model(c->anim, c->frame);
    if (!m) return;
    /* x and z of four corners of the car mesh's bounding box (mesh +0x4c, +0x40, +0x10, +0x1c: built by 0x456110
     * as (max x, max z), (min x, max z), (min x, min z), (max x, min z), all at min y); y stays the quad's own (0) */
    g_v[0].x = m->max.x; g_v[0].z = m->max.z;
    g_v[1].x = m->min.x; g_v[1].z = m->max.z;
    g_v[2].x = m->min.x; g_v[2].z = m->min.z;
    g_v[3].x = m->max.x; g_v[3].z = m->min.z;
    /* the car's matrix (instance +0x10, with its pitch and roll), moved by (+2, , +2) and put 0.5 above the road at the
     * car's position: chunk_height_at(g_road_chunk_order[obj +0x18], x, z) - (-0.5) */
    Mat4 w = *body;
    w.m[13] = chunk_height_at(&r->track.chunks[c->chunk], body->m[12], body->m[14]) + 0.5f;
    w.m[12] += 2.0f;
    w.m[14] += 2.0f;
    GfxVertex t[6];
    for (int i = 0; i < g_ni; i++) t[i] = g_v[g_idx[i]];
    /* alpha_blending: ALPHABLENDENABLE 1, SRCALPHA / INVSRCALPHA, object3d_draw, ALPHABLENDENABLE 0; otherwise drawn as
     * it is (opaque black). The texture's colour key cuts the rounded corners in both cases; z test and write on. */
    GfxState st = { g_alpha_blending ? BLEND_ALPHA : BLEND_NONE, 2, 1, 1, 1, 0 };
    if (!g_alpha_blending)
        for (int i = 0; i < g_ni; i++) t[i].a = 255;                /* no blending: the vertex alpha plays no part */
    gfx_set_model(&w);
    gfx_draw_tris(t, g_ni, g_tex, &st);
}
