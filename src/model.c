/* model.c - see model.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "model.h"
#include "res.h"
#include "texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int mesh, texture; Model m; int tried; } Slot;

static ExeTables g_tab;
static Slot *g_slots;
static int g_nslots, g_capslots;

int models_init(void) { return exetab_load(&g_tab); }

void models_free(void)
{
    for (int i = 0; i < g_nslots; i++) gfx_mesh_free(g_slots[i].m.mesh);
    free(g_slots);
    g_slots = NULL;
    g_nslots = g_capslots = 0;
    exetab_free(&g_tab);
}

const ExeTables *models_tables(void) { return &g_tab; }
int anim_find(const char *name) { return exetab_find_anim(&g_tab, name); }
int anim_frames(int anim) { return anim >= 0 && anim < g_tab.nanims ? g_tab.anim[anim].nframes : 0; }

static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* .d3d: u32 nverts, nfaces; nverts x 33 bytes (x,y,z, nx,ny,nz, u,v floats + 1 unused byte);
 * nfaces x 9 bytes (u16 a,b,c + 3 unused bytes). E04_koor.D3D has 24 zero bytes in front (it is never loaded). */
static int load_mesh(Model *m, const char *name, unsigned tex)
{
    size_t n = 0;
    uint8_t *d = res_load(name, &n);
    if (!d || n < 8) { free(d); fprintf(stderr, "mesh %s missing\n", name); return 0; }
    uint32_t nv = rd32(d), nf = rd32(d + 4);
    if (8 + (size_t)nv * 33 + (size_t)nf * 9 != n || nv > 65535) { free(d); fprintf(stderr, "mesh %s: bad size\n", name); return 0; }
    GfxVertex *v = malloc(sizeof *v * (nv ? nv : 1));
    uint16_t *idx = malloc(sizeof *idx * (nf ? nf : 1) * 3);
    m->min = v3(1e9f, 1e9f, 1e9f);
    m->max = v3(-1e9f, -1e9f, -1e9f);
    for (uint32_t i = 0; i < nv; i++) {
        const uint8_t *p = d + 8 + i * 33;
        v[i].x = rdf(p); v[i].y = rdf(p + 4); v[i].z = rdf(p + 8);
        v[i].u = rdf(p + 24); v[i].v = rdf(p + 28);
        v[i].r = v[i].g = v[i].b = v[i].a = 255;
        if (v[i].x < m->min.x) m->min.x = v[i].x;
        if (v[i].y < m->min.y) m->min.y = v[i].y;
        if (v[i].z < m->min.z) m->min.z = v[i].z;
        if (v[i].x > m->max.x) m->max.x = v[i].x;
        if (v[i].y > m->max.y) m->max.y = v[i].y;
        if (v[i].z > m->max.z) m->max.z = v[i].z;
    }
    int ni = 0;
    for (uint32_t i = 0; i < nf; i++) {
        const uint8_t *p = d + 8 + nv * 33 + i * 9;
        int a = p[0] | p[1] << 8, b = p[2] | p[3] << 8, c = p[4] | p[5] << 8;
        if ((uint32_t)a >= nv || (uint32_t)b >= nv || (uint32_t)c >= nv) continue;
        idx[ni++] = (uint16_t)a; idx[ni++] = (uint16_t)b; idx[ni++] = (uint16_t)c;
    }
    /* colour key = alpha test; faces are visible when clockwise on screen (D3DCULL_CCW), see gfx cull */
    GfxRange r = { tex, 0, ni, gfx_state_opaque() };
    r.state.cull = 1;
    m->mesh = gfx_mesh_create(v, (int)nv, idx, ni, &r, 1);
    m->nverts = (int)nv;
    m->nfaces = (int)nf;
    free(v);
    free(idx);
    free(d);
    return 1;
}

const Model *model_get(int mesh, int texture)
{
    for (int i = 0; i < g_nslots; i++)
        if (g_slots[i].mesh == mesh && g_slots[i].texture == texture) return g_slots[i].m.mesh ? &g_slots[i].m : NULL;
    if (g_nslots == g_capslots) {
        g_capslots = g_capslots ? g_capslots * 2 : 64;
        g_slots = realloc(g_slots, sizeof *g_slots * (size_t)g_capslots);
    }
    Slot *s = &g_slots[g_nslots++];
    memset(s, 0, sizeof *s);
    s->mesh = mesh;
    s->texture = texture;
    if (mesh < 0 || mesh >= g_tab.nmeshes) return NULL;
    const Texture *t = texture >= 0 && texture < g_tab.ntextures ? tex_get(g_tab.texture[texture]) : NULL;
    if (!load_mesh(&s->m, g_tab.mesh[mesh], t ? t->id : 0)) return NULL;
    return &s->m;
}

const Model *anim_model(int anim, int frame)
{
    if (anim < 0 || anim >= g_tab.nanims || g_tab.anim[anim].nframes <= 0) return NULL;
    const Anim *a = &g_tab.anim[anim];
    if (frame < 0 || frame >= a->nframes) frame = 0;
    const AnimFrame *f = &g_tab.frames[a->first + frame];
    return model_get(f->mesh, f->texture);
}

Mat4 model_place(Vec3 pos, float yaw)
{
    /* world.x = v.x cos + v.z sin + x; world.z = -v.x sin + v.z cos + z (FORMAT_OBJECTS.md 2) */
    Mat4 r = m4_identity();
    float c = cosf(yaw), s = sinf(yaw);
    r.m[0] = c;  r.m[8] = s;
    r.m[2] = -s; r.m[10] = c;
    r.m[12] = pos.x; r.m[13] = pos.y; r.m[14] = pos.z;
    return r;
}

void model_draw_matrix(const Model *m, const Mat4 *world)
{
    if (!m || !m->mesh) return;
    gfx_set_model(world);
    gfx_mesh_draw(m->mesh);
}

void model_draw(const Model *m, Vec3 pos, float yaw)
{
    Mat4 w = model_place(pos, yaw);
    model_draw_matrix(m, &w);
}
