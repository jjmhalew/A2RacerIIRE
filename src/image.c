/* image.c - see image.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "image.h"
#include "plat.h"
#include "res.h"
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include "stb/stb_image_write.h"
/* PNG, JPEG and BMP for texture packs (mods/textures, res.h); the game's own PCX and TGA are decoded below */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include "stb/stb_image.h"

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* ZSoft PCX: 128-byte header, RLE (a byte >= 0xC0 repeats the next byte (b & 0x3F) times) per scan line and plane;
 * 8-bit images end in 0x0C + a 256-colour palette. */
int image_decode_pcx(const uint8_t *d, size_t n, Image *out)
{
    memset(out, 0, sizeof *out);
    if (n < 128 || d[0] != 0x0a || d[2] != 1) return 0;
    int bpp = d[3], planes = d[65], bpl = rd16(d + 66);
    int w = rd16(d + 8) - rd16(d + 4) + 1, h = rd16(d + 10) - rd16(d + 6) + 1;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || bpp != 8 || (planes != 1 && planes != 3 && planes != 4)) return 0;
    if (bpl < w) return 0;                      /* each plane's line holds at least the width */
    size_t line = (size_t)bpl * planes;
    uint8_t *raw = malloc(line * (size_t)h);
    if (!raw) return 0;
    size_t p = 128, o = 0, total = line * (size_t)h;
    while (o < total && p < n) {
        uint8_t b = d[p++];
        if (b >= 0xc0) {
            int cnt = b & 0x3f;
            uint8_t v = p < n ? d[p++] : 0;
            while (cnt-- && o < total) raw[o++] = v;
        } else raw[o++] = b;
    }
    if (o < total) memset(raw + o, 0, total - o);
    out->w = w;
    out->h = h;
    out->bpp = bpp * planes;
    out->rgba = malloc((size_t)w * h * 4);
    if (planes == 1) out->index = malloc((size_t)w * h);
    if (!out->rgba || (planes == 1 && !out->index)) { free(raw); image_free(out); return 0; }
    if (planes == 1) {
        if (n >= 769 && d[n - 769] == 0x0c) memcpy(out->pal, d + n - 768, 768);
        else for (int i = 0; i < 256; i++) out->pal[i * 3] = out->pal[i * 3 + 1] = out->pal[i * 3 + 2] = (uint8_t)i;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t c = raw[(size_t)y * line + x];
                out->index[(size_t)y * w + x] = c;
                uint8_t *q = out->rgba + ((size_t)y * w + x) * 4;
                q[0] = out->pal[c * 3]; q[1] = out->pal[c * 3 + 1]; q[2] = out->pal[c * 3 + 2]; q[3] = 255;
            }
    } else {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t *q = out->rgba + ((size_t)y * w + x) * 4;
                const uint8_t *s = raw + (size_t)y * line + x;
                q[0] = s[0]; q[1] = s[bpl]; q[2] = s[2 * bpl]; q[3] = planes == 4 ? s[3 * bpl] : 255;
            }
    }
    free(raw);
    return 1;
}

/* Truevision TGA: types 1/2/3 (mapped, true colour, grey) and 9/10/11 (the same RLE). Rows bottom-up unless
 * descriptor bit 5 is set. */
int image_decode_tga(const uint8_t *d, size_t n, Image *out)
{
    memset(out, 0, sizeof *out);
    if (n < 18) return 0;
    int idlen = d[0], cmtype = d[1], type = d[2];
    int cmfirst = rd16(d + 3), cmlen = rd16(d + 5), cmbits = d[7];
    int w = rd16(d + 12), h = rd16(d + 14), bits = d[16], desc = d[17];
    int base = type & 7, rle = type & 8;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || base < 1 || base > 3) return 0;
    size_t p = 18 + (size_t)idlen;
    uint8_t pal[256 * 4];
    memset(pal, 0, sizeof pal);
    if (cmtype == 1) {
        int cb = (cmbits + 7) / 8;
        for (int i = 0; i < cmlen; i++, p += (size_t)cb) {
            if (p + (size_t)cb > n) return 0;
            int k = cmfirst + i;
            if (k < 0 || k > 255) continue;
            uint8_t *q = pal + k * 4;
            if (cb >= 3) { q[0] = d[p + 2]; q[1] = d[p + 1]; q[2] = d[p]; q[3] = cb == 4 ? d[p + 3] : 255; }
            else { int v = rd16(d + p); q[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); q[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
                   q[2] = (uint8_t)((v & 31) * 255 / 31); q[3] = 255; }
        }
    }
    int bpp = (bits + 7) / 8;
    if (bpp < 1 || bpp > 4 || (base == 2 && bpp < 2)) return 0;
    size_t npix = (size_t)w * h;
    uint8_t *px = malloc(npix * 4);
    if (!px) return 0;
    size_t i = 0;
    uint8_t cur[4] = { 0 };
    int run = 0, raw = 0;
    while (i < npix) {
        if (rle && !run && !raw) {
            if (p >= n) break;
            uint8_t c = d[p++];
            if (c & 0x80) { run = (c & 0x7f) + 1; if (p + (size_t)bpp > n) break; memcpy(cur, d + p, (size_t)bpp); p += (size_t)bpp; }
            else raw = c + 1;
        }
        const uint8_t *s;
        if (rle && run) { s = cur; run--; }
        else {
            if (p + (size_t)bpp > n) break;
            s = d + p;
            p += (size_t)bpp;
            if (raw) raw--;
        }
        uint8_t *q = px + i * 4;
        if (base == 1) { const uint8_t *c = pal + (s[0]) * 4; q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = c[3]; }
        else if (base == 3) { q[0] = q[1] = q[2] = s[0]; q[3] = 255; }
        else if (bpp == 2) {
            int v = s[0] | s[1] << 8;
            q[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31);
            q[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31);
            q[2] = (uint8_t)((v & 31) * 255 / 31);
            q[3] = (bits == 16 && !(v & 0x8000) && (desc & 15)) ? 0 : 255;
        } else { q[0] = s[2]; q[1] = s[1]; q[2] = s[0]; q[3] = bpp == 4 ? s[3] : 255; }
        i++;
    }
    if (i < npix) memset(px + i * 4, 0, (npix - i) * 4);
    if (!(desc & 0x20)) {                       /* bottom-up: flip */
        size_t row = (size_t)w * 4;
        uint8_t *tmp = malloc(row);
        for (int y = 0; tmp && y < h / 2; y++) {
            memcpy(tmp, px + (size_t)y * row, row);
            memcpy(px + (size_t)y * row, px + (size_t)(h - 1 - y) * row, row);
            memcpy(px + (size_t)(h - 1 - y) * row, tmp, row);
        }
        free(tmp);
    }
    out->w = w;
    out->h = h;
    out->bpp = bits;
    out->rgba = px;
    return 1;
}

int image_decode(const char *name, const uint8_t *d, size_t n, Image *out)
{
    const char *e = strrchr(name, '.');
    if (e && !_stricmp(e, ".tga")) return image_decode_tga(d, n, out);
    if (e && (!_stricmp(e, ".png") || !_stricmp(e, ".jpg") || !_stricmp(e, ".jpeg") || !_stricmp(e, ".bmp"))) {
        int w, h, c;
        memset(out, 0, sizeof *out);
        uint8_t *p = n <= 0x7fffffff ? stbi_load_from_memory(d, (int)n, &w, &h, &c, 4) : NULL;
        if (!p) return 0;
        out->w = w;
        out->h = h;
        out->bpp = c * 8;
        out->rgba = malloc((size_t)w * h * 4);         /* image_free uses free() */
        if (out->rgba) memcpy(out->rgba, p, (size_t)w * h * 4);
        stbi_image_free(p);
        return out->rgba != NULL;
    }
    return image_decode_pcx(d, n, out);
}

int image_load(const char *name, Image *out)
{
    size_t n = 0;
    uint8_t *d = res_load(name, &n);
    memset(out, 0, sizeof *out);
    if (!d) return 0;
    int ok = image_decode(name, d, n, out);
    free(d);
    return ok;
}

void image_free(Image *img)
{
    free(img->rgba);
    free(img->index);
    img->rgba = img->index = NULL;
}

void image_key(Image *img, uint8_t r, uint8_t g, uint8_t b)
{
    size_t n = (size_t)img->w * img->h;
    for (size_t i = 0; i < n; i++) {
        uint8_t *q = img->rgba + i * 4;
        if (q[0] == r && q[1] == g && q[2] == b) q[3] = 0;
    }
}

int image_write_png(const char *path, const Image *img)
{
    return stbi_write_png(path, img->w, img->h, 4, img->rgba, img->w * 4);
}
