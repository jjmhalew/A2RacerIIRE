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
#define MAX_OBJS 256

/* the original's object (0xa0 bytes) and its 3D instance (0x88 bytes) in one */
typedef struct Obj {
    int used, active, physics;      /* physics: 1 = integrated by obj_move_all */
    int anim, frame, visible;
    Vec3 pos;                       /* instance +0x50: world units (1/10 m) */
    float pitch, yaw, roll;         /* instance +0x74/+0x78/+0x7c: 1024 per turn */
    int chunk, prev_chunk;          /* +0x18 / +0x1c */
    float inv_mass, inv_inertia, restitution;
    Vec3 vel;                       /* +0x6c: local m/s, x lateral, y vertical, z forward */
    float pitch_vel, yawrate, roll_vel;   /* +0x78 / +0x7c (rad/s) / +0x80 */
    Vec3 force;
    float torque;
    int no_vel_rotate, in_collision, always_update;
    int car_type;                   /* g_car_types index, -1 = not a car */
    int (*update)(int o);
    void (*collide)(int o);
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

typedef struct {
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
    /* timing */
    double accum, last_time;
    float dt;
    unsigned tick;
    int detail;                     /* overlay detail 0..3 (D) */
    int input_keys[8];              /* Game.ini [INPUT] LEFT RIGHT UP DOWN EXTRA1 EXTRA2 */
    int quit;
    float test_gas, test_steer;     /* --drive: hold gas / steer (tests) */
} Race;

int  race_init(Race *r, int etappe, int car);
void race_free(Race *r);
/* advances the race by the real time since the last call and draws a frame; 0 = left the race */
int  race_frame(Race *r, double now);
#endif
