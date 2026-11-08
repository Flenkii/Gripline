/* Noclip - see noclip.h. Own code.

   The car's rigid body is a CCarDynamics; ForzaCar+0x4c08 points to its interface at +0x3a0
   (vtable game+0x2eb4bb8, or game+0x2eb5e18 for the ref-counted class). Its slots 0x1b8 / 0xb58
   read / write the whole body state: position, linear velocity, angular velocity and the
   orientation rows (right, up, forward; world y is up). The game's own "store car position"
   behaviour (game+0x93ca40) reads the same body through slots 0x270 (position) and 0x970
   (orientation row 2 = heading). */
#include <xmmintrin.h>
#include "gripline.h"
#include "game_addresses.h"
#include "logging.h"
#include "support.h"
#include "noclip.h"
#include "keyboard_pad.h"
#include "nitro_link.h"
#include "notify.h"
#include "game_units.h"
#include "game_language.h"
#include "trip.h"
#include "telemetry.h"
#include "logbook.h"
#include "cruise.h"
#include "records.h"
#include "key_menu.h"
#include "wheel_watch.h"
#include "cockpit_view.h"
#include "no_damage.h"
#include "traffic_files.h"
#include "wheel_buttons.h"
#include "walking.h"
#include "drive_look.h"

#define RVA_DYNAMICS_VTABLE 0x2eb4bb8u
#define RVA_DYNAMICS_REF_VTABLE 0x2eb5e18u
#define CAR_DYNAMICS 0x4c08
#define SLOT_GET_STATE 0x1b8
#define SLOT_SET_STATE 0xb58

#define SPEED 20.0f      /* m/s forward/back (72 km/h) */
#define CLIMB 10.0f      /* m/s up/down */
#define FAST 4.0f        /* NoclipFast held */
#define TURN 1.8f        /* rad/s */

/* freecam: slower, for camera shots */
#define FREE_SPEED 8.0f      /* m/s (29 km/h) */
#define FREE_CLIMB 5.0f      /* m/s */
#define FREE_LOOK 1.2f       /* rad/s, arrow keys */
#define FREE_PITCH_MAX 1.4f  /* about 80 degrees up / down */
/* The driver camera sits FREECAM_AHEAD metres in front of the steering wheel (cockpit_view.c),
   so the car is always behind the picture. The steering wheel itself is roughly FREE_WHEEL_UP
   above and FREE_WHEEL_AHEAD in front of the body's position (a guess, the same for every car:
   only used so that turning the view turns about the camera, not about the car). */
#define FREE_WHEEL_UP 0.6f
#define FREE_WHEEL_AHEAD 0.3f
#define FREE_PARK_MS 2000u   /* after freecam: the car is held at its place while the world loads */
#define FREE_DRIVER_WAIT_MS 5000u

typedef struct {
    __m128 position, velocity, spin, right, up, forward;
} BodyState;
_Static_assert(sizeof(BodyState) == 0x60, "body state");

typedef void (*CarUpdateFn)(void *, float, float, unsigned);
typedef void *(*PlayerGetter)(void *, int);
typedef void *(*CarGetter)(void *, void **);
typedef void (*ObjectFn)(void *);
typedef void *(*GetStateFn)(void *, BodyState *);
typedef void (*SetStateFn)(void *, const BodyState *);

static unsigned char *game;
static CarUpdateFn original;
static volatile LONG on; /* 0, MODE_NOCLIP, MODE_FREECAM or MODE_WALK */
static int key_held, free_key_held, walk_key_held, warned;
static ULONGLONG next_online_check;
static int online, online_known;

/* flight, only touched on the game's update thread (and when switching, while the hook is off) */
static void *volatile body;  /* the body being flown */
static float pos[3], dir[2]; /* position, heading (x, z) */
static float mirror;         /* +1: right row = (z, 0, -x) of the heading, -1: the other way */
static float pitch_sin, pitch_cos = 1.0f, pitch; /* freecam only, noclip stays level */
static float cam[3];          /* freecam: the camera's place (the car is moved behind it) */

/* freecam: the car's place before, put back when freecam goes off */
static _Alignas(16) BodyState parked;
static void *volatile parked_body;
static volatile LONG64 park_until;
static ULONGLONG driver_wait;

/* info card (height, speed) */
#define INFO_TAG 0x0c11b000u
#define INFO_EVERY_MS 250u
#define INFO_DELAY_MS 3000u /* the "noclip on" card with the keys first */

static ULONGLONG info_from, info_next, info_time;

int noclip_active(void) {
    return on != 0;
}

int noclip_freecam_active(void) {
    return on == MODE_FREECAM;
}

int noclip_own_look(void) {
    return on == MODE_FREECAM || on == MODE_WALK;
}

/* ------------------------------------------------------------------ controller */

/* XINPUT_GAMEPAD bits */
#define XI_DPAD_UP 0x0001u
#define XI_DPAD_DOWN 0x0002u
#define XI_DPAD_LEFT 0x0004u
#define XI_START 0x0010u
#define XI_BACK 0x0020u
#define XI_LS 0x0040u
#define XI_A 0x1000u
#define XI_B 0x2000u
#define XI_Y 0x8000u
#define PAD_FRESH_MS 250u
#define STICK_DEAD 0.24f
#define TRIGGER_DEAD 0.12f

/* the last reading of each controller (game input thread), used in the car update */
static volatile struct {
    float lx, ly, rx, ry, lt, rt;
    LONG fast, jump, crouch;
    LONG64 tick;
} pads[4];
static unsigned short pad_buttons[4];
static volatile LONG pad_request; /* MODE_* asked for with LS + D-pad, handled on the watch thread */

static float stick(short value) {
    float v = (float)value / 32767.0f, a = v < 0.0f ? -v : v;
    if (a < STICK_DEAD) return 0.0f;
    a = (a - STICK_DEAD) / (1.0f - STICK_DEAD);
    if (a > 1.0f) a = 1.0f;
    return v < 0.0f ? -a : a;
}

static float trigger(unsigned char value) {
    float v = (float)value / 255.0f;
    return v < TRIGGER_DEAD ? 0.0f : (v - TRIGGER_DEAD) / (1.0f - TRIGGER_DEAD);
}

void noclip_pad_input(unsigned user, unsigned char *gamepad) {
    if (user >= 4 || !gamepad) return;
    if (key_menu_pad_input(gamepad)) { /* the Gripline menu has the controller */
        pads[user].tick = 0;
        return;
    }
    unsigned short buttons;
    short axes[4];
    memory_copy(&buttons, gamepad, sizeof(buttons));
    memory_copy(axes, gamepad + 4, sizeof(axes));
    unsigned short pressed = (unsigned short)(buttons & ~pad_buttons[user]);
    pad_buttons[user] = buttons;
    if (buttons & XI_LS) {
        /* LS held + D-pad up = noclip, + D-pad down = freecam, + D-pad left = cruise control (V7), + Y = walking;
           the D-pad press stays away from the game */
        if (pressed & XI_DPAD_UP) InterlockedExchange(&pad_request, MODE_NOCLIP);
        if (pressed & XI_DPAD_DOWN) InterlockedExchange(&pad_request, MODE_FREECAM);
        if (pressed & XI_DPAD_LEFT) cruise_request_toggle();
        if (pressed & XI_Y) InterlockedExchange(&pad_request, MODE_WALK); /* LS + Y: get out (V7 test) */
        buttons &= (unsigned short)~(XI_DPAD_UP | XI_DPAD_DOWN | XI_DPAD_LEFT | XI_Y);
        memory_copy(gamepad, &buttons, sizeof(buttons));
    }
    { /* cruise control: its throttle on the layout's throttle trigger (LT = byte 2, RT = byte 3) */
        int gas = controller_action_input(0), brake = controller_action_input(1);
        cruise_pad(gamepad, gas == 10 ? 2 : gas == 11 ? 3 : -1, brake == 10 ? 2 : brake == 11 ? 3 : -1);
    }
    pads[user].lx = stick(axes[0]), pads[user].ly = stick(axes[1]);
    pads[user].rx = stick(axes[2]), pads[user].ry = stick(axes[3]);
    pads[user].lt = trigger(gamepad[2]), pads[user].rt = trigger(gamepad[3]);
    pads[user].fast = (buttons & XI_A) != 0;
    pads[user].jump = (buttons & XI_A) != 0;
    pads[user].crouch = (buttons & XI_B) != 0; /* walking: B held = crouch (V7.1) */
    pads[user].tick = (LONG64)GetTickCount64();
    if (on == MODE_FREECAM) { /* the right stick looks for the freecam, not for the game's camera */
        short zero[2] = {0, 0};
        memory_copy(gamepad + 8, zero, sizeof(zero));
    }
    if (on == MODE_WALK) {
        /* on foot: Y gets back in; the game only keeps Start and Back (pause, map), the car stands */
        if (pressed & XI_Y) InterlockedExchange(&pad_request, MODE_WALK);
        unsigned short keep = (unsigned short)(buttons & (XI_START | XI_BACK));
        memory_copy(gamepad, &keep, sizeof(keep));
        for (unsigned i = 2; i < 12; ++i) gamepad[i] = 0; /* triggers and sticks */
    }
}

/* all controllers read lately: the strongest of each axis */
typedef struct {
    float lx, ly, rx, ry, lt, rt;
    int fast, jump, crouch;
} PadAxes;

static float stronger(float a, float b) {
    return (b < 0.0f ? -b : b) > (a < 0.0f ? -a : a) ? b : a;
}

static PadAxes pad_axes(void) {
    PadAxes p = {0};
    ULONGLONG now = GetTickCount64();
    for (unsigned i = 0; i < 4; ++i) {
        LONG64 tick = pads[i].tick;
        if (!tick || now - (ULONGLONG)tick > PAD_FRESH_MS) continue;
        p.lx = stronger(p.lx, pads[i].lx), p.ly = stronger(p.ly, pads[i].ly);
        p.rx = stronger(p.rx, pads[i].rx), p.ry = stronger(p.ry, pads[i].ry);
        p.lt = stronger(p.lt, pads[i].lt), p.rt = stronger(p.rt, pads[i].rt);
        p.fast |= pads[i].fast != 0;
        p.jump |= pads[i].jump != 0;
        p.crouch |= pads[i].crouch != 0;
    }
    return p;
}

static float limit1(float x) {
    return x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
}

static int in_game(const void *address) {
    return (const unsigned char *)address >= game && (const unsigned char *)address < game + GAME_IMAGE_SIZE;
}

static void *slot(void *object, unsigned offset) {
    void *vtable, *fn;
    if (!memory_read_pointer(object, &vtable) || !memory_read_pointer((unsigned char *)vtable + offset, &fn) ||
        !in_game(fn))
        return NULL;
    return fn;
}

static void *find_local_car(void) {
    void *manager, *player, *local = NULL;
    if (!memory_read_pointer(game + GAME_RVA_PLAYER_MANAGER, &manager) || !manager) return NULL;
    player = ((PlayerGetter)(game + GAME_RVA_GET_LOCAL_PLAYER))(manager, 0);
    if (!player) return NULL;
    ((CarGetter)(game + GAME_RVA_GET_PLAYER_CAR))(player, &local);
    if (!local) return NULL;
    ObjectFn release = (ObjectFn)slot(local, 0x18);
    if (release) release(local);
    return local;
}

/* The own car, looked up on the game thread at most four times a second (the update runs for
   every car in every frame). */
static void *local_car;
static ULONGLONG local_checked;

static int is_local_car(void *car) {
    void *vtable;
    if (!memory_read_pointer(car, &vtable) || vtable != game + GAME_RVA_PLAYER_CAR_VTABLE) return 0;
    ULONGLONG now = GetTickCount64();
    if (now - local_checked >= 250) {
        local_checked = now;
        local_car = find_local_car();
    }
    return local_car == car;
}

static void *car_body(void *car) {
    void *dynamics, *vtable;
    if (!memory_read_pointer((unsigned char *)car + CAR_DYNAMICS, &dynamics) || !dynamics ||
        !memory_read_pointer(dynamics, &vtable) ||
        (vtable != game + RVA_DYNAMICS_VTABLE && vtable != game + RVA_DYNAMICS_REF_VTABLE))
        return NULL;
    return dynamics;
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}

static int heading_from(const float forward[3], const float right[3]) {
    float length = root(forward[0] * forward[0] + forward[2] * forward[2]);
    if (!(length > 0.1f)) { /* nose straight up or down: the right row still lies flat */
        length = root(right[0] * right[0] + right[2] * right[2]);
        if (!(length > 0.1f)) return 0;
        dir[0] = -right[2] / length;
        dir[1] = right[0] / length;
    } else {
        dir[0] = forward[0] / length;
        dir[1] = forward[2] / length;
    }
    /* which way the game's right row points relative to the heading */
    mirror = right[0] * dir[1] - right[2] * dir[0] >= 0.0f ? 1.0f : -1.0f;
    return 1;
}

static int begin(void *dynamics) {
    GetStateFn get = (GetStateFn)slot(dynamics, SLOT_GET_STATE);
    if (!get) return 0;
    _Alignas(16) BodyState state;
    get(dynamics, &state);
    _Alignas(16) float p[4], f[4], r[4];
    _mm_store_ps(p, state.position);
    _mm_store_ps(f, state.forward);
    _mm_store_ps(r, state.right);
    for (unsigned i = 0; i < 3; ++i) {
        if (!float_is_finite(p[i]) || !float_is_finite(f[i]) || !float_is_finite(r[i])) return 0;
    }
    if (!heading_from(f, r)) return 0;
    pos[0] = p[0], pos[1] = p[1] + 0.5f, pos[2] = p[2]; /* a little off the road */
    pitch = 0.0f, pitch_sin = 0.0f, pitch_cos = 1.0f;
    if (on == MODE_FREECAM || on == MODE_WALK) {
        parked = state;
        parked.velocity = _mm_setzero_ps();
        parked.spin = _mm_setzero_ps();
        parked_body = dynamics;
        /* the camera starts where the car would put it; the car does not move */
        float ahead = FREE_WHEEL_AHEAD + FREECAM_AHEAD;
        cam[0] = pos[0] + dir[0] * ahead, cam[1] = pos[1] + FREE_WHEEL_UP, cam[2] = pos[2] + dir[1] * ahead;
    }
    if (on == MODE_WALK) walking_enter(p, r, f); /* the walker gets out next to the parked car */
    body = dynamics;
    return 1;
}

/* the flown state: level (freecam: pitched), no speed, so the game's physics has nothing to move */
static void place(void *dynamics) {
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (!set) return;
    _Alignas(16) BodyState state;
    state.position = _mm_setr_ps(pos[0], pos[1], pos[2], 0.0f);
    state.velocity = _mm_setzero_ps();
    state.spin = _mm_setzero_ps();
    state.right = _mm_setr_ps(mirror * dir[1], 0.0f, -mirror * dir[0], 0.0f);
    state.up = _mm_setr_ps(-pitch_sin * dir[0], pitch_cos, -pitch_sin * dir[1], 0.0f);
    state.forward = _mm_setr_ps(pitch_cos * dir[0], pitch_sin, pitch_cos * dir[1], 0.0f);
    set(dynamics, &state);
}

static void put_back(void *dynamics) {
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (set) set(dynamics, &parked);
}

/* walking: the car at its place, or (far from it) under the ground below the walker, see walking.h */
static void hold_walking(void *dynamics) {
    float at[3];
    if (!walking_car_hidden(at) || !float_is_finite(at[0]) || !float_is_finite(at[1]) || !float_is_finite(at[2])) {
        put_back(dynamics);
        return;
    }
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (!set) return;
    _Alignas(16) BodyState state = parked;
    state.position = _mm_setr_ps(at[0], at[1], at[2], 0.0f);
    set(dynamics, &state);
}

static void sin_cos(float a, float *s, float *c) {
    float a2 = a * a; /* |a| <= 1.4: the series is exact to about 1e-4 */
    *s = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
    *c = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f)));
}

/* turns the heading by a small angle (positive = left, away from the right row) */
static void turn_heading(float a) {
    float a2 = a * a;
    float s = a * (1.0f - a2 / 6.0f + a2 * a2 / 120.0f), c = 1.0f - a2 / 2.0f + a2 * a2 / 24.0f;
    float left_x = -mirror * dir[1], left_z = mirror * dir[0];
    float x = dir[0] * c + left_x * s, z = dir[1] * c + left_z * s;
    float length = root(x * x + z * z);
    if (length > 0.5f) dir[0] = x / length, dir[1] = z / length;
}

static void fly(float dt) {
    if (!(dt > 0.0f)) return;
    if (dt > 0.1f) dt = 0.1f;
    PadAxes pad = pad_axes(); /* left stick: forward/back and turn, RT/LT up/down, A faster */
    float boost = keyboard_held(KEYBOARD_HOLD_FAST) || pad.fast ? FAST : 1.0f;
    float turn = limit1((float)(keyboard_held(KEYBOARD_HOLD_LEFT) - keyboard_held(KEYBOARD_HOLD_RIGHT)) - pad.lx);
    float ahead = limit1((float)(keyboard_held(KEYBOARD_HOLD_FORWARD) - keyboard_held(KEYBOARD_HOLD_BACK)) + pad.ly);
    float climb = limit1((float)(keyboard_held(KEYBOARD_HOLD_UP) - keyboard_held(KEYBOARD_HOLD_DOWN)) + pad.rt - pad.lt);
    /* left = minus the right row; small angle per update, so a short series is exact enough */
    if (turn != 0.0f) turn_heading(turn * TURN * dt);
    pos[0] += dir[0] * ahead * SPEED * boost * dt;
    pos[2] += dir[1] * ahead * SPEED * boost * dt;
    pos[1] += climb * CLIMB * boost * dt;
}

/* Freecam: arrow keys look (turn, pitch), W/S fly where the camera looks, A/D sideways,
   Space/Ctrl up/down. The camera point moves; the car is put behind it. */
static void fly_free(float dt) {
    if (!(dt > 0.0f)) return;
    if (dt > 0.1f) dt = 0.1f;
    PadAxes pad = pad_axes(); /* left stick flies, right stick looks, RT/LT up/down, A faster */
    float boost = keyboard_held(KEYBOARD_HOLD_FAST) || pad.fast ? FAST : 1.0f;
    float yaw = limit1((float)(keyboard_held(KEYBOARD_HOLD_LOOK_LEFT) - keyboard_held(KEYBOARD_HOLD_LOOK_RIGHT)) - pad.rx);
    float tilt = limit1((float)(keyboard_held(KEYBOARD_HOLD_LOOK_UP) - keyboard_held(KEYBOARD_HOLD_LOOK_DOWN)) + pad.ry);
    float ahead = limit1((float)(keyboard_held(KEYBOARD_HOLD_FORWARD) - keyboard_held(KEYBOARD_HOLD_BACK)) + pad.ly);
    float side = limit1((float)(keyboard_held(KEYBOARD_HOLD_RIGHT) - keyboard_held(KEYBOARD_HOLD_LEFT)) + pad.lx);
    float climb = limit1((float)(keyboard_held(KEYBOARD_HOLD_UP) - keyboard_held(KEYBOARD_HOLD_DOWN)) + pad.rt - pad.lt);
    if (yaw != 0.0f) turn_heading(yaw * FREE_LOOK * dt);
    if (tilt != 0.0f) {
        pitch += tilt * FREE_LOOK * dt;
        if (pitch > FREE_PITCH_MAX) pitch = FREE_PITCH_MAX;
        if (pitch < -FREE_PITCH_MAX) pitch = -FREE_PITCH_MAX;
    }
    sin_cos(pitch, &pitch_sin, &pitch_cos);
    float f[3] = {pitch_cos * dir[0], pitch_sin, pitch_cos * dir[1]};
    float r[3] = {mirror * dir[1], 0.0f, -mirror * dir[0]};
    float u[3] = {-pitch_sin * dir[0], pitch_cos, -pitch_sin * dir[1]};
    float step = FREE_SPEED * boost * dt;
    for (unsigned i = 0; i < 3; ++i) cam[i] += (f[i] * ahead + r[i] * side) * step;
    cam[1] += climb * FREE_CLIMB * boost * dt;
    float behind = FREE_WHEEL_AHEAD + FREECAM_AHEAD;
    for (unsigned i = 0; i < 3; ++i) pos[i] = cam[i] - f[i] * behind - u[i] * FREE_WHEEL_UP;
}

/* walking.c: one step's input. Keys: Throttle/Brake forward/back, SteerLeft/SteerRight sideways,
   arrows and the mouse look, NoclipFast (LShift) runs, NoclipUp (Space) jumps, NoclipDown (LCtrl)
   held crouches. Controller: left stick walks, right stick looks, RT runs, A jumps, B held crouches. */
void noclip_walk_input(WalkInput *in) {
    PadAxes pad = pad_axes();
    in->yaw = limit1((float)(keyboard_held(KEYBOARD_HOLD_LOOK_LEFT) - keyboard_held(KEYBOARD_HOLD_LOOK_RIGHT)) - pad.rx);
    in->pitch = limit1((float)(keyboard_held(KEYBOARD_HOLD_LOOK_UP) - keyboard_held(KEYBOARD_HOLD_LOOK_DOWN)) + pad.ry);
    in->ahead = limit1((float)(keyboard_held(KEYBOARD_HOLD_FORWARD) - keyboard_held(KEYBOARD_HOLD_BACK)) + pad.ly);
    in->side = limit1((float)(keyboard_held(KEYBOARD_HOLD_RIGHT) - keyboard_held(KEYBOARD_HOLD_LEFT)) + pad.lx);
    in->run = keyboard_held(KEYBOARD_HOLD_FAST) || pad.rt > 0.5f;
    in->jump = keyboard_held(KEYBOARD_HOLD_UP) || pad.jump;
    in->crouch = keyboard_held(KEYBOARD_HOLD_DOWN) || pad.crouch;
    keyboard_walk_mouse(&in->mouse_x, &in->mouse_y);
}

static void card(const char *german, const char *english) {
    notify_show(game_language_is_german(game) ? german : english);
}

static void tinted(const char *german, const char *english, int color) {
    int de = game_language_is_german(game);
    /* locked online: always the red warning, whatever card colour is set (rainbow too) */
    notify_show_tinted(de ? german : english, color, color == NOTIFY_COLOR_RED ? (de ? "GESPERRT" : "LOCKED") : NULL, 0);
}

static NoclipHookFn switch_hook;
static int view_before = -1, second_before, view_changed; /* V7.1, see fade_begin */

static void put_int_card(char *out, size_t *n, int value) {
    char digits[12];
    unsigned d = 0, v = value < 0 ? (unsigned)-value : (unsigned)value;
    do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
    if (value < 0) out[(*n)++] = '-';
    while (d) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

static void put_text_card(char *out, size_t *n, const char *text) {
    while (*text) out[(*n)++] = *text++;
    out[*n] = 0;
}

static int hooked;

static void set_on(LONG value) {
    if (value && !hooked) {
        log_append("NOCLIP: the car update hook is not on.\r\n");
        return;
    }
    LONG before = on;
    if (before == MODE_FREECAM && value != MODE_FREECAM) {
        /* the car back to where freecam started, held there a moment */
        InterlockedExchange64(&park_until, (LONG64)(GetTickCount64() + FREE_PARK_MS));
        cockpit_view_want_driver(0);
    }
    if (value == MODE_FREECAM) {
        parked_body = NULL;
        InterlockedExchange64(&park_until, 0);
        driver_wait = GetTickCount64() + FREE_DRIVER_WAIT_MS;
        cockpit_view_want_driver(1); /* the freecam is the driver camera, moved out of the car */
    }
    if (before == MODE_WALK && value != MODE_WALK && value != MODE_FREECAM && view_changed) {
        cockpit_view_want_camera(view_before, second_before); /* back in the car: the view from before */
        view_changed = 0;
    }
    if (before == MODE_WALK && value == MODE_FREECAM) view_changed = 0;
    if (before == MODE_WALK && value != MODE_WALK) /* the car back at its place (it may be under the walker) */
        InterlockedExchange64(&park_until, (LONG64)(GetTickCount64() + FREE_PARK_MS));
    if (value == MODE_WALK) {
        parked_body = NULL;
        InterlockedExchange64(&park_until, 0);
    }
    body = NULL;
    InterlockedExchange(&on, value);
    walking_set_active(value == MODE_WALK);
}

/* V7 walking, getting out and in (Marvin 06.11.2026): fade to black, door opens (the switch happens
   now, so the camera is settled), right after it the door shuts, 2 s dark, fade back to the picture. */
#define FADE_DOWN_MS 300
#define FADE_SHUT_MS 800  /* after the door opened: the opening sound is over */
#define FADE_HOLD_MS 2000 /* dark in all */
#define FADE_UP_MS 450
static int fade_stage; /* 0 none, 1 getting dark, 2 dark, 3 getting light (watch thread only) */
static LONG fade_to;   /* MODE_WALK = get out, 0 = get in */
static ULONGLONG fade_at;
static int fade_shut; /* the door has shut (sound played) */

static void walk_out_card(void) {
    card("Ausgestiegen - Walking an\nWASD/linker Stick laufen, Maus/Pfeile/rechter Stick schauen, Shift/RT rennen, "
         "Strg/B ducken, Leertaste/A springen, F/Y einsteigen",
         "Out of the car - walking on\nWASD/left stick walk, mouse/arrows/right stick look, Shift/RT run, "
         "Ctrl/B crouch, Space/A jump, F/Y get in");
    /* V7.1 (Marvin 07.11.2026): mouse steering pauses while walking, the mouse looks around instead */
    if (keyboard_mouse_steering()) {
        int de = game_language_is_german(game);
        notify_show_tinted(de ? "Maus-Lenkung pausiert\nBeim Laufen schaut die Maus, im Auto lenkt sie wieder."
                              : "Mouse steering paused\nWhile walking the mouse looks around, back in the car it steers again.",
                           NOTIFY_COLOR_ORANGE, de ? "HINWEIS" : "NOTE", 0);
        log_append("NOCLIP: walking - mouse steering paused until back in the car.\r\n");
    }
}

/* V7.1 (Marvin 07.11.2026): out of a cockpit view the car would be seen as the game's cut interior
   model. In the black the camera goes outside first; back in the car the old view comes back. */
#define FADE_CAMERA_WAIT_MS 3000 /* at most this much longer black while the camera changes */
static void fade_begin(LONG to) {
    fade_to = to;
    fade_stage = 1;
    fade_at = GetTickCount64();
    notify_fade_black(1000, FADE_DOWN_MS);
    if (to == MODE_WALK) {
        view_before = walking_view_camera();
        second_before = cockpit_view_second();
        view_changed = view_before >= 0 && !walking_view_outside();
        if (view_changed) cockpit_view_want_outside();
    }
}

static void fade_stop(void) { /* online or a failed switch: the picture back at once */
    if (!fade_stage) return;
    fade_stage = 0;
    notify_fade_black(0, 200);
}

static void fade_tick(ULONGLONG now) {
    if (fade_stage == 1 && now >= fade_at + FADE_DOWN_MS) {
        /* out of the car: wait in the black until the outside camera is there */
        if (fade_to == MODE_WALK && cockpit_view_want_pending() && now < fade_at + FADE_DOWN_MS + FADE_CAMERA_WAIT_MS)
            return;
        walking_door_sound(0);
        set_on(fade_to);
        if (on != fade_to) { /* not switched (see the log) */
            if (fade_to == MODE_WALK && view_changed) cockpit_view_want_camera(view_before, second_before), view_changed = 0;
            fade_stop();
            return;
        }
        if (fade_to == MODE_WALK) {
            info_from = now + FADE_HOLD_MS + INFO_DELAY_MS;
            log_append("NOCLIP: walking on (out of the car)\r\n");
        } else {
            log_append("NOCLIP: walking off - back in the car.\r\n");
        }
        fade_stage = 2;
        fade_at = now;
        fade_shut = 0;
    } else if (fade_stage == 2 && !fade_shut && now >= fade_at + FADE_SHUT_MS) {
        fade_shut = 1;
        walking_door_sound(1);
    } else if (fade_stage == 2 && now >= fade_at + FADE_HOLD_MS &&
               !(cockpit_view_want_pending() && now < fade_at + FADE_HOLD_MS + FADE_CAMERA_WAIT_MS)) {
        notify_fade_black(0, FADE_UP_MS);
        if (fade_to == MODE_WALK) walk_out_card();
        else card("Eingestiegen", "Back in the car");
        fade_stage = 3;
        fade_at = now;
    } else if (fade_stage == 3 && now >= fade_at + FADE_UP_MS) {
        fade_stage = 0;
    }
}

/* walking: the walk key / controller Y near the car gets back in */
static void walk_key(void) {
    float distance = walking_car_distance();
    if (distance >= 0.0f && distance <= WALK_ENTER_DISTANCE) {
        fade_begin(0); /* in the car while the picture is black */
        return;
    }
    char de[96], en[96];
    size_t n = 0, m = 0;
    int metres = distance < 0.0f ? 0 : (int)(distance + 0.5f);
    de[0] = en[0] = 0;
    put_text_card(de, &n, "Zu weit vom Auto weg (");
    put_int_card(de, &n, metres);
    put_text_card(de, &n, " m)\nGeh n\xc3\xa4her ran zum Einsteigen (F6/F7 = sofort zur\xc3\xbc""ck)");
    put_text_card(en, &m, "Too far from the car (");
    put_int_card(en, &m, metres);
    put_text_card(en, &m, " m)\nWalk closer to get in (F6/F7 = back at once)");
    card(de, en);
}

static void handle_toggle(void) {
    ULONGLONG now = GetTickCount64();
    if (now >= next_online_check) {
        next_online_check = now + 500;
        int was_online = online;
        online = game_session_online();
        if (online != was_online && online_known) {
            /* like Nitro: a card when the session goes online (noclip locked) and back offline */
            if (online) {
                if (on) set_on(0);
                log_append("NOCLIP: online session - noclip and freecam locked.\r\n");
                tinted("Noclip/Freecam/Walking gesperrt\nOnline-Sitzung (FH2MP)",
                       "Noclip/freecam/walking locked\nOnline session (FH2MP)", NOTIFY_COLOR_RED);
            } else {
                log_append("NOCLIP: offline again - noclip and freecam available.\r\n");
                tinted("Noclip/Freecam wieder verf\xc3\xbcgbar\nOffline - Taste F6 / F7",
                       "Noclip/freecam available again\nOffline - key F6 / F7", NOTIFY_COLOR_GREEN);
            }
        }
        online_known = 1;
    }
    if (online) fade_stop();
    if (on && online) { /* the first check already online */
        set_on(0);
        log_append("NOCLIP: online session - noclip / freecam off.\r\n");
        tinted("Noclip/Freecam gesperrt\nOnline-Sitzung (FH2MP)", "Noclip/freecam locked\nOnline session (FH2MP)",
               NOTIFY_COLOR_RED);
    }
    if (on == MODE_FREECAM && driver_wait && now >= driver_wait) {
        driver_wait = 0;
        if (!cockpit_view_driver_active()) {
            log_append("NOCLIP: freecam - the driver camera did not come (camera change not taken).\r\n");
            card("Freecam: bitte Cockpit-Kamera w\xc3\xa4hlen\n(Kamera wechseln, Tab)",
                 "Freecam: please pick the cockpit camera\n(change camera, Tab)");
        }
    }
    int held = keyboard_held(KEYBOARD_HOLD_NOCLIP), free_held = keyboard_held(KEYBOARD_HOLD_FREECAM);
    int walk_held = keyboard_held(KEYBOARD_HOLD_WALK);
    int pressed = held && !key_held, free_pressed = free_held && !free_key_held, walk_pressed = walk_held && !walk_key_held;
    key_held = held, free_key_held = free_held, walk_key_held = walk_held;
    LONG request = InterlockedExchange(&pad_request, 0); /* controller: LS + D-pad up / down / right, Y */
    if (request == MODE_NOCLIP) pressed = 1;
    if (request == MODE_FREECAM) free_pressed = 1;
    if (request == MODE_WALK) walk_pressed = 1;
    if (fade_stage) { /* getting out / in: keys wait until the picture is back */
        fade_tick(now);
        return;
    }
    if (!pressed && !free_pressed && !walk_pressed) return;
    LONG mode = pressed ? MODE_NOCLIP : free_pressed ? MODE_FREECAM : MODE_WALK;
    if (on == MODE_WALK && mode == MODE_WALK) {
        walk_key();
        return;
    }
    if (on == MODE_WALK) { /* F6 / F7 while walking: back in the car at once */
        set_on(0);
        log_append("NOCLIP: walking off (noclip/freecam key) - back in the car.\r\n");
        card("Walking aus\nZur\xc3\xbc""ck im Auto", "Walking off\nBack in the car");
        return;
    }
    if (on) { /* either key ends what is on */
        LONG was = on;
        set_on(0);
        log_append(was == MODE_FREECAM ? "NOCLIP: freecam off\r\n" : "NOCLIP: off\r\n");
        if (was == MODE_FREECAM) card("Freecam aus\nAuto steht wieder am Startpunkt", "Freecam off\nCar back where it was");
        else card("Noclip aus", "Noclip off");
        return;
    }
    if (!game_simulation_running()) {
        log_append("NOCLIP: not now - the game is in a menu or loading.\r\n");
        return;
    }
    if (mode == MODE_WALK && !walking_ready()) {
        log_append("NOCLIP: walking not ready (see WALKING above).\r\n");
        card("Walking geht nicht\nSpiel-Code anders (Gripline.log)", "Walking not working\nGame code differs (Gripline.log)");
        return;
    }
    if (online) {
        log_append("NOCLIP: refused - online session.\r\n");
        if (mode == MODE_WALK)
            tinted("Walking gesperrt\nNur offline (nicht in FH2MP)", "Walking locked\nOffline only (not in FH2MP)",
                   NOTIFY_COLOR_RED);
        else if (mode == MODE_FREECAM)
            tinted("Freecam gesperrt\nNur offline (nicht in FH2MP)", "Freecam locked\nOffline only (not in FH2MP)",
                   NOTIFY_COLOR_RED);
        else
            tinted("Noclip gesperrt\nNur offline (nicht in FH2MP)", "Noclip locked\nOffline only (not in FH2MP)",
                   NOTIFY_COLOR_RED);
        return;
    }
    if (mode == MODE_WALK) { /* out of the car while the picture is black */
        fade_begin(MODE_WALK);
        return;
    }
    set_on(mode);
    if (!on) return;
    info_from = GetTickCount64() + INFO_DELAY_MS;
    if (mode == MODE_FREECAM) {
        log_append("NOCLIP: freecam on\r\n");
        card("Freecam an\nPfeile/rechter Stick schauen, WASD/linker Stick fliegen, Leertaste/Strg/RT/LT hoch/runter",
             "Freecam on\nArrows/right stick look, WASD/left stick fly, Space/Ctrl/RT/LT up/down");
    } else {
        log_append("NOCLIP: on\r\n");
        card("Noclip an\nW/S/linker Stick fliegen, A/D drehen, Leertaste/Strg/RT/LT hoch/runter",
             "Noclip on\nW/S/left stick fly, A/D turn, Space/Ctrl/RT/LT up/down");
    }
}

/* the own car's speed for the trip summary, after the game's update */
static void sample_trip(void *car, float dt) {
    void *dynamics = car_body(car);
    GetStateFn get = dynamics ? (GetStateFn)slot(dynamics, SLOT_GET_STATE) : NULL;
    if (!get) return;
    _Alignas(16) BodyState state;
    get(dynamics, &state);
    _Alignas(16) float v[4];
    _mm_store_ps(v, state.velocity);
    if (!(float_is_finite(v[0]) && float_is_finite(v[1]) && float_is_finite(v[2]))) return;
    trip_sample(dt, v);
    logbook_sample(car, dt, v);
    {
        _Alignas(16) float p[4];
        _mm_store_ps(p, state.position);
        if (float_is_finite(p[0]) && float_is_finite(p[1]) && float_is_finite(p[2])) {
            records_sample(dt, p, root(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
            drive_look_car(p, root(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])); /* V7.1 mouse look: pivot, speed */
        }
    }
    _Alignas(16) float r[4], f[4], w[4];
    _mm_store_ps(r, state.right);
    _mm_store_ps(f, state.forward);
    _mm_store_ps(w, state.spin);
    if (float_is_finite(r[0]) && float_is_finite(r[2]) && float_is_finite(f[0]) && float_is_finite(f[1]) &&
        float_is_finite(f[2]) && float_is_finite(w[1]))
        telemetry_sample(dt, v, r, f, w);
    if (float_is_finite(f[0]) && float_is_finite(f[1]) && float_is_finite(f[2])) cruise_sample(dt, v, f);
    if (float_is_finite(r[0]) && float_is_finite(r[2]) && float_is_finite(f[0]) && float_is_finite(f[2])) drive_look_mirror(r, f);
}

/* V7.1 walking (Marvin 07.11.2026): out of the car the driver figure still sat in it. The game's own
   "draw the driver" switch of the car (game+0xf75500 (car, on): the driver model's slot 0x358, the
   flag at car+0x4f10; the game calls it itself, e.g. in photo mode) hides it while walking and puts
   the car's own setting back after. Checked against the game's code first. */
#define RVA_SET_DRAW_DRIVER 0xf75500u
#define CAR_DRAW_DRIVER 0x4f10u
typedef void (*SetDrawDriverFn)(void *car, unsigned char on);
static const unsigned char set_draw_driver_code[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20,
                                                     0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x89, 0x38, 0x4d, 0x00, 0x00};
static int draw_driver_ok = -1; /* -1 not checked, 0 game code differs, 1 ok */
static void *driver_hidden_car;
static unsigned char driver_was_drawn;

static int draw_driver_ready(void) {
    if (draw_driver_ok < 0) {
        draw_driver_ok = game && memory_equal(game + RVA_SET_DRAW_DRIVER, set_draw_driver_code, sizeof(set_draw_driver_code));
        log_append(draw_driver_ok ? "NOCLIP: walking - the driver figure is hidden while walking.\r\n"
                                  : "NOCLIP: walking - game code differs, the driver figure stays visible.\r\n");
    }
    return draw_driver_ok;
}

/* car update thread: walking on = hidden (again, if the game drew it again), off = back */
static void walk_driver(void *car, int walking) {
    if (!draw_driver_ready()) return;
    unsigned char drawn = 0;
    if (walking) {
        if (!memory_read((unsigned char *)car + CAR_DRAW_DRIVER, &drawn, 1)) return;
        if (driver_hidden_car != car) driver_hidden_car = car, driver_was_drawn = drawn;
        else if (drawn) driver_was_drawn = 1; /* the game switched it on meanwhile (camera change) */
        if (drawn) ((SetDrawDriverFn)(game + RVA_SET_DRAW_DRIVER))(car, 0);
    } else if (driver_hidden_car == car) {
        driver_hidden_car = NULL;
        if (driver_was_drawn) ((SetDrawDriverFn)(game + RVA_SET_DRAW_DRIVER))(car, 1);
    }
}

void noclip_car_update(void *car, float dt, float frame_dt, unsigned frame) {
    void *dynamics = NULL;
    int local = game && car && is_local_car(car);
    if (local) traffic_load_reached(dt); /* the start reached the road (load guard, traffic_files.c) */
    void *park = NULL;
    if (local && (on == MODE_WALK || driver_hidden_car == car)) walk_driver(car, on == MODE_WALK && walking_active());
    if (on == MODE_WALK && local) {
        /* on foot: the car stands where it was; the walker is moved in the camera (walking.c) */
        void *walk_body = car_body(car);
        if (walk_body && walk_body != body && !begin(walk_body)) walk_body = NULL;
        if (walk_body && walk_body == parked_body) park = walk_body, hold_walking(park);
    } else if (on && local) {
        dynamics = car_body(car);
        if (dynamics && dynamics != body && !begin(dynamics)) dynamics = NULL;
        if (!dynamics && !warned) {
            warned = 1;
            log_append("NOCLIP: the car's body was not recognised - noclip does nothing.\r\n");
            card("Noclip geht nicht\nAuto nicht erkannt (Gripline.log)", "Noclip not working\nCar not recognised (Gripline.log)");
        }
        if (dynamics) {
            if (on == MODE_FREECAM) fly_free(dt);
            else fly(dt);
            place(dynamics); /* the update starts from the flown place ... */
        }
    }
    if (!on && local && park_until) {
        if (GetTickCount64() < (ULONGLONG)park_until && parked_body && parked_body == car_body(car)) {
            park = parked_body;
            put_back(park);
        } else {
            InterlockedExchange64(&park_until, 0);
        }
    }
    original(car, dt, frame_dt, frame);
    if (dynamics && on) place(dynamics); /* ... and what the game moved in it is taken back */
    else if (park && on == MODE_WALK) hold_walking(park);
    else if (park) put_back(park);
    else if (local && !on) sample_trip(car, dt);
    no_damage_after_update(car, local, local_car != NULL);
}

/* ------------------------------------------------------------------ info card */

static float info_pos[3];
static int info_have;

static void put_int(char *out, size_t *n, int value) {
    char digits[12];
    unsigned d = 0, v = value < 0 ? (unsigned)-value : (unsigned)value;
    do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
    if (value < 0) out[(*n)++] = '-';
    while (d) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

static void put_text(char *out, size_t *n, const char *text) {
    while (*text) out[(*n)++] = *text++;
    out[*n] = 0;
}

/* height (the game's world y, metres) and speed while flying, a live card at the top right */
static ULONGLONG units_next;
static int imperial;

static void walk_info(ULONGLONG now) {
    int de = game_language_is_german(game);
    if (now >= units_next) {
        units_next = now + 3000;
        imperial = game_units_imperial(game);
    }
    int kmh = walking_speed_kmh();
    float distance = walking_car_distance();
    char text[128];
    size_t n = 0;
    text[0] = 0;
    put_text(text, &n, de ? "Zu Fu\xc3\x9f - " : "On foot - ");
    put_int(text, &n, imperial ? (int)(kmh / 1.609344f + 0.5f) : kmh);
    put_text(text, &n, imperial ? " mph\n" : " km/h\n");
    if (distance >= 0.0f && distance <= WALK_ENTER_DISTANCE) {
        put_text(text, &n, de ? "Einsteigen: F / Y" : "Get in: F / Y");
    } else if (distance >= 0.0f) {
        put_text(text, &n, de ? "Auto: " : "Car: ");
        put_int(text, &n, (int)(imperial ? distance * 3.28084f + 0.5f : distance + 0.5f));
        put_text(text, &n, imperial ? " ft" : " m");
    }
    if (!walking_has_collision()) put_text(text, &n, de ? " (ohne Kollision)" : " (no collision)");
    else if (walking_no_floor()) put_text(text, &n, de ? " (hier kein Boden geladen)" : " (no floor loaded here)");
    /* V7.1: the walking gauge at the bottom right shows speed and car already; the card then only
       comes for the notes about the collision */
    if (walk_gauge_style() != WALK_GAUGE_OFF && walking_has_collision() && !walking_no_floor()) return;
    if (notify_kind_on(NOTIFY_KIND_LIVE)) notify_show_live(text, INFO_TAG, 1000);
}

static void info_step(ULONGLONG now) {
    if (!on || !body) {
        info_have = 0;
        return;
    }
    if (now < info_from || now < info_next) return;
    info_next = now + INFO_EVERY_MS;
    if (on == MODE_WALK) {
        walk_info(now);
        return;
    }
    int free = on == MODE_FREECAM;
    float p[3] = {free ? cam[0] : pos[0], free ? cam[1] : pos[1], free ? cam[2] : pos[2]};
    int kmh = 0;
    if (info_have && now > info_time) {
        float dx = p[0] - info_pos[0], dy = p[1] - info_pos[1], dz = p[2] - info_pos[2];
        kmh = (int)(root(dx * dx + dy * dy + dz * dz) / ((float)(now - info_time) / 1000.0f) * 3.6f + 0.5f);
    }
    info_pos[0] = p[0], info_pos[1] = p[1], info_pos[2] = p[2];
    info_time = now;
    info_have = 1;
    int de = game_language_is_german(game);
    if (now >= units_next) { /* the game's Units option (mph / km/h), read again every few seconds */
        units_next = now + 3000;
        imperial = game_units_imperial(game);
    }
    float height = imperial ? p[1] * 3.28084f : p[1];
    char text[96];
    size_t n = 0;
    text[0] = 0;
    put_text(text, &n, free ? "Freecam - " : "Noclip - ");
    put_int(text, &n, imperial ? (int)(kmh / 1.609344f + 0.5f) : kmh);
    put_text(text, &n, imperial ? " mph\n" : " km/h\n");
    put_text(text, &n, de ? "H\xc3\xb6he " : "Height ");
    put_int(text, &n, (int)(height < 0 ? height - 0.5f : height + 0.5f));
    put_text(text, &n, imperial ? " ft" : " m");
    if (notify_kind_on(NOTIFY_KIND_LIVE)) notify_show_live(text, INFO_TAG, 1000);
}

/* The key, the online check, the noclip card, the trip card and the wheel watch run on an own thread; in the game's
   update Gripline only follows the own car. */
static DWORD WINAPI watch_thread(LPVOID unused) {
    (void)unused;
    for (;;) {
        handle_toggle();
        ULONGLONG now = GetTickCount64();
        info_step(now);
        trip_step(now);
        telemetry_step(now);
        logbook_step(now);
        cruise_step(now, game);
        records_step(now);
        wheel_watch_step(game, now);
        Sleep(30);
    }
}

void noclip_start(unsigned char *game_image, void *original_update, NoclipHookFn hook_switch) {
    original = (CarUpdateFn)original_update;
    game = game_image;
    switch_hook = hook_switch;
    trip_start(game_image);
    telemetry_start(game_image);
    /* on for the whole session: noclip and the trip summary both follow the own car */
    hooked = switch_hook && switch_hook(1);
    if (!hooked) log_append("NOCLIP: the car update hook could not be switched on - noclip and trip card off.\r\n");
    HANDLE thread = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else log_append("NOCLIP: no thread - noclip off.\r\n");
}

#ifdef FH2_TWEAKS_TEST
/* one flown update of a body, without the car lookup (tests/test_noclip.py) */
__declspec(dllexport) int Fh2TestNoclipFly(unsigned char *game_image, void *dynamics, unsigned dt_ms) {
    game = game_image;
    if (dynamics != body && !begin(dynamics)) return 0;
    fly((float)dt_ms / 1000.0f);
    place(dynamics);
    return 1;
}
/* sets the mode directly (tests/test_walking.py) */
__declspec(dllexport) void Fh2TestNoclipMode(int mode) {
    on = mode;
    walking_set_active(mode == MODE_WALK);
}
/* one controller reading (XINPUT_GAMEPAD, 12 bytes) as the game's controller hook gives it */
__declspec(dllexport) unsigned Fh2TestNoclipPad(unsigned user, unsigned char *gamepad) {
    noclip_pad_input(user, gamepad);
    return (unsigned)InterlockedExchange(&pad_request, 0);
}
/* freecam flight without a body (tests/test_freecam.py): reset = start at the origin, heading +z;
   out = position, camera, forward, up (3 floats each) */
__declspec(dllexport) void Fh2TestFreecamStep(int reset, unsigned dt_ms, float *out) {
    if (reset) {
        on = MODE_FREECAM;
        dir[0] = 0.0f, dir[1] = 1.0f, mirror = 1.0f;
        pitch = 0.0f, pitch_sin = 0.0f, pitch_cos = 1.0f;
        pos[0] = pos[1] = pos[2] = 0.0f;
        cam[0] = 0.0f, cam[1] = FREE_WHEEL_UP, cam[2] = FREE_WHEEL_AHEAD + FREECAM_AHEAD;
    }
    fly_free((float)dt_ms / 1000.0f);
    float f[3] = {pitch_cos * dir[0], pitch_sin, pitch_cos * dir[1]};
    float u[3] = {-pitch_sin * dir[0], pitch_cos, -pitch_sin * dir[1]};
    for (unsigned i = 0; i < 3; ++i) out[i] = pos[i], out[3 + i] = cam[i], out[6 + i] = f[i], out[9 + i] = u[i];
}
#endif
