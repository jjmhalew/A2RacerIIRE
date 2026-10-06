/* exetab.h - the original's static tables that pair meshes with textures (docs/FORMAT_D3D.md 3), read at start-up from
 * the race program spel.dat on the CD (copied with the data), so this port carries none of them:
 *   mesh names 0x48d0e0 (357), texture names 0x48e1a0 (91), frames 0x48e5e8 (386: mesh + texture),
 *   anims 0x492050 (312: a list of frames) with their names at 0x490430 ("N_GOLF_ANIM").
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_EXETAB_H
#define A2_EXETAB_H
#include <stddef.h>
#include <stdint.h>

typedef struct { int mesh, texture; } AnimFrame;
typedef struct { char name[32]; int nframes; int first; } Anim;     /* frames in ExeTables.frames[first..] */

typedef struct {
    int nmeshes, ntextures, nanims, nframes_total;
    char (*mesh)[16];          /* "mpolizei.d3d" */
    char (*texture)[16];       /* "cars01.pcx" */
    Anim *anim;
    AnimFrame *frames;
} ExeTables;

int  exetab_load(ExeTables *t);                 /* from <data>/spel.dat; 0 = not found / not the 1.01 build */
void exetab_free(ExeTables *t);
int  exetab_find_anim(const ExeTables *t, const char *name);   /* ignoring case; -1 = none (the original returns 0) */

/* a little PE reader, also for other tables */
typedef struct { uint8_t *data; size_t size; uint32_t base; int nsec; struct { uint32_t va, vsize, raw, rsize; } sec[16]; } PeFile;
int  pe_open(PeFile *pe, const char *name);
void pe_close(PeFile *pe);
const uint8_t *pe_ptr(const PeFile *pe, uint32_t va, size_t n);   /* NULL when outside the file's data */
uint32_t pe_u32(const PeFile *pe, uint32_t va);
const char *pe_str(const PeFile *pe, uint32_t va);
#endif
