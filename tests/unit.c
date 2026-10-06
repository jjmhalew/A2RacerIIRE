/* unit.c - tests that need no game data (CI: tests/run.sh): the ini reader/writer, the PCX and TGA decoders on images
 * made here, the Smacker decoder on garbage, the WAV reader.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "audio.h"
#include "image.h"
#include "ini.h"
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the few platform calls the tested files make, without a window or sound device */
#include "plat.h"
#include "res.h"
FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }
void plat_audio_start(PlatAudioFn fn, void *user) { (void)fn; (void)user; }
int plat_audio_rate(void) { return 44100; }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}
uint8_t *res_load(const char *name, size_t *size) { (void)name; (void)size; return NULL; }

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void test_ini(void)
{
    const char *t = "; comment\r\n[PLAYER]\r\nmoney=812000\r\ncar1 = 15000\t\t; Trabant\r\n\r\n[OPTIONS]\r\nNrlaps=2\r\n";
    Ini *ini = ini_parse(t, strlen(t));
    CHECK(ini_int(ini, "player", "MONEY", 0) == 812000);
    CHECK(ini_int(ini, "PLAYER", "car1", 0) == 15000);           /* GetPrivateProfileInt reads the leading number */
    CHECK(ini_int(ini, "OPTIONS", "nrlaps", 0) == 2);
    CHECK(ini_int(ini, "OPTIONS", "missing", 7) == 7);
    CHECK(ini_get(ini, "NOPE", "x") == NULL);
    ini_set_int(ini, "PLAYER", "money", 5);
    ini_set(ini, "PLAYER", "new", "abc");
    ini_set(ini, "NEWSEC", "k", "v");
    CHECK(ini_int(ini, "PLAYER", "money", 0) == 5);
    CHECK(!strcmp(ini_get(ini, "PLAYER", "new"), "abc"));
    CHECK(!strcmp(ini_get(ini, "NEWSEC", "k"), "v"));
    CHECK(ini_int(ini, "OPTIONS", "Nrlaps", 0) == 2);
    ini_free(ini);
}

static void test_pcx(void)
{
    /* 3x2 8-bit PCX, one RLE run across the line end, palette at the end */
    uint8_t f[128 + 16 + 769];
    memset(f, 0, sizeof f);
    f[0] = 10; f[1] = 5; f[2] = 1; f[3] = 8;
    f[8] = 2; f[10] = 1;                      /* xmax 2, ymax 1 */
    f[65] = 1; f[66] = 4;                     /* 1 plane, 4 bytes per line (padded) */
    uint8_t *p = f + 128;
    int n = 0;
    p[n++] = 1; p[n++] = 2; p[n++] = 0xc2; p[n++] = 3;   /* 1 2 3 3 */
    p[n++] = 4; p[n++] = 0xc1; p[n++] = 0xc5; p[n++] = 6; p[n++] = 7;   /* 4 (0xc5 escaped) 6 7 */
    p[n] = 0x0c;
    uint8_t *pal = p + n + 1;
    for (int i = 0; i < 256; i++) { pal[i * 3] = (uint8_t)i; pal[i * 3 + 1] = (uint8_t)(255 - i); pal[i * 3 + 2] = 9; }
    Image img;
    CHECK(image_decode_pcx(f, (size_t)(128 + n + 769), &img));
    CHECK(img.w == 3 && img.h == 2);
    CHECK(img.index[0] == 1 && img.index[2] == 3 && img.index[3] == 4 && img.index[4] == 0xc5 && img.index[5] == 6);
    CHECK(img.rgba[4 * 4] == 0xc5 && img.rgba[4 * 4 + 1] == 255 - 0xc5 && img.rgba[4 * 4 + 2] == 9);
    image_free(&img);
}

static void test_tga(void)
{
    uint8_t f[18 + 4 * 3];
    memset(f, 0, sizeof f);
    f[2] = 2; f[12] = 2; f[14] = 2; f[16] = 24;          /* 2x2, 24 bits, bottom-up */
    uint8_t px[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };   /* B,G,R */
    memcpy(f + 18, px, 12);
    Image img;
    CHECK(image_decode_tga(f, sizeof f, &img));
    CHECK(img.w == 2 && img.h == 2);
    /* the first stored row is the bottom one */
    CHECK(img.rgba[(1 * 2 + 0) * 4 + 0] == 3 && img.rgba[(1 * 2 + 0) * 4 + 2] == 1);
    CHECK(img.rgba[0] == 9 && img.rgba[2] == 7);
    image_free(&img);
}

static void test_wav(void)
{
    uint8_t w[44 + 4];
    memset(w, 0, sizeof w);
    memcpy(w, "RIFF", 4); memcpy(w + 8, "WAVEfmt ", 8);
    w[16] = 16; w[20] = 1; w[22] = 1; w[24] = 0x22; w[25] = 0x56;   /* PCM, mono, 22050 */
    w[34] = 8;                                                     /* 8 bits */
    memcpy(w + 36, "data", 4); w[40] = 4;
    w[44] = 128; w[45] = 255; w[46] = 0; w[47] = 129;
    Sample s;
    CHECK(sample_decode_wav(w, sizeof w, &s));
    CHECK(s.frames == 4 && s.channels == 1 && s.rate == 22050);
    CHECK(s.data[0] == 0 && s.data[1] == 127 * 256 && s.data[2] == -128 * 256);
    free(s.data);
}

static void test_smk_garbage(void)
{
    uint8_t buf[4096];
    unsigned seed = 1;
    for (int round = 0; round < 200; round++) {
        for (size_t i = 0; i < sizeof buf; i++) { seed = seed * 1103515245u + 12345u; buf[i] = (uint8_t)(seed >> 16); }
        memcpy(buf, "SMK2", 4);
        buf[4] = 64; buf[8] = 48; buf[12] = 5;            /* plausible size and frame count */
        smk_t *s = smk_open_mem(buf, sizeof buf);
        if (s) {
            for (int k = 0; k < 8 && smk_next_frame(s) != SMK_DONE; k++) {}
            smk_close(s);
        }
    }
    CHECK(1);                                             /* no crash */
}

int main(void)
{
    test_ini();
    test_pcx();
    test_tga();
    test_wav();
    test_smk_garbage();
    printf(g_fail ? "%d test(s) failed\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
