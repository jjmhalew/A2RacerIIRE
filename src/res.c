/* res.c - see res.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "res.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char name[24]; uint32_t off, size; } Entry;
struct ResArchive {
    FILE *f;
    int count;
    int named;              /* entries have file names (RCS, Mrcs); the others are numbered */
    Entry *e;
};

static char g_dir[512] = ".";
static ResArchive *g_rcs;

void res_set_dir(const char *d)
{
    snprintf(g_dir, sizeof g_dir, "%s", d && *d ? d : ".");
    size_t n = strlen(g_dir);
    while (n > 1 && (g_dir[n - 1] == '/' || g_dir[n - 1] == '\\')) g_dir[--n] = 0;
    if (g_rcs) { res_close(g_rcs); g_rcs = NULL; }
}

const char *res_dir(void) { return g_dir; }

void res_path(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", g_dir, name); }

static uint8_t *read_whole(const char *p, size_t *size)
{
    FILE *f = plat_fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n + 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) { free(d); d = NULL; }
    fclose(f);
    if (d) { d[n] = 0; if (size) *size = (size_t)n; }
    return d;
}

/* -1 = not looked yet, 0 = no mods folder, 1 = there is one */
static int g_mods = -1;

int res_mod_find(char *out, size_t cap, const char *sub, const char *name)
{
    const char *dirs[2] = { plat_user_dir(), plat_base_dir() };
    char p[1024];
    if (g_mods < 0) {
        g_mods = 0;
        for (int i = 0; i < 2; i++) {
            snprintf(p, sizeof p, "%smods", dirs[i]);
            if (plat_exists(p)) g_mods = 1;
        }
    }
    if (!g_mods || !name || !*name) return 0;
    for (int i = 0; i < 2; i++) {
        snprintf(p, sizeof p, "%smods/%s/%s", dirs[i], sub, name);
        FILE *f = plat_fopen(p, "rb");
        if (f) {
            fclose(f);
            snprintf(out, cap, "%s", p);
            return 1;
        }
    }
    return 0;
}

uint8_t *res_load_path(const char *path, size_t *size) { return read_whole(path, size); }

uint8_t *res_load_file(const char *name, size_t *size)
{
    char p[1024];
    if (res_mod_find(p, sizeof p, "data", name)) return read_whole(p, size);
    res_path(p, sizeof p, name);
    return read_whole(p, size);
}

void res_user_path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s%s", plat_user_dir(), name);
    if (plat_exists(out)) return;
    size_t n = 0;
    uint8_t *d = res_load_file(name, &n);         /* the CD's copy, when it has one */
    if (!d) return;
    FILE *f = fopen(out, "wb");
    if (f) { fwrite(d, 1, n, f); fclose(f); }
    free(d);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

ResArchive *res_open(const char *base)
{
    char n[128];
    size_t isz = 0;
    snprintf(n, sizeof n, "%s.IND", base);
    uint8_t *ind = res_load_file(n, &isz);
    if (!ind || isz < 2) { free(ind); return NULL; }
    char p[1024];
    snprintf(n, sizeof n, "%s.IMG", base);
    res_path(p, sizeof p, n);
    FILE *f = plat_fopen(p, "rb");
    if (!f) { free(ind); return NULL; }
    fseek(f, 0, SEEK_END);
    uint32_t total = (uint32_t)ftell(f);
    ResArchive *a = calloc(1, sizeof *a);
    if (!a) { fclose(f); free(ind); return NULL; }
    a->f = f;
    a->count = ind[0] | ind[1] << 8;
    a->e = calloc((size_t)a->count + 1, sizeof *a->e);
    if (!a->e) { free(ind); res_close(a); return NULL; }
    int named = a->named = isz == 2 + (size_t)a->count * 24;
    if (!named && isz != 2 + (size_t)a->count * 4) { free(ind); res_close(a); return NULL; }
    for (int i = 0; i < a->count; i++) {
        if (named) {
            memcpy(a->e[i].name, ind + 2 + i * 24, 20);
            a->e[i].name[20] = 0;
            a->e[i].name[strnlen(a->e[i].name, 20)] = 0;
            a->e[i].off = rd32(ind + 2 + i * 24 + 20);
        } else {
            snprintf(a->e[i].name, sizeof a->e[i].name, "%04d", i);
            a->e[i].off = rd32(ind + 2 + i * 4);
        }
    }
    for (int i = 0; i < a->count; i++) {
        uint32_t next = i + 1 < a->count ? a->e[i + 1].off : total;
        a->e[i].size = next >= a->e[i].off ? next - a->e[i].off : 0;
    }
    free(ind);
    return a;
}

void res_close(ResArchive *a)
{
    if (!a) return;
    if (a->f) fclose(a->f);
    free(a->e);
    free(a);
}

int res_count(const ResArchive *a) { return a ? a->count : 0; }
const char *res_name(const ResArchive *a, int i) { return a && i >= 0 && i < a->count ? a->e[i].name : ""; }

int res_find(const ResArchive *a, const char *name)
{
    if (!a) return -1;
    for (int i = 0; i < a->count; i++)
        if (!_stricmp(a->e[i].name, name)) return i;
    return -1;
}

uint8_t *res_read(ResArchive *a, int i, size_t *size)
{
    if (!a || i < 0 || i >= a->count) return NULL;
    char p[1024];
    if (a->named && res_mod_find(p, sizeof p, "data", a->e[i].name)) return read_whole(p, size);
    uint8_t *d = malloc(a->e[i].size + 1);
    if (!d) return NULL;
    fseek(a->f, (long)a->e[i].off, SEEK_SET);
    if (fread(d, 1, a->e[i].size, a->f) != a->e[i].size) { free(d); return NULL; }
    d[a->e[i].size] = 0;
    if (size) *size = a->e[i].size;
    return d;
}

uint8_t *res_load(const char *name, size_t *size)
{
    if (!g_rcs) g_rcs = res_open("RCS");
    int i = res_find(g_rcs, name);
    if (i >= 0) return res_read(g_rcs, i, size);
    return res_load_file(name, size);
}
