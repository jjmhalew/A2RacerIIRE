/* rules.h - the race rules of spel.dat (docs/GAMEFLOW.md 4-5): game states and actions (ev_main.cpp), the timers
 * (gm_clock.cpp), start camera and countdown, the track triggers of the player (speed cameras, speed limits, quick
 * service, start, laps and finish, time checks), the highway time limit, damage and total loss, police stops, game
 * over, results and prize money, pause / quit, and the exchange with the menu through Game.ini.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_RULES_H
#define A2_RULES_H
#include "ini.h"
#include "race.h"

enum {                          /* g_game_state 0x4d9a7c */
    GS_RACING = 0, GS_START = 1, GS_FINISHED = 3, GS_QUIT = 4, GS_GAMEOVER = 6, GS_COPSTOP = 8
};

/* what the rules need from the rest of the race; filled by race.c (NULL = not available) */
typedef struct {
    int  (*player_position)(void);                       /* 0-based position among player + 3 racers */
    void (*save_racers)(Ini *game_ini, int player_position);   /* Game.ini [RACERS] with the finish estimates (ai.c) */
} RulesHooks;

/* The arrest (state 8) and its game over, and the game over of a wrecked car, are started by src/ai.c through
 * Race.game_event; rules_tick follows them. Race.hud_message (set by ai.c) is shown like rules_message. */

void rules_init(Race *r, const RulesHooks *hooks);        /* after the track and the player car are set up */
void rules_free(void);
void rules_tick(Race *r);                                 /* once per game tick, after the objects moved */
void rules_player_chunk_changed(Race *r, int prev, int cur);   /* rceng_check_chunk_triggers 0x442590 for the player */
void rules_radar_bonus(Race *r);                          /* a speed-camera pole knocked over (0x428d30) */
void rules_message(int text);                             /* flash a language.ini text for 2 s (0x4882c0) */
/* the camera while the rules own it (start fly-in, finish, game over, police stop); 0 = the player's camera */
int  rules_camera(Race *r, Vec3 *eye, Vec3 *target);
void rules_draw_3d(Race *r);                              /* the countdown letters (gfx_3dft.cpp) */
void rules_draw_hud(Race *r);                             /* the rules' part of the overlay (HUD.md 4) */
int  rules_hud_on(void);                                  /* the dashboard is drawn (states 0 and 1) */
int  rules_paused(void);                                  /* the pause / quit dialog is open: no game ticks */
int  rules_done(void);                                    /* the race program would end now (state 4) */
/* the HUD's numbers */
int  rules_race_ms(void);
int  rules_lap_ms(void);
int  rules_lap(void);
int  rules_nrlaps(void);
int  rules_lap_record(void);
int  rules_race_record(void);
int  rules_last_lap(void);
int  rules_position(void);
void rules_keys(Race *r);                                 /* check_game_keys 0x424ee0 (P, Esc, T, cheats); every frame */
/* the loading screen of etappe (Raser.ini [wait_hi] with .tga) with the bar at load step 0..10 (Raser.ini [progress]) */
void rules_loading_screen(int etappe, int step);
#endif
