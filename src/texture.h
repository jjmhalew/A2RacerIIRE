/* texture.h - textures by file name, loaded once from RCS. The original loads the .pcx (8-bit modes) or, in the
 * 640x480x16 mode, the .tga with the same name where the CD has one (docs/FORMAT_IMAGES.md); this port always draws
 * in true colour, so it takes the .tga when there is one.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_TEXTURE_H
#define A2_TEXTURE_H
#include "image.h"
#include <stdint.h>

typedef struct {
    unsigned id;
    int w, h;
} Texture;

/* name with or without extension ("e00_weg.pcx", "e00_weg"); NULL when neither file exists */
const Texture *tex_get(const char *name);
/* the decoded image (RGBA) of name, as tex_get picks it; 0 = missing. Free with image_free. */
int tex_image(const char *name, Image *out);
void tex_free_all(void);
/* Game.ini [GFX OPTIONS] gamma=1 ("dark", the CD's setting): every colour channel c -> c*c/256 when an image is
 * converted to the screen format (pformat_set_gamma 0x468220), except the sky backdrop */
void tex_set_gamma(int on);
void tex_apply_gamma(uint8_t *rgba, int npixels);
#endif
