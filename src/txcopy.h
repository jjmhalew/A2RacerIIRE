/* txcopy.h - the texture copies of spel.dat (txt_main.cpp, txt_reml.cpp, txt_vbrd.cpp, tr_vbord.cpp; docs/RENDER.md
 * "Texture copies"): RC sprites blitted into the 3D textures while racing - the player car's brake lights, the
 * blinking police lights, the speed-limit matrix signs and the direction signs above the highway. Game.ini [OPTIONS]
 * texturecopy=1 switches them on, together with the radar (radar.c), as g_radar_enabled 0x4d9aa4 does.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_TXCOPY_H
#define A2_TXCOPY_H
#include "race.h"

/* indices into the copy table 0x48c968 (16 x 0x58 bytes): {anim 8-bit, anim 16-bit, x, y, texture name, track type} */
enum { TXC_BRAKE = 0,          /* 0..6: brake lights of the player cars 0..6 (RACER01/02), frames 2*car (on), +1 (off) */
       TXC_SPEED = 7,          /* speed-limit matrix sign (a_TRFFC1), highway */
       TXC_SIGN_A = 8,         /* 8..10: direction sign at (9,122) of etappe 1/3/5 (triggers 22..30) */
       TXC_SIGN_B = 11,        /* 11..13: direction sign at (9,7) of etappe 1/3/5 (triggers 31..39) */
       TXC_SIREN = 14,         /* police lights (CARS01), frames 0 / 1 */
       TXC_BEACON = 15,        /* the ADAC car's beacon; adac_beacon_update 0x40c4a0 never runs (its flag is never set) */
       TXC_COUNT = 16 };

/* init_game's part (sub_42b060 0x42b060, the check_texture_copy test 0x424032): reads Game.ini texturecopy, finds the
 * textures of the copies that belong to this track type and puts them back as they are on the CD */
void txcopy_init(const Race *r);
void txcopy_free(void);
int  txcopy_enabled(void);                     /* g_radar_enabled 0x4d9aa4 */
/* txt_blit_anim 0x42ae90: frame `frame` of the copy's anim into its texture; nothing when texture copying is off */
void txcopy_blit(int index, int frame);
/* the brake lights of handle_player_car 0x425f0b: set_brake_light_texture 0x42b100 when Race.brake_light changes */
void txcopy_brake_lights(const Race *r);
/* the texture part of the player's track triggers: speed limits 4/5/6/8 (0x42a340.., set_speed_sign_texture
 * 0x42b190) and the direction signs 22..39 (tr_vbord.cpp, handler table 0x48c488) */
void txcopy_trigger(const Race *r, int id);
#endif
