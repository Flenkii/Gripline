/* Walking - see walking.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "walking.h"
#include "keyboard_pad.h"

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
static int grounded, bob_on, no_floor, recovered_logged;
static float last_ground;
static float bob_phase;
static float car_place[3], car_right[3], car_forward[3];
static LARGE_INTEGER last_step;
static int stepped;

/* read on other threads */
static volatile LONG distance_cm = -100, speed_x10, collision_ok;
static float smooth_speed;
static float eye[3];

static _Alignas(16) unsigned char params_free[0x40], params_floor[0x40];
static void *world;
static int world_tried; /* tests: no lookup */

#define WALK_RADIUS 0.5f     /* the parameters' sphere; eye above its centre: */
#define WALK_EYE 1.12f       /* about 1.62 m above the ground */
#define WALK_SPEED 2.0f      /* m/s (7 km/h) */
#define WALK_RUN 6.0f        /* m/s (22 km/h) */
#define WALK_JUMP 4.2f       /* m/s up: about 0.9 m high */
#define WALK_GRAVITY 9.81f
#define WALK_LOOK 1.6f       /* rad/s */
#define WALK_PITCH_MAX 1.4f
#define WALK_MAX_DISTANCE 300.0f /* from the car: the game streams collision around the car (guess) */
#define WALK_FALL_LIMIT 150.0f   /* below the car: fell through the world, back to the car */
#define WALK_STEP 0.4f     /* steps and curbs up to 40 cm */
#define WALK_SNAP 0.3f     /* stays on the ground going down up to this per step */
#define WALK_HOVER 0.01f   /* the sphere rests this far above the floor */
#define WALK_RECOVER 1.5f  /* sunk into the floor: looked for again from this far above */
#define WALK_NO_FLOOR 4.0f /* metres of falling without any floor: no collision loaded there */

int walking_ready(void) {
    return ready;
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

int walking_no_floor(void) {
    return no_floor;
}

void walking_set_active(int on) {
    if (!on) InterlockedExchange(&entered, 0);
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
    out[0] = p[0], out[1] = p[1], out[2] = p[2];
    void *hit = NULL;
    memory_copy(&hit, result + 0x10, 8);
    return hit != NULL;
}

/* ------------------------------------------------------------------ the walker */

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
    if (in.yaw != 0.0f) turn(in.yaw * WALK_LOOK * dt);
    if (in.pitch != 0.0f) {
        pitch += in.pitch * WALK_LOOK * dt;
        if (pitch > WALK_PITCH_MAX) pitch = WALK_PITCH_MAX;
        if (pitch < -WALK_PITCH_MAX) pitch = -WALK_PITCH_MAX;
    }
    sin_cos(pitch, &pitch_sin, &pitch_cos);
    float speed = in.run ? WALK_RUN : WALK_SPEED;
    float rx = mirror * dir[1], rz = -mirror * dir[0];
    float mx = (dir[0] * in.ahead + rx * in.side) * speed * dt, mz = (dir[1] * in.ahead + rz * in.side) * speed * dt;
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
            float to[3] = {raised[0] + mx, raised[1], raised[2] + mz};
            slide(raised, to, 0, centre);
        }
    }
    /* gravity and jumping: stops on a floor */
    if (in.jump && grounded) fall = WALK_JUMP, grounded = 0, lifted = 0.0f;
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
            if (slide(top, to, 1, again) && again[1] > to[1]) {
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
            end[1] = last_ground, fall = 0.0f, grounded = 1; /* still no floor here: stay level */
        } else {
            grounded = 0;
            if (hit) fall = 0.0f; /* the head hit a ceiling */
        }
        centre[0] = end[0], centre[1] = end[1], centre[2] = end[2];
        if (!grounded && fall < 0.0f && centre[1] < last_ground - WALK_NO_FLOOR) {
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
    }
    float dx = centre[0] - before[0], dz = centre[2] - before[2];
    float moved = dt > 0.0f ? root(dx * dx + dz * dz) / dt : 0.0f;
    smooth_speed += (moved - smooth_speed) * (dt * 6.0f > 1.0f ? 1.0f : dt * 6.0f);
    InterlockedExchange(&speed_x10, (LONG)(smooth_speed * 36.0f + 0.5f));
    /* a little head bob while walking on the ground */
    if (grounded && moved > 0.3f) bob_phase += dt * (in.run ? 13.0f : 9.0f), bob_on = 1;
    else bob_on = 0;
    float bob = 0.0f;
    if (bob_on) {
        float t = bob_phase;
        while (t > 3.14159265f) t -= 6.2831853f;
        float s, c;
        sin_cos(t > 1.5707963f ? 3.14159265f - t : t < -1.5707963f ? -3.14159265f - t : t, &s, &c);
        bob = s * (in.run ? 0.05f : 0.03f);
    }
    eye[0] = centre[0], eye[1] = centre[1] + WALK_EYE + bob, eye[2] = centre[2];
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
    d[0] = pitch_cos * dir[0], d[1] = pitch_sin, d[2] = pitch_cos * dir[1];
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
    void *result = ((CameraUpdateFn)previous_update[i])(camera, dt, a, b);
    if (active) {
        walk_frame();
        put_eye(camera, cameras[i].family);
    }
    return result;
}

static void *camera_view(void *camera, void *out) {
    int i = camera_index(camera);
    if (i < 0) return out;
    if (active) put_eye(camera, cameras[i].family);
    return ((CameraViewFn)previous_view[i])(camera, out);
}

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
    out[3] = pitch_cos * dir[0], out[4] = pitch_sin, out[5] = pitch_cos * dir[1];
    out[6] = walking_car_distance();
    out[7] = (float)walking_speed_kmh();
}
#endif
