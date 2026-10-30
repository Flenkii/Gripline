/* Trip summary - see trip.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "support.h"
#include "logging.h"
#include "trip.h"
#include "keyboard_pad.h"
#include "notify.h"
#include "game_language.h"
#include "game_units.h"

#define MIN_GAP_MS 120000u    /* cards at most every two minutes */
#define MIN_NEW_METRES 500.0  /* and only after half a kilometre more */
#define MAX_SPEED 150.0f      /* m/s: anything faster is a teleport (fast travel, rewind) */
#define MOVING 1.0f           /* m/s: driving time counts above this */

static unsigned char *game;
static SRWLOCK lock = SRWLOCK_INIT;
static double metres, seconds;
static float top; /* m/s */
static double shown_metres;
static ULONGLONG shown_at;
static int was_running;

void trip_start(unsigned char *game_image) {
    game = game_image;
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}

void trip_sample(float dt, const float velocity[3]) {
    if (!(dt > 0.0f) || dt > 0.25f) return;
    float speed = root(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    if (!(speed < MAX_SPEED)) return;
    AcquireSRWLockExclusive(&lock);
    metres += (double)speed * dt;
    if (speed > MOVING) seconds += dt;
    if (speed > top) top = speed;
    ReleaseSRWLockExclusive(&lock);
}

static void put_text(char *out, size_t *n, size_t cap, const char *text) {
    while (*text && *n + 1 < cap) out[(*n)++] = *text++;
    out[*n] = 0;
}

static void put_int(char *out, size_t *n, size_t cap, unsigned value) {
    char digits[12];
    unsigned d = 0;
    do digits[d++] = (char)('0' + value % 10); while ((value /= 10) && d < sizeof(digits));
    while (d && *n + 1 < cap) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

static void format(char *text, size_t cap, double m, double s, float t, int imperial) {
    int de = game_language_is_german(game);
    size_t n = 0;
    text[0] = 0;
    unsigned tenths = (unsigned)(m / (imperial ? 160.9344 : 100.0) + 0.5);
    put_text(text, &n, cap, de ? "Fahrt-Bilanz: " : "Trip: ");
    put_int(text, &n, cap, tenths / 10);
    put_text(text, &n, cap, de ? "," : ".");
    put_int(text, &n, cap, tenths % 10);
    put_text(text, &n, cap, imperial ? " mi\n" : " km\n");
    put_text(text, &n, cap, "max ");
    put_int(text, &n, cap, (unsigned)(t * (imperial ? 2.236936f : 3.6f) + 0.5f));
    put_text(text, &n, cap, imperial ? " mph" : " km/h");
    put_text(text, &n, cap, de ? ", Fahrzeit " : ", driving time ");
    unsigned minutes = (unsigned)(s / 60.0 + 0.5);
    if (minutes >= 60) {
        put_int(text, &n, cap, minutes / 60);
        put_text(text, &n, cap, " h ");
        put_int(text, &n, cap, minutes % 60);
    } else {
        put_int(text, &n, cap, minutes);
    }
    put_text(text, &n, cap, " min");
}

static void log_units(void) {
    static int logged;
    int option, language;
    if (logged || !game_units_raw(game, &option, &language)) return;
    logged = 1;
    char line[96];
    size_t n = 0;
    line[0] = 0;
    put_text(line, &n, sizeof(line), "UNITS: option ");
    if (option < 0) put_text(line, &n, sizeof(line), "-");
    put_int(line, &n, sizeof(line), (unsigned)(option < 0 ? -option : option));
    put_text(line, &n, sizeof(line), ", language ");
    if (language < 0) put_text(line, &n, sizeof(line), "-");
    put_int(line, &n, sizeof(line), (unsigned)(language < 0 ? -language : language));
    put_text(line, &n, sizeof(line), game_units_imperial(game) ? " -> mph\r\n" : " -> km/h\r\n");
    log_append(line);
}

static void show(double m, double s, float t) {
    log_units();
    char text[128];
    format(text, sizeof(text), m, s, t, game_units_imperial(game));
    notify_show(text);
}

void trip_step(unsigned long long now) {
    if (!game) return;
    int running = game_simulation_running();
    int paused_now = was_running && !running;
    was_running = running;
    if (!paused_now) return;
    AcquireSRWLockShared(&lock);
    double m = metres, s = seconds;
    float t = top;
    ReleaseSRWLockShared(&lock);
    if (m - shown_metres < MIN_NEW_METRES || (shown_at && now - shown_at < MIN_GAP_MS)) return;
    shown_metres = m;
    shown_at = now;
    show(m, s, t);
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_cards.py: dt/speed samples (cm/s), then the card text of a pause */
__declspec(dllexport) void Fh2TestTripSample(unsigned dt_ms, int vx_cm, int vy_cm, int vz_cm) {
    float v[3] = {(float)vx_cm / 100.0f, (float)vy_cm / 100.0f, (float)vz_cm / 100.0f};
    trip_sample((float)dt_ms / 1000.0f, v);
}
__declspec(dllexport) void Fh2TestTripText(int imperial, char *out, unsigned size) {
    format(out, size, metres, seconds, top, imperial);
}
#endif
