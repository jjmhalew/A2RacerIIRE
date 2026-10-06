/* ai.h - the computer-controlled cars of the race (spel.dat ai_*.cpp, Car_main.cpp, Car_cops.cpp, ev_cops.cpp;
 * docs/AI.md): traffic on the waypoint graph (HANDLE_TRAFFIC_AI 0x40b770, change_lane, handle_basic_ai), the three CPU
 * racers (the state machines of ai_fsm.cpp / ai_path.cpp, or the old basic-AI racers with Game.ini ai_model=1), the
 * police (type-9 .omp cars: speeding check, pursuit, arrest and its game action), the waypoint obstacle lists and the
 * object handling window of the engine, the traffic lights, and the race position of the player.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_AI_H
#define A2_AI_H
#include "ini.h"
#include "race.h"

/* set-up, in the original's order: the cops while the track loads (road_load_omp 0x4451d0), the player
 * (init_player_car 0x4256a0), then traffic, racers, FSMs, cop targets and the ADAC car (init_all_cars 0x410b60) */
void ai_init_cops(Race *r);
int  ai_init_player(int car, int (*handler)(int o), int (*collide)(int o));   /* -> the player object */
int  ai_start_chunk(void);                     /* g_startchunk 0x4d9a88 */
void ai_init_cars(Race *r);
void ai_free(void);

/* add_car 0x410bc0: a car of g_car_types[car_type] in lane/side of chunk, facing along the road (dir 1) or against
 * it (dir 0); list_type 1 player, 2 traffic, 3 racer, 4 cop, 5 ADAC */
int  add_car(int car_type, int chunk, int lane, int side, int dir, int (*handler)(int o), int (*collide)(int o),
             int list_type);

/* engine hooks of obj_move_all / rceng_handle_objects */
int  ai_obj_in_window(int o);                  /* chunk within player_chunk +-2*visiblechunks, or always updated */
int  ai_chunk_in_window(int chunk);            /* the same window test for a chunk */
void ai_wp_remove(int o);                      /* rceng_remove_object_in_waypoint_obstacle_list 0x442520 */
void ai_wp_add(int o);                         /* rceng_add_object_in_waypoint_obstacle_list 0x4424b0 */

/* per tick after obj_move_all: traffic lights, traffic recycling, game actions (arrest, game over), the FSMs */
void ai_tick(void);
/* game_action_start 0x424d20 for the actions the AI uses: 0 driving, 6 game over, 8 arrest */
void ai_game_action_start(int action);
/* the old cop model drives the player car while it arrests it (player_autopilot_on 0x4258f0); 1 = done here */
int  ai_plr_autopilot(int o);

int  ai_difficulty(void);                      /* g_difficulty 0x4d9ab0: Game.ini [OPTIONS] Difficulty, 0 easy / 1 hard */
/* g_visible_chunks 0x4d9a8c: Game.ini [OPTIONS] visiblechunks (10..50, CD 30): the chunks drawn (road, objects) and
 * the AI's handling and traffic windows */
int  ai_visible_chunks(void);
/* the frame-rate governor of handle_game 0x424c98, once per drawn frame with its real duration: below 10 fps while
 * racing (g_game_event 0) visiblechunks drops by one, not below 25 (never rises again) */
void ai_frame_rate_governor(double frame_seconds, int racing);
int  ai_player_position(void);                /* g_player_position 0x4d9e60: racers ahead of the player (0 = leading) */
/* the race end (sub_422d40): extrapolates unfinished racers (racers_finish_estimate 0x4114a0) and writes
 * Game.ini [RACERS] n=car,position,time and count; player_position = the player's place when crossing the line */
void ai_save_racers(Ini *game_ini, int player_position);

/* sound hooks for src/audio.c (NULL = silent) */
extern void (*ai_snd_object)(int obj, int sample, int on);   /* an object's looping sample: engine, siren (40) */
extern int  (*ai_snd_play)(int sample);                      /* a one-shot (police voices); returns a handle */
extern int  (*ai_snd_playing)(int handle);
#endif
