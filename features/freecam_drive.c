/* V7.2 freecam while driving - see freecam_drive.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "freecam_drive.h"
#include "keyboard_pad.h"
#include "support.h"

#define FREE_SPEED 8.0f      /* m/s */
#define FREE_FAST 4.0f       /* times this with Shift */
#define FREE_EASE 4.0f       /* 1/s: speeding up and slowing down */
#define FREE_LOOK 1.8f       /* rad/s at full stick / arrow */
#define FREE_LOOK_EASE 8.0f  /* 1/s */
#define FREE_MOUSE_RAD 0.0018f /* rad per mouse count at WalkMouseLook=100 (as walking) */
#define FREE_MOUSE_EASE 18.0f
#define FREE_PITCH_MAX 0.97f /* sine */
#define FREE_FIX_EASE 7.0f   /* 1/s: turning after the car when fixed */
#define FREE_CAR_AIM 0.6f    /* fixed: aims this far above the car's middle */
#define PAD_FRESH_MS 250u

#define VK_RBUTTON_ 0x02
#define VK_SHIFT_ 0x10
#define VK_SPACE_ 0x20
#define VK_PRIOR_ 0x21
#define VK_NEXT_ 0x22
#define VK_LEFT_ 0x25
#define VK_UP_ 0x26
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28
#define VK_LCONTROL_ 0xa2
#define VK_RSHIFT_ 0xa1

static volatile LONG on, fixed, fix_changed, restart, range = 500;

/* the own car (car thread -> camera thread) */
static volatile LONG car_seq, car_known;
static float car_p[3], car_r[3], car_f[3];

/* the controller (input thread -> camera thread) */
static volatile LONG pad_rx, pad_ry; /* x 10000 */
static volatile LONG64 pad_tick;
static volatile LONG pad_click;

/* the camera, only on the camera thread */
static float eye[3], heading[2] = {0.0f, 1.0f}, pitch_s, pitch_c = 1.0f, mirror = 1.0f;
static float vel[3], yaw_rate, pitch_rate, mouse_yaw, mouse_pitch;
static LARGE_INTEGER last;
static int right_held;

void freecam_drive_set(int value) {
    if (value) InterlockedExchange(&restart, 1);
    else InterlockedExchange(&fixed, 0), InterlockedExchange(&car_known, 0);
    InterlockedExchange(&on, value ? 1 : 0);
}

int freecam_drive_active(void) {
    return on != 0;
}

int freecam_drive_fixed(void) {
    return fixed != 0;
}

int freecam_drive_fix_changed(void) {
    return InterlockedExchange(&fix_changed, 0) != 0;
}

void freecam_drive_set_range(int metres) {
    if (metres >= 0 && metres <= 100000) InterlockedExchange(&range, metres);
}

int freecam_drive_range(void) {
    return (int)range;
}

int freecam_drive_arrows(void) {
    return keyboard_drives();
}

void freecam_drive_car(const float position[3], const float right[3], const float forward[3]) {
    InterlockedIncrement(&car_seq); /* odd: being written */
    for (unsigned i = 0; i < 3; ++i) car_p[i] = position[i], car_r[i] = right[i], car_f[i] = forward[i];
    InterlockedIncrement(&car_seq);
    InterlockedExchange(&car_known, 1);
}

static int read_car(float p[3], float r[3], float f[3]) {
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        LONG before = car_seq;
        if (before & 1) continue;
        for (unsigned i = 0; i < 3; ++i) p[i] = car_p[i], r[i] = car_r[i], f[i] = car_f[i];
        if (car_seq == before) return car_known != 0;
    }
    return 0;
}

void freecam_drive_pad(float rx, float ry, int click) {
    InterlockedExchange(&pad_rx, (LONG)(rx * 10000.0f));
    InterlockedExchange(&pad_ry, (LONG)(ry * 10000.0f));
    InterlockedExchange64(&pad_tick, (LONG64)GetTickCount64());
    if (click) InterlockedExchange(&pad_click, 1);
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x > 0.0f ? x : 0.0f)));
}

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

static float ease(float dt, float rate) { /* 1 - e^(-rate dt), close enough */
    float x = rate * dt;
    return x / (1.0f + x + 0.5f * x * x) * (1.0f + 0.5f * x);
}

static void sin_cos(float a, float *s, float *c) { /* small angles (one frame) */
    a = clampf(a, -0.6f, 0.6f);
    float a2 = a * a;
    *s = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f));
    *c = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f);
}

static int key(unsigned char vk) {
    return keyboard_key_down(vk) ? 1 : 0;
}

/* positive a turns left (as walking.c's turn) */
static void turn(float a) {
    float s, c;
    sin_cos(a, &s, &c);
    float left_x = -mirror * heading[1], left_z = mirror * heading[0];
    float x = heading[0] * c + left_x * s, z = heading[1] * c + left_z * s;
    float length = root(x * x + z * z);
    if (length > 0.5f) heading[0] = x / length, heading[1] = z / length;
}

static void tilt(float a) { /* positive looks up */
    float s, c;
    sin_cos(a, &s, &c);
    float ps = pitch_s * c + pitch_c * s;
    pitch_s = clampf(ps, -FREE_PITCH_MAX, FREE_PITCH_MAX);
    pitch_c = root(1.0f - pitch_s * pitch_s);
}

static void look_at(const float d[3], float k) { /* eases the direction towards d */
    float h = root(d[0] * d[0] + d[2] * d[2]), n = root(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (h < 0.01f || n < 0.01f) return;
    heading[0] += (d[0] / h - heading[0]) * k, heading[1] += (d[2] / h - heading[1]) * k;
    float length = root(heading[0] * heading[0] + heading[1] * heading[1]);
    if (length > 0.01f) heading[0] /= length, heading[1] /= length;
    pitch_s += (clampf(d[1] / n, -FREE_PITCH_MAX, FREE_PITCH_MAX) - pitch_s) * k;
    pitch_c = root(1.0f - pitch_s * pitch_s);
}

static void begin(const float game_eye[3], const float game_look[3]) {
    for (unsigned i = 0; i < 3; ++i) eye[i] = game_eye[i], vel[i] = 0.0f;
    float h = root(game_look[0] * game_look[0] + game_look[2] * game_look[2]);
    if (h > 0.05f) heading[0] = game_look[0] / h, heading[1] = game_look[2] / h;
    pitch_s = clampf(game_look[1], -FREE_PITCH_MAX, FREE_PITCH_MAX);
    pitch_c = root(1.0f - pitch_s * pitch_s);
    yaw_rate = pitch_rate = mouse_yaw = mouse_pitch = 0.0f;
    right_held = key(VK_RBUTTON_);
    InterlockedExchange(&pad_click, 0);
    float p[3], r[3], f[3];
    mirror = 1.0f;
    if (read_car(p, r, f)) /* the game's right row = mirror * (z, 0, -x) of the heading (as walking.c) */
        mirror = r[0] * f[2] - r[2] * f[0] >= 0.0f ? 1.0f : -1.0f;
    float m0, m1;
    keyboard_walk_mouse(&m0, &m1); /* what the mouse did before does not turn the camera */
    last.QuadPart = 0;
}

int freecam_drive_frame(const float game_eye[3], const float game_look[3], float out_eye[3], float out_look[3]) {
    if (!on) return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!float_is_finite(game_eye[i]) || !float_is_finite(game_look[i])) return 0;
    if (InterlockedExchange(&restart, 0)) begin(game_eye, game_look);
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)frequency.QuadPart) : 0.0f;
    last = now;
    if (!(dt > 0.0f) || dt > 0.1f) dt = dt > 0.1f ? 0.1f : 0.0f;
    if (!game_simulation_running()) dt = 0.0f; /* paused: the camera stays */

    /* fix: right mouse button / right stick click */
    int right_now = key(VK_RBUTTON_);
    int click = (right_now && !right_held) || InterlockedExchange(&pad_click, 0);
    right_held = right_now;
    if (click && dt > 0.0f) InterlockedExchange(&fixed, !fixed), InterlockedExchange(&fix_changed, 1);

    /* moving */
    float ahead, side, up;
    int fast, arrows = freecam_drive_arrows();
    if (arrows) {
        ahead = (float)(key(VK_UP_) - key(VK_DOWN_)), side = (float)(key(VK_RIGHT_) - key(VK_LEFT_));
        up = (float)(key(VK_PRIOR_) - key(VK_NEXT_)), fast = key(VK_RSHIFT_);
    } else {
        ahead = (float)(key('W') - key('S')), side = (float)(key('D') - key('A'));
        up = (float)((key(VK_SPACE_) | key('E')) - (key(VK_LCONTROL_) | key('Q'))), fast = key(VK_SHIFT_);
    }
    float look_x = 0.0f, look_y = 0.0f;
    if (!arrows) look_x = (float)(key(VK_RIGHT_) - key(VK_LEFT_)), look_y = (float)(key(VK_UP_) - key(VK_DOWN_));
    if (GetTickCount64() - (ULONGLONG)pad_tick < PAD_FRESH_MS) {
        float rx = (float)pad_rx / 10000.0f, ry = (float)pad_ry / 10000.0f;
        if (rx * rx > look_x * look_x) look_x = rx;
        if (ry * ry > look_y * look_y) look_y = ry;
    }
    if (dt == 0.0f) ahead = side = up = look_x = look_y = 0.0f;

    /* looking: stick / arrows at an eased rate, the mouse handed out over a few frames */
    float mx = 0.0f, my = 0.0f;
    if (!keyboard_mouse_steering()) keyboard_walk_mouse(&mx, &my);
    if (dt == 0.0f) mx = my = 0.0f;
    yaw_rate += (look_x * FREE_LOOK - yaw_rate) * ease(dt, FREE_LOOK_EASE);
    pitch_rate += (look_y * FREE_LOOK - pitch_rate) * ease(dt, FREE_LOOK_EASE);
    mouse_yaw = clampf(mouse_yaw - mx * FREE_MOUSE_RAD, -1.5f, 1.5f);
    mouse_pitch = clampf(mouse_pitch - my * FREE_MOUSE_RAD, -1.5f, 1.5f);
    float km = ease(dt, FREE_MOUSE_EASE), myaw = mouse_yaw * km, mpitch = mouse_pitch * km;
    mouse_yaw -= myaw, mouse_pitch -= mpitch;
    turn(-yaw_rate * dt + myaw);
    tilt(pitch_rate * dt + mpitch);

    float forward[3] = {heading[0] * pitch_c, pitch_s, heading[1] * pitch_c};
    float right[3] = {mirror * heading[1], 0.0f, -mirror * heading[0]};
    float length = root(ahead * ahead + side * side + up * up);
    if (length > 1.0f) ahead /= length, side /= length, up /= length;
    float speed = FREE_SPEED * (fast ? FREE_FAST : 1.0f), kv = ease(dt, FREE_EASE);
    for (unsigned i = 0; i < 3; ++i) {
        float want = (forward[i] * ahead + right[i] * side + (i == 1 ? up : 0.0f)) * speed;
        vel[i] += (want - vel[i]) * kv;
        eye[i] += vel[i] * dt;
    }

    /* the car: at most range metres away; fixed: the camera turns after it */
    float p[3], r[3], f[3];
    if (read_car(p, r, f)) {
        float d[3] = {eye[0] - p[0], eye[1] - p[1], eye[2] - p[2]};
        float distance = root(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float limit = (float)range;
        if (limit > 0.0f && distance > limit)
            for (unsigned i = 0; i < 3; ++i) eye[i] = p[i] + d[i] / distance * limit, vel[i] = 0.0f;
        if (fixed && dt > 0.0f) {
            float to[3] = {p[0] - eye[0], p[1] + FREE_CAR_AIM - eye[1], p[2] - eye[2]};
            look_at(to, ease(dt, FREE_FIX_EASE));
        }
    }
    forward[0] = heading[0] * pitch_c, forward[1] = pitch_s, forward[2] = heading[1] * pitch_c;
    for (unsigned i = 0; i < 3; ++i) out_eye[i] = eye[i], out_look[i] = forward[i];
    return 1;
}
