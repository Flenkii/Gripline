/* Live telemetry card - see telemetry.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "notify.h"
#include "game_units.h"
#include "game_language.h"
#include "telemetry.h"

#define TELEMETRY_TAG 0x7e1e0000u
#define TELEMETRY_EVERY_MS 200u

static unsigned char *game;
static volatile LONG enabled;
static SRWLOCK lock = SRWLOCK_INIT;
static struct {
    float speed, slip, side_g, long_g, last_long, last_side;
    int have_last;
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

void telemetry_sample(float dt, const float velocity[3], const float right[3], const float forward[3]) {
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
    t.last_long = along, t.last_side = side;
    t.have_last = 1;
    t.time = GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
}

static void put_text(char *out, size_t *n, size_t cap, const char *text) {
    while (*text && *n + 1 < cap) out[(*n)++] = *text++;
    out[*n] = 0;
}

static void put_int(char *out, size_t *n, size_t cap, int value) {
    char digits[12];
    unsigned d = 0, v = value < 0 ? (unsigned)-value : (unsigned)value;
    do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
    if (value < 0 && *n + 1 < cap) out[(*n)++] = '-';
    while (d && *n + 1 < cap) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

/* g with two decimals: 0.85 / -1.20 */
static void put_g(char *out, size_t *n, size_t cap, float g) {
    int h = (int)(g * 100.0f + (g < 0 ? -0.5f : 0.5f));
    if (h < 0) {
        put_text(out, n, cap, "-");
        h = -h;
    }
    put_int(out, n, cap, h / 100);
    put_text(out, n, cap, ".");
    if (h % 100 < 10) put_text(out, n, cap, "0");
    put_int(out, n, cap, h % 100);
    put_text(out, n, cap, " g");
}

void telemetry_step(unsigned long long now) {
    if (!enabled || now < next_card) return;
    next_card = now + TELEMETRY_EVERY_MS;
    AcquireSRWLockShared(&lock);
    ULONGLONG time = t.time;
    float speed = t.speed, slip = t.slip, side_g = t.side_g, long_g = t.long_g;
    ReleaseSRWLockShared(&lock);
    if (!time || now - time > 500) return; /* not driving (menu, pause, noclip): the card runs out */
    if (now >= units_next) {
        units_next = now + 3000;
        imperial = game_units_imperial(game);
    }
    int de = game_language_is_german(game);
    const char *grip = slip < 4.0f ? (de ? "haftet" : "gripping") : slip < 12.0f ? (de ? "rutscht" : "sliding")
                                                                                : (de ? "Drift" : "drifting");
    char text[160];
    size_t n = 0;
    text[0] = 0;
    put_text(text, &n, sizeof(text), de ? "Telemetrie - " : "Telemetry - ");
    float kmh = speed * 3.6f;
    put_int(text, &n, sizeof(text), (int)((imperial ? kmh / 1.609344f : kmh) + 0.5f));
    put_text(text, &n, sizeof(text), imperial ? " mph\n" : " km/h\n");
    put_text(text, &n, sizeof(text), "Grip: ");
    put_text(text, &n, sizeof(text), grip);
    put_text(text, &n, sizeof(text), " (");
    put_int(text, &n, sizeof(text), (int)(slip + 0.5f));
    put_text(text, &n, sizeof(text), "\xc2\xb0)  ");
    put_text(text, &n, sizeof(text), de ? "Quer " : "Side ");
    put_g(text, &n, sizeof(text), absf(side_g));
    put_text(text, &n, sizeof(text), de ? "  L\xc3\xa4ngs " : "  Long ");
    put_g(text, &n, sizeof(text), long_g);
    notify_show_live(text, TELEMETRY_TAG, 700);
}
