/* screen.h - the display modes of Raser.ini [PROGRAM] RESOLUTION (set_game_display, the start of init_game 0x423d20,
 * jump table 0x4243d4; docs/RENDER.md 1, HUD.md 1):
 *   0: 320x200x8  - g_res_index 0, g_8bit 1, g_hw_accel 0 (the software renderer, D3D RAMP)
 *   1: 640x480x8  - g_res_index 1, g_8bit 1, g_hw_accel 0
 *   2, 3: 640x480x16 - g_res_index 1, g_8bit 0, g_hw_accel 1 (HAL, the "3D Kaart" of the menu)
 * The 8-bit modes show everything through the etappe's 8-bit palette (e0N_pal.pcx), use the .pcx textures, the
 * 320x200 overlay layout and fonts, the scrolling 2D sky, nearest filtering and TEXTUREMAPBLEND COPY, and have no
 * fog, smoke, object lights or blending.
 * PORT: an 8-bit mode is drawn at its own size into an offscreen target and scaled up into the window with nearest
 * filtering, in a 4:3 rectangle as a monitor of the time showed it; the 16-bit mode draws at the window's size.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_SCREEN_H
#define A2_SCREEN_H
#include <stdint.h>

/* at the start of a race (its loading screen and race_init): reads the mode and the etappe's palette, and switches
 * the textures (texture.h) and the overlay (hud.h) to it */
void scr_init(int etappe);
int  scr_resolution(void);           /* the Raser.ini value, g_ini_resolution 0x4895b0 */
int  scr_res_index(void);            /* g_res_index 0x4d9a6c: 0 = 320x200, 1 = 640x480 */
int  scr_8bit(void);                 /* g_8bit 0x4d9a70 */
int  scr_hw3d(void);                 /* g_hw_accel 0x4d9aac */
int  scr_w(void);                    /* the original's screen, the overlay's coordinates: 320x200 or 640x480 */
int  scr_h(void);
const uint8_t *scr_palette(void);    /* the 8-bit palette (load_game_palette 0x420900), 768 bytes RGB */

/* one frame: gfx_begin on the offscreen target (8-bit modes) or on the window; *w x *h = the size drawn to */
void scr_frame_begin(float r, float g, float b, int *w, int *h);
void scr_frame_size(int *w, int *h);
void scr_frame_end(void);            /* the target scaled up into the window; then the window is the drawing size */
/* gfx_2d_begin for the overlay: scr_w x scr_h */
void scr_2d_begin(void);
/* a point of the overlay (scr_w x scr_h) in window pixels, as scr_frame_end shows it */
void scr_to_window(float x, float y, float *sx, float *sy);
#endif
