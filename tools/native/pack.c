/* pack.c - makes the single-file build for your own use (make_standalone.bat): the launcher stub (bundle.c) +
 * A2RacerRE.exe + the files of the manifest (src/datafiles.h) from your copy of the CD's data folder, in the payload
 * layout bundle.c documents. The result holds the game's files: it is for your own machine only, never share it.
 * Usage: pack <stub.exe> <A2RacerRE.exe> <data folder> <out.exe>
 * Build: zig cc -std=c99 -O2 -o pack.exe tools/native/pack.c
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/datafiles.h"

#define NFILES ((int)(sizeof DATA_FILES / sizeof DATA_FILES[0]))

static int append(FILE *o, FILE *f, uint64_t *size)
{
    static unsigned char buf[4 << 20];
    size_t n;
    *size = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        if (fwrite(buf, 1, n, o) != n) return 0;
        *size += n;
    }
    return 1;
}

static int append_path(FILE *o, const char *path, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "pack: cannot open %s\n", path); return 0; }
    int ok = append(o, f, size);
    fclose(f);
    return ok;
}

static void put(FILE *o, const void *p, size_t n) { fwrite(p, 1, n, o); }

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: pack <stub.exe> <A2RacerRE.exe> <data folder> <out.exe>\n"); return 2; }
    FILE *o = fopen(argv[4], "wb");
    if (!o) { fprintf(stderr, "pack: cannot write %s\n", argv[4]); return 1; }
    static uint64_t offs[NFILES + 1], sizes[NFILES + 1];
    static char names[NFILES + 1][80];
    uint64_t sz;
    int n = 0;
    if (!append_path(o, argv[1], &sz)) return 1;
    offs[0] = (uint64_t)_ftelli64(o);
    if (!append_path(o, argv[2], &sizes[0])) return 1;              /* entry 0: the game, as the launcher runs it */
    snprintf(names[n++], sizeof names[0], "A2RacerRE.exe");
    for (int i = 0; i < NFILES; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", argv[3], DATA_FILES[i].name);
        FILE *f = fopen(p, "rb");
        if (!f) {
            if (!DATA_FILES[i].required) continue;                  /* the installer's files, never read */
            fprintf(stderr, "pack: %s is missing (a required game file)\n", p);
            fclose(o);
            remove(argv[4]);
            return 1;
        }
        offs[n] = (uint64_t)_ftelli64(o);
        int ok = append(o, f, &sizes[n]);
        fclose(f);
        if (!ok) { fclose(o); remove(argv[4]); return 1; }
        if (sizes[n] != DATA_FILES[i].size)
            fprintf(stderr, "pack: %s differs from the Dutch 1.01 CD (%llu bytes, not %u)\n", p,
                    (unsigned long long)sizes[n], DATA_FILES[i].size);
        snprintf(names[n++], sizeof names[0], "data/%s", DATA_FILES[i].name);
    }
    uint64_t ioff = (uint64_t)_ftelli64(o);
    for (int i = 0; i < n; i++) {
        uint32_t nl = (uint32_t)strlen(names[i]);
        put(o, &nl, 4);
        put(o, names[i], nl);
        put(o, &offs[i], 8);
        put(o, &sizes[i], 8);
    }
    uint32_t count = (uint32_t)n;
    uint64_t id = (uint64_t)time(NULL) * 1000003u ^ ioff;
    put(o, &ioff, 8);
    put(o, &count, 4);
    put(o, &id, 8);
    put(o, "A2RCPK01", 8);
    if (fclose(o)) { fprintf(stderr, "pack: write error\n"); return 1; }
    printf("pack: %s - %u files, %.0f MB\n", argv[4], count, (double)ioff / (1 << 20));
    return 0;
}
