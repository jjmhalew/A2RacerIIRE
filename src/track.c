/* track.c - see track.h; the file layouts and the original's functions are in docs/FORMAT_ROAD.md.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "track.h"
#include "ini.h"
#include "plat.h"
#include "res.h"
#include "texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *d;
    size_t n, p;
    int bad;
} Rd;

static uint32_t rd_u32(Rd *r)
{
    if (r->p + 4 > r->n) { r->bad = 1; r->p = r->n; return 0; }
    const uint8_t *q = r->d + r->p;
    r->p += 4;
    return q[0] | q[1] << 8 | q[2] << 16 | (uint32_t)q[3] << 24;
}
static int32_t rd_i32(Rd *r) { return (int32_t)rd_u32(r); }
static float rd_f32(Rd *r) { uint32_t u = rd_u32(r); float f; memcpy(&f, &u, 4); return f; }
static Vec3 rd_vec(Rd *r) { Vec3 v; v.x = rd_f32(r); v.y = rd_f32(r); v.z = rd_f32(r); return v; }

static int open_file(Rd *r, int etappe, const char *ext)
{
    char n[32];
    snprintf(n, sizeof n, "etappe%d.%s", etappe, ext);
    memset(r, 0, sizeof *r);
    r->d = res_load_file(n, &r->n);
    if (!r->d) fprintf(stderr, "%s missing\n", n);
    return r->d != NULL;
}

/* Raser.ini [MAPS] N = type,etappeN,weg,gras,gra2,vang,sky_lo,sky_hi,NAME,music,timer (gm_load_track_info 0x4225d0) */
static void read_map_info(Track *t, int etappe)
{
    char p[600], key[8];
    res_user_path(p, sizeof p, "Raser.ini");
    Ini *ini = ini_load(p);
    snprintf(key, sizeof key, "%d", etappe);
    const char *v = ini_get(ini, "MAPS", key);
    char buf[256];
    snprintf(buf, sizeof buf, "%s", v ? v : "");
    char *f[12] = { 0 };
    int nf = 0;
    for (char *s = buf; nf < 12;) {
        f[nf++] = s;
        char *c = strchr(s, ',');
        if (!c) break;
        *c = 0;
        s = c + 1;
    }
    static const char *defs[4] = { "e00_weg", "e00_gras", "e00_gra2", "e00_vang" };
    t->city = nf > 0 ? atoi(f[0]) : 0;
    /* file arg order weg, gras, gra2, vang -> groups road, land, extra, rail */
    static const int grp[4] = { G_ROAD, G_LAND, G_EXTRA, G_RAIL };
    for (int i = 0; i < 4; i++) snprintf(t->texname[grp[i]], sizeof t->texname[0], "%s", nf > 2 + i ? f[2 + i] : defs[i]);
    snprintf(t->name, sizeof t->name, "%s", nf > 8 ? f[8] : "");
    snprintf(t->sky[0], sizeof t->sky[0], "%s", nf > 6 ? f[6] : "scrl_lo");
    snprintf(t->sky[1], sizeof t->sky[1], "%s", nf > 7 ? f[7] : "scrl_hi");
    ini_free(ini);
}

/* frame vectors and angles of chunk i towards point next (calc_frame / calc_angles, 0x444f67..) */
static void chunk_frame(Chunk *c, Vec3 next)
{
    Vec3 f = v3_norm(v3_sub(next, c->pos));
    c->fwd = f;
    c->right = v3_norm(v3(f.z, 0, -f.x));
    c->up = v3_norm(v3_cross(f, c->right));
    c->plane_d = v3_dot(c->up, c->pos);
    c->pitch = (int)(atan2f(f.y, sqrtf(f.x * f.x + f.z * f.z)) * (float)ANGLE_UNITS / (2 * PI_F));
    c->yaw = (int)((atan2f(f.z, f.x) - PI_F / 2) * (float)ANGLE_UNITS / (2 * PI_F));
}

typedef struct { GfxVertex *v; uint16_t *idx; int nv, ni, cap_v, cap_i; } Pool;

static void pool_quad(Pool *p, Rd *r)
{
    if (p->nv + 4 > p->cap_v) { p->cap_v = (p->cap_v + 4) * 2; p->v = realloc(p->v, sizeof *p->v * (size_t)p->cap_v); }
    if (p->ni + 6 > p->cap_i) { p->cap_i = (p->cap_i + 6) * 2; p->idx = realloc(p->idx, sizeof *p->idx * (size_t)p->cap_i); }
    int base = p->nv;
    for (int k = 0; k < 4; k++) {
        GfxVertex *v = &p->v[p->nv++];
        v->x = rd_f32(r); v->y = rd_f32(r); v->z = rd_f32(r);
        v->u = rd_f32(r); v->v = rd_f32(r);
        v->r = v->g = v->b = v->a = 255;                  /* D3DLVERTEX colour 0x00FFFFFF, pre-lit */
    }
    for (int k = 0; k < 6; k++) {
        int c = rd_i32(r);
        p->idx[p->ni++] = (uint16_t)(base + (c & 3));
    }
}

int track_load(Track *t, int etappe)
{
    memset(t, 0, sizeof *t);
    t->index = etappe;
    read_map_info(t, etappe);
    Rd r;
    /* .cmp guard rails (rails_load_cmp 0x44c2f0, rail_set 0x44c440) */
    if (!open_file(&r, etappe, "cmp")) return 0;
    t->nrails = (int)rd_u32(&r);
    if (t->nrails < 0 || (size_t)t->nrails > (r.n - r.p) / 24) { free((void *)r.d); return 0; }   /* damaged file */
    t->rails = calloc((size_t)t->nrails + 1, sizeof *t->rails);
    for (int i = 0; i < t->nrails && !r.bad; i++) {
        Rail *l = &t->rails[i];
        l->p = rd_vec(&r);
        l->d = rd_vec(&r);
        l->len = sqrtf(l->d.x * l->d.x + l->d.z * l->d.z);
        l->yaw = (atan2f(l->d.z, l->d.x) - PI_F / 2) * (float)ANGLE_UNITS / (2 * PI_F);
    }
    free((void *)r.d);
    /* .dmp driver map (drmap_load 0x443c40) */
    if (!open_file(&r, etappe, "dmp")) return 0;
    t->nwaypoints = (int)rd_u32(&r);
    if (t->nwaypoints < 0 || (size_t)t->nwaypoints > (r.n - r.p) / 36) { free((void *)r.d); return 0; }
    t->wps = calloc((size_t)t->nwaypoints + 1, sizeof *t->wps);
    for (int i = 0; i < t->nwaypoints && !r.bad; i++) {
        Waypoint *w = &t->wps[i];
        w->flags = rd_i32(&r); w->flags2 = rd_i32(&r);
        w->pos = rd_vec(&r);
        w->next = rd_i32(&r); w->next_inner = rd_i32(&r); w->next_outer = rd_i32(&r); w->prev = rd_i32(&r);
        int *link[4] = { &w->next, &w->next_inner, &w->next_outer, &w->prev };
        for (int k = 0; k < 4; k++)                     /* -1 = none; anything else outside the map too */
            if (*link[k] < -1 || *link[k] >= t->nwaypoints) *link[k] = -1;
    }
    free((void *)r.d);
    /* .pmp chunk data (chunk_load_pmp 0x442b00) */
    if (!open_file(&r, etappe, "pmp")) return 0;
    t->nchunks = (int)rd_u32(&r);
    if (t->nchunks <= 1 || t->nchunks > 100000) { free((void *)r.d); return 0; }
    t->chunks = calloc((size_t)t->nchunks, sizeof *t->chunks);
    for (int i = 0; i < t->nchunks && !r.bad; i++) {
        Chunk *c = &t->chunks[i];
        c->pos = rd_vec(&r);
        c->trigger[0] = rd_i32(&r);
        c->trigger[1] = rd_i32(&r);
        c->nrails = (int)rd_u32(&r);
        for (int k = 0; k < c->nrails; k++) { int v = rd_i32(&r); if (k < 12) c->rails[k] = v; }
        for (int s = 0; s < 2; s++) {
            c->nwp[s] = (int)rd_u32(&r);
            for (int k = 0; k < c->nwp[s]; k++) { int v = rd_i32(&r); if (k < 12) c->wp[s][k] = v; }
        }
        if (c->nrails > 12) c->nrails = 12;
        if (c->nwp[0] > 12) c->nwp[0] = 12;
        if (c->nwp[1] > 12) c->nwp[1] = 12;
    }
    int bad = r.bad;
    free((void *)r.d);
    if (bad) { fprintf(stderr, "etappe%d.pmp: truncated\n", etappe); return 0; }
    /* frames: chunk i towards i+1; the last one towards chunk 0, then its vectors are those of n-2 (0x4450ad) */
    for (int i = 0; i < t->nchunks; i++) chunk_frame(&t->chunks[i], t->chunks[(i + 1) % t->nchunks].pos);
    {
        Chunk *last = &t->chunks[t->nchunks - 1], *prev = &t->chunks[t->nchunks - 2];
        last->right = prev->right; last->up = prev->up; last->fwd = prev->fwd; last->plane_d = prev->plane_d;
    }
    /* .rmp road mesh (0x444cf0 / chunk_load_rmp 0x4431b0 / plane_load 0x44bdb0) */
    if (!open_file(&r, etappe, "rmp")) return 0;
    rd_u32(&r);                                          /* unused count */
    int n = (int)rd_u32(&r);
    if (n != t->nchunks) fprintf(stderr, "etappe%d: .rmp has %d chunks, .pmp %d\n", etappe, n, t->nchunks);
    Pool pool[G_COUNT];
    memset(pool, 0, sizeof pool);
    static const int file_order[4] = { G_ROAD, G_LAND, G_EXTRA, G_RAIL };
    for (int i = 0; i < n && i < t->nchunks && !r.bad; i++) {
        int count[4];
        Rd save;
        /* the lists come in file order; each group's range is recorded as it is appended */
        for (int l = 0; l < 4; l++) {
            int g = file_order[l];
            count[l] = (int)rd_u32(&r);
            t->chunks[i].range[g][0] = pool[g].ni;
            for (int q = 0; q < count[l] && !r.bad; q++) pool_quad(&pool[g], &r);
            t->chunks[i].range[g][1] = pool[g].ni;
        }
        (void)save;
    }
    bad = r.bad;
    free((void *)r.d);
    if (bad) fprintf(stderr, "etappe%d.rmp: truncated\n", etappe);
    for (int g = 0; g < G_COUNT; g++) {
        const Texture *tx = tex_get(t->texname[g]);
        t->tex[g] = tx ? tx->id : 0;
        GfxRange rg = { t->tex[g], 0, pool[g].ni, gfx_state_opaque(), 0 };
        rg.state.alpha_test = 1;
        if (pool[g].nv > 65535) fprintf(stderr, "etappe%d: group %d has %d vertices\n", etappe, g, pool[g].nv);
        t->mesh[g] = gfx_mesh_create(pool[g].v, pool[g].nv, pool[g].idx, pool[g].ni, &rg, 1);
        free(pool[g].v);
        free(pool[g].idx);
    }
    return 1;
}

void track_free(Track *t)
{
    for (int g = 0; g < G_COUNT; g++) gfx_mesh_free(t->mesh[g]);
    free(t->chunks);
    free(t->wps);
    free(t->rails);
    memset(t, 0, sizeof *t);
}

int track_wrap(const Track *t, int c)
{
    int n = t->nchunks;
    c %= n;
    return c < 0 ? c + n : c;
}

static float before_line(const Chunk *c, Vec3 p)
{
    return (p.x - c->pos.x) * c->right.z - (p.z - c->pos.z) * c->right.x;
}

int track_find_chunk(const Track *t, int start, Vec3 p)
{
    int n = t->nchunks;
    if (start < 0 || start >= n) start = 0;
    if (before_line(&t->chunks[start], p) > 0) {
        for (int c = start; c >= 0; c--)
            if (before_line(&t->chunks[c], p) < 0) return c;
    } else {
        for (int c = start; c < n; c++)
            if (before_line(&t->chunks[c], p) > 0) return c - 1 == -1 ? n - 1 : c - 1;
    }
    for (int c = 0; c < n; c++)
        if (before_line(&t->chunks[c], p) > 0) return c - 1 == -1 ? n - 1 : c - 1;
    return n - 1;   /* the original returns n here (out of range) */
}

int track_visible_start(const Track *t, int c, Vec3 dir, int behind, int visible)
{
    float d = v3_dot(v3_norm(dir), t->chunks[track_wrap(t, c)].fwd);
    int start;
    if (d > 0.5736f) start = c - behind;
    else if (d < -0.5736f) start = c + behind - visible;
    else start = c - visible / 2;
    if (start < 0) start += t->nchunks - 1;      /* sic: n - 1, as the original */
    return start;
}

static void draw_range(const Track *t, int first, int last, int fog)
{
    (void)fog;
    for (int g = 0; g < G_COUNT; g++) {
        int a = t->chunks[first].range[g][0], b = t->chunks[last].range[g][1];
        if (b > a) gfx_mesh_draw_sub(t->mesh[g], 0, a, b - a);
    }
}

void track_draw(const Track *t, int start, int count, int fog)
{
    int n = t->nchunks;
    int s = start < 0 ? 0 : start >= n ? n - 1 : start;
    int e = start + count;
    if (e < 0) e = 0;
    if (e >= n) {
        draw_range(t, s, n - 1, fog);
        int e2 = e - n;
        if (e2 > n - 1) e2 = n - 1;
        draw_range(t, 0, e2, fog);
    } else draw_range(t, s, e, fog);
}
