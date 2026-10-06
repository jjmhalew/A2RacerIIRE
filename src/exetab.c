/* exetab.c - see exetab.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "exetab.h"
#include "plat.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int pe_open(PeFile *pe, const char *name)
{
    memset(pe, 0, sizeof *pe);
    pe->data = res_load_file(name, &pe->size);
    if (!pe->data || pe->size < 0x200 || pe->data[0] != 'M' || pe->data[1] != 'Z') { pe_close(pe); return 0; }
    uint32_t nt = rd32(pe->data + 0x3c);
    if (nt + 0xf8 > pe->size || memcmp(pe->data + nt, "PE\0\0", 4)) { pe_close(pe); return 0; }
    int nsec = pe->data[nt + 6] | pe->data[nt + 7] << 8;
    int opt = pe->data[nt + 20] | pe->data[nt + 21] << 8;
    pe->base = rd32(pe->data + nt + 24 + 28);
    const uint8_t *s = pe->data + nt + 24 + opt;
    pe->nsec = nsec > 16 ? 16 : nsec;
    for (int i = 0; i < pe->nsec; i++, s += 40) {
        if ((size_t)(s - pe->data) + 40 > pe->size) break;
        pe->sec[i].vsize = rd32(s + 8);
        pe->sec[i].va = rd32(s + 12) + pe->base;
        pe->sec[i].rsize = rd32(s + 16);
        pe->sec[i].raw = rd32(s + 20);
    }
    return 1;
}

void pe_close(PeFile *pe) { free(pe->data); memset(pe, 0, sizeof *pe); }

const uint8_t *pe_ptr(const PeFile *pe, uint32_t va, size_t n)
{
    for (int i = 0; i < pe->nsec; i++) {
        uint32_t off = va - pe->sec[i].va;
        if (va >= pe->sec[i].va && off + n <= pe->sec[i].rsize && pe->sec[i].raw + off + n <= pe->size)
            return pe->data + pe->sec[i].raw + off;
    }
    return NULL;
}

uint32_t pe_u32(const PeFile *pe, uint32_t va)
{
    const uint8_t *p = pe_ptr(pe, va, 4);
    return p ? rd32(p) : 0;
}

const char *pe_str(const PeFile *pe, uint32_t va)
{
    const uint8_t *p = pe_ptr(pe, va, 1);
    if (!p) return NULL;
    size_t max = pe->size - (size_t)(p - pe->data);
    return memchr(p, 0, max < 256 ? max : 256) ? (const char *)p : NULL;
}

/* the addresses of the Dutch 1.01 spel.dat (05-11-1998) */
enum {
    MESH_TABLE = 0x48d0e0, TEX_TABLE = 0x48e1a0, FRAME_TABLE = 0x48e5e8, NFRAMES = 386,
    ANIM_TABLE = 0x492050, ANIM_NAMES = 0x490430, NANIMS = 312
};

static int name_table(const PeFile *pe, uint32_t va, char (**out)[16])
{
    int n = 0;
    while (n < 1000 && pe_u32(pe, va + (uint32_t)n * 12)) n++;
    *out = calloc((size_t)n + 1, 16);
    for (int i = 0; i < n; i++) {
        const char *s = pe_str(pe, pe_u32(pe, va + (uint32_t)i * 12));
        snprintf((*out)[i], 16, "%s", s ? s : "");
    }
    return n;
}

int exetab_load(ExeTables *t)
{
    memset(t, 0, sizeof *t);
    PeFile pe;
    if (!pe_open(&pe, "spel.dat")) { fprintf(stderr, "spel.dat not found\n"); return 0; }
    const char *check = pe_str(&pe, pe_u32(&pe, ANIM_NAMES));
    if (!check || !strstr(check, "_ANIM")) {
        fprintf(stderr, "spel.dat is not the expected version (1.01)\n");
        pe_close(&pe);
        return 0;
    }
    t->nmeshes = name_table(&pe, MESH_TABLE, &t->mesh);
    t->ntextures = name_table(&pe, TEX_TABLE, &t->texture);
    /* frames: 16 bytes {mesh handle, texture handle, mesh index, texture index} */
    AnimFrame *fr = calloc(NFRAMES, sizeof *fr);
    for (int i = 0; i < NFRAMES; i++) {
        fr[i].mesh = (int)pe_u32(&pe, FRAME_TABLE + (uint32_t)i * 16 + 8);
        fr[i].texture = (int)pe_u32(&pe, FRAME_TABLE + (uint32_t)i * 16 + 12);
    }
    /* anims: 9 packed bytes {int nframes; int *frame_indices; u8 flag} */
    t->nanims = NANIMS;
    t->anim = calloc(NANIMS, sizeof *t->anim);
    int total = 0;
    for (int i = 0; i < NANIMS; i++) total += (int)pe_u32(&pe, ANIM_TABLE + (uint32_t)i * 9);
    t->frames = calloc((size_t)total + 1, sizeof *t->frames);
    for (int i = 0; i < NANIMS; i++) {
        Anim *a = &t->anim[i];
        const char *nm = pe_str(&pe, pe_u32(&pe, ANIM_NAMES + (uint32_t)i * 4));
        snprintf(a->name, sizeof a->name, "%s", nm ? nm : "");
        a->nframes = (int)pe_u32(&pe, ANIM_TABLE + (uint32_t)i * 9);
        uint32_t list = pe_u32(&pe, ANIM_TABLE + (uint32_t)i * 9 + 4);
        a->first = t->nframes_total;
        for (int k = 0; k < a->nframes; k++) {
            /* a list pointing into .bss (OBJ_LIGHT01_ANIM) reads as frame 0 */
            const uint8_t *p = pe_ptr(&pe, list + (uint32_t)k * 4, 4);
            int f = p ? (int)rd32(p) : 0;
            t->frames[t->nframes_total++] = f >= 0 && f < NFRAMES ? fr[f] : fr[0];
        }
    }
    free(fr);
    pe_close(&pe);
    return t->nmeshes > 0 && t->ntextures > 0;
}

void exetab_free(ExeTables *t)
{
    free(t->mesh);
    free(t->texture);
    free(t->anim);
    free(t->frames);
    memset(t, 0, sizeof *t);
}

int exetab_find_anim(const ExeTables *t, const char *name)
{
    for (int i = 0; i < t->nanims; i++)
        if (!_stricmp(t->anim[i].name, name)) return i;
    return -1;
}
