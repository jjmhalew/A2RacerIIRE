/* hud.h - the in-race 2D overlay (docs/HUD.md): RC.IMG sprites and .fnt fonts, the language.ini texts and the layout
 * g_hud_layout (0x488510). The 640x480x16 mode uses row 1 (640x480 coordinates), the e02_pl16 palette and the hi
 * fonts; the 8-bit modes (hud_set_mode) row 0 (320x200 coordinates, 320 px to the right in 640x480x8), the lo fonts
 * and the etappe's 8-bit palette.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_HUD_H
#define A2_HUD_H
#include <stdint.h>

int  hud_init(void);                 /* after models_init (reads tables from spel.dat); 0 = failed */
void hud_free(void);
/* the mode (screen.h, may come before hud_init): bit8 = g_8bit (the set), res_index = g_res_index, pal8 = the 8-bit
 * palette (768 bytes; a new one makes the 8-bit set's textures again) */
void hud_set_mode(int bit8, int res_index, const uint8_t *pal8);
const char *hud_text(int n);         /* language.ini text n (0..55) of the LANGUAGE set */
int  hud_item_anim(int item);        /* the RC anim of a layout item in the current set, -1 = a text item */
int  hud_item_frames(int item);      /* the frame count of its anim, 0 = none */

/* primitives in the original's screen (scr_w x scr_h), between scr_2d_begin and gfx_2d_end */
void hud_sprite(int anim, int frame, int x, int y, int at_hotspot);        /* hud_draw_item 0x41c0f0 */
void hud_item(int item, int frame);                                        /* sprite of layout item */
/* the left `part` (0..1) of a layout item's sprite, at least 2 pixels (the loading bar, gfx_pbar.cpp 0x41c4c0) */
void hud_item_part(int item, int frame, float part);
int  hud_text_width(int font, const char *s);
void hud_print(int font, int x, int y, int center, const char *s);         /* hud_draw_text 0x41bfc0 */
void hud_item_text(int item, int font, int center, const char *s);
int  hud_font_height(int font);      /* the height of 'A' (the line height of ascii_printf) */
/* one pixel row (0 = top) of hud_print(font, x, y, 0, s), drawn at y + row (the game-over scanline sweep) */
void hud_print_row(int font, int x, int y, int row, const char *s);
void hud_item_pos(int item, int *x, int *y);
int  hud_item_rect(int item, int *x, int *y, int *w, int *h);              /* where its first frame lies; 0 = none */

/* for blitting into textures (txcopy.c, radar.c): the pixels of frame `frame` of RC anim `anim` as RGBA in the current
 * set's palette (e02_pl16, or the 8-bit one), index 255 transparent (alpha 0); malloc'd, NULL = no such frame.
 * hud_pal_rgba: one palette colour. */
uint8_t *hud_sprite_rgba(int anim, int frame, int *w, int *h);
void hud_pal_rgba(int index, uint8_t *rgba);
#endif
