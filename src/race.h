/* race.h - the race (spel.dat): objects and their physics (obj_main.cpp), the player car (plr_car.cpp, the new
 * steering model of car_drive_new 0x40fc30), cameras (cam_main.cpp) and the frame loop (handle_game 0x424a70).
 * docs/PHYSICS.md, docs/INPUT.md, docs/RENDER.md.
 *
 * Timing (PORT): the original runs one variable step per drawn frame, and its handling depends on the frame rate
 * (lateral grip x0.8 per frame, the keyboard brake only builds up below 40 fps, ...). Here the game logic runs at a
 * fixed RACE_HZ like a 1998 PC; the picture is drawn at the display's rate, interpolated between the last two ticks.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_RACE_H
#define A2_RACE_H
#include "objects.h"
#include "track.h"
#include "vmath.h"

#define RACE_HZ 30
#define MAX_OBJS 512                /* the cars and the bodies of the .omp objects (up to 152 per track, objects.c) */

/* the original's object (0xa0 bytes) and its 3D instance (0x99 bytes) in one */
typedef struct Obj {
    int used, active, physics;      /* active = +0x34 "on"; physics: 1 = integrated by obj_move_all */
    int anim, frame, visible;       /* visible = +0x2c */
    Vec3 pos;                       /* instance +0x50: world units (1/10 m) */
    float pitch, yaw, roll;         /* instance +0x74/+0x78/+0x7c: ANGLE_UNITS per turn */
    int chunk, prev_chunk;          /* +0x18 / +0x1c */
    int important;                  /* +0x04: player, racers, cops (side -1 in the waypoint obstacle lists) */
    int ai;                         /* +0x0c: t_ai_state index (src/ai.c), -1 = none */
    int sample;                     /* +0x14: the looping sample of the object (engine, siren 40) */
    int waypoint;                   /* +0x20: waypoint whose obstacle list holds the object */
    int side;                       /* +0x28: carriageway 0 / 1 */
    float inv_mass, inv_inertia, restitution;
    Vec3 vel;                       /* +0x6c: local m/s, x lateral, y vertical, z forward */
    float pitch_vel, yawrate, roll_vel;   /* +0x78 / +0x7c (rad/s) / +0x80 */
    Vec3 force;
    float torque;
    int no_vel_rotate;              /* +0x38 "on track": AI driving, the velocity turns with the body (no drift) */
    int in_collision, always_update;     /* +0x3c / +0x40 */
    int car_type;                   /* g_car_types index, -1 = not a car */
    int (*update)(int o);           /* +0x44 */
    int (*collide)(int o);          /* +0x48, called on the first sub-step of a contact */
    /* drawing: the previous tick, for interpolation */
    Vec3 draw_pos0, draw_pos1;
    float draw_yaw0, draw_yaw1;
} Obj;

typedef struct {
    float accel[2][6], vmax[2][6];
    float shift_up, shift_down, unused, brake, engine_brake, roll_gain, steer;
} CarsDat;
typedef struct { float roll_damp, roll_factor, pitch_damp, pitch_factor, height_damp, height_factor; } CarsBody;

typedef struct {
    char anim[20];
    float mass, length, width, cg_front, cg_rear;
} CarType;

typedef struct Race {
    Track track;
    ObjectSet objects;
    int etappe;
    Obj obj[MAX_OBJS];
    int nobj;
    CarsDat cars[10];
    CarsBody body[10];
    CarType car_types[16];
    /* player (plr_car.cpp globals) */
    int plr_obj, plr_car;
    float steer, gas;               /* g_plr_steer / g_plr_gas */
    int brake, handbrake, turbo_active, turbo;   /* g_plr_brake is an int */
    int gear, skid, brake_light, stop_latch, forced_brake;
    float rpm;
    float hb_time, hb_rev_time, hb_speed, hb_heading;
    int hb_sliding;
    int upg_motor, upg_banden, upg_abs, upg_turbo, upg_gearbox;
    int speed_kmh, progress, damage;
    int race_started, control;
    int game_event;
    /* camera */
    int cam_mode, cam_saved_mode;
    Vec3 cam_pos, cam_prev, cam_target;
    Vec3 cam_draw0, cam_draw1, tgt_draw0, tgt_draw1;
    int cam_chunk;
    int ev_cam;                     /* the arrest camera is placed */
    /* timing */
    double accum, last_time;
    float dt;
    unsigned tick;
    int detail;                     /* overlay detail 0..3 (D) */
    int input_keys[8];              /* Game.ini [INPUT] LEFT RIGHT UP DOWN EXTRA1 EXTRA2 */
    int input_device;               /* Game.ini [INPUT] DEVICE 1..4 (the old model's traction quirk) */
    int stuur_model;                /* Game.ini [OPTIONS] stuur_model: 0 new, 1 old (car_old.c) */
    float test_gas, test_steer;     /* --drive: hold gas / steer (tests) */
    unsigned sky_tex;
    int start_chunk;
    float race_ms;                  /* timer 3: race time (rules.c), for the racers' finish times */
    /* set by src/ai.c and src/collide.c */
    int hud_message;                /* g_hud_message 0x4882c0: language.ini text to show (item 76), -1 = none */
    int hud_msg_shown;              /* 0x4882c4: the latched message */
    float hud_msg_ms;               /* when it disappears (ms of game time) */
    int event_running;              /* g_event_running 0x4d9aa0: the dashboard is not drawn (arrest, game over) */
    int game_over_reason;           /* 0x4867a4: 0 total loss, 1 time up, 2 arrested (texts 40 / 41 / 42) */
    int collision_off;              /* Game.ini [OPTIONS] collisionoff */
    Vec3 plr_pos;                   /* g_plr_pos 0x4d9e48, copied at the start of handle_player_car */
} Race;

/* shared with src/ai.c and src/collide.c (obj_main.cpp / the car physics) */
int   obj_add(int anim, Vec3 pos, float yaw);
void  obj_init_physics(Obj *o, float mass, float w, float l, float restitution);
float chunk_height_at(const Chunk *c, float x, float z);
void  car_axes(const Obj *c, Vec3 *right, Vec3 *up, Vec3 *fwd);
void  car_body_roll(int o);
void  car_body_pitch(int o);
void  car_body_height(int o);
float car_road_height_reset(int o);
float race_sin(int a);              /* the SIN / COS tables, index & (ANGLE_UNITS - 1) */
float race_cos(int a);

int  race_init(Race *r, int etappe, int car);
void race_free(Race *r);
/* advances the race by the real time since the last call and draws a frame; 0 = left the race */
int  race_frame(Race *r, double now);
#endif
