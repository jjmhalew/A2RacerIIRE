/* car_old.h - the old steering model of Autobahn Raser I (Game.ini [OPTIONS] stuur_model=1, docs/PHYSICS.md 7):
 * forces from axle loads, a Pacejka-like tyre curve, a torque parabola with its own automatic gearbox, drag, rolling
 * resistance, brakes (ABS) and handbrake, summed into the object's force/torque for obj_integrate. Player only.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_CAR_OLD_H
#define A2_CAR_OLD_H
#include "race.h"

void car_old_init(Race *r, int easy);                     /* the car-type record with the upgrades; g_plr_vmax */
/* plr_input_keyboard 0x4265b0, old-model branch: per-frame steps of steer (0..60, 30 straight) and gas (0..40) */
void car_old_input(Race *r, int left, int right, int up, int down);
/* handle_player_car's old branch (0x425e20) + car_drive_old 0x40cfd0 */
void car_old_drive(Race *r, int o, int input_device);
int  car_old_gear(void);
#endif
