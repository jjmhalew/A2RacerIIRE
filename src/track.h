/* track.h - a track ("etappe"): the road mesh and per-chunk data of etappeN.rmp / .pmp / .cmp / .dmp
 * (docs/FORMAT_ROAD.md, the original's rd_main.cpp / rd_chunk.cpp / rd_drmap.cpp / rd_rail.cpp).
 * Coordinates are the original's: Direct3D left-handed, y up, one chunk = 128 units.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_TRACK_H
#define A2_TRACK_H
#include "gfx.h"
#include "vmath.h"

/* The engine's angle unit: math3d_init_tables 0x450280 is called with 0x1000 entries (sub_431610), which sets
 * c_angle_units 0x49b014 = 4096.0 and the mask 0x49b018 = 4095 at run time (the .data defaults 1024 / 1023 are
 * overwritten before anything uses them). Object yaw/pitch/roll, chunk and rail angles are 4096ths of a turn. */
#define ANGLE_UNITS 4096

enum { G_ROAD, G_LAND, G_RAIL, G_EXTRA, G_COUNT };   /* draw order (0x445550); the .rmp lists are road, land, extra, rail */

typedef struct {
    int range[G_COUNT][2];      /* [start, end) into the group's index buffer (chunk +0x00..+0x1c) */
    Vec3 pos;                   /* +0x24 start of the chunk on the centre line */
    Vec3 right, up, fwd;        /* +0x30 / +0x3c / +0x48 */
    int pitch, yaw;             /* +0x54 / +0x58, ANGLE_UNITS per turn */
    float plane_d;              /* +0xdc */
    int trigger[2];             /* +0xec / +0xf0 */
    int nrails, rails[12];      /* .cmp indices */
    int nwp[2], wp[2][12];      /* waypoints per side (0 = left / oncoming, 1 = right), lane order */
} Chunk;

typedef struct {
    int flags, flags2;
    Vec3 pos;
    int next, next_inner, next_outer, prev;
} Waypoint;

typedef struct {
    Vec3 p, d;                  /* start, vector to the end */
    float yaw, len;             /* ANGLE_UNITS per turn; horizontal length */
} Rail;

typedef struct {
    int index;                  /* 0..6 */
    int city;                   /* Raser.ini [MAPS] arg 0: 1 = city (laps), 0 = highway */
    char name[64];              /* "AMSTERDAM - DEN HAAG" */
    char sky[2][16];            /* [MAPS] args 6 / 7: the software modes' sky at 320x200 / 640x480 ("scrl_lo") */
    int nchunks;
    Chunk *chunks;
    int nwaypoints;
    Waypoint *wps;
    int nrails;
    Rail *rails;
    GfxMesh *mesh[G_COUNT];
    unsigned tex[G_COUNT];
    char texname[G_COUNT][16];
} Track;

int  track_load(Track *t, int etappe);     /* 0 = failed (message in stderr) */
void track_free(Track *t);
/* the original's road_find_chunk (0x445a10): the chunk whose start line point p is past, searching from `start` */
int  track_find_chunk(const Track *t, int start, Vec3 p);
int  track_wrap(const Track *t, int c);
/* road_visible_start (0x4252b0): first chunk to draw for a camera on chunk c looking along dir */
int  track_visible_start(const Track *t, int c, Vec3 dir, int behind, int visible);
/* road_draw (0x4454f0 / 0x445cf0 / 0x445550): chunks start .. start+count (inclusive), wrapping */
void track_draw(const Track *t, int start, int count, int fog);
#endif
