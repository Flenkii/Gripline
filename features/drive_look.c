/* Mouse look in the car (V7.1) - see drive_look.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "drive_look.h"
#include "keyboard_pad.h"
#include "noclip.h"
#include "walking.h"

#define LOOK_RAD_PER_COUNT 0.0018f /* at DriveMouseLook=100, like walking */
#define LOOK_EASE 0.035f           /* the camera follows the mouse a little behind (s) */
#define LOOK_RETURN_EASE 0.45f     /* back to the game's camera (s) */
#define DRIVE_LOOK_RETURN_MS 2000u /* mouse left alone this long while driving: back */
#define LOOK_RETURN_SPEED 2.0f     /* m/s: slower counts as standing (the camera stays) */
#define LOOK_YAW_MAX 3.1f          /* about all the way round to looking back */
#define LOOK_PITCH_INSIDE 1.2f
#define LOOK_PITCH_UP_OUTSIDE 0.3f   /* outside the camera goes down when looking up: not into the road */
#define LOOK_PITCH_DOWN_OUTSIDE 0.8f
#define CAR_FRESH_MS 500u
#define PIVOT_DEFAULT 6.0f         /* m in front of a follow camera when the car is not known */

/* car thread */
static volatile LONG car_lock;
static float car_pos[3], car_mirror = 1.0f;
static volatile float car_speed;
static volatile LONG64 car_tick;

/* camera thread */
static float target_yaw, target_pitch, yaw, pitch;
static ULONGLONG moved_at;
static LARGE_INTEGER last;
static int logged;
#define SAVED_CAMERAS 8
static struct {
    void *camera;
    unsigned char eye[24], dir[12];
} saved[SAVED_CAMERAS];
static int pivot_logged;

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x > 0.0f ? x : 0.0f)));
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static float ease(float dt, float tau) {
    if (!(dt > 0.0f)) return 0.0f;
    float x = dt / tau;
    if (x > 8.0f) return 1.0f;
    return 1.0f - 1.0f / (1.0f + x + x * x / 2.0f + x * x * x / 6.0f + x * x * x * x / 24.0f);
}

/* sine and cosine of any angle: into -pi/2..pi/2, then a series */
static void sincos_any(float a, float *s, float *c) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    float sign = 1.0f;
    if (a > 1.5707963f) a = 3.14159265f - a, sign = -1.0f;
    else if (a < -1.5707963f) a = -3.14159265f - a, sign = -1.0f;
    float a2 = a * a;
    *s = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
    *c = sign * (1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f))));
}

/* v turned by angle around the unit axis k (right-hand rule in the game's axes) */
static void rotate(float v[3], const float k[3], float angle) {
    float s, c;
    sincos_any(angle, &s, &c);
    float cross[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
    float dot = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    for (unsigned i = 0; i < 3; ++i) v[i] = v[i] * c + cross[i] * s + k[i] * dot * (1.0f - c);
}

void drive_look_car(const float position[3], float speed) {
    if (InterlockedCompareExchange(&car_lock, 1, 0)) return;
    car_pos[0] = position[0], car_pos[1] = position[1], car_pos[2] = position[2];
    car_speed = speed;
    car_tick = (LONG64)GetTickCount64();
    InterlockedExchange(&car_lock, 0);
}

void drive_look_mirror(const float right[3], const float forward[3]) {
    /* the game's right row against the heading: which way round "turn right" is */
    car_mirror = right[0] * forward[2] - right[2] * forward[0] >= 0.0f ? 1.0f : -1.0f;
}

static int car_place(float out[3]) {
    if (InterlockedCompareExchange(&car_lock, 1, 0)) return 0;
    int fresh = car_tick && GetTickCount64() - (ULONGLONG)car_tick < CAR_FRESH_MS;
    out[0] = car_pos[0], out[1] = car_pos[1], out[2] = car_pos[2];
    InterlockedExchange(&car_lock, 0);
    return fresh;
}

void drive_look_frame(void) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)frequency.QuadPart) : 0.0f;
    if (last.QuadPart && dt < 0.002f) return; /* several cameras in one frame */
    last = now;
    if (!(dt > 0.0f) || dt > 0.1f) dt = 1.0f / 60.0f;
    if (walking_active() || noclip_active()) { /* they have the camera: start straight afterwards */
        target_yaw = target_pitch = yaw = pitch = 0.0f;
        return;
    }
    if (!game_simulation_running()) return; /* paused: everything stays */
    ULONGLONG tick = GetTickCount64();
    float mx, my;
    if (keyboard_drive_mouse(&mx, &my) && (mx != 0.0f || my != 0.0f)) {
        target_yaw = clampf(target_yaw + mx * LOOK_RAD_PER_COUNT, -LOOK_YAW_MAX, LOOK_YAW_MAX);
        target_pitch = clampf(target_pitch - my * LOOK_RAD_PER_COUNT, -LOOK_PITCH_INSIDE, LOOK_PITCH_INSIDE);
        moved_at = tick;
        if (!logged) {
            logged = 1;
            log_append("DRIVELOOK: the mouse turns the car camera (FH2_Keyboard.ini DriveMouseLook).\r\n");
        }
    } else if (tick - moved_at >= DRIVE_LOOK_RETURN_MS && car_speed > LOOK_RETURN_SPEED) {
        float k = ease(dt, LOOK_RETURN_EASE); /* driving: back to the game's camera */
        target_yaw -= target_yaw * k, target_pitch -= target_pitch * k;
    }
    if (keyboard_mouse_steering()) target_yaw = target_pitch = 0.0f; /* the mouse steers: no look */
    float k = ease(dt, LOOK_EASE);
    yaw += (target_yaw - yaw) * k, pitch += (target_pitch - pitch) * k;
    if (target_yaw == 0.0f && yaw > -0.0005f && yaw < 0.0005f) yaw = 0.0f;
    if (target_pitch == 0.0f && pitch > -0.0005f && pitch < 0.0005f) pitch = 0.0f;
}

int drive_look_apply(void *camera, unsigned index, unsigned family, unsigned eye_offset, unsigned dir_offset) {
    if (index >= SAVED_CAMERAS) return 0;
    saved[index].camera = NULL;
    if (yaw == 0.0f && pitch == 0.0f) return 0;
    if (walking_active() || noclip_active()) return 0;
    unsigned char *c = (unsigned char *)camera;
    double e[3];
    float d[4];
    if (!memory_read(c + eye_offset, e, sizeof(e)) || !memory_read(c + dir_offset, d, sizeof(d))) return 0;
    float length = root(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(length > 1e-4f) || !(length < 1e6f)) return 0;
    float v[3] = {d[0] / length, d[1] / length, d[2] / length};
    float h = root(v[0] * v[0] + v[2] * v[2]);
    if (h < 0.05f) return 0; /* straight up / down: no heading to turn from */
    memory_copy(saved[index].eye, e, sizeof(saved[index].eye));
    memory_copy(saved[index].dir, d, sizeof(saved[index].dir));
    float p = family ? clampf(pitch, -LOOK_PITCH_DOWN_OUTSIDE, LOOK_PITCH_UP_OUTSIDE) : pitch;
    float up[3] = {0.0f, car_mirror, 0.0f};
    float right[3] = {car_mirror * v[2] / h, 0.0f, -car_mirror * v[0] / h};
    float down_axis[3] = {-right[0], 0.0f, -right[2]}; /* turning around minus right lifts the look */
    if (family == 0) {
        /* inside: the look turns, the eye stays */
        rotate(v, down_axis, p);
        rotate(v, up, yaw);
    } else {
        /* outside: the camera circles around the car (the point of the look nearest to it) */
        float car[3], t = PIVOT_DEFAULT;
        int known = car_place(car);
        if (known) {
            float to[3] = {car[0] - (float)e[0], car[1] - (float)e[1], car[2] - (float)e[2]};
            float along = to[0] * v[0] + to[1] * v[1] + to[2] * v[2];
            if (along > 1.0f && along < 40.0f) t = along;
        }
        if (pivot_logged < 3) {
            ++pivot_logged;
            log_append_value(known ? "DRIVELOOK: outside camera circles the car, cm in front: "
                                   : "DRIVELOOK: outside camera, car not known - cm in front: ", (int)(t * 100.0f));
        }
        float pivot[3] = {(float)e[0] + v[0] * t, (float)e[1] + v[1] * t, (float)e[2] + v[2] * t};
        float back[3] = {-v[0] * t, -v[1] * t, -v[2] * t};
        rotate(back, down_axis, p);
        rotate(back, up, yaw);
        rotate(v, down_axis, p);
        rotate(v, up, yaw);
        e[0] = pivot[0] + back[0], e[1] = pivot[1] + back[1], e[2] = pivot[2] + back[2];
    }
    d[0] = v[0] * length, d[1] = v[1] * length, d[2] = v[2] * length;
    memory_copy(c + eye_offset, e, sizeof(e));
    memory_copy(c + dir_offset, d, 12);
    saved[index].camera = camera;
    return 1;
}

void drive_look_restore(void *camera, unsigned index, unsigned eye_offset, unsigned dir_offset) {
    if (index >= SAVED_CAMERAS || saved[index].camera != camera) return;
    unsigned char *c = (unsigned char *)camera;
    memory_copy(c + eye_offset, saved[index].eye, sizeof(saved[index].eye));
    memory_copy(c + dir_offset, saved[index].dir, 12);
    saved[index].camera = NULL;
}

#ifdef FH2_TWEAKS_TEST
/* tests: the offsets at once (no easing) */
__declspec(dllexport) void Fh2TestDriveLookSet(int yaw_mrad, int pitch_mrad, int mirror) {
    target_yaw = yaw = (float)yaw_mrad / 1000.0f;
    target_pitch = pitch = (float)pitch_mrad / 1000.0f;
    car_mirror = (float)mirror;
}
__declspec(dllexport) int Fh2TestDriveLookApply(void *camera, unsigned family) {
    return drive_look_apply(camera, family, family, family ? 0x190u : 0x1a0u, family ? 0x1b0u : 0x1c0u);
}
__declspec(dllexport) void Fh2TestDriveLookCar(const float *position) {
    drive_look_car(position, 0.0f);
}
#endif
