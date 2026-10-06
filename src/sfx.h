/* sfx.h - the race's sounds (docs/SOUND.md): the 86-entry sample table g_sndfx_table 0x48baf0 (read from spel.dat),
 * 2D one-shots and loops, the player car's engine / tyre squeal / rattle loops, and the positional sounds of other
 * objects relative to the player car (snd_main.cpp). Volumes are the original's DirectSound values in 1/100 dB, made
 * quieter by Raser.ini [SOUND] SFXVOLUME (an attenuation: 0 = no sound effects).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_SFX_H
#define A2_SFX_H
#include "vmath.h"

enum {
    SFX_TICK = 10, SFX_FLITS = 11, SFX_PIONB1 = 16, SFX_PIONB3 = 17, SFX_PAAL = 18, SFX_JUICH = 21, SFX_MOTOR = 23,
    SFX_TOEREN1 = 24, SFX_CARSOFT = 25, SFX_CARMID = 27, SFX_CARHARD = 29, SFX_AUTO1 = 31, SFX_RAMMEL = 37,
    SFX_RAMMEL2 = 38, SFX_SLIPLOOP = 39, SFX_SIRENE = 40, SFX_TOETER = 41, SFX_GETSET = 42, SFX_GO = 43,
    SFX_VLIEGT = 44, SFX_TREIN = 45, SFX_HELI = 46, SFX_REPAIR = 47, SFX_COP1 = 48, SFX_COP2 = 54, SFX_COP3 = 59,
    SFX_ACHTER = 64, SFX_OPGEV = 77, SFX_SCHUUR = 82, SFX_CHECKP = 83, SFX_TURBO = 84, SFX_TOEREN2 = 85, SFX_COUNT = 86
};

void sfx_init(void);                     /* the table, Raser.ini [SOUND] ON / SFXVOLUME, Game.ini 3dsound */
void sfx_free(void);
void sfx_stop_all(void);
void sfx_pause(int paused);              /* the pause dialog mutes the mixer */
int  sfx_play(int idx);                  /* sndfx_play_sample 0x4286a0: 2D, volume -500 - SFX; returns a voice */
int  sfx_play_vol(int idx, int vol);     /* 2D at a given 1/100 dB volume (police speech: -100) */
int  sfx_loop(int idx);                  /* sndfx_play_sample_loop 0x428700 */
void sfx_stop(int voice);
int  sfx_playing(int voice);
/* the player's car, once per tick */
void sfx_engine(float rpm, int motor_upgrade);            /* plr_snd_engine_update 0x427070 */
void sfx_squeal(int skid);                                /* 0x4259a0 */
void sfx_rattle(int damage);                              /* 0x48b440: >= 65, >= 75 */
void sfx_turbo(int on);
/* positional (sndfx_play_object_sample 0x428bc0): key identifies the object; call every tick while it sounds */
void sfx_listener(Vec3 pos, Vec3 right);                  /* the player car: position and right-hand direction */
int  sfx_object(int key, int idx, Vec3 src, int loop);    /* 0 = out of range (stopped) */
void sfx_object_stop(int key);
#endif
