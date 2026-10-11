/*
 * smktest.c - command-line test for the Smacker decoder (smk.c).
 *
 * Part of A2RacerIIRE, a reimplementation of the "A2 Racer II" engine.
 * Copyright (C) 2026 A2RacerIIRE contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage: smktest file.smk outdir [first] [count]
 *
 * Decodes every frame, prints the film properties, damaged-frame count and
 * decoding speed, writes frames first..first+count-1 (default 0..0) as
 * outdir/frame_NNNNN.ppm (Y-scaling applied) and the complete audio of
 * track 0 as outdir/audio0.wav (signed 16-bit PCM).
 */
#include "smk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, unsigned long v)
{
    put16(f, (unsigned)(v & 0xFFFF));
    put16(f, (unsigned)((v >> 16) & 0xFFFF));
}

static int write_ppm(const char *path, const uint8_t *rgb, unsigned w, unsigned h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    fwrite(rgb, 3, (size_t)w * h, f);
    return fclose(f);
}

static int write_wav(const char *path, const int16_t *pcm, size_t frames,
                     unsigned rate, unsigned ch)
{
    size_t i, n = frames * ch;
    unsigned long bytes = (unsigned long)(n * 2);
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);                       /* PCM */
    put16(f, ch);
    put32(f, rate);
    put32(f, (unsigned long)rate * ch * 2);
    put16(f, ch * 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    for (i = 0; i < n; i++)
        put16(f, (unsigned)(uint16_t)pcm[i]);
    return fclose(f);
}

int main(int argc, char **argv)
{
    smk_t *s;
    unsigned w, h, frames, usec, rate = 0, ch = 0, bits = 0, dh, t;
    int ymode, coding, r, damaged = 0, decoded = 0;
    long first = 0, count = 1;
    int16_t *pcm = NULL;
    size_t pcm_frames = 0, pcm_cap = 0;
    uint8_t *rgb;
    clock_t c0, c1;
    double secs;
    char path[1024];
    static const char *const ynames[] = { "normal", "interlaced", "doubled" };
    static const char *const anames[] = { "none", "PCM", "DPCM", "unsupported(Bink)" };

    if (argc < 3) {
        fprintf(stderr, "usage: %s file.smk outdir [first] [count]\n", argv[0]);
        return 2;
    }
    if (argc > 3) first = strtol(argv[3], NULL, 10);
    if (argc > 4) count = strtol(argv[4], NULL, 10);

    s = smk_open_file(argv[1]);
    if (!s) {
        fprintf(stderr, "%s: cannot open or not a Smacker file\n", argv[1]);
        return 1;
    }
    smk_info(s, &w, &h, &frames, &usec, &ymode);
    dh = smk_display_height(s);
    printf("%s: SMK%d %ux%u, %u frames, %u us/frame (%.3f fps), Y %s, "
           "length %.3f s\n", argv[1], smk_version(s), w, h, frames, usec,
           1e6 / usec, ynames[ymode], frames * (double)usec / 1e6);
    for (t = 0; t < SMK_MAX_TRACKS; t++) {
        unsigned tr, tc, tb;
        coding = smk_audio_info(s, (int)t, &tr, &tc, &tb);
        if (coding != SMK_AUDIO_NONE)
            printf("  audio track %u: %s, %u Hz, %u ch, %u bit\n", t,
                   anames[coding], tr, tc, tb);
    }
    coding = smk_audio_info(s, 0, &rate, &ch, &bits);

    rgb = (uint8_t *)malloc((size_t)w * dh * 3);
    if (!rgb) {
        smk_close(s);
        return 1;
    }

    c0 = clock();
    while ((r = smk_next_frame(s)) != SMK_DONE) {
        int idx = smk_frame_index(s);
        size_t n;
        const int16_t *a = smk_audio(s, 0, &n);
        decoded++;
        if (r == SMK_FRAME_DAMAGED) {
            damaged++;
            fprintf(stderr, "frame %d: damaged data\n", idx);
        }
        if (n && ch) {
            if (pcm_frames + n > pcm_cap) {
                size_t nc = (pcm_frames + n) * 2;
                int16_t *np = (int16_t *)realloc(pcm, nc * ch * sizeof *np);
                if (!np)
                    break;
                pcm = np;
                pcm_cap = nc;
            }
            memcpy(pcm + pcm_frames * ch, a, n * ch * sizeof *a);
            pcm_frames += n;
        }
        if (idx >= first && idx < first + count) {
            smk_render_rgb24(s, rgb);
            sprintf(path, "%s/frame_%05d.ppm", argv[2], idx);
            if (write_ppm(path, rgb, w, dh) != 0)
                fprintf(stderr, "cannot write %s\n", path);
        }
    }
    c1 = clock();
    secs = (double)(c1 - c0) / CLOCKS_PER_SEC;

    printf("decoded %d frames (%d damaged) in %.3f s = %.1f fps\n", decoded,
           damaged, secs, secs > 0 ? decoded / secs : 0.0);
    if (coding != SMK_AUDIO_NONE && ch) {
        double peak = 0, sum = 0;
        size_t i, clip = 0;
        for (i = 0; i < pcm_frames * ch; i++) {
            double v = pcm[i];
            if (v < 0) v = -v;
            if (v > peak) peak = v;
            if (v >= 32767) clip++;
            sum += (double)pcm[i] * pcm[i];
        }
        printf("audio track 0: %lu samples/ch = %.3f s (video %.3f s), "
               "peak %.0f, RMS %.1f, clipped %lu\n",
               (unsigned long)pcm_frames, rate ? pcm_frames / (double)rate : 0.0,
               frames * (double)usec / 1e6, peak,
               pcm_frames ? sqrt(sum / (double)(pcm_frames * ch)) : 0.0,
               (unsigned long)clip);
        sprintf(path, "%s/audio0.wav", argv[2]);
        if (write_wav(path, pcm, pcm_frames, rate, ch) != 0)
            fprintf(stderr, "cannot write %s\n", path);
    }
    free(pcm);
    free(rgb);
    smk_close(s);
    return damaged ? 1 : 0;
}
