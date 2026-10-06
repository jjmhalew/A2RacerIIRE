/* res.h - the game's files: loose files in the data dir and the resource archives (<name>.IND index + <name>.IMG
 * data, docs/FORMAT_IMG.md). Names are matched ignoring case, as on the original's file system.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_RES_H
#define A2_RES_H
#include <stddef.h>
#include <stdint.h>

void res_set_dir(const char *data_dir);        /* the folder with Game.ini, RCS.IMG, etappe0.rmp ... */
const char *res_dir(void);
void res_path(char *out, size_t cap, const char *name);   /* data_dir + name */

/* a whole loose file; NULL when missing. Free with free(). */
uint8_t *res_load_file(const char *name, size_t *size);

typedef struct ResArchive ResArchive;
ResArchive *res_open(const char *base);        /* "RCS" -> RCS.IND + RCS.IMG */
void res_close(ResArchive *a);
int res_count(const ResArchive *a);
const char *res_name(const ResArchive *a, int i);   /* "0012" for the unnamed archives */
int res_find(const ResArchive *a, const char *name);   /* index or -1, ignoring case */
uint8_t *res_read(ResArchive *a, int i, size_t *size);   /* malloc'd */

/* The settings files the original writes (Game.ini, Raser.ini, menu.ini, SLOTS.RAS, options.ras): this port keeps
 * them in the user folder (plat_user_dir), copied from the data folder the first time they are asked for, so the
 * game files stay as they came from the CD. The menu and the race exchange the career through them, as menu.dat and
 * spel.dat do (docs/GAMEFLOW.md). */
void res_user_path(char *out, size_t cap, const char *name);

/* the race archive RCS, opened on first use; then the loose files */
uint8_t *res_load(const char *name, size_t *size);

/* Mods: a "mods" folder in the user folder or next to the program (the user folder wins).
 *   mods/data/NAME       replaces the game file NAME, loose or in a named archive (RCS, Mrcs): sounds, images, ...
 *   mods/textures/NAME.png (.tga .pcx .jpg .bmp)   replaces texture NAME at any size (texture.c), alpha or the
 *                        colour key (255,0,127) for transparency; A2RacerRE --dump-textures DIR writes the originals.
 * res_mod_find puts the path of mods/SUB/NAME in out; 0 = no such file. */
int res_mod_find(char *out, size_t cap, const char *sub, const char *name);
uint8_t *res_load_path(const char *path, size_t *size);     /* a whole file by path; NULL when missing */
#endif
