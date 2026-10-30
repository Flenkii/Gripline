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
#include "game_language.h"

#define RVA_DYNAMICS_VTABLE 0x2eb4bb8u
#define RVA_DYNAMICS_REF_VTABLE 0x2eb5e18u
#define CAR_DYNAMICS 0x4c08
#define SLOT_GET_STATE 0x1b8
#define SLOT_SET_STATE 0xb58

#define SPEED 20.0f      /* m/s forward/back (72 km/h) */
#define CLIMB 10.0f      /* m/s up/down */
#define FAST 4.0f        /* NoclipFast held */
#define TURN 1.8f        /* rad/s */

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
static volatile LONG on;
static int key_held, warned;
static ULONGLONG next_online_check;
static int online;

/* flight, only touched on the game's update thread */
static void *body;           /* the body being flown */
static float pos[3], dir[2]; /* position, heading (x, z) */
static float mirror;         /* +1: right row = (z, 0, -x) of the heading, -1: the other way */

int noclip_active(void) {
    return on != 0;
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

static int is_local_car(void *car) {
    void *vtable, *manager, *player, *local = NULL;
    if (!memory_read_pointer(car, &vtable) || vtable != game + GAME_RVA_PLAYER_CAR_VTABLE ||
        !memory_read_pointer(game + GAME_RVA_PLAYER_MANAGER, &manager) || !manager)
        return 0;
    player = ((PlayerGetter)(game + GAME_RVA_GET_LOCAL_PLAYER))(manager, 0);
    if (!player) return 0;
    ((CarGetter)(game + GAME_RVA_GET_PLAYER_CAR))(player, &local);
    if (!local) return 0;
    ObjectFn release = (ObjectFn)slot(local, 0x18);
    if (release) release(local);
    return local == car;
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
    body = dynamics;
    return 1;
}

/* the flown state: level, no speed, so the game's physics has nothing to move */
static void place(void *dynamics) {
    SetStateFn set = (SetStateFn)slot(dynamics, SLOT_SET_STATE);
    if (!set) return;
    _Alignas(16) BodyState state;
    state.position = _mm_setr_ps(pos[0], pos[1], pos[2], 0.0f);
    state.velocity = _mm_setzero_ps();
    state.spin = _mm_setzero_ps();
    state.right = _mm_setr_ps(mirror * dir[1], 0.0f, -mirror * dir[0], 0.0f);
    state.up = _mm_setr_ps(0.0f, 1.0f, 0.0f, 0.0f);
    state.forward = _mm_setr_ps(dir[0], 0.0f, dir[1], 0.0f);
    set(dynamics, &state);
}

static void fly(float dt) {
    if (!(dt > 0.0f)) return;
    if (dt > 0.1f) dt = 0.1f;
    float boost = keyboard_held(KEYBOARD_HOLD_FAST) ? FAST : 1.0f;
    float turn = (float)(keyboard_held(KEYBOARD_HOLD_LEFT) - keyboard_held(KEYBOARD_HOLD_RIGHT));
    float ahead = (float)(keyboard_held(KEYBOARD_HOLD_FORWARD) - keyboard_held(KEYBOARD_HOLD_BACK));
    float climb = (float)(keyboard_held(KEYBOARD_HOLD_UP) - keyboard_held(KEYBOARD_HOLD_DOWN));
    if (turn != 0.0f) {
        /* left = minus the right row; small angle per update, so a short series is exact enough */
        float a = turn * TURN * dt, a2 = a * a;
        float s = a * (1.0f - a2 / 6.0f + a2 * a2 / 120.0f), c = 1.0f - a2 / 2.0f + a2 * a2 / 24.0f;
        float left_x = -mirror * dir[1], left_z = mirror * dir[0];
        float x = dir[0] * c + left_x * s, z = dir[1] * c + left_z * s;
        float length = root(x * x + z * z);
        if (length > 0.5f) dir[0] = x / length, dir[1] = z / length;
    }
    pos[0] += dir[0] * ahead * SPEED * boost * dt;
    pos[2] += dir[1] * ahead * SPEED * boost * dt;
    pos[1] += climb * CLIMB * boost * dt;
}

static void card(const char *german, const char *english) {
    notify_show(game_language_is_german(game) ? german : english);
}

static void set_on(LONG value) {
    InterlockedExchange(&on, value);
    body = NULL;
}

static void handle_toggle(void) {
    ULONGLONG now = GetTickCount64();
    if (now >= next_online_check) {
        next_online_check = now + 500;
        online = game_session_online();
    }
    if (on && online) {
        set_on(0);
        log_append("NOCLIP: online session - noclip off.\r\n");
        card("Noclip aus\nOnline-Sitzung (FH2MP)", "Noclip off\nOnline session (FH2MP)");
    }
    int held = keyboard_held(KEYBOARD_HOLD_NOCLIP);
    int pressed = held && !key_held;
    key_held = held;
    if (!pressed) return;
    if (online) {
        log_append("NOCLIP: refused - online session.\r\n");
        card("Noclip gesperrt\nNur offline (nicht in FH2MP)", "Noclip locked\nOffline only (not in FH2MP)");
        return;
    }
    set_on(!on);
    log_append(on ? "NOCLIP: on\r\n" : "NOCLIP: off\r\n");
    if (on) {
        card("Noclip an\nW/S fliegen, A/D drehen, Leertaste/Strg hoch/runter",
             "Noclip on\nW/S fly, A/D turn, Space/Ctrl up/down");
    } else {
        card("Noclip aus", "Noclip off");
    }
}

void noclip_car_update(void *car, float dt, float frame_dt, unsigned frame) {
    void *dynamics = NULL;
    if (game && car && is_local_car(car)) {
        handle_toggle();
        if (on) {
            dynamics = car_body(car);
            if (dynamics && dynamics != body && !begin(dynamics)) dynamics = NULL;
            if (!dynamics && !warned) {
                warned = 1;
                log_append("NOCLIP: the car's body was not recognised - noclip does nothing.\r\n");
                card("Noclip geht nicht\nAuto nicht erkannt (Gripline.log)", "Noclip not working\nCar not recognised (Gripline.log)");
            }
            if (dynamics) {
                fly(dt);
                place(dynamics); /* the update starts from the flown place ... */
            }
        }
    }
    original(car, dt, frame_dt, frame);
    if (dynamics && on) place(dynamics); /* ... and what the game moved in it is taken back */
}

void noclip_start(unsigned char *game_image, void *original_update) {
    original = (CarUpdateFn)original_update;
    game = game_image;
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
#endif
