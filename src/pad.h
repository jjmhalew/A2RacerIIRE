/* pad.h - the port's controller layer: the race's actions mapped onto the elements of every game controller and
 * joystick plat.h has open (PORT: the original only knows keyboard keys and a joystick's fixed buttons, INPUT.md).
 * The mapping lives in controller.ini in the user folder (written with commented defaults when it is not there):
 *
 *   [race]   steer_left = LEFTX- DPLEFT      one or more elements per action, separated by spaces
 *   [tuning] stick_deadzone = 15 ...         dead zones, steering response, rumble
 *   [touch]  show = auto ...                 the on-screen touch controls of the race (touch.h)
 *
 * Elements: A B X Y LB RB LT RT LS RS BACK START GUIDE DPUP DPDOWN DPLEFT DPRIGHT MISC1 PADDLE1..4 TOUCHPAD and the
 * PlayStation names CROSS CIRCLE SQUARE TRIANGLE L1 R1 L2 R2 L3 R3 SHARE OPTIONS for game controllers; the stick
 * halves LEFTX- LEFTX+ LEFTY- LEFTY+ RIGHTX- ... ; for raw joysticks (wheels, pedals, flight sticks) axisN+ axisN-
 * (one half), axisN (the whole range as 0..1: a pedal), buttonN, hatNup hatNdown hatNleft hatNright (N from 0).
 * A ~ in front inverts an axis (~axis2: a pedal that rests at the top). The input of all devices is merged: an action
 * takes the strongest of its elements on any device.
 *
 * Buttons and hats are digital and drive the car like the keys do (DEVICE 2, the original's joypad); stick halves,
 * triggers and raw axes are analogue (DEVICE 3: steering is the position, gas / brake the pressure).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_PAD_H
#define A2_PAD_H
#include <stddef.h>

enum { PA_STEER_LEFT, PA_STEER_RIGHT, PA_GAS, PA_BRAKE, PA_TURBO, PA_HANDBRAKE, PA_LOOK_BACK, PA_CAMERA, PA_HORN,
       PA_PAUSE, PA_COUNT };
#define PAD_MAX_BINDS 4

void  pad_load(void);                       /* (re)read controller.ini (done by the first call of anything below) */
int   pad_save(void);                       /* write the [race] mapping back, keeping the file's comments */
const char *pad_action_key(int action);     /* its name in controller.ini: steer_left, gas, ... */

int   pad_digital(int action);              /* a button / hat of the action is held */
float pad_analog(int action);               /* 0..1: its strongest axis / trigger, after the dead zones */
int   pad_held(int action);                 /* digital, or analogue past half way */
int   pad_pressed(int action);              /* pad_held went on since the previous plat_poll */
float pad_steer(void);                      /* -1..1 from the analogue steering elements, with the steering curve */
/* Game.ini [INPUT] SENSITIVITY 0..100 (the menu's Gevoeligheid slider): the original's 31-entry steering curve
 * shapes the analogue steering instead of controller.ini's steer_linearity; -1 = off (steer_linearity).
 * controller.ini's steer_sensitivity multiplies either. */
void  pad_set_steer_curve(int sensitivity);

/* the race's touch controls (touch.c) as one more device: digital[a] / analog[a] 0..1 per action, set once per frame
 * (pad_pressed compares with the previous frame's); NULL, NULL = no touch input */
void  pad_set_touch(const float *digital, const float *analog);

/* controller.ini [touch]: the on-screen controls of the race */
enum { TOUCH_SHOW_OFF, TOUCH_SHOW_AUTO, TOUCH_SHOW_ON };
typedef struct {
    int show;                               /* TOUCH_SHOW_*: auto = after the first touch, until a key / pad is used */
    int size, opacity;                      /* percent */
    int drag;                               /* steering: 0 = left / right buttons, 1 = a drag zone (analogue) */
    int tilt;                               /* steering by tilting the device: 0 off, 1 on, -1 on and inverted */
    int tilt_angle;                         /* degrees of tilt for full lock */
} PadTouchConfig;
const PadTouchConfig *pad_touch_config(void);

void  pad_rumble(float strength, int ms);   /* 0..1 on the device used last, scaled by the configuration */
void  pad_race_active(int on);              /* the wheels' centring springs on / off (FF wheels) */

/* the controls screen: element names, capture and binding */
const char *pad_bind_name(int action, int i);   /* the i-th element of an action (controller.ini spelling), NULL */
const char *pad_display_name(const char *element, int kind);  /* as on the pad: "Kruis", "RT", "L-stick links" */
int   pad_display_kind(void);               /* PADKIND_* of the device used last */
void  pad_capture_begin(void);              /* remember the rest positions */
int   pad_capture(char *out, size_t cap);   /* 1 = an element was pressed / moved since: its name in out */
void  pad_set_primary(int action, const char *element);   /* the first element of the action; removed elsewhere */
void  pad_swap_primary(int a, int b);
void  pad_backup(void);                     /* the controls screen's Annuleren: back to the saved mapping */
void  pad_restore(void);
#endif
