/* Walking - see walking.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "walking.h"
#include "keyboard_pad.h"
#include "drive_look.h"

/* ------------------------------------------------------------------ game addresses */

#define RVA_SLIDE_SPHERE 0xa92020u    /* (out, world, from, to, anchor, parameters) -> out */
#define RVA_SLIDE_PARAMS 0x2c6c2f0u   /* the photo camera's parameters (0x20 bytes + std::function) */
#define RVA_FLOOR_FILTER_VT 0x2c6d138u /* lambda: stop on a floor-like polygon */
#define RVA_MODULES 0x34aba60u        /* -> +0x50 module registry (+0x8 list, +0x18 wanted id) */
#define SLOT_UPDATE 0x20
#define SLOT_VIEW 0x50

/* cameras: vtable, family (0 = eye at +0x1a0 / direction at +0x1c0, 1 = +0x190 / +0x1b0),
   the update and view functions the game puts there (only these are chained) */
static const struct {
    unsigned vtable, update, view;
    unsigned char family;
    const char *name;
} cameras[] = {
    {0x2e78120u, 0x103c590u, 0x1034360u, 0, "driver"},      {0x2e77c30u, 0x103c590u, 0x1034360u, 0, "cockpit"},
    {0x2e77ea8u, 0x103c590u, 0x1034360u, 0, "hood"},        {0x2e703b0u, 0x103c590u, 0x1034360u, 0, "bumper"},
    {0x2e783a0u, 0x103c820u, 0x1034430u, 1, "follow"},      {0x2e78610u, 0x103c820u, 0x1034430u, 1, "follow low"},
    {0x2e78880u, 0x103c820u, 0x1034430u, 1, "follow high"},
};
#define CAMERA_COUNT (sizeof(cameras) / sizeof(cameras[0]))
static const unsigned eye_field[2] = {0x1a0, 0x190}, dir_field[2] = {0x1c0, 0x1b0};

/* the game's UI draws (as Extra Options' autodrive cinematic view): slot, function */
static const unsigned ui_slots[6] = {0x2e7f040u, 0x2ecfaa8u, 0x2ecfaa0u, 0x2e832a8u, 0x2f09088u, 0x2f09080u};
static const unsigned ui_functions[6] = {0x1140ed0u, 0x15b51a0u, 0x15b5110u, 0x1140ed0u, 0x15b51a0u, 0x15b5110u};
#define RVA_UI_RELEASE_ROOT 0x257730u

/* ------------------------------------------------------------------ state */

typedef void *(*CameraUpdateFn)(void *camera, float dt, uintptr_t a, uintptr_t b);
typedef void *(*CameraViewFn)(void *camera, void *out);
typedef void *(*SlideFn)(void *out, void *world, const __m128 *from, const __m128 *to, const __m128 *anchor,
                         const void *parameters);
typedef void *(*GetterFn)(void *);

static unsigned char *game;
static int ready;
static void *previous_update[CAMERA_COUNT], *previous_view[CAMERA_COUNT];
static void *previous_ui[6];

static volatile LONG active, entered; /* entered: walking_enter gave the car's place */
static volatile LONG step_from_enter;

/* the walker, only changed in the camera update (one thread) */
static float centre[3];      /* sphere centre, WALK_RADIUS above the ground when standing */
static float dir[2] = {0, 1}; /* heading (x, z) */
static float mirror = 1.0f;  /* the game's right row = mirror * (z, 0, -x) of the heading */
static float pitch, pitch_sin, pitch_cos = 1.0f;
static float fall;           /* vertical speed, m/s (+ = up) */
static int grounded, bob_on, no_floor, recovered_logged, jump_logged;
static float last_ground;
static float bob_phase;
/* V7.1 smoothing state */
static float vel[2];               /* horizontal velocity, m/s (world x, z) */
static float yaw_rate, pitch_rate; /* rad/s, eased */
static float mouse_yaw, mouse_pitch; /* mouse turn still to do, rad */
static float crouch;               /* 0 standing .. 1 crouched */
static float bob_amount;           /* 0..1 */
static float eye_y;                /* the eased eye height */
static int eye_y_set;
static float look[3] = {0, 0, 1};
/* V7.1 handheld camera: level (any thread), wave phases (camera thread) */
static volatile LONG handheld = 2;
/* V7.1b (Marvin 07.11.2026: "not natural yet - too even, every step the same"): random, never
   repeating movement (smooth noise) and steps that each differ, pushed into a small spring */
typedef struct { float from, to, t; } HandNoise;
static HandNoise hand_noise[6];
static unsigned hand_seed = 0x9e3779b9u;
static float step_gain = 1.0f, step_gain_to = 1.0f; /* this step's strength, eased */
static int step_half = -1;
static float spring_pitch, spring_pitch_v, spring_yaw, spring_yaw_v, spring_up, spring_up_v;
#ifdef FH2_TWEAKS_TEST
static float test_mouse[2]; /* tests: mouse counts for the next step */
#endif  /* the direction given to the camera (with the stride's small sway) */
static float car_place[3], car_right[3], car_forward[3];
static LARGE_INTEGER last_step;
static int stepped;

/* read on other threads */
static volatile LONG distance_cm = -100, speed_x10, collision_ok;
static float smooth_speed;
static float eye[3];
static float head[3]; /* the head sphere's centre (WALK_HEAD above the body, lower or aside under branches) */

static _Alignas(16) unsigned char params_free[0x40], params_floor[0x40];
static void *world;
static int world_tried; /* tests: no lookup */

#define WALK_RADIUS 0.5f     /* the parameters' sphere; eye above its centre: */
#define WALK_EYE 1.12f       /* about 1.62 m above the ground */
#define WALK_HEAD 1.0f       /* a second sphere for the head this far above the body: the eye stays
                                inside it, so it never goes into slanted trunks, branches or walls */
#define WALK_SPEED 2.0f      /* m/s (7 km/h) */
#define WALK_RUN 6.0f        /* m/s (22 km/h) */
#define WALK_JUMP 4.2f       /* m/s up: about 0.9 m high */
#define WALK_GRAVITY 9.81f
#define WALK_LOOK 1.6f       /* rad/s */
/* V7.1 smooth walking (Marvin 07.11.2026), own values: every change eases in over a short time
   instead of jumping. Times in seconds until about two thirds of the way. */
#define WALK_CROUCH_SPEED 1.1f  /* m/s while crouching (no running then) */
#define WALK_EASE_START 0.17f   /* speeding up (also walk <-> run) */
#define WALK_EASE_STOP 0.11f    /* slowing down / stopping */
#define WALK_EASE_AIR 0.9f      /* in the air: little control */
#define WALK_EASE_LOOK 0.07f    /* arrows / right stick: turning starts and stops softly */
#define WALK_EASE_MOUSE 0.03f   /* mouse: follows a little behind instead of snapping */
#define WALK_MOUSE_RAD 0.0018f  /* rad per mouse count at WalkMouseLook=100 */
#define WALK_EASE_CROUCH 0.13f
#define WALK_CROUCH_DROP 0.65f  /* the head this much lower when crouched: eye ~0.97 m above the ground */
#define WALK_EASE_EYE 0.08f     /* steps, curbs, landing: the eye follows the body softly */
#define WALK_EYE_BELOW 0.4f     /* ... but never more than this below / above where it belongs */
#define WALK_EYE_ABOVE 0.12f
#define WALK_EASE_BOB 0.3f      /* head bob fades in and out */
#define WALK_PITCH_MAX 1.4f
#define WALK_MAX_DISTANCE 2000.0f /* from the car */
#define WALK_HIDE_FROM 45.0f /* the game loads floor collision only ~65 m around the car: farther away the car */
#define WALK_HIDE_BACK 40.0f /* goes along under the walker (under the ground), back at its place nearer */
#define WALK_HIDE_BELOW 30.0f
#define WALK_FALL_LIMIT 150.0f   /* below the car: fell through the world, back to the car */
#define WALK_STEP 0.4f     /* steps and curbs up to 40 cm */
#define WALK_SNAP 0.3f     /* stays on the ground going down up to this per step */
#define WALK_HOVER 0.01f   /* the sphere rests this far above the floor */
#define WALK_RECOVER 1.5f  /* sunk into the floor: looked for again from this far above */
#define WALK_SLIDE_SLACK 0.3f /* a slide result may be this much farther than the move itself */
#define WALK_RECOVER_MAX 0.6f /* the floor found again lifts the walker at most this much */
#define WALK_PROBE 25.0f   /* looks this far down for a floor (less than WALK_HIDE_BELOW: not the car) */
#define WALK_NO_FLOOR 4.0f /* metres of falling without any floor: no collision loaded there */

int walking_ready(void) {
    return ready;
}

/* ------------------------------------------------------------------ door sounds */

#include "door_sounds.h"
#define SND_ASYNC_ 0x0001u
#define SND_NODEFAULT_ 0x0002u
#define SND_MEMORY_ 0x0004u
typedef int(WINAPI *PlaySoundWFn)(const void *, void *, unsigned long);
typedef unsigned(WINAPI *GetSystemDirectoryWFn)(wchar_t *, unsigned);

void walking_door_sound(int shut) {
    static PlaySoundWFn play;
    static int tried;
    if (!tried) {
        tried = 1;
        /* the full path: Extra Options is itself a winmm.dll in the game folder */
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        GetSystemDirectoryWFn system_dir =
            kernel ? (GetSystemDirectoryWFn)(uintptr_t)GetProcAddress(kernel, "GetSystemDirectoryW") : NULL;
        wchar_t path[300];
        unsigned n = system_dir ? system_dir(path, 280) : 0;
        if (n && n < 280) {
            static const wchar_t name[] = L"\\winmm.dll";
            for (unsigned i = 0; i < sizeof(name) / sizeof(name[0]); ++i) path[n + i] = name[i];
            HMODULE winmm = LoadLibraryExW(path, NULL, 0);
            if (winmm) play = (PlaySoundWFn)(uintptr_t)GetProcAddress(winmm, "PlaySoundW");
        }
        log_append(play ? "WALKING: door sounds ready.\r\n" : "WALKING: winmm PlaySoundW not found - no door sounds.\r\n");
    }
    if (play) play(shut ? door_close_wav : door_open_wav, NULL, SND_ASYNC_ | SND_NODEFAULT_ | SND_MEMORY_);
}

int walking_active(void) {
    return active != 0;
}

float walking_car_distance(void) {
    LONG cm = distance_cm;
    return cm < 0 ? -1.0f : (float)cm / 100.0f;
}

int walking_speed_kmh(void) {
    return (int)((speed_x10 + 5) / 10);
}

int walking_has_collision(void) {
    return collision_ok != 0;
}

/* noclip.c, car update thread: 1 = hold the car at out (under the walker) instead of its place */
static volatile LONG hiding;
static float hide_at[3];
int walking_car_hidden(float out[3]) {
    if (!active || !entered || !stepped) return 0;
    float d = walking_car_distance();
    if (d > WALK_HIDE_FROM) InterlockedExchange(&hiding, 1);
    else if (d >= 0.0f && d < WALK_HIDE_BACK) InterlockedExchange(&hiding, 0);
    if (!hiding) return 0;
    out[0] = hide_at[0], out[1] = hide_at[1], out[2] = hide_at[2];
    return 1;
}

int walking_no_floor(void) {
    return no_floor;
}

void walking_set_active(int on) {
    if (!on) InterlockedExchange(&entered, 0), InterlockedExchange(&hiding, 0);
    InterlockedExchange(&active, on ? 1 : 0);
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x > 0.0f ? x : 0.0f)));
}

static void sin_cos(float a, float *s, float *c) {
    float a2 = a * a; /* |a| <= 1.6 */
    *s = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
    *c = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f)));
}

/* any angle (the bob's phases): back to -pi..pi, then mirrored into the range above */
static void sin_cos_wide(float a, float *s, float *c) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    if (a > 1.5707963f) {
        sin_cos(3.14159265f - a, s, c), *c = -*c;
    } else if (a < -1.5707963f) {
        sin_cos(-3.14159265f - a, s, c), *c = -*c;
    } else {
        sin_cos(a, s, c);
    }
}

void walking_set_handheld(int level) {
    InterlockedExchange(&handheld, level < 0 ? 0 : level >= WALK_HANDHELD_COUNT ? WALK_HANDHELD_COUNT - 1 : level);
}

int walking_handheld(void) {
    return (int)handheld;
}

/* -1..1, a new number each call (xorshift) */
static float hand_random(void) {
    hand_seed ^= hand_seed << 13, hand_seed ^= hand_seed >> 17, hand_seed ^= hand_seed << 5;
    return (float)(hand_seed & 0xffffu) / 32767.5f - 1.0f;
}

/* smooth noise: from one random value to the next in 1/rate s, eased (no corners), never the same */
static float hand_noise_step(HandNoise *n, float dt, float rate) {
    if (n->from == 0.0f && n->to == 0.0f) n->from = hand_random(), n->to = hand_random(); /* first use */
    n->t += dt * rate;
    while (n->t >= 1.0f) n->t -= 1.0f, n->from = n->to, n->to = hand_random();
    float t = n->t, e = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    return n->from + (n->to - n->from) * e;
}

/* a small spring (the hand holding the phone): pushed by the steps, swings back softly */
static void hand_spring(float *x, float *v, float dt, float hz, float damping) {
    float w = 6.2831853f * hz;
    *v += (-w * w * *x - 2.0f * damping * w * *v) * dt;
    *x += *v * dt;
}

void walking_enter(const float position[3], const float right[3], const float forward[3]) {
    for (unsigned i = 0; i < 3; ++i) car_place[i] = position[i], car_right[i] = right[i], car_forward[i] = forward[i];
    InterlockedExchange(&step_from_enter, 1);
    InterlockedExchange(&entered, 1);
}

/* ------------------------------------------------------------------ collision */

static int in_game(const void *p) {
    return (const unsigned char *)p >= game && (const unsigned char *)p < game + GAME_IMAGE_SIZE;
}

static void *slot_function(void *object, unsigned offset) {
    void *vtable, *fn;
    if (!object || !memory_read_pointer(object, &vtable) || !memory_read_pointer((unsigned char *)vtable + offset, &fn) ||
        !in_game(fn))
        return NULL;
    return fn;
}

/* The collision world the photo camera uses: registry -> module with the wanted id -> +0x10
   interface -> slot 0x30 -> slot 0x130. The game's own lookup waits until the module exists; this
   one only looks. */
static void *find_world(void) {
    void *root_object, *registry, *head, *node;
    unsigned id = 0;
    if (!memory_read_pointer(game + RVA_MODULES, &root_object) || !root_object ||
        !memory_read_pointer((unsigned char *)root_object + 0x50, &registry) || !registry ||
        !memory_read((unsigned char *)registry + 0x18, &id, 4) ||
        !memory_read_pointer((unsigned char *)registry + 8, &head) || !head || !memory_read_pointer(head, &node))
        return NULL;
    for (unsigned guard = 0; node && node != head && guard < 512; ++guard) {
        void *module = NULL;
        unsigned short module_id = 0;
        if (!memory_read_pointer((unsigned char *)node + 0x10, &module)) return NULL;
        if (module && memory_read((unsigned char *)module + 0x38, &module_id, 2) && module_id == (unsigned short)id) {
            void *face = (unsigned char *)module + 0x10;
            GetterFn get = (GetterFn)slot_function(face, 0x30);
            void *x = get ? get(face) : NULL;
            GetterFn get_world = (GetterFn)slot_function(x, 0x130);
            return get_world ? get_world(x) : NULL;
        }
        if (!memory_read_pointer(node, &node)) return NULL;
    }
    return NULL;
}

/* the RTTI class name of a game object (diagnostics in Gripline.log) */
static void log_class(const char *label, void *object) {
    void *vtable, *locator;
    unsigned type = 0;
    char line[160];
    size_t n = 0;
    if (!object || !memory_read_pointer(object, &vtable) || !in_game(vtable) ||
        !memory_read_pointer((unsigned char *)vtable - 8, &locator) || !in_game(locator) ||
        !memory_read((unsigned char *)locator + 0xc, &type, 4) || type >= GAME_IMAGE_SIZE)
        return;
    while (label[n] && n < 60) line[n] = label[n], ++n;
    const char *name = (const char *)game + type + 0x10;
    for (unsigned i = 0; i < 90 && name[i] >= 0x20 && name[i] < 0x7f; ++i) line[n++] = name[i];
    line[n++] = '\r', line[n++] = '\n', line[n] = 0;
    log_append(line);
}

static void make_params(void) {
    memory_copy(params_free, game + RVA_SLIDE_PARAMS, 0x20); /* iterations, radius, skin ... as the game has them */
    params_free[0x1c] = params_free[0x1d] = params_free[0x1e] = 0; /* no photo mode distance / height limits */
    params_free[0x1f] = 1;                                          /* the world's collision */
    for (unsigned i = 0x20; i < 0x40; ++i) params_free[i] = 0;      /* no filter */
    memory_copy(params_floor, params_free, 0x40);
    void *vt = game + RVA_FLOOR_FILTER_VT, *self = params_floor + 0x20;
    memory_copy(params_floor + 0x20, &vt, 8);
    memory_copy(params_floor + 0x38, &self, 8); /* std::function: its callable lives in its own buffer */
}

/* moves the sphere from -> to; 1 = it hit something. out = where it ended. */
static int slide(const float from[3], const float to[3], int floor, float out[3]) {
    for (unsigned i = 0; i < 3; ++i) out[i] = to[i];
    if (!world) return 0;
    _Alignas(16) __m128 a = _mm_setr_ps(from[0], from[1], from[2], 0.0f);
    _Alignas(16) __m128 b = _mm_setr_ps(to[0], to[1], to[2], 0.0f);
    _Alignas(16) unsigned char result[0x20];
    memory_copy(result, &a, 16);
    for (unsigned i = 16; i < 0x20; ++i) result[i] = 0;
    __try {
        ((SlideFn)(game + RVA_SLIDE_SPHERE))(result, world, &a, &b, &a, floor ? params_floor : params_free);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        world = NULL; /* from now on without collision */
        InterlockedExchange(&collision_ok, 0);
        log_append("WALKING: the collision check failed - walking on without collision.\r\n");
        return 0;
    }
    _Alignas(16) float p[4];
    memory_copy(p, result, 16);
    if (!float_is_finite(p[0]) || !float_is_finite(p[1]) || !float_is_finite(p[2])) {
        for (unsigned i = 0; i < 3; ++i) out[i] = from[i];
        return 1;
    }
    /* the sphere can only end within the move (plus a little sliding). When it starts inside something
       (a trunk, a branch) the game's function sometimes gives back a place far away - near the car, as
       the photo mode puts its camera back there (Marvin's video 06.11.2026: jumps of 10-20 m between
       trees). Such a result is thrown away: the sphere stays where it was. */
    float mx = to[0] - from[0], my = to[1] - from[1], mz = to[2] - from[2];
    float ex = p[0] - from[0], ey = p[1] - from[1], ez = p[2] - from[2];
    float reach = root(mx * mx + my * my + mz * mz) + WALK_SLIDE_SLACK;
    if (ex * ex + ey * ey + ez * ez > reach * reach) {
        for (unsigned i = 0; i < 3; ++i) out[i] = from[i];
        if (!jump_logged) {
            jump_logged = 1;
            log_append_value("WALKING: a collision result far off the move was ignored, metres: ",
                             (int)root(ex * ex + ey * ey + ez * ez));
        }
        return 1;
    }
    out[0] = p[0], out[1] = p[1], out[2] = p[2];
    void *hit = NULL;
    memory_copy(&hit, result + 0x10, 8);
    return hit != NULL;
}

/* ------------------------------------------------------------------ the walker */

/* share of the way to a target after dt with ease time tau (1 - e^(-dt/tau), series of e^x) */
static float ease(float dt, float tau) {
    if (!(dt > 0.0f)) return 0.0f;
    float x = dt / tau;
    if (x > 8.0f) return 1.0f;
    return 1.0f - 1.0f / (1.0f + x + x * x / 2.0f + x * x * x / 6.0f + x * x * x * x / 24.0f);
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void turn(float a) {
    float s, c;
    sin_cos(a, &s, &c);
    float left_x = -mirror * dir[1], left_z = mirror * dir[0];
    float x = dir[0] * c + left_x * s, z = dir[1] * c + left_z * s;
    float length = root(x * x + z * z);
    if (length > 0.5f) dir[0] = x / length, dir[1] = z / length;
}

static void spawn(void) {
    float length = root(car_forward[0] * car_forward[0] + car_forward[2] * car_forward[2]);
    if (length > 0.1f) dir[0] = car_forward[0] / length, dir[1] = car_forward[2] / length;
    mirror = car_right[0] * dir[1] - car_right[2] * dir[0] >= 0.0f ? 1.0f : -1.0f;
    pitch = 0.0f, pitch_sin = 0.0f, pitch_cos = 1.0f;
    fall = 0.0f, grounded = 0, no_floor = 0;
    vel[0] = vel[1] = 0.0f, yaw_rate = pitch_rate = 0.0f, mouse_yaw = mouse_pitch = 0.0f;
    crouch = 0.0f, bob_amount = 0.0f, bob_phase = 0.0f, eye_y_set = 0;
    if (!world && !world_tried) { /* again at every getting out until found */
        make_params();
        __try {
            world = find_world();
        } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            world = NULL;
        }
        log_append_module(world ? "WALKING: collision world found " : "WALKING: no collision world - walking without collision ",
                          world);
        log_class("WALKING: collision world class ", world);
    }
    InterlockedExchange(&collision_ok, world != NULL);
    /* out at the left of the car (the driver's door of most cars), from inside the car outwards */
    float rl = root(car_right[0] * car_right[0] + car_right[2] * car_right[2]);
    float lx = rl > 0.1f ? -car_right[0] / rl : -mirror * dir[1], lz = rl > 0.1f ? -car_right[2] / rl : mirror * dir[0];
    float from[3] = {car_place[0], car_place[1] + 1.0f, car_place[2]};
    float to[3] = {car_place[0] + lx * 2.0f, car_place[1] + 1.0f, car_place[2] + lz * 2.0f};
    slide(from, to, 0, centre);
    last_ground = centre[1];
    stepped = 0;
}

static void step(float dt) {
    WalkInput in = {0};
    noclip_walk_input(&in);
#ifdef FH2_TWEAKS_TEST
    in.mouse_x += test_mouse[0], in.mouse_y += test_mouse[1];
    test_mouse[0] = test_mouse[1] = 0.0f;
#endif
    /* looking: arrows / stick turn at an eased rate, the mouse turn is handed out over a few frames */
    float hand = handheld == 1 ? 0.5f : handheld == 2 ? 1.0f : handheld >= 3 ? 1.7f : 0.0f;
    float kl = ease(dt, WALK_EASE_LOOK * (1.0f + 0.7f * hand)); /* the hand turns a little behind */
    yaw_rate += (in.yaw * WALK_LOOK - yaw_rate) * kl;
    pitch_rate += (in.pitch * WALK_LOOK - pitch_rate) * kl;
    mouse_yaw = clampf(mouse_yaw - in.mouse_x * WALK_MOUSE_RAD, -1.5f, 1.5f);
    mouse_pitch = clampf(mouse_pitch - in.mouse_y * WALK_MOUSE_RAD, -1.5f, 1.5f);
    float km = ease(dt, WALK_EASE_MOUSE * (1.0f + 1.2f * hand));
    float myaw = mouse_yaw * km, mpitch = mouse_pitch * km;
    mouse_yaw -= myaw, mouse_pitch -= mpitch;
    float yaw_step = yaw_rate * dt + myaw;
    if (yaw_step > 1e-6f || yaw_step < -1e-6f) turn(yaw_step);
    pitch = clampf(pitch + pitch_rate * dt + mpitch, -WALK_PITCH_MAX, WALK_PITCH_MAX);
    if (pitch >= WALK_PITCH_MAX || pitch <= -WALK_PITCH_MAX) mouse_pitch = 0.0f;
    sin_cos(pitch, &pitch_sin, &pitch_cos);
    /* crouching: eases down and up; the head sphere below keeps it low under something */
    crouch += ((in.crouch ? 1.0f : 0.0f) - crouch) * ease(dt, WALK_EASE_CROUCH);
    int crouching = in.crouch || crouch > 0.5f;
    /* moving: the velocity eases to the wanted one (speeding up, slowing down, walk <-> run) */
    float speed = crouching ? WALK_CROUCH_SPEED : in.run ? WALK_RUN : WALK_SPEED;
    float rx = mirror * dir[1], rz = -mirror * dir[0];
    float ia = in.ahead, is = in.side, il = root(ia * ia + is * is);
    if (il > 1.0f) ia /= il, is /= il; /* diagonally not faster */
    float want[2] = {(dir[0] * ia + rx * is) * speed, (dir[1] * ia + rz * is) * speed};
    float v2 = vel[0] * vel[0] + vel[1] * vel[1], w2 = want[0] * want[0] + want[1] * want[1];
    float kv = ease(dt, !grounded && world ? WALK_EASE_AIR : w2 >= v2 ? WALK_EASE_START : WALK_EASE_STOP);
    vel[0] += (want[0] - vel[0]) * kv, vel[1] += (want[1] - vel[1]) * kv;
    if (w2 == 0.0f && vel[0] * vel[0] + vel[1] * vel[1] < 0.0004f) vel[0] = vel[1] = 0.0f;
    float mx = vel[0] * dt, mz = vel[1] * dt;
    float head_up = WALK_HEAD - WALK_CROUCH_DROP * crouch;
    float before[3] = {centre[0], centre[1], centre[2]};

    /* along the ground: first up by a step (curbs, stairs), then forward sliding along walls; the
       sphere never grazes the floor it stands on */
    float lifted = 0.0f;
    if (mx != 0.0f || mz != 0.0f) {
        float dx = centre[0] + mx - car_place[0], dz = centre[2] + mz - car_place[2];
        if (dx * dx + dz * dz <= WALK_MAX_DISTANCE * WALK_MAX_DISTANCE) {
            float raised[3] = {centre[0], centre[1], centre[2]};
            if (world && grounded) {
                float up[3] = {centre[0], centre[1] + WALK_STEP, centre[2]};
                slide(centre, up, 0, raised);
                lifted = raised[1] - centre[1];
            }
            if (world) {
                /* the head first: where it is stopped (a slanted trunk, a branch, a roof edge) the body
                   stops too. Under something low the head ducks. */
                float body_top[3] = {raised[0], raised[1] + head_up, raised[2]}, hf[3];
                slide(raised, body_top, 0, hf);
                if (hf[1] - raised[1] > 0.3f) {
                    float ht[3] = {hf[0] + mx, hf[1], hf[2] + mz}, hd[3];
                    slide(hf, ht, 0, hd);
                    float hx = hd[0] - hf[0], hz = hd[2] - hf[2];
                    if (hx * hx + hz * hz < mx * mx + mz * mz) mx = hx, mz = hz;
                }
            }
            float to[3] = {raised[0] + mx, raised[1], raised[2] + mz};
            slide(raised, to, 0, centre);
        }
    }
    /* gravity and jumping: stops on a floor */
    if (in.jump && grounded && !crouching) fall = WALK_JUMP, grounded = 0, lifted = 0.0f;
    fall -= WALK_GRAVITY * dt;
    if (fall < -50.0f) fall = -50.0f;
    if (world) {
        float dy = fall * dt - lifted;
        if (grounded && dy > -(lifted + WALK_SNAP)) dy = -(lifted + WALK_SNAP); /* down a slope or a step */
        float to[3] = {centre[0], centre[1] + dy, centre[2]}, end[3];
        int hit = slide(centre, to, 1, end);
        if (!hit && dy < 0.0f) {
            /* nothing below: maybe the sphere sank into the floor - look again from above the head */
            float top[3] = {centre[0], centre[1] + WALK_RECOVER, centre[2]}, again[3];
            /* only a floor just above the sphere counts: next to a tree trunk or a wall the sphere
               from above stops at once (on the trunk), which lifted the walker up into the air
               (Marvin 06.11.2026) */
            if (slide(top, to, 1, again) && again[1] > to[1] && again[1] < top[1] - 0.05f &&
                again[1] <= centre[1] + WALK_RECOVER_MAX) {
                hit = 1;
                end[0] = again[0], end[1] = again[1], end[2] = again[2];
                if (!recovered_logged) {
                    recovered_logged = 1;
                    log_append("WALKING: the floor was found again from above (sphere had sunk in).\r\n");
                }
            }
        }
        if (hit && dy < 0.0f) {
            grounded = 1;
            fall = 0.0f;
            end[1] += WALK_HOVER;
            last_ground = end[1];
            no_floor = 0;
        } else if (no_floor && end[1] < last_ground) {
            float below[3] = {end[0], end[1] - WALK_PROBE, end[2]}, ground[3];
            if (slide(end, below, 1, ground) && ground[1] < end[1]) {
                end[0] = ground[0], end[1] = ground[1] + WALK_HOVER, end[2] = ground[2]; /* a floor below again */
                fall = 0.0f, grounded = 1, no_floor = 0, last_ground = end[1];
            } else {
                end[1] = last_ground, fall = 0.0f, grounded = 1; /* still no floor here: stay level */
            }
        } else {
            grounded = 0;
            if (hit) fall = 0.0f; /* the head hit a ceiling */
        }
        centre[0] = end[0], centre[1] = end[1], centre[2] = end[2];
        float probe_to[3] = {centre[0], centre[1] - WALK_PROBE, centre[2]}, probe[3];
        if (!grounded && fall < 0.0f && centre[1] < last_ground - WALK_NO_FLOOR &&
            !(slide(centre, probe_to, 1, probe) && probe[1] < centre[1])) {
            /* (a floor farther down, e.g. after a jump from high up: just fall on) */
            /* fell several metres without any floor: the game has no collision loaded here. Walk on at
               the last floor height instead of falling through the world. */
            centre[1] = last_ground, fall = 0.0f, grounded = 1;
            if (!no_floor) {
                float cx = centre[0] - car_place[0], cz = centre[2] - car_place[2];
                log_append_value("WALKING: no floor collision here, metres from the car: ", (int)root(cx * cx + cz * cz));
            }
            no_floor = 1;
        }
    } else {
        /* no collision world: walk at the height of the car, no gravity */
        centre[1] = car_place[1] + WALK_RADIUS, fall = 0.0f, grounded = 1;
    }
    if (centre[1] < car_place[1] - WALK_FALL_LIMIT) { /* fell out of the world: back to the car */
        log_append("WALKING: fell through the world - back at the car.\r\n");
        spawn();
        before[0] = centre[0], before[1] = centre[1], before[2] = centre[2];
    }
    float dx = centre[0] - before[0], dz = centre[2] - before[2];
    float moved = dt > 0.0f ? root(dx * dx + dz * dz) / dt : 0.0f;
    smooth_speed += (moved - smooth_speed) * (dt * 6.0f > 1.0f ? 1.0f : dt * 6.0f);
    InterlockedExchange(&speed_x10, (LONG)(smooth_speed * 36.0f + 0.5f));
    /* blocked by a wall: the velocity is only what was really walked (no run-up against the wall) */
    if (dt > 0.0f) {
        float ax = dx / dt, az = dz / dt, va = vel[0] * vel[0] + vel[1] * vel[1];
        if (ax * ax + az * az < va) vel[0] = ax, vel[1] = az;
    }
    /* head bob: fades in with the speed and out when standing or in the air; one stride = two steps
       (the eye goes down at each step, a little to the side of the stepping foot, the look sways gently) */
    float hs = root(vel[0] * vel[0] + vel[1] * vel[1]);
    float bob_want = grounded ? clampf(hs / WALK_SPEED, 0.0f, 1.0f) : 0.0f;
    bob_amount += (bob_want - bob_amount) * ease(dt, WALK_EASE_BOB);
    if (bob_amount < 0.002f && bob_want == 0.0f) bob_amount = 0.0f;
    bob_on = bob_amount > 0.0f;
    float bob = 0.0f, side_sway = 0.0f, look_sway = 0.0f;
    if (bob_on) {
        float strides = 0.55f + 0.16f * hs; /* per second: ~0.9 walking, ~1.5 running */
        bob_phase += dt * 6.2831853f * strides;
        if (bob_phase > 6.2831853f) bob_phase -= 6.2831853f;
        float s1, c1, s2, c2;
        sin_cos_wide(bob_phase, &s1, &c1);
        sin_cos_wide(2.0f * bob_phase, &s2, &c2);
        float run_share = clampf((hs - WALK_SPEED) / (WALK_RUN - WALK_SPEED), 0.0f, 1.0f);
        float height = (0.026f + 0.02f * run_share) * (1.0f - 0.5f * crouch);
        bob = -height * c2 * bob_amount;
        side_sway = (0.022f + 0.012f * run_share) * s1 * bob_amount;
        look_sway = 0.0035f * (1.0f + run_share) * s1 * bob_amount;
    }
    if (world) { /* the eye inside the head sphere, under whatever is above */
        float top[3] = {centre[0], centre[1] + head_up, centre[2]};
        slide(centre, top, 0, head);
    } else {
        head[0] = centre[0], head[1] = centre[1] + head_up, head[2] = centre[2];
    }
    /* the eye height eases after the body (steps, curbs, landing), big jumps (teleports) at once */
    float eye_want = head[1] + (WALK_EYE - WALK_HEAD);
    if (!eye_y_set || eye_y - eye_want > 1.5f || eye_want - eye_y > 1.5f) eye_y = eye_want, eye_y_set = 1;
    eye_y += (eye_want - eye_y) * ease(dt, WALK_EASE_EYE);
    eye_y = clampf(eye_y, eye_want - WALK_EYE_BELOW, eye_want + WALK_EYE_ABOVE);
    /* handheld camera: drift and breathing standing still, a hand that bounces with the steps */
    float hand_yaw = 0.0f, hand_pitch = 0.0f, hand_side = 0.0f, hand_up = 0.0f;
    if (hand > 0.0f && dt > 0.0f) {
        float moving = bob_amount, run_share = clampf((hs - WALK_SPEED) / (WALK_RUN - WALK_SPEED), 0.0f, 1.0f);
        float calm = 1.0f - 0.45f * crouch; /* crouched the hands are steadier */
        /* standing: slow drift and breathing; walking and running: faster, stronger shake */
        float slow = 0.35f + 0.25f * moving, fast = 1.4f + 1.6f * moving + 1.5f * run_share;
        float shake = hand * calm * (0.4f + 0.6f * moving + 0.8f * run_share);
        hand_yaw = hand * calm * 0.011f * hand_noise_step(&hand_noise[0], dt, slow) +
                   shake * 0.0022f * hand_noise_step(&hand_noise[1], dt, fast);
        hand_pitch = hand * calm * 0.007f * hand_noise_step(&hand_noise[2], dt, slow * 1.3f) +
                     shake * 0.0018f * hand_noise_step(&hand_noise[3], dt, fast * 1.2f);
        hand_side = hand * calm * 0.008f * hand_noise_step(&hand_noise[4], dt, slow * 0.8f);
        hand_up = hand * calm * 0.005f * hand_noise_step(&hand_noise[5], dt, slow * 1.1f);
        /* every footfall pushes the spring a little differently: strength, side, timing feel */
        int half = bob_on ? (bob_phase >= 3.14159265f) : -1;
        if (half >= 0 && step_half >= 0 && half != step_half) {
            step_gain_to = 1.0f + 0.35f * hand_random();
            float kick = hand * calm * moving * (0.7f + 0.5f * run_share) * (0.75f + 0.25f * hand_random());
            spring_pitch_v -= kick * 0.09f;                              /* the phone dips at the footfall */
            spring_yaw_v += kick * 0.05f * (half ? 1.0f : -1.0f) * (0.6f + 0.4f * hand_random()); /* to the foot's side */
            spring_up_v -= kick * 0.12f;
        }
        step_half = half;
        hand_spring(&spring_pitch, &spring_pitch_v, dt, 2.6f, 0.45f);
        hand_spring(&spring_yaw, &spring_yaw_v, dt, 1.9f, 0.5f);
        hand_spring(&spring_up, &spring_up_v, dt, 2.2f, 0.5f);
        hand_pitch += spring_pitch, hand_yaw += spring_yaw, hand_up += spring_up;
        /* the regular bob gets this step's strength too */
        step_gain += (step_gain_to - step_gain) * ease(dt, 0.12f);
        bob *= step_gain, side_sway *= step_gain, look_sway *= step_gain;
    } else if (hand == 0.0f) {
        step_gain = step_gain_to = 1.0f, step_half = -1;
        spring_pitch = spring_pitch_v = spring_yaw = spring_yaw_v = spring_up = spring_up_v = 0.0f;
    }
    eye[0] = head[0] + rx * (side_sway + hand_side), eye[1] = eye_y + bob + hand_up,
    eye[2] = head[2] + rz * (side_sway + hand_side);
    {
        float ss, sc, ps = pitch_sin, pc = pitch_cos;
        sin_cos(look_sway + hand_yaw, &ss, &sc);
        if (hand_pitch != 0.0f) sin_cos(clampf(pitch + hand_pitch, -WALK_PITCH_MAX, WALK_PITCH_MAX), &ps, &pc);
        float lx = -mirror * dir[1], lz = mirror * dir[0]; /* left, as in turn() */
        look[0] = pc * (dir[0] * sc + lx * ss), look[1] = ps, look[2] = pc * (dir[1] * sc + lz * ss);
    }
    hide_at[0] = centre[0], hide_at[1] = centre[1] - WALK_HIDE_BELOW, hide_at[2] = centre[2];
    float cx = centre[0] - car_place[0], cz = centre[2] - car_place[2];
    InterlockedExchange(&distance_cm, (LONG)(root(cx * cx + cz * cz) * 100.0f));
    stepped = 1;
}

/* once per frame, whichever camera updates first */
static void walk_frame(void) {
    if (!active || !entered) return;
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (InterlockedExchange(&step_from_enter, 0)) {
        spawn();
        last_step = now;
    }
    double dt = (double)(now.QuadPart - last_step.QuadPart) / (double)frequency.QuadPart;
    if (stepped && dt < 0.002) return;
    last_step = now;
    if (!(dt > 0.0) || dt > 0.1) dt = 1.0 / 60.0;
    if (!game_simulation_running()) dt = 0.0; /* paused: stand still, still put the eye */
    step((float)dt);
}

static void put_eye(void *camera, unsigned family) {
    if (!active || !entered || !stepped) return;
    double e[3] = {eye[0], eye[1], eye[2]};
    float d[4];
    unsigned char *c = (unsigned char *)camera;
    if (!memory_read(c + dir_field[family], d, sizeof(d))) return;
    d[0] = look[0], d[1] = look[1], d[2] = look[2];
    memory_copy(c + eye_field[family], e, sizeof(e));
    memory_copy(c + dir_field[family], d, 12);
}

static int camera_index(void *camera) {
    void *vtable;
    if (!memory_read_pointer(camera, &vtable)) return -1;
    for (unsigned i = 0; i < CAMERA_COUNT; ++i)
        if (vtable == game + cameras[i].vtable) return (int)i;
    return -1;
}

static void *camera_update(void *camera, float dt, uintptr_t a, uintptr_t b) {
    int i = camera_index(camera);
    if (i < 0) return NULL; /* not one of ours: cannot happen, the slot is only in our vtables */
    unsigned family = cameras[i].family;
    drive_look_restore(camera, (unsigned)i, eye_field[family], dir_field[family]); /* the game's own values first */
    void *result = ((CameraUpdateFn)previous_update[i])(camera, dt, a, b);
    if (active) {
        walk_frame();
        put_eye(camera, family);
    } else {
        drive_look_frame(); /* V7.1 mouse look in the car */
        drive_look_apply(camera, (unsigned)i, family, eye_field[family], dir_field[family]);
    }
    return result;
}

/* V7.1: the camera the game looks through (its view getter runs for it), for the camera change
   when getting out of / into the car (noclip.c, cockpit_view.c) */
static volatile LONG view_index = -1;
static volatile LONG64 view_tick;

int walking_view_camera(void) {
    LONG64 seen = view_tick;
    if (!seen || GetTickCount64() - (ULONGLONG)seen > 300u) return -1;
    return (int)view_index;
}

int walking_view_outside(void) {
    int i = walking_view_camera();
    return i >= 0 && cameras[i].family == 1;
}

static void *camera_view(void *camera, void *out) {
    int i = camera_index(camera);
    if (i < 0) return out;
    InterlockedExchange(&view_index, i);
    InterlockedExchange64(&view_tick, (LONG64)GetTickCount64());
    if (active) put_eye(camera, cameras[i].family);
    return ((CameraViewFn)previous_view[i])(camera, out);
}

/* The game's multi camera (game+0xfe61c0) moves a follow camera towards its car when something is
   between them (vtable slot 0x230 game+0x103ef50, only when slot 0x238 says 1 - the follow cameras do,
   the cockpit cameras not). The walker is far from the car, behind trees and houses: the eye jumped
   towards the car (Marvin 06.11.2026, gone in the cockpit view). While walking slot 0x238 says 0. */
#define SLOT_AVOID 0x238u
static const unsigned follow_vtables[3] = {0x2e783a0u, 0x2e78610u, 0x2e78880u};
static void *previous_avoid[3];
typedef unsigned char (*AvoidFn)(void *);
static unsigned char avoid(unsigned i, void *camera) {
    if (active && entered) return 0;
    return ((AvoidFn)previous_avoid[i])(camera);
}
static unsigned char avoid0(void *c) { return avoid(0, c); }
static unsigned char avoid1(void *c) { return avoid(1, c); }
static unsigned char avoid2(void *c) { return avoid(2, c); }

/* ------------------------------------------------------------------ HUD */

typedef void (*Draw3Fn)(void *, void *, void *);
typedef void (*Draw2Fn)(void *, void *);
typedef void (*ReleaseFn)(void *);

static int hud_hidden(void) {
    static volatile LONG running;
    static volatile LONG64 checked;
    if (!active || !entered) return 0;
    ULONGLONG now = GetTickCount64();
    if (now - (ULONGLONG)checked >= 50) { /* the draws of one frame nearly always agree */
        InterlockedExchange64(&checked, (LONG64)now);
        InterlockedExchange(&running, game_simulation_running());
    }
    return running != 0;
}

/* skipped draws give back what the game handed to them (as Extra Options' cinematic view) */
static void wait_render(unsigned i, void *renderer, void *surface, void *parameters) {
    if (!hud_hidden()) {
        ((Draw3Fn)previous_ui[i])(renderer, surface, parameters);
        return;
    }
    void *owned = *(void **)surface; /* the graphics wrapper releases at +0x18 */
    if (owned) ((ReleaseFn)(*(void ***)owned)[0x18 / 8])(owned);
}

static void draw(unsigned i, void *application, void *screen, void *transform) {
    if (!hud_hidden()) {
        ((Draw3Fn)previous_ui[i])(application, screen, transform);
        return;
    }
    unsigned char *root_screen = *(unsigned char **)screen; /* Render consumes the screen */
    if (root_screen && InterlockedDecrement((volatile LONG *)(root_screen + 0x10)) == 0)
        ((ReleaseFn)(game + RVA_UI_RELEASE_ROOT))(root_screen);
}

static void present(unsigned i, void *application, void *transform) {
    if (!hud_hidden()) ((Draw2Fn)previous_ui[i])(application, transform);
}

static void ui0(void *a, void *b, void *c) { wait_render(0, a, b, c); }
static void ui1(void *a, void *b, void *c) { draw(1, a, b, c); }
static void ui2(void *a, void *b) { present(2, a, b); }
static void ui3(void *a, void *b, void *c) { wait_render(3, a, b, c); }
static void ui4(void *a, void *b, void *c) { draw(4, a, b, c); }
static void ui5(void *a, void *b) { present(5, a, b); }

/* ------------------------------------------------------------------ hooks */

/* puts hook into a vtable slot; previous = what was there (the game's function or another mod's).
   expected = the game's own function: a slot holding something else that is not code is left alone. */
static int chain(unsigned slot_rva, void *hook, void **previous) {
    void **slot = (void **)(game + slot_rva);
    void *current = NULL;
    if (!memory_read_pointer(slot, &current) || !current) return 0;
    if (current == hook) return 1;
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    *previous = current;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void *), old, &ignored);
    return 1;
}

int walking_start(unsigned char *game_image) {
    if (ready || !game_image) return ready;
    game = game_image;
    /* the game code walking relies on: the slide function starts as it did when it was found */
    static const unsigned char slide_start[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10};
    void *filter_call = NULL;
    if (!memory_equal(game + RVA_SLIDE_SPHERE, slide_start, sizeof(slide_start)) ||
        !memory_read_pointer(game + RVA_FLOOR_FILTER_VT + 0x10, &filter_call) || filter_call != game + 0x399720u) {
        log_append("WALKING: game code differs - walking off.\r\n");
        return 0;
    }
    unsigned cameras_hooked = 0;
    for (unsigned i = 0; i < CAMERA_COUNT; ++i) {
        void *update = NULL, *view = NULL;
        memory_read_pointer(game + cameras[i].vtable + SLOT_UPDATE, &update);
        memory_read_pointer(game + cameras[i].vtable + SLOT_VIEW, &view);
        /* the game's own functions, or (another mod) code outside the vtable: both are chained */
        if (!update || !view) continue;
        if (chain(cameras[i].vtable + SLOT_UPDATE, (void *)(uintptr_t)camera_update, &previous_update[i]) &&
            chain(cameras[i].vtable + SLOT_VIEW, (void *)(uintptr_t)camera_view, &previous_view[i]))
            ++cameras_hooked;
        if (update != game + cameras[i].update || view != game + cameras[i].view)
            log_append_module("WALKING: a camera was already hooked by ", update != game + cameras[i].update ? update : view);
    }
    if (cameras_hooked != CAMERA_COUNT) {
        log_append_value("WALKING: cameras hooked (of 7): ", (int)cameras_hooked);
        if (!cameras_hooked) return 0;
    }
    static void *const avoid_hooks[3] = {(void *)(uintptr_t)avoid0, (void *)(uintptr_t)avoid1,
                                         (void *)(uintptr_t)avoid2};
    unsigned avoid_hooked = 0;
    for (unsigned i = 0; i < 3; ++i) {
        void *current = NULL;
        if (!memory_read_pointer(game + follow_vtables[i] + SLOT_AVOID, &current) || !current) continue;
        if (current != game + 0xc47af0u) log_append_module("WALKING: follow camera avoid already hooked by ", current);
        if (chain(follow_vtables[i] + SLOT_AVOID, avoid_hooks[i], &previous_avoid[i])) ++avoid_hooked;
    }
    if (avoid_hooked != 3) log_append_value("WALKING: follow camera avoid hooked (of 3): ", (int)avoid_hooked);
    static void *const ui_hooks[6] = {(void *)(uintptr_t)ui0, (void *)(uintptr_t)ui1, (void *)(uintptr_t)ui2,
                                      (void *)(uintptr_t)ui3, (void *)(uintptr_t)ui4, (void *)(uintptr_t)ui5};
    unsigned ui_hooked = 0;
    for (unsigned i = 0; i < 6; ++i) {
        void *current = NULL;
        if (!memory_read_pointer(game + ui_slots[i], &current) || !current) continue;
        if (current != game + ui_functions[i]) log_append_module("WALKING: UI draw already hooked by ", current);
        if (chain(ui_slots[i], ui_hooks[i], &previous_ui[i])) ++ui_hooked;
    }
    if (ui_hooked != 6) log_append_value("WALKING: UI draws hooked (of 6, HUD may stay): ", (int)ui_hooked);
    ready = 1;
    log_append("WALKING: ready - key Walk (F) / LS + Y, offline only.\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestWalkHidden(float *out) {
    return walking_car_hidden(out);
}
/* tests/test_walking.py: one step without a game world (no collision) */
__declspec(dllexport) void Fh2TestWalkStep(unsigned char *game_image, int reset, unsigned dt_ms, float *out) {
    game = game_image;
    if (reset) {
        static const float p[3] = {10.0f, 5.0f, 20.0f}, r[3] = {1.0f, 0.0f, 0.0f}, f[3] = {0.0f, 0.0f, 1.0f};
        world_tried = 1, world = NULL;
        walking_enter(p, r, f);
        walking_set_active(1);
        InterlockedExchange(&step_from_enter, 0);
        spawn();
    }
    step((float)dt_ms / 1000.0f);
    out[0] = eye[0], out[1] = eye[1], out[2] = eye[2];
    out[3] = look[0], out[4] = look[1], out[5] = look[2];
    out[6] = walking_car_distance();
    out[7] = (float)walking_speed_kmh();
}

__declspec(dllexport) void Fh2TestWalkHandheld(int level) {
    walking_set_handheld(level);
}
__declspec(dllexport) void Fh2TestWalkMouse(int dx, int dy) {
    test_mouse[0] = (float)dx, test_mouse[1] = (float)dy;
}
#endif
