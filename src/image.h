/* image.h - the game's image formats decoded to 32-bit RGBA: PCX (8-bit with a palette at the end, or 24-bit in three
 * planes) and TGA (uncompressed or RLE; 8-bit mapped, 15/16, 24 or 32 bits); for texture packs also PNG, JPEG and BMP.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_IMAGE_H
#define A2_IMAGE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *rgba;          /* w*h*4, top row first */
    uint8_t *index;         /* w*h palette indices for 8-bit images, else NULL */
    uint8_t pal[768];       /* RGB palette of 8-bit images */
    int bpp;                /* bits per pixel of the file */
} Image;

int  image_decode_pcx(const uint8_t *d, size_t n, Image *out);
int  image_decode_tga(const uint8_t *d, size_t n, Image *out);
int  image_decode(const char *name, const uint8_t *d, size_t n, Image *out);   /* by extension */
int  image_load(const char *name, Image *out);                                /* res_load + decode */
void image_free(Image *img);
/* colour key: pixels equal to (r,g,b) get alpha 0 */
void image_key(Image *img, uint8_t r, uint8_t g, uint8_t b);
int  image_write_png(const char *path, const Image *img);                     /* for tools / screenshots */
#endif
