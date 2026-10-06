/* texture.c - see texture.h.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "texture.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char name[32]; Texture t; } Entry;
static Entry *g_tex;
static int g_gamma;

void tex_set_gamma(int on) { g_gamma = on; }
void tex_apply_gamma(uint8_t *q, int n)
{
    if (!g_gamma) return;
    for (int i = 0; i < n; i++, q += 4) {
        q[0] = (uint8_t)(q[0] * q[0] / 256); q[1] = (uint8_t)(q[1] * q[1] / 256); q[2] = (uint8_t)(q[2] * q[2] / 256);
    }
}
static int g_n, g_cap;

static void base_name(const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "%s", name);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

/* a texture pack's image: mods/textures/NAME with one of these extensions (res.h) */
static int mod_image(const char *b, Image *out)
{
    static const char *EXT[5] = { "png", "tga", "pcx", "jpg", "bmp" };
    char n[48], p[1024];
    for (int i = 0; i < 5; i++) {
        snprintf(n, sizeof n, "%s.%s", b, EXT[i]);
        if (!res_mod_find(p, sizeof p, "textures", n)) continue;
        size_t sz = 0;
        uint8_t *d = res_load_path(p, &sz);
        int ok = d && image_decode(n, d, sz, out);
        free(d);
        if (ok) return 1;
        fprintf(stderr, "texture pack: %s could not be read\n", p);
    }
    return 0;
}

static int orig_image(const char *b, Image *out)
{
    char n[40];
    snprintf(n, sizeof n, "%s.tga", b);
    if (image_load(n, out)) return 1;
    snprintf(n, sizeof n, "%s.pcx", b);
    return image_load(n, out);
}

int tex_image(const char *name, Image *out)
{
    char b[32];
    base_name(name, b, sizeof b);
    return mod_image(b, out) || orig_image(b, out);
}

int tex_dump_all(const char *dir)
{
    ResArchive *a = res_open("RCS");
    int n = 0;
    char done[512][24];
    for (int i = 0; i < res_count(a); i++) {
        const char *name = res_name(a, i), *e = strrchr(name, '.');
        if (!e || (_stricmp(e, ".pcx") && _stricmp(e, ".tga"))) continue;
        char b[32], p[1024];
        base_name(name, b, sizeof b);
        int seen = 0;
        for (int k = 0; k < n && !seen; k++) seen = !_stricmp(done[k], b);
        if (seen || n >= 512) continue;
        Image img;
        if (!orig_image(b, &img)) continue;
        snprintf(p, sizeof p, "%s/%s.png", dir, b);
        if (image_write_png(p, &img)) snprintf(done[n++], sizeof done[0], "%s", b);
        else fprintf(stderr, "cannot write %s\n", p);
        image_free(&img);
    }
    res_close(a);
    return n;
}

/* the transparent colour of the textures, (255, 0, 127) (docs/FORMAT_IMAGES.md) -> alpha 0. Keyed texels take the
 * average colour of their opaque neighbours, so bilinear filtering and mipmaps do not bleed pink into the edges. */
static void key_and_bleed(Image *img)
{
    int w = img->w, h = img->h, any = 0;
    for (int i = 0; i < w * h; i++) {
        uint8_t *q = img->rgba + (size_t)i * 4;
        if (q[0] == 255 && q[1] == 0 && q[2] == 127) { q[3] = 0; any = 1; }
    }
    if (!any) return;
    for (int pass = 0; pass < 4; pass++) {
        int changed = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t *q = img->rgba + ((size_t)y * w + x) * 4;
                if (q[3] || (q[0] != 255 || q[1] != 0 || q[2] != 127)) continue;
                int r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const uint8_t *o = img->rgba + ((size_t)yy * w + xx) * 4;
                        if (o[0] == 255 && o[1] == 0 && o[2] == 127) continue;
                        r += o[0]; g += o[1]; b += o[2]; n++;
                    }
                if (n) { q[0] = (uint8_t)(r / n); q[1] = (uint8_t)(g / n); q[2] = (uint8_t)(b / n); changed = 1; }
            }
        if (!changed) break;
    }
    for (int i = 0; i < w * h; i++) {          /* the rest (far from any edge): grey */
        uint8_t *q = img->rgba + (size_t)i * 4;
        if (!q[3] && q[0] == 255 && q[1] == 0 && q[2] == 127) { q[0] = q[1] = q[2] = 128; }
    }
}

int tex_pixels(const char *name, Image *out)
{
    char b[32];
    base_name(name, b, sizeof b);
    if (!tex_image(b, out)) return 0;
    key_and_bleed(out);
    if (_strnicmp(b, "back", 4)) tex_apply_gamma(out->rgba, out->w * out->h);   /* not the sky (0x423565) */
    return 1;
}

int tex_orig_size(const char *name, int *w, int *h)
{
    char b[32];
    Image img;
    base_name(name, b, sizeof b);
    if (!orig_image(b, &img)) return 0;
    *w = img.w;
    *h = img.h;
    image_free(&img);
    return 1;
}

const Texture *tex_get(const char *name)
{
    char b[32];
    base_name(name, b, sizeof b);
    for (int i = 0; i < g_n; i++)
        if (!_stricmp(g_tex[i].name, b)) return g_tex[i].t.id ? &g_tex[i].t : NULL;
    if (g_n == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 64;
        g_tex = realloc(g_tex, sizeof *g_tex * (size_t)g_cap);
    }
    Entry *e = &g_tex[g_n++];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", b);
    Image img;
    if (!tex_pixels(b, &img)) { fprintf(stderr, "texture %s missing\n", b); return NULL; }
    e->t.id = gfx_texture(img.rgba, img.w, img.h, TEX_MIPMAP);
    e->t.w = img.w;
    e->t.h = img.h;
    image_free(&img);
    return &e->t;
}

void tex_free_all(void)
{
    for (int i = 0; i < g_n; i++) gfx_texture_free(g_tex[i].t.id);
    free(g_tex);
    g_tex = NULL;
    g_n = g_cap = 0;
}
