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
#include "summon.h"
#include "car_doors.h"
#include "freecam_drive.h"

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
#define XI_RS 0x0080u
#define XI_A 0x1000u
#define XI_B 0x2000u
#define XI_X 0x4000u
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

void noclip_request(int mode) {
    if (mode == MODE_NOCLIP || mode == MODE_FREECAM || mode == MODE_WALK) InterlockedExchange(&pad_request, mode);
}


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
        if (pressed & XI_Y) InterlockedExchange(&pad_request, MODE_WALK); /* LS + Y: get out (V7) */
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
    if (freecam_drive_active()) { /* V7.2 freecam while driving: right stick looks, its click fixes */
        freecam_drive_pad(pads[user].rx, pads[user].ry, (pressed & XI_RS) != 0);
        short zero[2] = {0, 0};
        memory_copy(gamepad + 8, zero, sizeof(zero));
        buttons &= (unsigned short)~XI_RS;
        memory_copy(gamepad, &buttons, sizeof(buttons));
    }
    if (on == MODE_WALK) {
        /* on foot: Y gets back in; the game only keeps Start and Back (pause, map), the car stands */
        if (pressed & XI_Y) InterlockedExchange(&pad_request, MODE_WALK);
        if (pressed & XI_X) summon_request(); /* V7.1 summon the car */
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
    if (!memory_read_pointer(game + FH2_RVA_PLAYER_MANAGER, &manager) || !manager) return NULL;
    player = ((PlayerGetter)(game + FH2_RVA_GET_LOCAL_PLAYER))(manager, 0);
    if (!player) return NULL;
    ((CarGetter)(game + FH2_RVA_GET_PLAYER_CAR))(player, &local);
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
    if (!memory_read_pointer(car, &vtable) || vtable != game + FH2_RVA_PLAYER_CAR_VTABLE) return 0;
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
static float hidden_last[3];
static int hidden_have_last;
static void hold_walking(void *dynamics) {
    float at[3];
    if (!walking_car_hidden(at) || !float_is_finite(at[0]) || !float_is_finite(at[1]) || !float_is_finite(at[2])) {
        hidden_have_last = 0;
        put_back(dynamics);
        return;
    }
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (!set) return;
    { /* diagnostics (walking.c): did the game's physics move the hidden car since it was put there? */
        static unsigned logged;
        GetStateFn get = (GetStateFn)slot(dynamics, SLOT_GET_STATE);
        if (get && hidden_have_last && logged < 3) {
            _Alignas(16) BodyState now;
            get(dynamics, &now);
            _Alignas(16) float p[4];
            _mm_store_ps(p, now.position);
            float up = p[1] - hidden_last[1], ax = p[0] - hidden_last[0], az = p[2] - hidden_last[2];
            /* the walker moves it a few cm per update; more is the physics */
            if (float_is_finite(up) && (up > 0.5f || up < -0.5f || ax * ax + az * az > 1.0f)) {
                ++logged;
                log_append_value("WALKING DIAG: the hidden car was moved by the game, cm up: ", (int)(up * 100.0f));
            }
        }
        hidden_last[0] = at[0], hidden_last[1] = at[1], hidden_last[2] = at[2], hidden_have_last = 1;
    }
    _Alignas(16) BodyState state = parked;
    state.position = _mm_setr_ps(at[0], at[1], at[2], 0.0f);
    { /* Marvin 08.11.2026: the picture leaned with the hidden car's own lean (it depended on the way
         one looked relative to the car) - hidden, the car is held level, only its heading kept */
        _Alignas(16) float f[4];
        _mm_store_ps(f, parked.forward);
        float l = root(f[0] * f[0] + f[2] * f[2]);
        if (l > 0.1f) {
            float fx = f[0] / l, fz = f[2] / l;
            _Alignas(16) float r[4];
            _mm_store_ps(r, parked.right);
            float side = r[0] * fz - r[2] * fx >= 0.0f ? 1.0f : -1.0f; /* the right row's handedness */
            state.forward = _mm_setr_ps(fx, 0.0f, fz, 0.0f);
            state.up = _mm_setr_ps(0.0f, 1.0f, 0.0f, 0.0f);
            state.right = _mm_setr_ps(side * fz, 0.0f, -side * fx, 0.0f);
        }
    }
    set(dynamics, &state);
}

/* V7.1 summon (summon.h), after the game's update: where the summoned car is now becomes its place
   (getting in, walking off and the brake all use it); while braking its speed is taken away softly */
#define SUMMON_BRAKE_DECEL 6.0f /* m/s^2 */
static void summon_follow(void *dynamics, float dt, int phase) {
    GetStateFn get = (GetStateFn)slot(dynamics, SLOT_GET_STATE);
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (!get || !set) return;
    _Alignas(16) BodyState state;
    get(dynamics, &state);
    _Alignas(16) float p[4], v[4], r[4], f[4];
    _mm_store_ps(p, state.position), _mm_store_ps(v, state.velocity);
    _mm_store_ps(r, state.right), _mm_store_ps(f, state.forward);
    for (unsigned i = 0; i < 3; ++i)
        if (!float_is_finite(p[i]) || !float_is_finite(v[i])) return;
    float speed = root(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    { /* creeping: the real way gone since the last frame (the set speed says little: the brakes eat it,
         Gripline makes up the rest, an obstacle pushes back) */
        static float last[3];
        static int have;
        if (phase == SUMMON_CREEP && have && dt > 0.0f) {
            float dx = p[0] - last[0], dz = p[2] - last[2];
            speed = root(dx * dx + dz * dz) / dt;
        }
        last[0] = p[0], last[1] = p[1], last[2] = p[2];
        have = phase == SUMMON_CREEP;
    }
    if (phase == SUMMON_BRAKE) {
        /* a steady brake (the same force from any speed), the last bit softer */
        float left = speed - SUMMON_BRAKE_DECEL * (dt > 0.0f ? dt : 0.0f) * (speed < 2.0f ? 0.5f : 1.0f);
        float keep = speed > 0.0f && left > 0.3f ? left / speed : 0.0f;
        state.velocity = _mm_mul_ps(state.velocity, _mm_set1_ps(keep));
        state.spin = _mm_mul_ps(state.spin, _mm_set1_ps(keep));
        set(dynamics, &state);
        if (keep == 0.0f) summon_braked();
        speed *= keep;
    }
    parked = state;
    parked.velocity = _mm_setzero_ps();
    parked.spin = _mm_setzero_ps();
    walking_move_car(p, r, f);
    summon_car_seen(p, speed);
}

/* V7.1 summon, SUMMON_CREEP (Marvin 08.11.2026: the car should come to him also from off the road,
   not appear on the road; then "make it cooler"): before the game's update the car gets a speed along
   its own nose and a turn towards the target, like a car driven at a calm pace; the game's physics does
   the rest (ground, slopes, wheels, collisions). Up / down speed stays the physics'.
   Speed (F8 style: calm 16 / normal 22 / sporty 31 km/h): speeds up and brakes with limited force, slows in turns, and plans its stop (brake curve) so
   it comes to a stand at the target instead of braking at the last moment.
   To the walker it parks beside him: the target is SUMMON_PARK_SIDE m beside him, on the side that
   needs the least turning, fixed once the car is near, so it ends next to him with its side to him. */
#define CREEP_TURN 0.8f        /* rad/s at most */
#define CREEP_DECEL 6.0f       /* m/s^2 at most braking (from the AI's speed after the hand-over) */
#define CREEP_PLAN 2.0f        /* m/s^2 the planned soft stop */
#define CREEP_PARK_FIX 12.0f   /* metres to the walker: the parking side and direction stay */
#define CREEP_CLEAR 2.6f       /* metres car centre -> walker: never nearer */
static unsigned creep_seen;    /* the creep the parking side belongs to */
/* The speed Gripline wants the car at, kept here and not read back from the car: the parked car's own
   brakes take most of each frame's speed away again (Marvin's log 08.11.2026: the car never got going,
   "stuck" in the open), so the speed is set anew every frame from this. */
static float creep_command;
/* Where the car was put last frame and how far along its nose it was meant to go: what the parked car's
   brakes / tyres held back of that is added to its place (Marvin 08.11.2026: "it only gives a little
   gas"). An obstacle still stops it - the physics pushes it back, the car does not get on, summon.c sees
   it standing (summon_follow measures the real way gone). */
static float creep_last[3], creep_meant;
static float park_offset[2];
static int park_side, park_fixed;
static void summon_creep(void *dynamics, float dt) {
    float at[3];
    int to_walker;
    unsigned serial = summon_creep_target(at, &to_walker);
    if (!serial || !(dt > 0.0f)) return;
    int fresh = serial != creep_seen;
    if (fresh) creep_seen = serial, park_side = 0, park_fixed = 0;
    GetStateFn get = (GetStateFn)slot(dynamics, SLOT_GET_STATE);
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (!get || !set) return;
    _Alignas(16) BodyState state;
    get(dynamics, &state);
    _Alignas(16) float p[4], v[4], f[4], w[4];
    _mm_store_ps(p, state.position), _mm_store_ps(v, state.velocity), _mm_store_ps(f, state.forward), _mm_store_ps(w, state.spin);
    for (unsigned i = 0; i < 3; ++i)
        if (!float_is_finite(p[i]) || !float_is_finite(v[i]) || !float_is_finite(f[i])) return;
    float fl = root(f[0] * f[0] + f[2] * f[2]);
    if (!(fl > 0.2f)) return; /* nose up or down: leave it to the physics */
    float fx = f[0] / fl, fz = f[2] / fl;
    float walker_dist = -1.0f;
    if (to_walker) { /* the parking place beside the walker */
        float wx = at[0] - p[0], wz = at[2] - p[2];
        walker_dist = root(wx * wx + wz * wz);
        if (!park_fixed && walker_dist > 0.5f) {
            float nx = -wz / walker_dist, nz = wx / walker_dist; /* across the way to him */
            if (!park_side) { /* the side the nose is nearer to */
                float ax = wx + nx * SUMMON_PARK_SIDE, az = wz + nz * SUMMON_PARK_SIDE;
                float bx = wx - nx * SUMMON_PARK_SIDE, bz = wz - nz * SUMMON_PARK_SIDE;
                float ca = (fx * ax + fz * az) / root(ax * ax + az * az), cb = (fx * bx + fz * bz) / root(bx * bx + bz * bz);
                park_side = ca >= cb ? 1 : -1;
            }
            park_offset[0] = nx * park_side * SUMMON_PARK_SIDE, park_offset[1] = nz * park_side * SUMMON_PARK_SIDE;
            if (walker_dist < CREEP_PARK_FIX) park_fixed = 1;
        }
        at[0] += park_offset[0], at[2] += park_offset[1];
    }
    float tx = at[0] - p[0], tz = at[2] - p[2], dist = root(tx * tx + tz * tz);
    float along = v[0] * fx + v[2] * fz; /* a new creep starts from the car's own speed (the AI's hand-over) */
    if (fresh || !float_is_finite(creep_command)) {
        creep_command = along;
        creep_last[0] = p[0], creep_last[1] = p[1], creep_last[2] = p[2], creep_meant = 0.0f;
    } else {
        along = creep_command;
    }
    float held_back = 0.0f; /* metres along the nose the car fell short of last frame */
    {
        float gone = (p[0] - creep_last[0]) * fx + (p[2] - creep_last[2]) * fz;
        held_back = creep_meant - gone;
        if (creep_meant >= 0.0f) held_back = held_back < 0.0f ? 0.0f : held_back > creep_meant ? creep_meant : held_back;
        else held_back = held_back > 0.0f ? 0.0f : held_back < creep_meant ? creep_meant : held_back;
    }
    float want = 0.0f, yaw = 0.0f;
    if (dist > 0.01f) {
        float s = (fz * tx - fx * tz) / dist, c = (fx * tx + fz * tz) / dist; /* turn: + means spin +y */
        float a = c >= 0.0f ? s : s >= 0.0f ? 2.0f - s : -2.0f - s;       /* about the angle, -2..2 */
        if (dist > 1.5f) yaw = a * 1.5f; /* at the target: no turning on the spot */
        if (yaw > CREEP_TURN) yaw = CREEP_TURN;
        if (yaw < -CREEP_TURN) yaw = -CREEP_TURN;
        float stop_left = dist - (to_walker ? 0.3f : 0.0f);
        want = stop_left > 0.0f ? root(2.0f * CREEP_PLAN * stop_left) : 0.0f; /* the planned stop */
        if (want > summon_creep_speed()) want = summon_creep_speed();
        float turning = 2.0f + (summon_creep_speed() - 2.0f) * (c > 0.0f ? c : 0.0f); /* slower in turns, 2 m/s turning round */
        if (want > turning) want = turning;
        if (!to_walker && want < 1.2f) want = 1.2f; /* onto the road: keeps rolling, summon.c takes over */
        if (to_walker && dist < 0.6f) want = 0.0f;
        if (to_walker && want > 0.0f && want < 0.8f) want = 0.8f;
    }
    if (to_walker && walker_dist >= 0.0f && walker_dist < CREEP_CLEAR) want = 0.0f; /* never onto the walker */
    { /* never onto the walker, whatever the target (also the way round an obstacle) */
        float wp[3];
        if (walking_walker_position(wp)) {
            float wx = wp[0] - p[0], wz = wp[2] - p[2], wd = root(wx * wx + wz * wz);
            if (wd < CREEP_CLEAR || (wd < CREEP_CLEAR + 2.0f && (fx * wx + fz * wz) > 0.0f && !to_walker)) want = 0.0f;
        }
    }
    if (summon_creep_reverse()) want = -2.0f, yaw = 0.0f; /* stuck: back up straight, summon.c then drives round */
    float up = summon_creep_accel() * dt, down = CREEP_DECEL * dt;
    if (want > along) along = along + up < want ? along + up : want;
    else along = along - down > want ? along - down : want;
    creep_command = along;
    float turn_scale = along < 1.0f ? (along > 0.3f ? along : 0.3f) : 1.0f;
    if (want == 0.0f && along < 0.3f) turn_scale = 0.0f; /* standing: no turning on the spot */
    /* Steering: the car is turned directly (its whole orientation about the world's up axis), not through
       its spin - the parked car's tyres took a set spin away at once (Marvin 08.11.2026: "it only goes
       straight ahead and does not steer"). A small angle each frame; + turns the nose from +z towards +x. */
    float turn = yaw * (along >= 0.0f ? 1.0f : -1.0f) * turn_scale * dt;
    if (turn > 0.2f) turn = 0.2f;
    if (turn < -0.2f) turn = -0.2f;
    float ts = turn * (1.0f - turn * turn / 6.0f), tc = 1.0f - turn * turn / 2.0f;
    _Alignas(16) float r4[4], u4[4];
    _mm_store_ps(r4, state.right), _mm_store_ps(u4, state.up);
#define CREEP_YAW(q) _mm_setr_ps(q[0] * tc + q[2] * ts, q[1], q[2] * tc - q[0] * ts, 0.0f)
    state.right = CREEP_YAW(r4), state.up = CREEP_YAW(u4), state.forward = CREEP_YAW(f);
#undef CREEP_YAW
    float nfx = fx * tc + fz * ts, nfz = fz * tc - fx * ts;
    state.velocity = _mm_setr_ps(nfx * along, v[1], nfz * along, 0.0f); /* along the nose, no sliding sideways */
    float px = p[0] + nfx * held_back, pz = p[2] + nfz * held_back;
    state.position = _mm_setr_ps(px, p[1], pz, 0.0f);
    creep_last[0] = px, creep_last[1] = p[1], creep_last[2] = pz, creep_meant = along * dt;
    state.spin = _mm_setr_ps(w[0], 0.0f, w[2], 0.0f); /* the turn is ours; tilting stays the physics' */
    set(dynamics, &state);
}

/* V7.1 summon: the held car onto the road next to it (summon.c found it), level, along the road */
static void summon_onto_road(void *dynamics, const float at[3], const float ahead[3]) {
    float length = root(ahead[0] * ahead[0] + ahead[2] * ahead[2]);
    if (!(length > 0.1f)) return;
    float fx = ahead[0] / length, fz = ahead[2] / length;
    _Alignas(16) float r[4], f[4];
    _mm_store_ps(r, parked.right), _mm_store_ps(f, parked.forward);
    float m = r[0] * f[2] - r[2] * f[0] >= 0.0f ? 1.0f : -1.0f; /* the game's right row, as heading_from */
    parked.position = _mm_setr_ps(at[0], at[1], at[2], 0.0f);
    parked.right = _mm_setr_ps(m * fz, 0.0f, -m * fx, 0.0f);
    parked.up = _mm_setr_ps(0.0f, 1.0f, 0.0f, 0.0f);
    parked.forward = _mm_setr_ps(fx, 0.0f, fz, 0.0f);
    parked.velocity = _mm_setzero_ps(), parked.spin = _mm_setzero_ps();
    put_back(dynamics);
    float right[3] = {m * fz, 0.0f, -m * fx}, forward[3] = {fx, 0.0f, fz};
    walking_move_car(at, right, forward);
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

/* V7.2 polish (Marvin 14.11.2026: should feel like the game, not a mod): the key help of noclip /
   freecam only the first time each, one card that a newer one replaces, gone when switched off or
   when the game pauses (photo mode, menu) */
#define KEYS_TAG 0x4e4b00u
static void keys_card(int *shown, const char *german, const char *english) {
    if ((*shown)++) return;
    notify_show_step(game_language_is_german(game) ? german : english, KEYS_TAG, 7000);
}

static void keys_card_away(void) {
    notify_fade_tag(KEYS_TAG);
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
    /* polish (Marvin 14.11.2026: should feel like the game, not a mod): the keys only the first time */
    static int keys_shown;
    if (!keys_shown++)
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
static volatile LONG door_side; /* V7.2, below */
static void fade_begin(LONG to) {
    fade_to = to;
    fade_stage = 1;
    fade_at = GetTickCount64();
    notify_fade_black(1000, FADE_DOWN_MS);
    if (to == MODE_WALK) {
        walking_set_exit(door_side, 0.0f); /* V7.2: the driver's side if known (no door: next to it, looking ahead) */
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
        if (fade_to == MODE_WALK && view_changed && !walking_view_outside()) { /* V7.2: see door_stay */
            cockpit_view_want_camera(view_before, second_before), view_changed = 0;
            fade_stop();
            log_append("NOCLIP: getting out stopped - the game did not change to an outside camera (rewind / replay?).\r\n");
            card("Aussteigen geht gerade nicht\nDas Spiel wechselt die Kamera nicht (z.B. Rewind)",
                 "Cannot get out right now\nThe game does not change the camera (e.g. rewind)");
            return;
        }
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
        if (fade_to == MODE_WALK) walk_out_card(); /* (polish: no "back in the car" card any more) */
        fade_stage = 3;
        fade_at = now;
    } else if (fade_stage == 3 && now >= fade_at + FADE_UP_MS) {
        fade_stage = 0;
    }
}

/* V7.2 (Marvin 14.11.2026): getting out / in like in the game, without the black picture: the
   driver's door opens (the car's own animation, car_doors.h), the camera glides out of the seat
   through the door to the walker, the door shuts; getting in the other way round. Out of a cockpit
   camera the game would show its cut interior model from outside, so there the picture dips to black
   only while the camera changes (as short as it takes). A car without the door animation, or
   [Gripline] WalkDoor=0 (F8 > Settings), gets the fade as before. */
#define DOOR_ANSWER_MS 400     /* the car thread opens the door within this, else the fade */
#define DOOR_SEEN_MS 1100      /* the door opens while still seated (the head turns to it) */
/* polish (Marvin 14.11.2026, video 5: "noch nicht sehr gut"): longer, softer glides that turn evenly,
   a blink rather than a black picture (down quick, up slow while the camera already moves) */
#define DOOR_DIP_DOWN_MS 200
#define DOOR_DIP_UP_MS 450
#define DOOR_GLIDE_MS 1700     /* out of the seat / the camera before to the walker */
#define DOOR_IN_GLIDE_MS 1400  /* the walker into the seat */
#define DOOR_BACK_GLIDE_MS 1800 /* getting in with an outside camera: back to it (the door shuts on the way) */
#define DOOR_SHUT_AFTER_MS 150 /* after the glide out: the door shuts (the walker looks at it) */
#define DOOR_IN_SHUT_MS 250    /* back in a seat camera: the door shuts this long after the picture is back */
#define DOOR_IN_LOOK_MS 500    /* ... and then the head turns back to the road */
#define DOOR_SETTLE_MS 2500    /* after the door shut: the car's animation detail back (car_doors_settle) */
#define DOOR_BACK 0.9f         /* the walker stands this far behind the car's middle (the door's way) */
/* door8 (Tesla video 14.11.2026): from a cockpit camera the glide starts (getting in: ends) out here,
   clear of the car's shell - 0.5 m out showed the mirror, the ground and the shell's dark inside for
   about half a second. Seat eye about 0.4 m from the car's middle, its side about 0.6 m out of it. */
#define DOOR_SEAT_OUT 1.0f
#define DOOR_SEAT_BACK 0.35f /* behind the open door's edge */
#define DOOR_SEAT_UP 0.05f
/* door9 (Marvin's video 14.11.2026, 5 cars): looking out from there showed grass / road and black bits of the
   open door (the game's outside model has no inside of the door: from behind only its edges show). Now the
   look goes back at the seat, a little down: the car, its open side and the seats, the door out of view. */
#define DOOR_SEAT_LOOK_DOWN 0.25f
/* door10 (Marvin 14.11.2026: getting in, the camera stays too far out before the dip): into a cockpit
   camera the glide goes on into the door opening, the picture gets dark on the way (dark when it passes
   the car's side, the shell's inside is not seen) */
#define DOOR_IN_SEAT_OUT 0.3f
#define DOOR_IN_SEAT_BACK 0.1f
#define DOOR_IN_DARK_BEFORE_MS 550 /* the dip starts this long before the glide ends */
#define DOOR_IN_DIP_MS 320
#define DOOR_OUTSIDE 0.5f      /* the glide starts / ends this far out of the seat: the car's outside model
                                  has only a dark shell inside, the cockpit model is not drawn then */
static volatile LONG walk_door = 1;
static volatile LONG door_ask;    /* car thread: 1 open (getting out), 2 open (getting in), 3 close, 4 settle */
static volatile LONG door_answer; /* 0 waiting, 1 open, -1 no door */
static volatile LONG door_side = CAR_DOOR_LEFT;
static float door_out[3];         /* world, unit: out of the door */
static float door_back[3];        /* world, unit: to the car's back (door8) */
static float seat_local[3];       /* the seat eye from the car's middle: right, up, forward */
static float seat_look_local[3];
static volatile LONG seat_known;
static float view_local[3], view_look_local[3]; /* the outside camera before getting out, as seat_local */
static volatile LONG view_known;
static int door_stage;            /* watch thread: 1-6 getting out, 11-18 getting in */
static ULONGLONG door_at, door_settle_at;
static int door_dip, door_missing_logged;
static ULONGLONG door_dark_at; /* the dip began (the time in the dark is logged) */
static float door_from[3], door_from_look[3];

void noclip_set_walk_door(int on) {
    InterlockedExchange(&walk_door, on ? 1 : 0);
}

int noclip_walk_door(void) {
    return walk_door != 0;
}

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void unit3(float v[3]) {
    float n = root(dot3(v, v));
    if (n > 0.0001f) v[0] /= n, v[1] /= n, v[2] /= n;
}

/* the car's place and its right / up / forward rows (unit), car update thread */
static int car_rows(void *car, float p[3], float r[3], float u[3], float f[3]) {
    void *dynamics = car_body(car);
    GetStateFn get = dynamics ? (GetStateFn)slot(dynamics, SLOT_GET_STATE) : NULL;
    if (!get) return 0;
    _Alignas(16) BodyState state;
    get(dynamics, &state);
    _Alignas(16) float a[4], b[4], c[4], d[4];
    _mm_store_ps(a, state.position), _mm_store_ps(b, state.right), _mm_store_ps(c, state.up), _mm_store_ps(d, state.forward);
    for (unsigned i = 0; i < 3; ++i) {
        p[i] = a[i], r[i] = b[i], u[i] = c[i], f[i] = d[i];
        if (!float_is_finite(p[i]) || !float_is_finite(r[i]) || !float_is_finite(u[i]) || !float_is_finite(f[i])) return 0;
    }
    unit3(r), unit3(u), unit3(f);
    return 1;
}

/* car update thread, the own car: what the watch thread asked for */
static void door_car_step(void *car) {
    LONG ask = InterlockedExchange(&door_ask, 0);
    if (!ask) return;
    if (ask == 3) {
        car_door_move(game, car, door_side, 0);
        return;
    }
    if (ask == 4) {
        car_doors_settle(game, car);
        return;
    }
    float p[3], r[3], u[3], f[3];
    if (!car_rows(car, p, r, u, f)) {
        InterlockedExchange(&door_answer, -1);
        return;
    }
    if (ask == 1) { /* the driver's side from where the seat camera was (right-hand drive: right) */
        float e[3], l[3];
        if (walking_seat_eye(e, l)) {
            float d[3] = {e[0] - p[0], e[1] - p[1], e[2] - p[2]};
            seat_local[0] = dot3(d, r), seat_local[1] = dot3(d, u), seat_local[2] = dot3(d, f);
            seat_look_local[0] = dot3(l, r), seat_look_local[1] = dot3(l, u), seat_look_local[2] = dot3(l, f);
            InterlockedExchange(&seat_known, 1);
            InterlockedExchange(&door_side, seat_local[0] > 0.1f ? CAR_DOOR_RIGHT : CAR_DOOR_LEFT);
        }
    }
    if (ask == 1) { /* an outside camera: getting back in glides back to it (V7.2, Marvin: no cut) */
        float e[3], l[3];
        InterlockedExchange(&view_known, 0);
        if (walking_view_outside() && walking_seen_camera(e, l)) {
            float d[3] = {e[0] - p[0], e[1] - p[1], e[2] - p[2]};
            view_local[0] = dot3(d, r), view_local[1] = dot3(d, u), view_local[2] = dot3(d, f);
            view_look_local[0] = dot3(l, r), view_look_local[1] = dot3(l, u), view_look_local[2] = dot3(l, f);
            InterlockedExchange(&view_known, 1);
            char line[96];
            size_t n = 0;
            put_text_card(line, &n, "DOORS: outside camera before getting out, car-local cm: ");
            for (unsigned i = 0; i < 3; ++i) put_int_card(line, &n, (int)(view_local[i] * 100.0f)), put_text_card(line, &n, " ");
            put_text_card(line, &n, "\r\n");
            log_append(line);
        }
    }
    float side = door_side > 0 ? 1.0f : -1.0f;
    door_out[0] = r[0] * side, door_out[1] = r[1] * side, door_out[2] = r[2] * side;
    door_back[0] = -f[0], door_back[1] = -f[1], door_back[2] = -f[2];
    int ok = car_door_exists(game, car, door_side) && car_door_move(game, car, door_side, 1);
    InterlockedExchange(&door_answer, ok ? 1 : -1);
}

/* the seat in the world now, from the parked car (watch thread, getting in) */
static void seat_world(float eye_out[3], float look_out[3]) {
    _Alignas(16) float p[4], r[4], u[4], f[4];
    _mm_store_ps(p, parked.position), _mm_store_ps(r, parked.right), _mm_store_ps(u, parked.up), _mm_store_ps(f, parked.forward);
    unit3(r), unit3(u), unit3(f);
    /* not known (an outside camera all the time): about where a driver's eyes are */
    float side = door_side > 0 ? 1.0f : -1.0f;
    float sl[3] = {side * 0.38f, 0.55f, -0.2f}, ll[3] = {0.0f, -0.08f, 1.0f};
    if (seat_known) for (unsigned i = 0; i < 3; ++i) sl[i] = seat_local[i], ll[i] = seat_look_local[i];
    for (unsigned i = 0; i < 3; ++i) {
        eye_out[i] = p[i] + r[i] * sl[0] + u[i] * sl[1] + f[i] * sl[2];
        look_out[i] = r[i] * ll[0] + u[i] * ll[1] + f[i] * ll[2];
    }
    unit3(look_out);
    door_out[0] = r[0] * side, door_out[1] = r[1] * side, door_out[2] = r[2] * side;
    door_back[0] = -f[0], door_back[1] = -f[1], door_back[2] = -f[2];
}

/* door8: the point beside the seat, clear of the car's shell, and the look from there */
static void seat_outside(float eye[3], float look[3]) {
    for (unsigned i = 0; i < 3; ++i) eye[i] += door_out[i] * DOOR_SEAT_OUT + door_back[i] * DOOR_SEAT_BACK;
    eye[1] += DOOR_SEAT_UP;
    if (!look) return;
    for (unsigned i = 0; i < 3; ++i) look[i] = -(door_out[i] * DOOR_SEAT_OUT + door_back[i] * DOOR_SEAT_BACK);
    look[1] -= DOOR_SEAT_UP + DOOR_SEAT_LOOK_DOWN;
    unit3(look);
}

/* the outside camera from before getting out, from the parked car (watch thread, getting in) */
static void view_world(float eye_out[3], float look_out[3]) {
    _Alignas(16) float p[4], r[4], u[4], f[4];
    _mm_store_ps(p, parked.position), _mm_store_ps(r, parked.right), _mm_store_ps(u, parked.up), _mm_store_ps(f, parked.forward);
    unit3(r), unit3(u), unit3(f);
    for (unsigned i = 0; i < 3; ++i) {
        eye_out[i] = p[i] + r[i] * view_local[0] + u[i] * view_local[1] + f[i] * view_local[2];
        look_out[i] = r[i] * view_look_local[0] + u[i] * view_look_local[1] + f[i] * view_look_local[2];
    }
    unit3(look_out);
}

static void door_close(ULONGLONG now) { /* watch thread */
    InterlockedExchange(&door_ask, 3);
    door_settle_at = now + DOOR_SETTLE_MS;
}

static void door_no_door(LONG to) {
    if (!door_missing_logged) {
        door_missing_logged = 1;
        log_append("DOORS: this car has no door animation Gripline knows (or the game code differs) - fade instead.\r\n");
    }
    door_stage = 0;
    fade_begin(to);
}

static void door_begin(LONG to, ULONGLONG now) {
    InterlockedExchange(&door_answer, 0);
    InterlockedExchange(&door_ask, to == MODE_WALK ? 1 : 2);
    door_stage = to == MODE_WALK ? 1 : 11;
    door_at = now;
}

static void door_stop(void) { /* online: everything back at once */
    if (!door_stage) return;
    walking_head_turn(NULL);
    if (door_dip) notify_fade_black(0, 200), door_dip = 0;
    door_stage = 0;
    door_close(GetTickCount64());
}

/* out of a cockpit camera, but the game did not change the camera (Marvin's test 14.11.: in the
   rewind menu): walking would show the car's cut interior model - stay in the car */
static void door_stay(ULONGLONG now) {
    walking_head_turn(NULL);
    if (view_changed) cockpit_view_want_camera(view_before, second_before), view_changed = 0;
    if (door_dip) notify_fade_black(0, DOOR_DIP_UP_MS), door_dip = 0;
    door_close(now);
    walking_door_sound(1);
    door_stage = 0;
    log_append("NOCLIP: getting out stopped - the game did not change to an outside camera (rewind / replay?).\r\n");
    card("Aussteigen geht gerade nicht\nDas Spiel wechselt die Kamera nicht (z.B. Rewind)",
         "Cannot get out right now\nThe game does not change the camera (e.g. rewind)");
}

static void door_walk(ULONGLONG now) {
    walking_head_turn(NULL);
    walking_set_exit(door_side, DOOR_BACK);
    walking_glide(0, door_from, door_from_look, door_out, DOOR_GLIDE_MS);
    if (view_changed) walking_glide_duck(); /* door8: out of the seat */
    set_on(MODE_WALK);
    if (on != MODE_WALK) { /* not switched (see the log): back as it was */
        if (view_changed) cockpit_view_want_camera(view_before, second_before), view_changed = 0;
        if (door_dip) notify_fade_black(0, 200), door_dip = 0;
        door_close(now);
        door_stage = 0;
        return;
    }
    if (door_dip) {
        log_append_value("DOORS: getting out - ms in the dark (the game's camera change): ", (int)(now - door_dark_at));
        notify_fade_black(0, DOOR_DIP_UP_MS), door_dip = 0;
    }
    info_from = now + DOOR_GLIDE_MS + INFO_DELAY_MS;
    log_append(door_side > 0 ? "NOCLIP: walking on (out of the car, right door)\r\n" : "NOCLIP: walking on (out of the car, left door)\r\n");
    door_stage = 5;
    door_at = now;
}

static void door_tick(ULONGLONG now) {
    switch (door_stage) {
        case 1: /* getting out: the door is opening? */
        case 11: {
            LONG to = door_stage == 1 ? MODE_WALK : 0;
            LONG answer = door_answer;
            if (answer < 0 || (!answer && now >= door_at + DOOR_ANSWER_MS)) {
                door_no_door(to);
                return;
            }
            if (!answer) return;
            walking_door_sound(0);
            door_at = now;
            if (to == MODE_WALK) {
                view_before = walking_view_camera();
                second_before = cockpit_view_second();
                view_changed = view_before >= 0 && !walking_view_outside();
                if (view_changed && walking_seat_eye(door_from, door_from_look)) {
                    /* door8: after the dip already outside, beside the open door (the seat's look turned
                       a little out; the head turns to the door before the dip) */
                    seat_outside(door_from, door_from_look);
                    walking_head_turn(door_out);
                } else if (!walking_seen_camera(door_from, door_from_look)) {
                    seat_world(door_from, door_from_look); /* (not parked yet: only a rough start) */
                }
                door_stage = 2;
            } else if (!view_changed && view_known) { /* an outside camera before: back to it */
                float to[3], to_look[3];
                view_world(to, to_look);
                walking_glide(1, to, to_look, door_out, DOOR_BACK_GLIDE_MS);
                door_stage = 15;
            } else {
                float seat[3], look_at[3];
                seat_world(seat, look_at);
                if (view_changed) { /* door10: into the door opening; door11 (Marvin: one does not get in head
                                         first looking at the passenger seat): looking ahead like the
                                         seat, as sitting down */
                    for (unsigned i = 0; i < 3; ++i)
                        seat[i] += door_out[i] * DOOR_IN_SEAT_OUT + door_back[i] * DOOR_IN_SEAT_BACK;
                    /* door12 (Marvin: a little more out, like the cockpit camera): the look of the seat
                       with the head turned to the door - the same as right after the dip */
                    walking_head_look(look_at, door_out, look_at);
                } else {
                    for (unsigned i = 0; i < 3; ++i) seat[i] += door_out[i] * DOOR_OUTSIDE * 0.9f;
                }
                walking_glide(1, seat, look_at, door_out, DOOR_IN_GLIDE_MS);
                if (view_changed) walking_glide_duck(); /* door8: into the seat */
                door_stage = 12;
            }
            return;
        }
        case 2: /* seated, the door open: now out */
            if (now < door_at + DOOR_SEEN_MS) return;
            if (view_changed) { /* a cockpit camera: dip while it changes to an outside camera */
                notify_fade_black(1000, DOOR_DIP_DOWN_MS);
                door_dip = 1;
                door_dark_at = now;
                door_stage = 3;
                door_at = now;
                return;
            }
            door_walk(now);
            return;
        case 3: /* dark: now the camera button (V7.2: not before, one press, so the dark is short) */
            if (now < door_at + DOOR_DIP_DOWN_MS) return;
            cockpit_view_want_outside();
            door_stage = 4;
            door_at = now;
            return;
        case 4:
            if (cockpit_view_want_pending() && now < door_at + FADE_CAMERA_WAIT_MS) return;
            if (!walking_view_outside()) {
                door_stay(now);
                return;
            }
            door_walk(now);
            return;
        case 5: /* out, the camera glides: then the door shuts (door9: out of a cockpit camera already on the
                   way, the open door shows only its edges from behind) */
            if (now < door_at + (view_changed ? DOOR_GLIDE_MS * 50 / 100 : DOOR_GLIDE_MS + DOOR_SHUT_AFTER_MS)) return;
            door_close(now);
            walking_door_sound(1);
            door_stage = 6;
            return;
        case 6: /* the glide done: walking */
            if (now < door_at + DOOR_GLIDE_MS) return;
            walk_out_card();
            door_stage = 0;
            return;
        case 12: /* getting in: the camera glides into the seat */
            if (now < door_at + (view_changed ? DOOR_IN_GLIDE_MS - DOOR_IN_DARK_BEFORE_MS : DOOR_IN_GLIDE_MS + 50)) return;
            if (view_changed) { /* back to a cockpit camera: dip while it changes (door10: on the way in) */
                notify_fade_black(1000, DOOR_IN_DIP_MS);
                door_dip = 1;
                door_dark_at = now;
                door_stage = 13;
                door_at = now;
                return;
            }
            set_on(0);
            door_close(now);
            walking_door_sound(1);
            log_append("NOCLIP: walking off - back in the car (door).\r\n");
            door_stage = 0;
            return;
        case 13:
            if (now < door_at + DOOR_IN_DIP_MS) return;
            set_on(0); /* the camera from before comes back (set_on) */
            /* polish: seated with the head still at the open door - it shuts in view (stage 17) */
            walking_head_turned(door_out);
            log_append("NOCLIP: walking off - back in the car (door).\r\n");
            door_stage = 14;
            door_at = now;
            return;
        case 14:
            if (cockpit_view_want_pending() && now < door_at + FADE_CAMERA_WAIT_MS) return;
            log_append_value("DOORS: getting in - ms in the dark (the game's camera change): ", (int)(now - door_dark_at));
            notify_fade_black(0, DOOR_DIP_UP_MS);
            door_dip = 0;
            door_stage = 17;
            door_at = now;
            return;
        case 17: /* the picture is back: the door shuts in view */
            if (now < door_at + DOOR_IN_SHUT_MS) return;
            door_close(now);
            walking_door_sound(1);
            door_stage = 18;
            door_at = now;
            return;
        case 18: /* then the head turns back to the road */
            if (now < door_at + DOOR_IN_LOOK_MS) return;
            walking_head_turn_back(800);
            door_stage = 0;
            return;
        case 15: /* gliding back to the outside camera: the door shuts on the way */
            if (now < door_at + DOOR_BACK_GLIDE_MS * 55 / 100) return;
            door_close(now);
            walking_door_sound(1);
            door_stage = 16;
            return;
        case 16: /* there: the game's camera again, from where the glide ended */
            if (now < door_at + DOOR_BACK_GLIDE_MS + 50) return;
            set_on(0);
            log_append("NOCLIP: walking off - back in the car (door, back to the camera).\r\n");
            door_stage = 0;
            return;
        default:
            door_stage = 0;
    }
}

/* V7.2 (Marvin 14.11.2026): freecam while driving (freecam_drive.h) - the car drives on, only the
   picture is free. Out of a cockpit camera the game's camera changes to an outside one first (a short
   dip, as getting out); off again: the camera from before. [Gripline] FreecamDrive=0: the old freecam. */
static volatile LONG free_drive = 1;
static int drive_stage; /* watch thread: 1 dipping, 2 the camera changes, 3 on */
static ULONGLONG drive_at, drive_inside_since;
static int drive_view_before = -1, drive_second_before, drive_view_changed, drive_dip;

void noclip_set_freecam_drive(int value) {
    InterlockedExchange(&free_drive, value ? 1 : 0);
}

int noclip_freecam_drive(void) {
    return free_drive != 0;
}

static void drive_cam_start(void) {
    freecam_drive_set(1);
    drive_stage = 3;
    drive_inside_since = 0;
    if (drive_dip) notify_fade_black(0, DOOR_DIP_UP_MS), drive_dip = 0;
    int arrows = freecam_drive_arrows();
    log_append(arrows ? "NOCLIP: freecam while driving on (arrows fly: a keyboard drives the car)\r\n"
                      : "NOCLIP: freecam while driving on\r\n");
    static int shown_arrows, shown_wasd;
    if (arrows)
        keys_card(&shown_arrows, "Freecam an - das Auto f\xc3\xa4hrt weiter\nPfeile fliegen, Bild\xe2\x86\x91/\xe2\x86\x93 hoch/runter, rechte Shift schneller, "
             "Maus/rechter Stick schauen, rechte Maustaste/Stick-Klick festsetzen, F7 aus",
             "Freecam on - the car drives on\nArrows fly, Page up/down up/down, right Shift faster, "
             "mouse/right stick look, right mouse button/stick click fix, F7 off");
    else
        keys_card(&shown_wasd, "Freecam an - das Auto f\xc3\xa4hrt weiter\nWASD fliegen, Leertaste/Strg hoch/runter, Shift schneller, "
             "Maus/Pfeile/rechter Stick schauen, rechte Maustaste/Stick-Klick festsetzen, F7 aus",
             "Freecam on - the car drives on\nWASD fly, Space/Ctrl up/down, Shift faster, "
             "mouse/arrows/right stick look, right mouse button/stick click fix, F7 off");
}

static void drive_cam_on(ULONGLONG now) {
    drive_view_before = walking_view_camera();
    drive_second_before = cockpit_view_second();
    drive_view_changed = drive_view_before >= 0 && !walking_view_outside();
    if (drive_view_changed) { /* a cockpit camera: the picture would be the car's cut interior model */
        notify_fade_black(1000, DOOR_DIP_DOWN_MS);
        drive_dip = 1;
        drive_stage = 1;
        drive_at = now;
        return;
    }
    drive_cam_start();
}

static void drive_cam_off(const char *why) {
    if (!drive_stage) return;
    freecam_drive_set(0);
    drive_stage = 0;
    if (drive_dip) notify_fade_black(0, 200), drive_dip = 0;
    if (drive_view_changed) cockpit_view_want_camera(drive_view_before, drive_second_before), drive_view_changed = 0;
    keys_card_away();
    log_append(why);
}

static void drive_cam_tick(ULONGLONG now) {
    switch (drive_stage) {
        case 1: /* dark: now the camera button */
            if (now < drive_at + DOOR_DIP_DOWN_MS) return;
            cockpit_view_want_outside();
            drive_stage = 2;
            drive_at = now;
            return;
        case 2:
            if (cockpit_view_want_pending() && now < drive_at + FADE_CAMERA_WAIT_MS) return;
            if (!walking_view_outside()) {
                drive_cam_off("NOCLIP: freecam while driving - the game did not change to an outside camera.\r\n");
                card("Freecam geht gerade nicht\nDas Spiel wechselt die Kamera nicht", "Freecam not possible right now\nThe game does not change the camera");
                return;
            }
            drive_cam_start();
            return;
        case 3:
            if (freecam_drive_fix_changed())
                card(freecam_drive_fixed() ? "Kamera festgesetzt\nSie bleibt stehen und schaut dem Auto nach" : "Kamera wieder frei",
                     freecam_drive_fixed() ? "Camera fixed\nIt stays where it is and follows the car" : "Camera free again");
            /* the player changed to a cockpit camera: the freecam ends (a short moment allowed) */
            if (game_simulation_running() && !walking_view_outside()) {
                if (!drive_inside_since) drive_inside_since = now;
                else if (now - drive_inside_since > 800) {
                    drive_view_changed = 0; /* the player chose that camera */
                    drive_cam_off("NOCLIP: freecam while driving off - another camera was chosen.\r\n");
                }
            } else {
                drive_inside_since = 0;
            }
            return;
        default:
            drive_stage = 0;
    }
}

/* walking: the walk key / controller Y near the car gets back in */
static void walk_key(void) {
    float distance = walking_car_distance();
    if (distance >= 0.0f && distance <= WALK_ENTER_DISTANCE) {
        if (walk_door && door_answer > 0 && summon_phase() == SUMMON_IDLE) door_begin(0, GetTickCount64()); /* the door that opened when getting out */
        else fade_begin(0); /* in the car while the picture is black */
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
    if (online) fade_stop(), door_stop();
    if (online && drive_stage) {
        drive_cam_off("NOCLIP: online session - freecam while driving off.\r\n");
        tinted("Freecam gesperrt\nOnline-Sitzung (FH2MP)", "Freecam locked\nOnline session (FH2MP)", NOTIFY_COLOR_RED);
    }
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
    { /* polish: the game pauses (photo mode, menu, loading): the noclip / freecam cards go (video 14.11.) */
        static int was_running = 1;
        int running = game_simulation_running();
        if (was_running && !running) keys_card_away(), notify_fade_tag(INFO_TAG);
        was_running = running;
    }
    int held = keyboard_held(KEYBOARD_HOLD_NOCLIP), free_held = keyboard_held(KEYBOARD_HOLD_FREECAM);
    int walk_held = keyboard_held(KEYBOARD_HOLD_WALK);
    { /* V7.1 summon key, only while walking (summon.h) */
        static int summon_held;
        int held_now = keyboard_held(KEYBOARD_HOLD_SUMMON);
        if (held_now && !summon_held && on == MODE_WALK && !fade_stage && !door_stage) {
            if (summon_ready()) summon_request();
            else summon_warning_sound(), card("Auto rufen geht nicht\nSpiel-Code anders (Gripline.log)", "Summon not working\nGame code differs (Gripline.log)");
        }
        summon_held = held_now;
    }
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
    if (door_stage) { /* V7.2: the same with the door */
        door_tick(now);
        return;
    }
    if (drive_stage) drive_cam_tick(now);
    if (drive_stage == 1 || drive_stage == 2) return; /* keys wait until the picture is back */
    if (drive_stage && (pressed || free_pressed || walk_pressed)) { /* any of the keys ends it */
        drive_cam_off("NOCLIP: freecam while driving off\r\n");
        return;
    }
    if (door_settle_at && now >= door_settle_at && InterlockedCompareExchange(&door_ask, 4, 0) == 0) door_settle_at = 0;
    if (!pressed && !free_pressed && !walk_pressed) return;
    LONG mode = pressed ? MODE_NOCLIP : free_pressed ? MODE_FREECAM : MODE_WALK;
    if (on == MODE_WALK && mode == MODE_WALK) {
        walk_key();
        return;
    }
    if (on == MODE_WALK) { /* F6 / F7 while walking: back in the car at once */
        set_on(0);
        log_append("NOCLIP: walking off (noclip/freecam key) - back in the car.\r\n");
        return;
    }
    if (on) { /* either key ends what is on */
        LONG was = on;
        set_on(0);
        log_append(was == MODE_FREECAM ? "NOCLIP: freecam off\r\n" : "NOCLIP: off\r\n");
        keys_card_away(); /* (polish: no "off" card, the picture says it) */
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
    if (mode == MODE_FREECAM && free_drive && walking_ready()) { /* V7.2: the car drives on */
        drive_cam_on(GetTickCount64());
        return;
    }
    if (mode == MODE_WALK) { /* V7.2: with the door, else out of the car while the picture is black */
        if (!game_simulation_running()) { /* paused, the rewind menu (Marvin's test 14.11.) */
            log_append("NOCLIP: getting out - the game is paused (menu / rewind), not now.\r\n");
            return;
        }
        if (walk_door && car_doors_ready(game)) door_begin(MODE_WALK, GetTickCount64());
        else fade_begin(MODE_WALK);
        return;
    }
    set_on(mode);
    if (!on) return;
    info_from = GetTickCount64() + INFO_DELAY_MS;
    if (mode == MODE_FREECAM) {
        log_append("NOCLIP: freecam on\r\n");
        static int shown_free;
        keys_card(&shown_free, "Freecam an\nPfeile/rechter Stick schauen, WASD/linker Stick fliegen, Leertaste/Strg/RT/LT hoch/runter",
             "Freecam on\nArrows/right stick look, WASD/left stick fly, Space/Ctrl/RT/LT up/down");
    } else {
        log_append("NOCLIP: on\r\n");
        static int shown_noclip;
        keys_card(&shown_noclip, "Noclip an\nW/S/linker Stick fliegen, A/D drehen, Leertaste/Strg/RT/LT hoch/runter",
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
    void *park = NULL, *follow = NULL;
    int summon = SUMMON_IDLE;
    if (local && door_ask) door_car_step(car); /* V7.2 getting out / in with the door */
    if (local && freecam_drive_active()) { /* V7.2 freecam while driving: where the car is */
        float p[3], r[3], u[3], f[3];
        if (car_rows(car, p, r, u, f)) freecam_drive_car(p, r, f);
    }
    if (local && (on == MODE_WALK || driver_hidden_car == car)) walk_driver(car, on == MODE_WALK && walking_active());
    if (on == MODE_WALK && local) {
        /* on foot: the car stands where it was; the walker is moved in the camera (walking.c) */
        void *walk_body = car_body(car);
        if (walk_body && walk_body != body && !begin(walk_body)) walk_body = NULL;
        if (walk_body && walk_body == parked_body) {
            summon = summon_phase();
            if (summon == SUMMON_DRIVE || summon == SUMMON_BRAKE || summon == SUMMON_CREEP) { /* the car drives itself */
                follow = walk_body;
                if (summon == SUMMON_CREEP) summon_creep(walk_body, dt);
            } else park = walk_body, hold_walking(park);
        }
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
    else if (follow) summon_follow(follow, dt, summon);
    else if (park && on == MODE_WALK) {
        hold_walking(park);
        if (summon == SUMMON_HOLD) { /* the route is planned: the car waits at its place */
            float at[3], ahead[3];
            if (summon_take_place(at, ahead)) summon_onto_road(park, at, ahead);
            _Alignas(16) float p[4];
            _mm_store_ps(p, parked.position);
            summon_car_seen(p, 0.0f);
        }
    }
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
/* one summon creep step on a body (tests/test_summon.py) */
__declspec(dllexport) void Fh2TestNoclipCreep(unsigned char *game_image, void *dynamics, unsigned dt_ms) {
    game = game_image;
    summon_creep(dynamics, (float)dt_ms / 1000.0f);
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
