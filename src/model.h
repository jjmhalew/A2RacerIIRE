/* model.h - the 3D models: a .d3d mesh (docs/FORMAT_D3D.md) with the texture its frame pairs it with, as a GPU mesh;
 * and anims (lists of such frames) by name, as the track files and the game code use them ("MPOLIZEI_ANIM").
 * Meshes are pre-lit (the original loads every one as D3DLVERTEX with white colour): the picture is the texture.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_MODEL_H
#define A2_MODEL_H
#include "exetab.h"
#include "gfx.h"
#include "vmath.h"

typedef struct {
    GfxMesh *mesh;
    Vec3 min, max;              /* bounds in model space */
    float radius;               /* largest vertex distance from the origin (stands in for the mesh +0x74 radius) */
    int nverts, nfaces;
} Model;

int  models_init(void);                         /* reads the tables from spel.dat; 0 = failed */
void models_free(void);
const ExeTables *models_tables(void);
int  anim_find(const char *name);               /* -1 = unknown */
int  anim_frames(int anim);
/* the model of frame `frame` of anim (loaded on first use); NULL when it cannot be loaded */
const Model *anim_model(int anim, int frame);
const Model *model_get(int mesh, int texture);
/* draws the model at pos, turned by yaw (radians, the original's convention: front +z turns towards +x) */
void model_draw(const Model *m, Vec3 pos, float yaw);
void model_draw_matrix(const Model *m, const Mat4 *world);
/* the original's object placement (FORMAT_OBJECTS.md 2) as a matrix in its left-handed space */
Mat4 model_place(Vec3 pos, float yaw);
#endif
