/* hud.h - the in-race 2D overlay of the 640x480x16 mode (docs/HUD.md): RC.IMG sprites and .fnt fonts painted in the
 * e02_pl16 palette, the language.ini texts, the layout of g_hud_layout row 1 (0x488510) in a 640x480 screen.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_HUD_H
#define A2_HUD_H

int  hud_init(void);                 /* after models_init (reads tables from spel.dat); 0 = failed */
void hud_free(void);
const char *hud_text(int n);         /* language.ini text n (0..55) of the LANGUAGE set */

/* primitives in the 640x480 screen, between gfx_2d_begin(640, 480) and gfx_2d_end */
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
#endif
