/* V7.2 turn signal display - see blinker.h.

   Reads only: the game's light inputs of the player's car. The game's material code (game+0xF5C900)
   turns them into its light functions: hazards = +0x51bb, left = +0x51bc without +0x51bd, right =
   +0x51bd without +0x51bc (both without hazards = nothing lights). Its light controllers (game+0xF5C7C0)
   start their timeline at 0 when such an input comes on and loop it (DefaultLightControllers.xml:
   IndicatorOnTime 0.36 s, IndicatorOffTime 0.33 s), so a timer that starts at the same change and runs
   with the car update's dt stays in step with the car's lights.
   The arrows are drawn here pixel by pixel (distance functions, smooth edges), the clicks are made
   here (no sound file) and played through Windows' own winmm (taken by name from the system folder, as
   the door sounds: no new imports). */
#include <immintrin.h>
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "blinker.h"
#include "walking.h"
#include "mod_partners.h"

#define LIGHT_INPUT_HAZARD 0x51bbu /* ForzaCar: hazards, left, right (three bytes) */
#define ON_TIME 0.36f
#define PERIOD (0.36f + 0.33f)

static volatile LONG arrows_on = 1, sound_on = 1, volume = 60;
static volatile LONG shown_mode, shown_lit;
static volatile LONG64 updated_at; /* GetTickCount64 of the last update in which the game ran */
static volatile LONG sound_pending; /* 1 tick, 2 tock */
static volatile LONG volume_changed = 1;
static volatile LONG sound_thread_started;

void blinker_set_arrows(int on) {
    InterlockedExchange(&arrows_on, on ? 1 : 0);
}
int blinker_arrows(void) {
    return (int)arrows_on;
}
void blinker_set_sound(int on) {
    InterlockedExchange(&sound_on, on ? 1 : 0);
}
int blinker_sound(void) {
    return (int)sound_on;
}
void blinker_set_volume(int percent) {
    percent = percent < 20 ? 20 : percent > 100 ? 100 : percent;
    InterlockedExchange(&volume, percent);
    InterlockedExchange(&volume_changed, 1);
}
int blinker_volume(void) {
    return (int)volume;
}

/* ------------------------------------------------------------------ sound */

typedef struct {
    unsigned short format, channels;
    unsigned long rate, bytes_per_second;
    unsigned short block, bits, extra;
} WaveFormat;
typedef struct WaveHeader {
    char *data;
    unsigned long length, recorded;
    ULONG_PTR user;
    unsigned long flags, loops;
    struct WaveHeader *next;
    ULONG_PTR reserved;
} WaveHeader;
typedef unsigned(WINAPI *WaveOpenFn)(void **, unsigned, const WaveFormat *, ULONG_PTR, ULONG_PTR, unsigned long);
typedef unsigned(WINAPI *WaveHeaderFn)(void *, WaveHeader *, unsigned);
typedef unsigned(WINAPI *WaveCloseFn)(void *);
typedef unsigned(WINAPI *GetSystemDirectoryWFn)(wchar_t *, unsigned);

#define RATE 44100u
#define CLICK_SAMPLES 2800u /* 63 ms */
#define WHDR_DONE_ 0x01u
#define WHDR_PREPARED_ 0x02u
#define WHDR_INQUEUE_ 0x10u

static struct {
    WaveOpenFn open;
    WaveHeaderFn prepare, unprepare, write;
    WaveCloseFn close, reset;
} wave;
static void *device;
static short click[2][CLICK_SAMPLES];
static WaveHeader header[2];

static int wave_ready(void) {
    static int tried, ok;
    if (tried) return ok;
    tried = 1;
    /* the full path: Extra Options is itself a winmm.dll in the game folder */
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    GetSystemDirectoryWFn system_dir = kernel ? (GetSystemDirectoryWFn)(uintptr_t)GetProcAddress(kernel, "GetSystemDirectoryW") : NULL;
    wchar_t path[300];
    unsigned n = system_dir ? system_dir(path, 280) : 0;
    HMODULE winmm = NULL;
    if (n && n < 280) {
        static const wchar_t name[] = L"\\winmm.dll";
        for (unsigned i = 0; i < sizeof(name) / sizeof(name[0]); ++i) path[n + i] = name[i];
        winmm = LoadLibraryExW(path, NULL, 0);
    }
    if (winmm) {
        wave.open = (WaveOpenFn)(uintptr_t)GetProcAddress(winmm, "waveOutOpen");
        wave.prepare = (WaveHeaderFn)(uintptr_t)GetProcAddress(winmm, "waveOutPrepareHeader");
        wave.unprepare = (WaveHeaderFn)(uintptr_t)GetProcAddress(winmm, "waveOutUnprepareHeader");
        wave.write = (WaveHeaderFn)(uintptr_t)GetProcAddress(winmm, "waveOutWrite");
        wave.close = (WaveCloseFn)(uintptr_t)GetProcAddress(winmm, "waveOutClose");
        wave.reset = (WaveCloseFn)(uintptr_t)GetProcAddress(winmm, "waveOutReset");
    }
    ok = wave.open && wave.prepare && wave.unprepare && wave.write && wave.close && wave.reset;
    log_append(ok ? "BLINKER: sound ready.\r\n" : "BLINKER: winmm waveOut not found - no turn signal sound.\r\n");
    return ok;
}

/* small-angle sine / cosine and e^-x (x small) without the C library */
static void sin_cos_small(float a, float *s, float *c) {
    float a2 = a * a;
    *s = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
    *c = 1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f)));
}
static float exp_small(float x) { /* e^-x for 0 <= x < 0.1 */
    return 1.0f - x * (1.0f - x / 2.0f * (1.0f - x / 3.0f * (1.0f - x / 4.0f)));
}

/* A relay click: a few short ringing partials, a little noise and a soft knock. tock = lower and a
   bit longer (the relay letting go). Scaled to `percent` of a quiet peak. */
static void make_click(short *out, int tock, int percent) {
    static const float tick_parts[5][3] = {{3200.0f, 0.55f, 0.0035f}, {1900.0f, 0.45f, 0.006f}, {5200.0f, 0.25f, 0.002f},
                                           {260.0f, 0.30f, 0.010f}, {0.0f, 0.35f, 0.0012f}};
    static const float tock_parts[5][3] = {{2300.0f, 0.50f, 0.0045f}, {1300.0f, 0.45f, 0.008f}, {3900.0f, 0.20f, 0.0025f},
                                           {190.0f, 0.35f, 0.014f}, {0.0f, 0.30f, 0.0015f}};
    const float(*parts)[3] = tock ? tock_parts : tick_parts;
    float re[5], im[5], rs[5], rc[5], env[5], fall[5];
    for (int p = 0; p < 5; ++p) {
        sin_cos_small(6.2831853f * parts[p][0] / (float)RATE, &rs[p], &rc[p]);
        re[p] = 0.0f, im[p] = 1.0f; /* starts at its peak: a click, not a fade-in */
        env[p] = parts[p][1];
        fall[p] = exp_small(1.0f / (parts[p][2] * (float)RATE));
    }
    static float buffer[CLICK_SAMPLES];
    unsigned noise = tock ? 0x9E3779B9u : 0x7F4A7C15u;
    float peak = 1e-6f;
    for (unsigned i = 0; i < CLICK_SAMPLES; ++i) {
        float v = 0.0f;
        for (int p = 0; p < 4; ++p) {
            v += im[p] * env[p];
            float r = re[p] * rc[p] - im[p] * rs[p], m = re[p] * rs[p] + im[p] * rc[p];
            re[p] = r, im[p] = m;
            env[p] *= fall[p];
        }
        noise = noise * 1664525u + 1013904223u;
        v += ((float)(noise >> 9) / 4194304.0f - 1.0f) * env[4];
        env[4] *= fall[4];
        if (i < 12) v *= (float)(i + 1) / 13.0f; /* no hard step at the start */
        if (i > CLICK_SAMPLES - 200) v *= (float)(CLICK_SAMPLES - i) / 200.0f;
        buffer[i] = v;
        float a = v < 0.0f ? -v : v;
        if (a > peak) peak = a;
    }
    float scale = 32767.0f * 0.45f * (float)percent / 100.0f * (tock ? 0.85f : 1.0f) / peak;
    for (unsigned i = 0; i < CLICK_SAMPLES; ++i) out[i] = (short)(buffer[i] * scale);
}

static void wave_close(void) {
    if (!device) return;
    wave.reset(device);
    for (int k = 0; k < 2; ++k)
        if (header[k].flags & WHDR_PREPARED_) wave.unprepare(device, &header[k], sizeof(header[k]));
    wave.close(device);
    device = NULL;
}

static int wave_open(void) {
    if (device) return 1;
    WaveFormat f = {1, 1, RATE, RATE * 2u, 2, 16, 0};
    void *d = NULL;
    if (wave.open(&d, 0xFFFFFFFFu /* WAVE_MAPPER */, &f, 0, 0, 0 /* CALLBACK_NULL */) || !d) return 0;
    device = d;
    for (int k = 0; k < 2; ++k) {
        memory_copy(&header[k], &(WaveHeader){0}, sizeof(header[k]));
        header[k].data = (char *)click[k];
        header[k].length = sizeof(click[k]);
        if (wave.prepare(device, &header[k], sizeof(header[k]))) {
            wave_close();
            return 0;
        }
    }
    return 1;
}

static void play(int which) {
    static int failed_logged;
    if (InterlockedExchange(&volume_changed, 0)) { /* new clicks: only while none plays */
        wave_close();
        make_click(click[0], 0, (int)volume);
        make_click(click[1], 1, (int)volume);
    }
    if (!wave_open()) {
        if (!failed_logged) failed_logged = 1, log_append("BLINKER: could not open the sound device.\r\n");
        return;
    }
    WaveHeader *h = &header[which];
    if (h->flags & WHDR_INQUEUE_) return; /* still playing (cannot happen at 0.33 s) */
    h->flags &= ~(unsigned long)WHDR_DONE_;
    if (wave.write(device, h, sizeof(*h))) { /* device gone (headset unplugged ...): open again next time */
        if (!failed_logged) failed_logged = 1, log_append("BLINKER: sound device lost, opening it again.\r\n");
        wave_close();
    }
}

static DWORD WINAPI sound_thread(void *unused) {
    (void)unused;
    if (!wave_ready()) return 0;
    for (;;) {
        LONG s = InterlockedExchange(&sound_pending, 0);
        if (s && sound_on) play(s == 2 ? 1 : 0);
        Sleep(shown_mode ? 4 : 40);
    }
}

static void request_sound(int tock) {
    if (!sound_on || mod_partners_takes_signal_sound()) return; /* Extra Options clicks itself */
    if (!InterlockedExchange(&sound_thread_started, 1)) {
        HANDLE t = CreateThread(NULL, 0, sound_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
        else {
            log_append("BLINKER: could not start the sound thread.\r\n");
            return;
        }
    }
    InterlockedExchange(&sound_pending, tock ? 2 : 1);
}

void blinker_preview(void) {
    InterlockedExchange(&volume_changed, 1);
    request_sound(0);
}

/* ------------------------------------------------------------------ the car */

static int last_mode, last_lit;
static float timeline;

void blinker_car_update(void *car, float dt) {
    int mode = BLINKER_NONE;
    if (arrows_on || sound_on) {
        unsigned char in[3];
        if (car && memory_read((unsigned char *)car + LIGHT_INPUT_HAZARD, in, sizeof(in))) {
            mode = in[0] ? BLINKER_HAZARD : in[1] && !in[2] ? BLINKER_LEFT : in[2] && !in[1] ? BLINKER_RIGHT : BLINKER_NONE;
        }
        if (walking_active()) mode = BLINKER_NONE; /* on foot: the car stands, no dashboard */
    }
    if (mode != last_mode) {
        if (mode && !last_mode) log_append(mode == BLINKER_HAZARD ? "BLINKER: hazards on.\r\n"
                                           : mode == BLINKER_LEFT  ? "BLINKER: left on.\r\n"
                                                                   : "BLINKER: right on.\r\n");
        else if (!mode) log_append("BLINKER: off.\r\n");
        timeline = 0.0f; /* the game starts its light timeline again at this change */
        last_mode = mode;
    }
    if (dt > 0.0f && dt < 0.5f) {
        InterlockedExchange64(&updated_at, (LONG64)GetTickCount64());
        if (mode) {
            timeline += dt;
            while (timeline >= PERIOD) timeline -= PERIOD;
        }
    }
    int lit = mode && timeline < ON_TIME;
    if (lit != last_lit) {
        request_sound(!lit); /* tick when it lights, tock when it goes out */
        last_lit = lit;
    }
    InterlockedExchange(&shown_mode, mode);
    InterlockedExchange(&shown_lit, lit);
}

/* ------------------------------------------------------------------ the arrows */

int blinker_display(int *mode, int *lit, float *alpha) {
    static float a;
    static int last_shown_mode;
    static ULONGLONG tick;
    ULONGLONG now = GetTickCount64();
    float dt = tick && now > tick ? (float)(now - tick) / 1000.0f : 0.0f;
    tick = now;
    int running = now - (ULONGLONG)updated_at < 300; /* the game stands (pause, loading): nothing */
    int m = arrows_on && running && !mod_partners_takes_signal_arrows() ? (int)shown_mode : BLINKER_NONE;
    if (m) last_shown_mode = m;
    a += m ? dt / 0.12f : -dt / 0.25f;
    a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
    *mode = m ? m : last_shown_mode;
    *lit = m && shown_lit;
    *alpha = a;
    return a > 0.0f && *mode;
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x > 0.0f ? x : 0.0f)));
}

static float segment_distance(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, wx = px - ax, wy = py - ay;
    float t = (wx * dx + wy * dy) / (dx * dx + dy * dy);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    wx -= dx * t, wy -= dy * t;
    return root(wx * wx + wy * wy);
}

/* signed distance (in units of the arrow's size) to an arrow pointing to `dir` (-1 left, 1 right):
   a head (triangle) and a shaft (box) */
static float arrow_distance(float x, float y, float dir) {
    x *= -dir; /* drawn pointing left */
    static const float tri[3][2] = {{-1.0f, 0.0f}, {-0.12f, -0.80f}, {-0.12f, 0.80f}};
    float d = 1e9f;
    int positive = 0, negative = 0;
    for (int i = 0; i < 3; ++i) {
        const float *a = tri[i], *b = tri[(i + 1) % 3];
        float e = segment_distance(x, y, a[0], a[1], b[0], b[1]);
        if (e < d) d = e;
        float cross = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
        if (cross > 0.0f) positive = 1;
        if (cross < 0.0f) negative = 1;
    }
    int inside = !(positive && negative);
    float tri_d = inside ? -d : d;
    /* box from x -0.16 .. 0.95, y -0.30 .. 0.30, rounded a little */
    float qx = (x < -0.16f ? -0.16f - x : x > 0.95f ? x - 0.95f : -((x + 0.16f) < (0.95f - x) ? (x + 0.16f) : (0.95f - x)));
    float qy = (y < 0.0f ? -y : y) - 0.30f;
    float box_d;
    if (qx > 0.0f || qy > 0.0f) {
        float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
        box_d = root(ox * ox + oy * oy);
    } else {
        box_d = qx > qy ? qx : qy;
    }
    return tri_d < box_d ? tri_d : box_d;
}

static void over(unsigned char *p, float r, float g, float b, float a) {
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    float k = 1.0f - a;
    p[0] = (unsigned char)(b * a + p[0] * k + 0.5f);
    p[1] = (unsigned char)(g * a + p[1] * k + 0.5f);
    p[2] = (unsigned char)(r * a + p[2] * k + 0.5f);
    p[3] = (unsigned char)(255.0f * a + p[3] * k + 0.5f);
}

/* place in 1080p units, measured on the game's speedometer (bottom right, its centre 241 px from the
   right edge, the top of its ring 304 px from the bottom): the arrows sit just above the ring */
#define CENTRE_FROM_RIGHT 0.2234f
#define ABOVE_BOTTOM 0.300f
#define APART 0.068f
#define SIZE 0.0215f

int blinker_width(int screen_h) {
    return (int)((float)screen_h * (CENTRE_FROM_RIGHT + APART + SIZE * 2.5f)) + 2;
}

static void draw_one(unsigned char *frame, int frame_w, int frame_h, float cx, float cy, float s, float dir, int lit, float alpha) {
    float reach = s * 1.9f;
    int x0 = (int)(cx - reach), x1 = (int)(cx + reach) + 1, y0 = (int)(cy - reach), y1 = (int)(cy + reach) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > frame_w) x1 = frame_w;
    if (y1 > frame_h) y1 = frame_h;
    float px_unit = 1.0f / s; /* one pixel in arrow units */
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float d = arrow_distance(((float)x + 0.5f - cx) / s, ((float)y + 0.5f - cy) / s, dir) / px_unit; /* in pixels */
            unsigned char *p = frame + ((size_t)y * (size_t)frame_w + (size_t)x) * 4;
            float cover = 0.5f - d;
            cover = cover < 0.0f ? 0.0f : cover > 1.0f ? 1.0f : cover;
            if (lit) {
                /* green glow around it, a dark rim for bright skies, then the green arrow */
                float g = d > 0.0f ? 1.0f - d / (s * 0.75f) : 1.0f;
                if (g > 0.0f) over(p, 40.0f, 230.0f, 90.0f, g * g * 0.45f * alpha);
                float rim = 0.5f - (d - 1.6f);
                rim = rim < 0.0f ? 0.0f : rim > 1.0f ? 1.0f : rim;
                over(p, 0.0f, 40.0f, 10.0f, (rim - cover) * 0.55f * alpha);
                over(p, 70.0f, 235.0f, 100.0f, cover * alpha);
                float hi = 0.5f - (d + s * 0.22f); /* lighter middle */
                hi = hi < 0.0f ? 0.0f : hi > 1.0f ? 1.0f : hi;
                over(p, 190.0f, 255.0f, 200.0f, hi * 0.35f * alpha);
            } else {
                /* off: dark glass with a thin light edge, like the game's HUD */
                over(p, 0.0f, 0.0f, 0.0f, cover * 0.40f * alpha);
                float edge = 1.0f - (d < 0.0f ? -d : d) / 1.1f;
                if (edge > 0.0f) over(p, 255.0f, 255.0f, 255.0f, edge * 0.30f * alpha);
            }
        }
}

void blinker_draw(unsigned char *frame, int frame_w, int frame_h, int screen_h, int mode, int lit, float alpha) {
    if (!frame || mode == BLINKER_NONE || alpha <= 0.0f) return;
    float h = (float)screen_h, s = h * SIZE;
    float cx = (float)frame_w - h * CENTRE_FROM_RIGHT, cy = h * (1.0f - ABOVE_BOTTOM);
    float apart = h * APART;
    draw_one(frame, frame_w, frame_h, cx - apart, cy, s, -1.0f, lit && (mode == BLINKER_LEFT || mode == BLINKER_HAZARD), alpha);
    draw_one(frame, frame_w, frame_h, cx + apart, cy, s, 1.0f, lit && (mode == BLINKER_RIGHT || mode == BLINKER_HAZARD), alpha);
}

#ifdef FH2_TWEAKS_TEST
/* tests: the car update with three input bytes at a fake car; returns mode | lit << 4 */
__declspec(dllexport) int Fh2TestBlinkerStep(unsigned hazard, unsigned left, unsigned right, unsigned dt_ms) {
    static unsigned char car[LIGHT_INPUT_HAZARD + 3];
    float dt = (float)dt_ms / 1000.0f;
    blinker_set_sound(0); /* no thread in the emulator */
    car[LIGHT_INPUT_HAZARD] = (unsigned char)hazard, car[LIGHT_INPUT_HAZARD + 1] = (unsigned char)left,
    car[LIGHT_INPUT_HAZARD + 2] = (unsigned char)right;
    blinker_car_update(car, dt);
    return (int)shown_mode | (int)shown_lit << 4;
}
__declspec(dllexport) int Fh2TestBlinkerClick(int tock, int percent, short *out, unsigned capacity) {
    if (capacity < CLICK_SAMPLES) return 0;
    make_click(out, tock, percent);
    return (int)CLICK_SAMPLES;
}
__declspec(dllexport) void Fh2TestBlinkerDraw(unsigned char *frame, int w, int h, int mode, int lit) {
    blinker_draw(frame, w, h, h, mode, lit, 1.0f);
}
#endif
