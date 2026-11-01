/* Live telemetry card - see telemetry.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "notify.h"
#include "game_units.h"
#include "game_language.h"
#include "telemetry.h"

#define TELEMETRY_TAG 0x7e1e0000u
#define TELEMETRY_EVERY_MS 30u

static unsigned char *game;
static volatile LONG enabled;
static SRWLOCK lock = SRWLOCK_INIT;
static volatile LONG show_mask = TELEMETRY_SHOW_DEFAULT;
static struct {
    float speed, slip, side_g, long_g, last_long, last_side, yaw, slope;
    float sprint_time, sprint_last, sprint_best, top; /* seconds; m/s */
    int have_last, sprint_armed;
    ULONGLONG time;
} t;
static ULONGLONG next_card, units_next;
static int imperial;

void telemetry_start(unsigned char *game_image) {
    game = game_image;
}

void telemetry_set_enabled(int on) {
    InterlockedExchange(&enabled, on ? 1 : 0);
}

int telemetry_enabled(void) {
    return (int)enabled;
}

void telemetry_set_show(int mask) {
    InterlockedExchange(&show_mask, mask < 0 ? TELEMETRY_SHOW_DEFAULT : mask & ((1 << TELEMETRY_SHOW_COUNT) - 1));
}

int telemetry_show(void) {
    return (int)show_mask;
}

static const wchar_t *const item_german[TELEMETRY_SHOW_COUNT] = {
    L"Tempo", L"Grip / Rutschwinkel", L"Querkraft", L"Bremsen / Gas", L"Drehrate",
    L"Steigung", L"0-100-Zeit", L"Höchsttempo", L"Bestwerte"};
static const wchar_t *const item_english[TELEMETRY_SHOW_COUNT] = {
    L"Speed", L"Grip / slip angle", L"Side force", L"Brake / throttle", L"Turn rate",
    L"Slope", L"0-60 time", L"Top speed", L"Best values"};

const wchar_t *telemetry_item_name(unsigned item, int german) {
    return item < TELEMETRY_SHOW_COUNT ? (german ? item_german[item] : item_english[item]) : L"";
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}

static float absf(float x) {
    return x < 0.0f ? -x : x;
}

/* atan for x >= 0 in degrees, about 0.3 degrees exact (no maths library in the DLL) */
static float atan_deg(float x) {
    int flip = x > 1.0f;
    if (flip) x = 1.0f / x;
    float a = x * 0.785398f + 0.273f * x * (1.0f - x);
    if (flip) a = 1.570796f - a;
    return a * 57.29578f;
}

static float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void telemetry_sample(float dt, const float velocity[3], const float right[3], const float forward[3], const float spin[3]) {
    if (!enabled || !(dt > 0.0005f) || dt > 0.25f) return;
    float along = dot(velocity, forward), side = dot(velocity, right);
    float speed = root(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    AcquireSRWLockExclusive(&lock);
    float k = dt * 6.0f; /* smoothing: about 1/6 s */
    if (k > 1.0f) k = 1.0f;
    if (t.have_last) {
        float a_long = (along - t.last_long) / dt / 9.81f, a_side = (side - t.last_side) / dt / 9.81f;
        if (a_long > 5.0f || a_long < -5.0f || a_side > 5.0f || a_side < -5.0f) a_long = a_side = 0.0f; /* rewind, reset */
        t.long_g += (a_long - t.long_g) * k;
        t.side_g += (a_side - t.side_g) * k;
    }
    float slip = absf(along) > 3.0f || absf(side) > 3.0f ? atan_deg(absf(side) / (absf(along) > 0.01f ? absf(along) : 0.01f)) : 0.0f;
    t.slip += (slip - t.slip) * k;
    t.speed = speed;
    float yaw = absf(spin[1]) * 57.29578f; /* world y is up: turn rate in degrees per second */
    if (yaw > 720.0f) yaw = 720.0f;
    t.yaw += (yaw - t.yaw) * k;
    float flat = root(forward[0] * forward[0] + forward[2] * forward[2]);
    float slope = flat > 0.1f ? forward[1] / flat * 100.0f : 0.0f; /* percent, + = uphill */
    t.slope += (slope - t.slope) * k;
    if (speed > t.top && speed < 150.0f) t.top = speed;
    /* 0-100 km/h (0-60 mph): armed when standing, runs while the car speeds up */
    float goal = imperial ? 26.8224f : 27.7778f;
    if (speed < 0.3f) {
        t.sprint_armed = 1;
        t.sprint_time = 0.0f;
    } else if (t.sprint_armed) {
        t.sprint_time += dt;
        if (speed >= goal) {
            t.sprint_last = t.sprint_time;
            if (t.sprint_best <= 0.0f || t.sprint_time < t.sprint_best) t.sprint_best = t.sprint_time;
            t.sprint_armed = 0;
        } else if (t.sprint_time > 60.0f || along < -0.5f) {
            t.sprint_armed = 0; /* too slow or backwards: not a sprint */
        }
    }
    t.last_long = along, t.last_side = side;
    t.have_last = 1;
    t.time = GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}

static unsigned panel_owner;
static float max_side, max_slip;

static void wput(wchar_t *out, unsigned cap, const wchar_t *text) {
    unsigned n = 0;
    while (out[n]) ++n;
    while (*text && n + 1 < cap) out[n++] = *text++;
    out[n] = 0;
}

static void wint(wchar_t *out, unsigned cap, int value) {
    wchar_t digits[12];
    unsigned d = 0, v = value < 0 ? (unsigned)-value : (unsigned)value;
    do digits[d++] = (wchar_t)(L'0' + v % 10); while ((v /= 10) && d < 12);
    unsigned n = 0;
    while (out[n]) ++n;
    if (value < 0 && n + 1 < cap) out[n++] = L'-';
    while (d && n + 1 < cap) out[n++] = digits[--d];
    out[n] = 0;
}

/* 0.85 g */
static void wg(wchar_t *out, unsigned cap, float g) {
    int h = (int)(absf(g) * 100.0f + 0.5f);
    wint(out, cap, h / 100);
    wput(out, cap, h % 100 < 10 ? L".0" : L".");
    wint(out, cap, h % 100);
    wput(out, cap, L" g");
}

/* ■■■□□□□□ for 0..full */
static void wbar(wchar_t *out, unsigned cap, float value, float full) {
    int cells = (int)(absf(value) / full * 8.0f + 0.5f);
    if (cells > 8) cells = 8;
    for (int i = 0; i < 8; ++i) wput(out, cap, i < cells ? L"\x25a0" : L"\x25a1");
    wput(out, cap, L"  ");
}

static void row(NotifyPanelRow *r, const wchar_t *label, int style) {
    r->label[0] = 0;
    r->keys[0] = 0;
    wput(r->label, 32, label);
    r->style = style;
}

/* 4.83 s */
static void wsec(wchar_t *out, unsigned cap, float sec) {
    int h = (int)(sec * 100.0f + 0.5f);
    wint(out, cap, h / 100);
    wput(out, cap, h % 100 < 10 ? L".0" : L".");
    wint(out, cap, h % 100);
    wput(out, cap, L" s");
}

static void wspeed(wchar_t *out, unsigned cap, float mps) {
    float kmh = mps * 3.6f;
    wint(out, cap, (int)((imperial ? kmh / 1.609344f : kmh) + 0.5f));
    wput(out, cap, imperial ? L" mph" : L" km/h");
}

void telemetry_step(unsigned long long now) {
    if (!enabled) {
        if (panel_owner) notify_panel_clear_owned(panel_owner), panel_owner = 0;
        return;
    }
    if (now < next_card) return;
    next_card = now + TELEMETRY_EVERY_MS;
    AcquireSRWLockShared(&lock);
    ULONGLONG time = t.time;
    float speed = t.speed, slip = t.slip, side_g = t.side_g, long_g = t.long_g, yaw = t.yaw, slope = t.slope;
    float sprint_time = t.sprint_time, sprint_last = t.sprint_last, sprint_best = t.sprint_best, top = t.top;
    int sprint_armed = t.sprint_armed;
    ReleaseSRWLockShared(&lock);
    if (!time || now - time > 500) { /* not driving (menu, pause, noclip) */
        if (panel_owner) notify_panel_clear_owned(panel_owner), panel_owner = 0;
        return;
    }
    if (now >= units_next) {
        units_next = now + 3000;
        imperial = game_units_imperial(game);
    }
    if (speed > 5.0f) {
        if (absf(side_g) > max_side && absf(side_g) < 2.5f) max_side = absf(side_g); /* crashes are not cornering */
        if (slip > max_slip) max_slip = slip;
    }
    int de = game_language_is_german(game), mask = (int)show_mask;
    static NotifyPanelRow rows[12];
    unsigned n = 0;
    if (mask & (1 << TELEMETRY_SHOW_SPEED)) {
        row(&rows[n], de ? L"Tempo" : L"Speed", NOTIFY_ROW_INFO);
        wspeed(rows[n++].keys, 32, speed);
    }
    if (mask & (1 << TELEMETRY_SHOW_GRIP)) {
        row(&rows[n], L"Grip", NOTIFY_ROW_NORMAL);
        wput(rows[n].keys, 32, slip < 4.0f ? (de ? L"haftet" : L"gripping") : slip < 12.0f ? (de ? L"rutscht  " : L"sliding  ")
                                                                                         : (de ? L"Drift  " : L"drifting  "));
        if (slip >= 4.0f) {
            wint(rows[n].keys, 32, (int)(slip + 0.5f));
            wput(rows[n].keys, 32, L"\x00b0");
        }
        ++n;
    }
    if (mask & (1 << TELEMETRY_SHOW_SIDE)) {
        row(&rows[n], de ? L"Querkraft" : L"Side force", NOTIFY_ROW_NORMAL);
        wbar(rows[n].keys, 32, side_g, 1.5f);
        wg(rows[n++].keys, 32, side_g);
    }
    if (mask & (1 << TELEMETRY_SHOW_LONG)) {
        row(&rows[n], long_g < -0.03f ? (de ? L"Bremsen" : L"Braking") : (de ? L"Beschleunigen" : L"Accelerating"), NOTIFY_ROW_NORMAL);
        wbar(rows[n].keys, 32, long_g, 1.5f);
        wg(rows[n++].keys, 32, long_g);
    }
    if (mask & (1 << TELEMETRY_SHOW_YAW)) {
        row(&rows[n], de ? L"Drehrate" : L"Turn rate", NOTIFY_ROW_NORMAL);
        wbar(rows[n].keys, 32, yaw, 90.0f);
        wint(rows[n].keys, 32, (int)(yaw + 0.5f));
        wput(rows[n++].keys, 32, L"\x00b0/s");
    }
    if (mask & (1 << TELEMETRY_SHOW_SLOPE)) {
        row(&rows[n], slope >= 0.0f ? (de ? L"Steigung" : L"Uphill") : (de ? L"Gefälle" : L"Downhill"), NOTIFY_ROW_NORMAL);
        wint(rows[n].keys, 32, (int)(absf(slope) + 0.5f));
        wput(rows[n++].keys, 32, L" %");
    }
    if (mask & (1 << TELEMETRY_SHOW_SPRINT)) {
        row(&rows[n], imperial ? L"0-60 mph" : L"0-100 km/h", NOTIFY_ROW_NORMAL);
        if (sprint_armed && sprint_time > 0.0f) wsec(rows[n].keys, 32, sprint_time);
        else if (sprint_last > 0.0f) {
            wsec(rows[n].keys, 32, sprint_last);
            if (sprint_best > 0.0f && sprint_best < sprint_last - 0.005f) {
                wput(rows[n].keys, 32, de ? L"  (Best " : L"  (best ");
                wsec(rows[n].keys, 32, sprint_best);
                wput(rows[n].keys, 32, L")");
            }
        } else wput(rows[n].keys, 32, de ? L"aus dem Stand" : L"from standstill");
        ++n;
    }
    if (mask & (1 << TELEMETRY_SHOW_TOP)) {
        row(&rows[n], de ? L"Höchsttempo" : L"Top speed", NOTIFY_ROW_HINT);
        wspeed(rows[n++].keys, 32, top);
    }
    if (mask & (1 << TELEMETRY_SHOW_BEST)) {
        row(&rows[n], de ? L"Beste Querkraft" : L"Best side force", NOTIFY_ROW_HINT);
        wg(rows[n++].keys, 32, max_side);
        row(&rows[n], de ? L"Bester Drift" : L"Best drift", NOTIFY_ROW_HINT);
        wint(rows[n].keys, 32, (int)(max_slip + 0.5f));
        wput(rows[n++].keys, 32, L"\x00b0");
    }
    if (!n) {
        if (panel_owner) notify_panel_clear_owned(panel_owner), panel_owner = 0;
        return;
    }
    unsigned id = notify_panel_show_owned(de ? L"Live-Telemetrie" : L"Live telemetry", rows, n, panel_owner);
    panel_owner = id; /* 0: another panel (key list, Gripline menu) is up - wait until it closes */
}
