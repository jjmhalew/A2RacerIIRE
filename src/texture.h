/* texture.h - textures by file name, loaded once from RCS. The original loads the .pcx (8-bit modes) or, in the
 * 640x480x16 mode, the .tga with the same name where the CD has one (docs/FORMAT_IMAGES.md); this port always draws
 * in true colour, so it takes the .tga when there is one.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_TEXTURE_H
#define A2_TEXTURE_H
#include "image.h"

typedef struct {
    unsigned id;
    int w, h;
} Texture;

/* name with or without extension ("e00_weg.pcx", "e00_weg"); NULL when neither file exists */
const Texture *tex_get(const char *name);
/* the decoded image (RGBA) of name, as tex_get picks it; 0 = missing. Free with image_free. */
int tex_image(const char *name, Image *out);
void tex_free_all(void);
#endif
