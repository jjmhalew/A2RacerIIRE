# Input in spel.dat (the race)

How *A2 Racer II* reads the keyboard, joypads, wheels/joysticks and drives a force-feedback stick, and how the
player's steer/gas/brake/turbo/handbrake values reach the car. All addresses are spel.dat VAs. Everything here comes
from the disassembly. Names in `code` are our own (see the name list at the end); "car" is the player-car index
`g_player_car` (0x4d9e28).

## 1. Overview

There are three separate input paths, chosen by `Game.ini [INPUT] DEVICE`:

| DEVICE | Meaning (Game.ini comment) | API actually used | Per-frame reader |
|---|---|---|---|
| 1 | Keyboard | `GetAsyncKeyState` with the Windows **VK codes** from Game.ini | `plr_input_keyboard` 0x4265b0 |
| 2 | Joypad | winmm `joyGetPosEx` (joystick 1), stick and buttons are turned into *virtual keys* | `plr_input_joypad` 0x4269c0 |
| 3 | Wheel/Joystick | winmm `joyGetPosEx`, analog X/Y | `plr_input_analog` 0x426e00 |
| 4 | Sidewinder ForceFeedback | **DirectInput 5** joystick (`c_dfDIJoystick`), plus force-feedback effects | `plr_input_ffjoy` 0x426f50 |

- **No DirectInput keyboard or mouse is used by the race.** The DirectInput wrapper library (`win3dlib`/`inputlib`,
  `di_*.cpp`) can add keyboard, mouse and joysticks 1-4, but spel.dat only ever calls `di_add_device(3)`
  (joystick 1) and only when DEVICE=4. There is therefore no VK->DIK table; all keys are VK codes.
- In-race keys (pause, quit, horn, cameras, ...) are always read with `GetAsyncKeyState`, independent of DEVICE.
- The window procedure / message pump (`sub_431540`) does nothing with WM_KEYDOWN; input is 100% polled once per frame.

Call graph:

```
init_game 0x423d20
  set_game_input 0x423910          reads [INPUT], init_input_module, set_input_devices, DirectInput+FF if DEVICE=4
sub_420fe0 -> joy_init 0x46e690    winmm joystick 1+2 detection/calibration (always, at startup)
init_player_car 0x4256a0           builds the 31-entry steering response table from SENSITIVITY
handle_game 0x424a70 (per frame)
  ... handle_player_car 0x425e20 (via object callback)
        if (g_player_control_enabled 0x4d9a9c) switch (g_input_device) -> 0x4265b0 / 0x4269c0 / 0x426e00 / 0x426f50
        set steering constants per device
        stuur_model==0 ? car_drive_new 0x40fc30(car, steer, gas, brake)
                       : car_drive_old 0x40cfd0(car, angle, accel)
        if DEVICE==4 && FF: ff_update_springs (inline, 0x426334..0x426576)
  check_game_keys 0x424ee0         T, M, D, Esc, P, Ctrl+Space+T/I
  handle_game_cameras 0x4144b0     1-4 (+5,7,8,9,0 in tweak mode)
  if (g_pause_request) game_pause_dialog 0x4248d0
collision handler 0x427730 -> plr_collision_feedback 0x427de0 (FF bump)
```

## 2. Game.ini [INPUT]

`set_game_input` 0x423910 (gm_init.cpp) opens `GAME.INI` and reads, with `ini_get_int` 0x46aad0:

| Key | Default | Use |
|---|---|---|
| DEVICE | 1 | 1..4, see table above; passed to `set_input_devices` |
| LEFT / RIGHT / UP / DOWN | 37 / 39 / 38 / 40 | VK codes: steer left/right, gas, brake |
| EXTRA1 | 17 (Ctrl) | VK: **turbo** |
| EXTRA2 | 18 (Alt) | VK: **handbrake** |
| SENSITIVITY | 50 | 0..100, stored in `g_input_sensitivity` 0x4d8e88, shapes the analog steering curve (section 6) |

Then:

```c
void set_game_input(void)                                   // 0x423910
{
    ini_open("GAME.INI");
    int dev  = ini_get_int("INPUT","DEVICE");
    int l = ini_get_int("INPUT","LEFT"),  r = ini_get_int("INPUT","RIGHT");
    int u = ini_get_int("INPUT","UP"),    d = ini_get_int("INPUT","DOWN");
    int e1 = ini_get_int("INPUT","EXTRA1"), e2 = ini_get_int("INPUT","EXTRA2");
    int sens = ini_get_int("INPUT","SENSITIVITY");
    init_input_module(l, r, u, d, e1, e2, 0, 0, sens);       // 0x424490
    set_input_devices(dev);                                  // 0x424590
    if (dev == 4) ff_init();                                 // inline, section 7
}
```

The menu (menu.dat `ini_io.cpp` 0x401b20) reads/writes the same keys (`Device`, `Sensitivity`, LEFT..EXTRA2); the
controls screen is `m_bediening.cpp` (0x402750..0x4035f0), which lets the user rebind the six keys.

## 3. The input module (gm_init.cpp)

Globals (all `int`):

| Addr | Name | Meaning |
|---|---|---|
| 0x4d8e60..0x4d8e7c | `g_input_key[8]` | VK per slot: 0 LEFT, 1 RIGHT, 2 UP, 3 DOWN, 4 EXTRA1, 5 EXTRA2, 6 (0), 7 (0) |
| 0x4d8e80 | `g_input_device` | 1 keyboard, 2 joypad, 3 analog stick/wheel ("STEAR"), 4 FF joystick |
| 0x4d8e84 | `g_input_key_mode` | device mask for `key_is_pressed`: 1 = keyboard, 2 = joystick |
| 0x4d8e88 | `g_input_sensitivity` | SENSITIVITY 0..100 |
| 0x4d8e8c | `g_ff_available` | 1 if the DI joystick has force feedback (DEVICE=4 only) |
| 0x4d8e90 | `g_input_inited` | set by `init_input_module` |

```c
void init_input_module(int left,int right,int up,int down,int ex1,int ex2,int ex3,int ex4,int sens) // 0x424490
{
    g_input_key[0..7] = {left,right,up,down,ex1,ex2,ex3,ex4};
    for (i = 0; i < 8; i++) joy_set_key(1, i, g_input_key[i]);   // 0x46ec80, joystick 1 slot i -> VK
    g_input_key_mode = 1;
    g_input_sensitivity = sens;
    g_input_inited = 1;
}

void set_input_devices(int dev)                               // 0x424590
{
    if (!g_input_inited) error("(set_input_devices) input module not inited ...");
    switch (dev) {
    case 1: g_input_device = 1; g_input_key_mode = 1; break;
    case 2: g_input_device = 2; g_input_key_mode = 2;
            // joypad: re-map joystick-1 slots (slot = stick direction / button, see section 4.2)
            joy_set_key(1,0,g_input_key[0]);  // stick left   -> LEFT
            joy_set_key(1,1,g_input_key[1]);  // stick right  -> RIGHT
            joy_set_key(1,2,0);               // stick up     -> nothing
            joy_set_key(1,3,g_input_key[3]);  // stick down   -> DOWN  (brake)
            joy_set_key(1,4,g_input_key[2]);  // button 1     -> UP    (gas)
            joy_set_key(1,6,g_input_key[6]);  // button 3     -> 0
            joy_set_key(1,5,g_input_key[4]);  // button 2     -> EXTRA1 (turbo)
            joy_set_key(1,7,g_input_key[7]);  // button 4     -> 0
            break;
    case 3: g_input_device = 3; g_input_key_mode = 2; break;
    case 4: g_input_device = 4; g_input_key_mode = 2; break;
    default: error("(set_input_devices) invalid device (%d)", dev);
    }
}
```

### 3.1 Digital read: `handle_input_module` 0x4246c0 (DEVICE 1 and 2)

Returns a bit mask. (The function at 0x4246c0 is hidden inside the listing of `sub_424590` because of the jump table.)

```c
enum { IN_LEFT=0x01, IN_RIGHT=0x02, IN_UP=0x04, IN_DOWN=0x08,
       IN_EXTRA1=0x10, IN_EXTRA2=0x20, IN_EXTRA3=0x40, IN_EXTRA4=0x80 };

int handle_input_module(void)                                 // 0x4246c0
{
    int bits = 0;
    if (!g_input_inited)       error(...);
    if (g_input_device == 3)   error("... trying to read wrong input device (device is STEAR)");
    if (g_input_device == 2)   joy_poll_keys();               // 0x46ebc0
    for (i = 0; i < 8; i++)
        if (key_is_pressed(g_input_key[i], g_input_key_mode)) // 0x466960
            bits |= 1 << i;
    return bits;
}
```

### 3.2 Analog read: `handle_input_module_stear` 0x4247f0 (DEVICE 3)

```c
int handle_input_module_stear(int w, int h, int *x, int *y)   // 0x4247f0
{
    int jx = 0, jy = 0, f = 0;
    if (g_input_device != 3) error("... (device is not STEAR)");
    joy_get_position(1, w, h, &jx, &jy, &f);                  // 0x46ed90
    *x = clamp(jx, 0, w);  *y = clamp(jy, 0, h);
    int bits = 0;
    if (f & 4) bits |= IN_LEFT;   if (f & 8) bits |= IN_RIGHT;
    if (f & 1) bits |= IN_UP;     if (f & 2) bits |= IN_DOWN;   // stick forward = UP
    bits |= f & 0xf0;                                         // buttons 1..4 -> 0x10..0x80
    return bits;
}
```

## 4. winlib key and joystick layer

### 4.1 Keys: `key_down` 0x466910 and `key_is_pressed` 0x466960

```c
// returns -1 (true) if the key is held. Bit 0x100 in vk means "with Shift".
int key_down(int vk)                                          // 0x466910 (in-race hotkeys)
{
    if (vk & 0x100) { if (GetAsyncKeyState(VK_SHIFT) >= 0) return 0; }
    else            { if (GetAsyncKeyState(VK_SHIFT) <  0) return 0; }   // Shift held -> every plain key reads "up"
    return GetAsyncKeyState(vk) < 0 ? -1 : 0;
}

int key_is_pressed(int vk, int devmask)                       // 0x466960
{
    if (devmask < 0 || devmask > 3) error("(keyispressed) illegal device requested (%d)");
    if ((devmask & 2) && joy_key_active(vk)) return -1;       // 0x46efc0
    if (devmask & 1) { same Shift rule and GetAsyncKeyState test as key_down }
    return 0;
}
```

Notes for a port: only the high bit of `GetAsyncKeyState` (key currently down) is used, so a key is "pressed" for as
long as it is held. **Holding Shift disables every key binding** (and Shift itself, VK 16, can never work as a binding).
`vk = 0` (unused slots) never reads as a pressed keyboard key.

### 4.2 winmm joystick (joystick.cpp-like part of winlib, 0x46e690..0x46f000)

Per joystick n = 1, 2 there is a 0x5c-byte record at `0x4f00c0 + n*0x5c` (joystick 1 = 0x4f011c):

| Off | Field |
|---|---|
| +0x00 | present flag |
| +0x04 + 8*s | VK assigned to slot s (s = 0..7) |
| +0x08 + 8*s | slot s active (0/1) |
| +0x44 / +0x48 | X / Y centre ("Xbase/Ybase": the position read once at init) |
| +0x4c / +0x50 | Xmax / Xmin (from `joyGetDevCaps`) |
| +0x54 / +0x58 | Ymax / Ymin |

Slots: 0 stick left, 1 stick right, 2 stick up (Y small), 3 stick down (Y large), 4..7 buttons 1..4.
`JOYINFOEX` lives at 0x4f0088 (`dwSize=0x34`, `dwFlags=0x83` = RETURNX|RETURNY|RETURNBUTTONS); joystick n uses
winmm id n-1.

```c
void joy_init(void)                                           // 0x46e690, called from sub_420fe0
{
    for n in 1,2:
        r = joyGetPosEx(n-1, &ji);                           // JOYERR_UNPLUGGED -> present=0, OK -> present=1
        if (r == JOYERR_NOERROR) { J[n].xc = ji.dwXpos; J[n].yc = ji.dwYpos; }
        if (joyGetDevCaps(n-1, &caps, sizeof caps) == 0) {
            J[n].xmax = caps.wXmax; J[n].xmin = caps.wXmin;
            J[n].ymax = caps.wYmax; J[n].ymin = caps.wYmin;
        }
}

void joy_set_key(int n, int slot, int vk) { J[n].key[slot] = vk; }   // 0x46ec80

// dead zone = 10 % of the centre value (double 0.1 at 0x4a1ba8), comparisons in floating point
void joy_update_keys(int n, unsigned X, unsigned Y, unsigned buttons) // 0x46eac0
{
    all 8 J[n].active = 0;                                     // 0x46ea70
    if (X > J[n].xc + J[n].xc*0.1) J[n].active[1] = 1;        // right
    if (X < J[n].xc - J[n].xc*0.1) J[n].active[0] = 1;        // left
    if (Y > J[n].yc + J[n].yc*0.1) J[n].active[3] = 1;        // down (pulled back)
    if (Y < J[n].yc - J[n].yc*0.1) J[n].active[2] = 1;        // up (pushed forward)
    for (b = 0; b < 4; b++) if (buttons & (1<<b)) J[n].active[4+b] = 1;
}

void joy_poll_keys(void)                                      // 0x46ebc0
{
    for n in 1,2 if (J[n].present) {
        if (joyGetPosEx(n-1,&ji)) joy_clear_keys(n);
        else joy_update_keys(n, ji.dwXpos, ji.dwYpos, ji.dwButtons);
    }
}

int joy_key_active(int vk)                                    // 0x46efc0
{   for n in 1,2 for s in 0..7 if (J[n].key[s] == vk && J[n].active[s]) return 1; return 0; }

// Analog position scaled to 0..w / 0..h, centre = w/2, h/2; Y is inverted (forward = large y)
int joy_get_position(int n, int w, int h, int *x, int *y, int *flags) // 0x46ed90
{
    int hw = w/2, hh = h/2;  *flags = 0;
    if (!J[n].present || joyGetPosEx(n-1,&ji)) return 0;
    unsigned X = ji.dwXpos, Y = ji.dwYpos;
    if      (X > xc*1.1) { *x = X / ((xmax - xc)/hw); *flags |= 8; }   // integer divides as written
    else if (X < xc*0.9) { *x = X / ((xc - xmin)/hw); *flags |= 4; }
    else                   *x = hw;
    if      (Y > yc*1.1) { *y = 2*hh - Y / ((ymax - yc)/hh); *flags |= 2; }
    else if (Y < yc*0.9) { *y = 2*hh - Y / ((yc - ymin)/hh); *flags |= 1; }
    else                   *y = hh;
    if (ji.dwButtons & 1) *flags |= 0x10;  if (ji.dwButtons & 2) *flags |= 0x20;
    if (ji.dwButtons & 4) *flags |= 0x40;  if (ji.dwButtons & 8) *flags |= 0x80;
    return 1;
}
```

(`xc*1.1` is evaluated as `xc + xc*0.1` in x87 precision; the result is the raw position divided by "raw units per output
unit", so for a 0..65535 stick with centre 32767, x runs 0..~w with a jump at the dead-zone edge.)

## 5. Per-device readers and the player-input globals

### 5.1 Globals the car reads

| Addr | Name | Type | New model (stuur_model=0) | Old model (stuur_model=1) |
|---|---|---|---|---|
| 0x4d9cd8 | `g_plr_steer` | float | -100 (left) .. +100 (right), 0 = straight | 0..60, 30 = straight |
| 0x4d9cdc | `g_plr_gas` | float | 0..100 | 0..40, 20 = neutral (<20 brakes/reverses) |
| 0x4d9e08 | `g_plr_brake` | int | 0..100 | (not used) |
| 0x4d9df8 | `g_plr_handbrake` | int | 0/1 | 0/1 |
| 0x4d9dfc | `g_plr_turbo_active` | int | 0/1, set by `plr_handle_turbo` | same |
| 0x4d9cec | `g_plr_speed_at_input` | float | copy of car[+0x74] (forward speed) taken by the digital readers | |
| 0x4d9df4 | `g_plr_gas_hold` | int | only ever written 0, old-model "if ==1 gas=20" is dead code | |
| 0x4d9af0 | `g_opt_stuur_model` | int | Game.ini `[OPTIONS] stuur_model` (0 new A2RII model, 1 old Autobahn Raser model), read in 0x421b70 | |

`init_player_car` 0x4256a0 initialises `g_plr_steer = 30.0`, `g_plr_gas = 20.0` (old-model neutral values, even in
the new model; the first frame of input overwrites them).

`dt` below is `timer_frame_dt(&g_game_timer 0x4e4970)` = `sub_441cd0`: the last frame time in seconds, forced to 0.1
if it is 0 or > 0.1. `(int)` is MSVC `_ftol` (truncation toward zero).

### 5.2 Keyboard (DEVICE 1) `plr_input_keyboard` 0x4265b0 and joypad (DEVICE 2) `plr_input_joypad` 0x4269c0

The two functions are identical except for the turbo/handbrake prologue:

```c
void plr_input_digital(int car, bool joypad)
{
    int bits = handle_input_module();
    g_plr_speed_at_input = cars[car].f74;                     // cars = *(0x4e48e0), stride 0xa0
    if (!joypad) {                                            // 0x4265b0
        g_plr_handbrake = (bits & IN_EXTRA2) ? 1 : 0;
        plr_handle_turbo(car, (bits & IN_EXTRA1) != 0);       // 0x425cf0
    } else {                                                  // 0x4269c0
        g_plr_handbrake = (bits & IN_EXTRA2) || key_down(VK_SPACE) ? 1 : 0;
        if (bits & IN_EXTRA1) { plr_handle_turbo(car, 1); bits |= IN_UP; }   // turbo button also gives gas
        else                    plr_handle_turbo(car, 0);
    }

    if (g_opt_stuur_model == 1) {                             // ---- old model, per-frame steps
        if (bits & IN_UP)        { if (gas < 40) gas += 5; else gas = 40; }
        else if (bits & IN_DOWN) { if (gas > 20) gas = 20; if (gas > 0) gas -= 10; else gas = 0; }
        else                     { gas = 20; g_plr_gas_hold = 0; }
        if (g_plr_gas_hold == 1) gas = 20;
    } else {                                                  // ---- new model
        if (bits & IN_UP)  { gas = gas + 70*dt; if (gas > 100) gas = 100; } else gas = 0;
        if (bits & IN_DOWN){ brake = (int)((float)brake + 40*dt); if (brake > 100) brake = 100; } else brake = 0;
    }

    if (g_opt_stuur_model == 1) {                             // ---- old model steering, centre 30
        if (bits & IN_LEFT)       { if (steer > 30) steer = 30; else if (steer == 0) steer = 0; else steer -= 2; }
        else if (bits & IN_RIGHT) { if (steer < 30) steer = 30; else if (steer < 60) steer += 2; }
        else                        steer = 30;
        return;
    }
    // ---- new model steering, -100..100
    float s = steer;
    float k = 2.0f * (1.0f - fabsf(s) * 0.01f);               // 0.01 at 0x47f84c; slows down near full lock
    if (bits & IN_LEFT) {
        if (steer > 0) { steer = 0; return; }                 // instant snap back to centre when reversing
        if (s == 0) s = -15;                                  // initial kick
        steer = s - dt*k*200;  if (steer < -100) steer = -100;
    } else if (bits & IN_RIGHT) {
        if (steer < 0) { steer = 0; return; }
        if (s == 0) s = 15;
        steer = s + dt*k*200;  if (steer > 100) steer = 100;
    } else {                                                  // self-centring at 400/s
        if (steer > 0) { s = steer - 400*dt; steer = s; if (steer < 0) steer = 0; }
        if (steer < 0) { steer = s + 400*dt;            if (steer > 0) steer = 0; }
    }
}
```

Porting note: the keyboard brake is an *int* that is truncated every frame, so `40*dt` must be >= 1 for it to grow:
at more than 40 fps the digital brake stays 0 (the original ran at ~15-30 fps). The old-model steps (+5/-10 gas,
+-2 steer per frame) are frame-rate dependent as well.

Joypad mapping that results (DEVICE 2, keys only reachable through joystick 1): stick left/right = steer, stick
back = brake, button 1 = gas, button 2 = turbo (+gas), Space bar = handbrake, stick forward / buttons 3, 4 = nothing.
The configured EXTRA2 key is never seen in joypad mode (the module only checks the joystick there).

### 5.3 Analog stick / wheel (DEVICE 3) `plr_input_analog` 0x426e00

```c
void plr_input_analog(int car)
{
    int x, y, bits;
    if (g_opt_stuur_model == 1) {
        bits = handle_input_module_stear(60, 40, &x, &y);     // x 0..60, y 0..40
        steer = (x <= 30) ? 30 - g_steer_curve[30 - x]        // 0x4d9cf8, see section 6
                          : 30 + g_steer_curve[x - 30];
        gas = (float)y;  if (gas < 10) gas = 0;               // forward = 40
    } else {
        bits = handle_input_module_stear(100, 100, &x, &y);   // x,y 0..100
        if (y >= 50) { gas = 2*y - 100; brake = 0; }          // forward half = gas
        else         { gas = 0;         brake = (50 - y)*2; } // back half = brake
        steer = 2*x - 100;                                    // linear, no curve, no SENSITIVITY
    }
    if (bits & 0x10) { g_plr_handbrake = 1; g_plr_brake_light = 0; } // button 1 = handbrake (0x4a63cc)
    else               g_plr_handbrake = 0;
    plr_handle_turbo(car, (bits & 0x20) != 0);                // button 2 = turbo
}
```

### 5.4 Force-feedback joystick (DEVICE 4) `plr_input_ffjoy` 0x426f50

DirectInput axes are set to -1000..+1000 (section 7). Always produces **old-model** ranges (steer 0..60, gas 0..40),
regardless of `stuur_model`; there is no brake.

```c
void plr_input_ffjoy(int car)
{
    int lx, ly, lz;
    di_joy_get_xyz(&g_di, g_di.joy1, &lx, &ly, &lz);           // 0x438c70: Poll + GetDeviceState
    int xi = (lx + 1000) * 60 / 2000;                         // 0..60
    int yi = (ly + 1000) * 40 / 2000;                         // 0..40
    steer = (xi <= 30) ? 30 - g_steer_curve[30 - xi] : 30 + g_steer_curve[xi - 30];
    gas   = (float)(40 - yi);                                 // stick forward (lY=-1000) = 40
    g_plr_handbrake = di_joy_button(&g_di, g_di.joy1, 1) != 0;  // 0x439990, button index 1 (2nd button)
    plr_handle_turbo(car, di_joy_button(&g_di, g_di.joy1, 0) != 0);  // trigger = turbo
}
```

### 5.5 What the player-car update does with them (`handle_player_car` 0x425e20)

```c
if (g_player_control_enabled /*0x4d9a9c*/) {
    switch (g_input_device) { 1: plr_input_keyboard; 2: plr_input_joypad; 3: plr_input_analog; 4: plr_input_ffjoy;
                              default: error("(handle_player_car) ... Invalid input device"); }
    ...
    switch (g_input_device) {                                 // old-model steering limits
    case 1: case 2: g_steer_k = 3.0f /*0x4d9df0*/; g_steer_min_deg = 5.0f /*0x4a5dc8*/; g_steer_max_deg = 25.0f /*0x4a5d48*/; break;
    case 3: case 4: g_steer_k = 3.0f;            g_steer_min_deg = 3.0f;            g_steer_max_deg = 20.0f;            break;
    }
    if (g_opt_stuur_model == 0)
        car_drive_new(car, steer, gas, (float)brake);         // 0x40fc30 (turbo forces gas=100 inside)
    else {
        float f     = powf(e, -(float)abs((int)cars[car].f74) * g_steer_k / g_plr_vmax /*0x4d9e68*/);
        float angle = (steer - 30) * (g_steer_min_deg + (g_steer_max_deg - g_steer_min_deg) * f)
                      * 0.000581776f;                         // = pi/5400: (steer-30)/30 * deg -> rad
        float accel = (gas - 20) * 0.05f;                     // -1..+1
        debug_print(0, 10, 430, "%3.3f", angle);              // sub_466420, on-screen debug text
        car_drive_old(car, angle, accel);                     // 0x40cfd0
    }
    if (g_input_device == 4 && g_ff_available) ff_update(car); // section 7.3
}
```

`g_plr_vmax` 0x4d9e68 is `sub_40ce10(car)` computed in `init_player_car`; `cars[car].f74` is the car's forward speed.
`car_drive_new` additionally forces `handbrake=1, gas=0` while the game state (0x4d9a7c) is 3, 6 or 8
(caught / game-over sequences). The front-wheel visual yaw in `sub_413900` is `-steer * 0.01 * (ANGLE_UNITS/12)`,
i.e. +-30 degrees at steer +-100.

### 5.6 Turbo: `plr_handle_turbo` 0x425cf0

(The disassembler merges it with `handle_player_car` which starts at 0x425e20.)

```c
void plr_handle_turbo(int car, int on)                        // 0x425cf0
{
    float *grip = (float *)(0x484bf2 + 238 * g_player_car_type /*0x4d9e2c*/);  // car physics table field
    if (on) {
        DWORD t = GetTickCount();
        if (t - g_turbo_last_tick /*0x4d9e1c*/ < 100) return; // one turbo unit per 100 ms
        g_turbo_last_tick = t;
        if (g_player_turbo /*0x4d9e3c*/ > 0) {
            if ((g_turbo_snd /*0x48b464*/ == -1 || !snd_is_playing(g_turbo_snd)) && !g_turbo_snd_on /*0x4d9e18*/) {
                g_turbo_snd = snd_play(0x54); g_turbo_snd_on = 1;
            }
            g_player_turbo--;
            if (*grip != 6.0f) g_turbo_saved /*0x4d9cf0*/ = *grip;
            *grip = 6.0f;
            g_plr_turbo_active = 1;
            return;
        }
    } else {
        if (g_turbo_snd != -1 && snd_is_playing(g_turbo_snd)) snd_stop(g_turbo_snd);
        g_turbo_snd_on = 0;
    }
    g_plr_turbo_active = 0;
    *grip = g_turbo_saved;
}
```

## 6. SENSITIVITY: the analog steering curve

Built at the end of `init_player_car` 0x4256a0 into `int g_steer_curve[31]` at 0x4d9cf8..0x4d9d70:

```c
int k = (100 - g_input_sensitivity) * 3 / 100 + 1;            // integer maths (imul 0x51eb851f, sar 5)
for (i = 0; i <= 30; i++) {
    double t = pow(2.7182817459106445 /*0x47f7a8*/, i * 0.033333335f /*0x47f7b0*/);
    g_steer_curve[i] = (int)(30.0f * pow((t - 1.0f) * 0.58197701f /*0x47f7b8 = 1/(e-1)*/, (float)k));
}
```

`k` (int, then converted to float) is 4 for SENSITIVITY 0, 3 for 1..33, 2 for 34..66 (default 50), 1 for 67..100.
Resulting tables (computed with the exact constants):

```
k=1: 0 0 1 1 2 3 3 4 5 6 6 7 8 9 10 11 12 13 14 15 16 17 18 20 21 22 24 25 26 28 30
k=2: 0 0 0 0 0 0 0 0 0 1 1 1 2 2 3 4 5 5 6 7 9 10 11 13 15 17 19 21 24 26 30
k=3: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 2 2 3 4 5 6 7 9 10 13 15 18 21 25 30
k=4: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 1 2 2 3 4 6 7 9 12 15 19 24 30
```

The table maps stick deflection |dx| = 0..30 to steering deflection 0..30 around the old-model centre 30. It is used
only by `plr_input_analog` (DEVICE 3, and only when stuur_model=1) and `plr_input_ffjoy` (DEVICE 4, always). Higher
SENSITIVITY = lower exponent = more response near the centre. With the default stuur_model=0 a DEVICE 3 wheel is
linear and SENSITIVITY has no effect.

## 7. DirectInput library (inputlib/win3dlib, 0x435000..0x43e000)

Only used when DEVICE = 4. The global `di_input` object is at 0x4e4810 (`g_di`).

### 7.1 Objects

`di_input` (0x4e4810):

| Off | Field |
|---|---|
| +0x00 | `IDirectInput*` (`DirectInputCreateA(hInst, 0x0500, &lpDI, NULL)`, sub_43e1c0 = import thunk) |
| +0x04..+0x31 | `T_ARRAY<di_dev_t*> m_devices` (50 entries: data +0x04, size +0x08, alloc flag +0x10, used map +0x14, names +0x18/+0x31) |
| +0x4a | inited flag (0x4e485a) |
| +0x4e, +0x52 | keyboard, mouse device index (-1) |
| +0x56 | joystick 1 device index (**0x4e4866** `g_di_joy1`) |
| +0x5a, +0x5e, +0x62 | joystick 2, 3, 4 device index |
| +0x66 | `di_info` device list (`m_device_list`, records of 0x70 bytes: +0x04 instance GUID, +0x3c attached, +0x40 has FF); joystick-1 info index at info+0x56 (object +0xbc) |

`di_dev_t` (vtables at 0x47f9b0 base, 0x47f9d8 keyboard, 0x47fa28 mouse, 0x47fa50 joystick, 0x47fa78 FF joystick;
10 virtuals: init, dtor, create 0x43ba10, release 0x43bdd0, acquire 0x43bc60, unacquire 0x43bdb0, set_data_format
0x43bac0, set_coop_level 0x43bb10, set_range_prop 0x43bd20, set_dword_prop 0x43bc90):

| Off | Field |
|---|---|
| +0x04 / +0x08 | `IDirectInputDevice*` / `IDirectInputDevice2*` (QueryInterface with IID at 0x47faa0) |
| +0x0c | `DIDEVCAPS` (dwSize 0x2c); +0x10 = dwFlags (0x100 FORCEFEEDBACK, 0x2/0x8 polled) |
| +0x38 | type (1 keyboard, 3 joystick, 4 FF joystick) |
| +0x3c | state 0 none, 1 created, 2 acquired |
| +0x40 | coop flags: 5 = EXCLUSIVE\|FOREGROUND (joysticks), 6 = NONEXCLUSIVE\|FOREGROUND (keyboard) |
| +0x44 | `DIDATAFORMAT` copy: joystick = c_dfDIJoystick (0x43f540: 0x18,0x10,DIDF_ABSAXIS,0x50,44 objs), keyboard = c_dfDIKeyboard (0x43f1d0) |
| +0x5c | instance GUID |
| +0x6c | `di_obj` list (`m_object_list`, EnumObjects) |
| +0xbe | `DIJOYSTATE` (lX +0xbe, lY +0xc2, lZ +0xc6, rgbButtons +0xee) |
| +0x10e | effect list (`m_effect_list`, EnumEffects; FF joystick only) |
| +0x14a | mouse event handle |

`di_effect` (0x180 bytes; base ctor 0x4358a0, constant-force ctor 0x435f70): +0x00 created, +0x04 "apply immediately"
flag, +0x0c effect GUID, +0x1c `IDirectInputEffect*`, +0x20 `DIEFFECT` (dwFlags 0x12 = CARTESIAN|OBJECTOFFSETS,
dwDuration 1 000 000 us, dwGain 7500, dwTriggerButton = DIEB_NOTRIGGER, envelope at +0x54 all zero, axes
DIJOFS_X/DIJOFS_Y, cbTypeSpecificParams 4), +0x68 axis mask, +0xec direction[], +0x170 extra DIEP flags,
+0x174 `DICONSTANTFORCE.lMagnitude`. Global effect array `T_ARRAY "di_effects"` of 40 (sub_435290(0x28)), data
pointer at **0x4e47c8** (`g_di_effects`).

### 7.2 Functions

```c
di_init(hinst)            0x4363c0 -> 0x436570: DirectInputCreateA; di_info_enum 0x43b1c0; print list 0x43acc0;
                          allocate m_devices[50]
di_info_enum              0x43b1c0: EnumDevices(0, cb 0x43b2e0, DIEDFL_ALLDEVICES)    -> register devices/types
                                    EnumDevices(0, cb 0x43b720, DIEDFL_ATTACHEDONLY)  -> +0x3c attached
                                    EnumDevices(0, cb 0x43b7f0, DIEDFL_FORCEFEEDBACK) -> +0x40 FF
di_add_device(type)       0x439f00: 1 keyboard 0x4368a0, 2 mouse 0x436d50, 3..6 joystick 1..4
                          (0x437650, 0x437af0, 0x437f90, 0x438430); returns 0, -1 not found, -2 not attached
di_add_joystick1          0x437650: new FF-joystick (0x160 bytes, ctor 0x43c4a0) if info.has_ff else joystick
                          (0x10e bytes, ctor 0x43c270); store in m_devices; vtbl->init(); "Device <%s> acquired."
joystick init             0x43c2d0: create; SetDataFormat(c_dfDIJoystick); SetCooperativeLevel(hwnd 0x4eabd0, 5);
                          Acquire; GetCapabilities
FF joystick init          0x43c530: same + EnumEffects (0x43d920 di_eneff.cpp) + effect list (0x43de30)
mouse init                0x43c220: ... + CreateEvent/SetEventNotification + DIPROP_BUFFERSIZE 16 (unused here)
di_set_axis_range(d,xmin,xmax,ymin,ymax) 0x439a70 -> 0x43c300: DIPROP_RANGE on DIJOFS_X and DIJOFS_Y (BYOFFSET)
di_set_deadzone(d,v)      0x439b30 -> 0x43c3d0: DIPROP_DEADZONE v on X and Y
di_set_autocenter(d,on)   0x439bf0 -> 0x43c3a0: DIPROP_AUTOCENTER (DIPH_DEVICE) on?1:0
di_has_ff(d)              0x439e00 -> 0x43bbe0: caps.dwFlags & DIDC_FORCEFEEDBACK
di_create_effect(d,name)  0x439cb0 -> 0x43c570: look up name in the device's EnumEffects list (0x43dc60); by
                          DIEFT type: CONSTANTFORCE -> new constant effect (0x4353f0(10000)), PERIODIC -> 0x435580,
                          0x100 (custom?) -> 0x435710; CreateEffect (0x4359b0/0x435950); returns index in g_di_effects
di_joy_poll(dev)          0x43c410: if polled device: Poll(), on DIERR_INPUTLOST (0x8007001e) re-acquire;
                          GetDeviceState(0x50, &state); on INPUTLOST re-acquire
di_joy_get_xyz(d,&x,&y,&z) 0x438c70: di_joy_poll; x=lX, y=lY, z=lZ
di_joy_button(d,b)        0x439990: returns state.rgbButtons[b] (0 or 0x80), b < 32
di_reacquire_all          0x439f80: Acquire every device (called from the message pump 0x431540 after the
                          window was re-activated, DEVICE=4 only)
di_exit                   0x4366a0: "exit direct input devices", release all
effect methods: set_duration 0x435b80 (+0x28, DIEP_DURATION), set_gain 0x435bb0, set_axes 0x435c00 (DIEP_AXES),
  set_direction(x,y,z) 0x435d80 (int, DIEP_DIRECTION), set_magnitude 0x436070 (DIEP_TYPESPECIFICPARAMS),
  start 0x435a60 = Start(1, 0), stop 0x435a70, download 0x435aa0, apply 0x435b20 = SetParameters(&eff, flags)
```

### 7.3 Force feedback in the race

Set up at the end of `set_game_input` when DEVICE = 4:

```c
di_init(hinst /*0x4d8d4c*/);  di_effects_init(40);           // 0x435290
if (di_add_device(3)) error("Unable to add joystick device (no joystick found).");
di_set_axis_range(g_di_joy1, -1000, 1000, -1000, 1000);
di_set_deadzone(g_di_joy1, 500);                              // 5 %
di_set_autocenter(g_di_joy1, 0);                              // stick's own spring off; game makes its own
g_ff_available = 0;
if (di_has_ff(g_di_joy1)) {
    g_ff_available = 1;
    g_ff_steer  /*0x4d8e4c*/ = di_create_effect(g_di_joy1, "Constant Force");
    g_ff_pedal  /*0x4d8e48*/ = di_create_effect(..., "Constant Force");
    g_ff_bump_x /*0x4d8e54*/ = di_create_effect(..., "Constant Force");
    g_ff_bump_y /*0x4d8e50*/ = di_create_effect(..., "Constant Force");
    g_ff_jolt   /*0x4d8e58*/ = di_create_effect(..., "Constant Force");
    //            duration       direction   magnitude, then start
    g_ff_steer  : INFINITE (-1)  (1,0,0)      0
    g_ff_pedal  : INFINITE       (0,1,0)      0
    g_ff_bump_x : 500 us         (1,0,0)      0
    g_ff_bump_y : 500 us         (0,1,0)      0
    g_ff_jolt   : INFINITE       (0,1,0)      0
}
```

Every frame (end of `handle_player_car`, DEVICE 4 with FF), with a fresh `di_joy_get_xyz` reading (lX, lY in
-1000..1000):

```c
// centring springs, made with constant forces that grow with the deflection
float ax = fabsf(lx);
float mx = (ax > 50) ? ((ax - 50) * 0.00105263f /*1/950*/ + 1.0f) * 4000 : ax * 0.02f * 4000;  // 0..8000
eff_set_direction(g_ff_steer, lx < 0 ? 1 : -1, 0, 0);
eff_set_magnitude(g_ff_steer, -(int)min(fabsf(mx), 10000));

float ay = fabsf(ly);
float my = (ay > 50) ? 3000 + (ay - 50) * 0.00105263f * 5000 : ay * 0.02f * 3000;              // 0..8000
eff_set_direction(g_ff_pedal, 0, ly < 0 ? 1 : -1, 0);
eff_set_magnitude(g_ff_pedal, -(int)min(fabsf(my), 10000));

// turbo / handbrake jolt along Y, sign of the car's forward speed
float m = 0;
if (g_plr_turbo_active || g_plr_handbrake) {
    eff_set_direction(g_ff_jolt, 0, cars[car].f74 >= 0 ? 1 : -1, 0);
    if (g_plr_handbrake) m = (fabsf(cars[car].f74) > 3.6) ? -10000 : 0;
    else                 m = 10000;                                      // turbo
}
eff_set_magnitude(g_ff_jolt, (int)m);
```

(The effects keep playing (infinite duration); every setter immediately calls `SetParameters`. The DirectInput sign
convention of direction x negative magnitude is reproduced literally; the effect is a spring that pushes the stick back
to the centre, stronger past 5 % deflection.)

Collisions: `plr_collision_feedback` 0x427de0 (called from the collision handler 0x427730) with the collision
vector at 0x4e677c/0x4e6780/0x4e6784:

```c
if (g_input_device == 4 && g_ff_available && g_ff_bump_x != -1 && g_ff_bump_y != -1) {
    eff_set_duration(g_ff_bump_x, 200000);  eff_set_duration(g_ff_bump_y, 200000);    // 0.2 s
    eff_set_magnitude(g_ff_bump_x, 10000);  eff_set_magnitude(g_ff_bump_y, 10000);
    eff_set_direction(g_ff_bump_x, coll.x < 0 ? 1 : -1, 0, 0);
    eff_set_direction(g_ff_bump_y, 0, coll.z < 0 ? -1 : 1, 0);
    eff_start(g_ff_bump_x);  eff_start(g_ff_bump_y);
}
// ...then the damage part (sub_427cc0, damage add 0x428390), not input
```

There is no road-surface rumble or engine vibration; the five constant forces above are all the FF there is.

## 8. In-race keys

All via `key_down` (GetAsyncKeyState, Shift must NOT be held). Read every frame from `handle_game` 0x424a70.

| Key (VK) | Where | Condition | Action |
|---|---|---|---|
| T (0x54) | check_game_keys 0x424ee0 | always | horn: `snd_play(0x29)` (sub_4286a0) every frame while held |
| M (0x4d) | 0x424ee0 | tweak_mode | wait for release; `g_dbg_cam_car` 0x4d9b2c = n+1, wraps 3 -> 0; camera follows car `0x4a55a0[n*0x60]` (n<3) or the player (n=3) via sub_4149b0 |
| Ctrl+Space+T | 0x424ee0 | timed track (`0x4d8e9c[137*track]==0`) | cheat: race timer 11 := 180 000 ms (3:00), message 0x2c (`0x4882c0`) |
| Ctrl+Space+I | 0x424ee0 | always | cheat: `g_no_damage` 0x4d9eb8 = 1 (damage is no longer added, sub_428390), message 0x2e |
| D (0x44) | 0x424ee0 | always | `g_opt_overlay_detail` 0x4d9b00 = (n+1) & 3 cycle 0..3, waits for release |
| Esc (0x1b) | 0x424ee0 | game state 0x4d9a7c not 3 or 6 | `g_pause_request` 0x4d9ab4 = 2 (quit question) |
| P (0x50) | 0x424ee0 | game state not 3 or 6 | `g_pause_request` = 1 (pause) |
| 1 (0x31) | handle_game_cameras 0x4144b0 | state 0 | camera 1 (`set_camera` 0x414b70) |
| 2 (0x32) | 0x4144b0 | state 0 | camera 3 |
| 3 (0x33) | 0x4144b0 | state 0, held | camera 5 while held (rear view); restores previous camera (0x4863cc) on release |
| 4 (0x34) | 0x4144b0 | state 0 | camera 0 |
| 5,7,8,9,0 | 0x4144b0 | tweak_mode, state 0 | cameras 4, 6, 7, 8, 9 (debug) |
| F12 (0x7b) | render 0x425090 | tweak_mode | show "frameRate: %f" and "vchunks: %d" |
| L (0x4c) | sub_41a780 | tweak_mode | debug text/resource display |
| Tab (9) | car_drive_new 0x40fc30 | tweak_mode | physics tweak editor (0x412e90, uses Space, +/- keypad, Up/Down, Tab) |
| Ctrl+Q | main loop 0x431710 | always | "Emergency exit, CTRL-Q pressed." (fatal error -> exit) |
| Ctrl+F10 | 0x431710 | always | screenshot `lvl%2.2d_%2.2d.bmp` |
| Esc / Enter / Space | ev_cops.cpp 0x4151f0, ev_g_ovr.cpp 0x415e80 | caught-by-police / game-over screens | continue |

`tweak_mode` = `g_tweak_mode` 0x4d9b24 = `raser.ini [helpdesk] tweak_mode` (read in 0x421b70).
There are no music keys or other F-keys in the race.

Pause/quit dialog `game_pause_dialog` 0x4248d0 (called at the end of `handle_game` when `g_pause_request != 0`):

```c
int saved = mixer_get_volume(4);  mixer_set_volume(4, 0);      // mute (Mixer.cpp 0x46faa0/0x46fa00)
for t in {8,9,10,0,3,11} timer_pause(t);                        // 0x421300
if (g_pause_request == 2) {                                     // quit?
    while (key_down(VK_ESCAPE));
    for (;;) {
        if (key_down('J') || key_down('Y')) { quit = 1; set_game_state(4); }   // 0x424d20: quit sequence
        if (key_down(VK_ESCAPE) || key_down('N')) { while (key_down(VK_ESCAPE)); break; }
        if (quit) break;
    }
} else if (g_pause_request == 1) {                              // pause: wait for any key 0..255 press+release
    while (key_down('P'));
    do for (vk = 0; vk < 256; vk++) if (key_down(vk)) { any = 1; while (key_down(vk)); } while (!any);
}
for t in {8,9,10,0,3,11} timer_resume(t);                       // 0x4213a0
mixer_set_volume(4, saved);  timer_reset_frame(&g_game_timer);  // 0x441d10
g_pause_request = 0;
```

## 9. Globals summary

| Addr | Name | Meaning |
|---|---|---|
| 0x4d8e48 | g_ff_pedal | FF effect index: Y-axis centring spring |
| 0x4d8e4c | g_ff_steer | FF effect index: X-axis centring spring |
| 0x4d8e50 | g_ff_bump_y | FF effect index: collision bump Y |
| 0x4d8e54 | g_ff_bump_x | FF effect index: collision bump X |
| 0x4d8e58 | g_ff_jolt | FF effect index: turbo/handbrake jolt (Y) |
| 0x4d8e60 | g_input_key[8] | VK per input slot |
| 0x4d8e80 | g_input_device | DEVICE 1..4 |
| 0x4d8e84 | g_input_key_mode | 1 keyboard / 2 joystick |
| 0x4d8e88 | g_input_sensitivity | SENSITIVITY |
| 0x4d8e8c | g_ff_available | FF joystick found |
| 0x4d8e90 | g_input_inited | |
| 0x4d9ab4 | g_pause_request | 1 pause, 2 quit question |
| 0x4d9af0 | g_opt_stuur_model | steering model |
| 0x4d9a9c | g_player_control_enabled | input is processed only when set |
| 0x4d9b00 | g_opt_overlay_detail | D key |
| 0x4d9b24 | g_tweak_mode | debug keys |
| 0x4d9b2c | g_dbg_cam_car | M key |
| 0x4d9cd8 / 0x4d9cdc | g_plr_steer / g_plr_gas | floats, see 5.1 |
| 0x4d9cec | g_plr_speed_at_input | |
| 0x4d9cf0 | g_turbo_saved | saved car-table value during turbo |
| 0x4d9cf8 | g_steer_curve[31] | SENSITIVITY curve |
| 0x4d9df0 / 0x4a5dc8 / 0x4a5d48 | g_steer_k / g_steer_min_deg / g_steer_max_deg | old-model steering limits |
| 0x4d9df4 | g_plr_gas_hold | dead |
| 0x4d9df8 | g_plr_handbrake | |
| 0x4d9dfc | g_plr_turbo_active | |
| 0x4d9e08 | g_plr_brake | int 0..100 |
| 0x4d9e18 / 0x48b464 | g_turbo_snd_on / g_turbo_snd | turbo sound 0x54 |
| 0x4d9e1c | g_turbo_last_tick | |
| 0x4d9e3c | g_player_turbo | decremented per 100 ms of turbo |
| 0x4d9e68 | g_plr_vmax | |
| 0x4d9eb8 | g_no_damage | Ctrl+Space+I |
| 0x4e4810 | g_di | di_input object |
| 0x4e4866 | g_di_joy1 | DI joystick-1 device index |
| 0x4e47c8 | g_di_effects | effect pointer array |
| 0x4e4970 | g_game_timer | frame timer (dt) |
| 0x4f0088 | g_joyinfo | JOYINFOEX |
| 0x4f00c0 | g_joy[3] | winmm joystick records (stride 0x5c, index 1..2) |

## 10. Open questions

- DIRECTION semantics of the FF springs: direction (+-1) is combined with a negated magnitude. Which way the Sidewinder
  actually pushed needs a test on hardware; the doc reproduces the values literally.
- DEVICE 4 always feeds old-model ranges (steer 0..60, gas 0..40) into `car_drive_new` when stuur_model=0
  (the default). Probably an untested combination in the original; a port should replicate it or decide.
- `sub_40da90` (physics) has a switch on `g_input_device` whose cases 1-3 are identical and case 4 skips a lateral
  velocity correction; not decoded here (physics).
- `0x4a63cc` is `g_plr_brake_light` (set by the engine code while braking, see PHYSICS.md 5.2); DEVICE 3 clears it on handbrake.
- The meaning of the car-table float at 0x484bf2 + 238*type that turbo sets to 6.0 belongs to the physics doc.

## 11. The port's controller layer (not in the original)

The original reads one winmm / DirectInput joystick with fixed meanings per DEVICE (sections 3-7). The port replaces
that with SDL2 game controllers and a mapping file, and keeps the keyboard path of DEVICE 1 exactly as above.

**Devices** (`src/plat_sdl.c`, `plat.h` `PlatDevice`): every joystick SDL reports is opened when it arrives and closed
when it leaves (`SDL_JOYDEVICEADDED` / `REMOVED`), up to 8 at once. One SDL has a mapping for (XInput / Xbox, DualShock
4 and DualSense through SDL's HIDAPI drivers with rumble enabled, Switch Pro, the Steam Deck and Steam Input's virtual
pads, the built-in database, `gamecontrollerdb.txt` in the user folder or next to the program) is a *game controller*
with Xbox-named elements by position; anything else (wheels, pedals, flight sticks) is a *raw joystick* with numbered
axes, buttons and hats. Haptic raw joysticks (force-feedback wheels) are opened with SDL haptic. The button labels
shown to the player (Xbox, PlayStation, Switch, Steam Deck) come from SDL's controller type, else from the name. The
device with the most recent input is "last used": it rumbles and its labels are shown. The menus see all devices
merged into one pad (`plat_pad`: d-pad, left stick, A / B / Start; a raw joystick's hats and first two buttons).

**Mapping** (`src/pad.c`, `pad.h`, `controller.ini` in the user folder, created with comments at the first use):
ten actions (`steer_left`, `steer_right`, `gas`, `brake`, `turbo`, `handbrake`, `look_back`, `camera`, `horn`,
`pause`) take up to four elements each: game controller buttons (`A`, `LB`, `DPLEFT`, ... and PlayStation aliases),
stick halves (`LEFTX-`), triggers (`LT`), raw `axisN+` / `axisN-` / `axisN` (whole range, a pedal), `buttonN`,
`hatNup`..., `~` inverting an axis. All devices are merged: an action takes its strongest element on any device.
Dead zones (`stick_deadzone`, `trigger_deadzone`, `axis_deadzone`) rescale the rest of the travel to 0..1.

**How the race uses it** (`race.c`, `rules.c`):

| Original | Port |
|---|---|
| DEVICE 2: joystick buttons become virtual keys | the *digital* elements (buttons, hats) are OR-ed with the six Game.ini keys, so gas, brake and steering build up exactly as on the keyboard (5.2), in both steering models |
| DEVICE 3: steer = stick position, gas / brake = stick Y (5.3) | the *analogue* elements (sticks, triggers, raw axes): new model `steer = 100 * pad_steer()`, gas and brake = the trigger pressure * 100 (the larger of key and pad); old model `steer = 30 + 30 * pad_steer()`, `gas = 20 + 20 * (gas - brake)` |
| SENSITIVITY curve (6), old model / DEVICE 4 only | with DEVICE 3 (the menu's wheel / joystick devices, the Sidewinder included: the menu writes it as 3) the 31-entry curve of SENSITIVITY shapes the analogue steering in both models, interpolated between its integer steps; with DEVICE 1 / 2 `steer_linearity` (an exponent) does; `steer_sensitivity` multiplies either |
| in-race keys (8) | `look_back` held = key 3, `camera` cycles 1 / 2 / 4, `horn` = T, `pause` = Esc (the quit question; A = yes, B / Start = no; a pad press ends the P pause); A / B / Start continue the end screens |
| DEVICE 4 FF springs and bumps (7.3) | `SDL_HAPTIC_SPRING` on force-feedback raw joysticks while the race runs (`wheel_spring` percent); rumble on every game controller: `player_car_collision` (0x427de0) rumbles with `0.2 + 0.8 * (J - 2000) / 28000` for `120 + 230 * that` ms from impulse J = 2000 up, the turbo gives a light buzz; `rumble` / `rumble_strength` |

**Menu** (`menu_screens.c` `menu_bediening`, `menu_ui.c`): with the joypad, wheel / joystick or Sidewinder device
chosen the key panel ("Knoppen") shows the first element of the six driving actions and *Instellen* captures them
(`configure_pad`, the same rules as `configure_keys`: an element already used by an earlier function is refused,
one of a later function is swapped; the element leaves the other actions; Start or Esc breaks off). OK writes
controller.ini, Annuleren reloads it; SLOTS.RAS keeps only the keys (its format is unchanged). The name edit box takes
the pad: up / down turn the letter under the cursor (repeating while held), left / right move it, X deletes.

### 11.1 Touch controls (not in the original)

For phones, tablets and Windows touch screens the race draws its own controls (`src/touch.c`, `touch.h`); the menus
keep using touches as SDL's emulated mouse.

**Fingers** (`plat_sdl.c`, `plat_fingers`): `SDL_FINGERDOWN` / `MOTION` / `UP` by finger id, the normalized
coordinates turned into drawable pixels; the mouse posing as a finger (`SDL_MOUSE_TOUCHID`) is ignored. A finger that
goes down and up between two polls is still listed for one poll (`fresh`), so a quick tap is not lost. While a race
runs (`plat_touch_capture`) the touch-emulated mouse presses and moves are dropped (releases still count), so a finger
on the gas does not also click. Focus loss and going to the background lift every finger (Android may not send their
release). `plat_input_kind` remembers whether a finger or a key / controller was used last; the Android back button
(Escape) does not count, as it is how a touch player pauses.

**Showing them** (`controller.ini [touch] show`): `auto` (the default) from the first finger until a key or
controller is used, `on` always, `off` never (fingers then do nothing in the race).

**Layout**, in window pixels, scaled by the shorter side of the window and `size`; drawn after the overlay with
`opacity`, as circles with simple icons (one batch of triangles on `gfx_2d_begin` in window pixels):

| Where | Controls |
|---|---|
| bottom left | steer left / right (`steer=buttons`), or a slider (`steer=drag`); look back above them |
| bottom right | brake (red, down arrow) and gas (green, up arrow); handbrake (P) and turbo (double chevron) above them |
| top left | pause, camera |

The overlay keeps its 4:3 frame, so on a wide screen (phones) its panels sit inside and the controls in the bars beside
it. When the gas / brake cluster would cover the speed panel (HUD.md 4.3) it shrinks to fit beside it (down to 70 %),
or else moves left of it, small enough to stay in the right half (down to 55 %; a 4:3 or 16:10 screen). The info
panel is on the right at the top, so pause and camera are at the top left.

**Fingers and controls**: a finger takes the control it went down on (a little beside a button counts; below the
look-back button the lower left is all steering, below turbo / handbrake the lower right is gas / brake, nearest by
x). A finger on left / right slides between the two, one on gas / brake likewise; every other control keeps its
finger. Fingers are independent, so steering, gas and turbo can be held together. A finger that was already down when
the race began does nothing.

**Into the action layer** (`pad_set_touch`, once per frame from `game_keys`): the touch controls are one more device
of pad.h. Buttons are *digital* (steer left / right, gas, brake, turbo, handbrake, look back, camera, pause), so gas,
brake and steering build up as with the keys (5.2) in the new model and go through `car_old_input` in the old one;
`pad_pressed` compares with the previous frame (camera cycles, pause opens the quit question). The drag slider is
*analogue*: the thumb's distance from where it went down, 1.5 button radii for full lock (the centre follows a thumb
that goes further, so turning back reacts at once), with a 4 % dead zone, as `steer_left` / `steer_right` analogue
values; `pad_steer` then applies `steer_linearity` / the SENSITIVITY curve and `steer_sensitivity` as for a stick,
and the race uses it as DEVICE 3 does (new model `steer = 100 * s`, old model `30 + 30 * s`).

**Tilt** (`tilt=1`, off by default; `-1` reverses it): the accelerometer through SDL's sensor API (`plat_accel`; the
accelerometer-as-joystick of SDL stays off). The device is held in landscape like a wheel, so gravity lies along its x
axis (+ or -, whichever way round Android shows the picture); the steering angle is `atan2(s * y, s * x)` with s the
sign of x, 2 degrees dead zone, `tilt_angle` degrees for full lock, nothing when the device lies flat (less than
3 m/s^2 in the screen's plane). It is analogue steering like the slider, used while no finger steers and only while
the touch controls are shown; the steering buttons are not drawn then.

**The race's questions**: during the quit question (Esc, back button, the pause button) the controls make way for a
tick (quit, as J / Y) and a cross (go on, as N); the pause button stays and also means go on. The P pause ends with a
tap. On the finish results and the game over screen a finger put down there counts as a continue key held
(`continue_key_down`), with the finish's wait for the release; a finger still on the gas from the race does not.

**Tests**: `A2_TEST_TOUCH="T:down ID X Y; T:move ID X Y; T:up ID; T:tap ID X Y; T:accel X Y Z; T:key"` pushes finger
events with `SDL_PushEvent` (X, Y 0..1 of the window), fixes the accelerometer's reading, or presses a key;
`A2_TEST_WINDOW=WxH` sets the window size (a phone's 20:9 is `1600x720`).
