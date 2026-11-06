/* Cruise control - see cruise.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "logging.h"
#include "cruise.h"
#include "keyboard_pad.h"
#include "notify.h"
#include "noclip.h"
#include "game_language.h"
#include "game_units.h"

#define STOP_SPEED 2.2222f  /* 8 km/h: slower (or backwards) switches it off */
#define KP 0.25f            /* throttle per m/s too slow */
#define KI 0.08f            /* throttle per m/s*s */
#define BASE 0.35f          /* throttle that roughly holds a speed; the integral does the rest */
/* V7 fix (Marvin 06.11.: "cruise control does not work"): lifting the throttle at high speed
   (engine braking, drag, a gear change) already passed the old -4.5 m/s^2 "crash" limit, so the
   cruise control switched itself off at once. Now only a real crash (> 1.2 g) ends it, and not in
   the first second. */
#define CRASH_DECEL -12.0f  /* m/s^2 */
#define SETTLE_S 1.0f       /* s after switching on: no crash check */

static volatile LONG active, want_card; /* want_card: 1 on, 2 off, 3 off by brake, 4 too slow, 5 crash, 6 slow */
static float target, integral, last_speed, speed_now, smooth_accel, since_on;
/* settings (F8 > Tempomat, Gripline.ini CruiseCrashOff / CruiseMinSpeed) */
static volatile LONG crash_off = 1, min_kmh = CRUISE_MIN_DEFAULT;
static volatile LONG slow_off = 1; /* CruiseSlowOff 0: never switches off for being slow, keeps giving throttle (Marvin 06.11.) */

void cruise_set_slow_off(int on) {
    InterlockedExchange(&slow_off, on ? 1 : 0);
}
int cruise_slow_off(void) {
    return (int)slow_off;
}

void cruise_set_options(int switch_off_on_crash, int min_speed_kmh) {
    InterlockedExchange(&crash_off, switch_off_on_crash ? 1 : 0);
    if (min_speed_kmh < CRUISE_MIN_LOWEST) min_speed_kmh = CRUISE_MIN_LOWEST;
    if (min_speed_kmh > CRUISE_MIN_HIGHEST) min_speed_kmh = CRUISE_MIN_HIGHEST;
    InterlockedExchange(&min_kmh, min_speed_kmh);
}

int cruise_crash_off(void) {
    return (int)crash_off;
}

int cruise_min_speed(void) {
    return (int)min_kmh;
}

static float min_speed(void) {
    return (float)min_kmh / 3.6f;
}
static volatile LONG throttle_bits; /* float */
static volatile LONG player_gas; /* player's throttle > 0.5 (float compare done in reading) */

static float root(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }

static void set_throttle(float t) {
    LONG bits;
    __builtin_memcpy(&bits, &t, 4);
    InterlockedExchange(&throttle_bits, bits);
}

int cruise_active(void) { return active != 0; }

void cruise_off(void) {
    if (InterlockedExchange(&active, 0)) {
        set_throttle(0.0f);
        InterlockedExchange(&want_card, 2);
    }
}

static volatile LONG toggle_request; /* controller LS + D-pad left (noclip.c) */

void cruise_request_toggle(void) {
    InterlockedExchange(&toggle_request, 1);
}

#ifdef FH2_TWEAKS_TEST
static int test_driving; /* tests: the game counts as driving */
#define DRIVING() (test_driving || (!noclip_active() && game_simulation_running()))
#else
#define DRIVING() (!noclip_active() && game_simulation_running())
#endif

static void stop(LONG why) {
    if (InterlockedExchange(&active, 0)) {
        set_throttle(0.0f);
        InterlockedExchange(&want_card, why);
    }
}

void cruise_sample(float dt, const float v[3], const float f[3]) {
    if (!(dt > 0.0f) || dt > 0.25f) return;
    float speed = root(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float along = v[0] * f[0] + v[1] * f[1] + v[2] * f[2];
    float accel = (speed - last_speed) / dt;
    last_speed = speed;
    speed_now = along > 0.0f ? speed : -speed;
    smooth_accel += (accel - smooth_accel) * (dt / (dt + 0.15f));
    if (!active) {
        integral = 0.0f;
        smooth_accel = 0.0f;
        since_on = 0.0f;
        return;
    }
    since_on += dt;
    if (slow_off && along < STOP_SPEED) {
        stop(6);
        return;
    }
    if (crash_off && since_on > SETTLE_S && smooth_accel < CRASH_DECEL) {
        stop(5);
        return;
    }
    /* more throttle by the player: the faster speed is kept */
    if (player_gas && speed > target) target = speed, integral = 0.0f;
    float error = target - speed;
    integral += error * dt;
    if (integral > 15.0f) integral = 15.0f;
    if (integral < -5.0f) integral = -5.0f;
    float t = KP * error + KI * integral + BASE;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    set_throttle(t);
}

double cruise_throttle(double player_throttle, double player_brake) {
    LONG bits = throttle_bits;
    float t;
    __builtin_memcpy(&t, &bits, 4);
    /* the value read may already hold the cruise throttle (it is added in two places): only clearly more
       is the player's own throttle */
    InterlockedExchange(&player_gas, active && player_throttle > 0.5 && player_throttle > (double)t + 0.25);
    if (!active) return 0.0;
    if (player_brake > 0.15) {
        stop(3);
        return 0.0;
    }
    return t;
}

/* the game's controller state (XINPUT_GAMEPAD, noclip.c's controller hook): adds the cruise throttle on
   the trigger the controller layout uses for throttle. This is the way that reaches the game with a real
   controller too; the Gripline keyboard pad gets it in keyboard_pad.c as well. */
void cruise_pad(unsigned char *gamepad, int gas_trigger, int brake_trigger) {
    if (!gamepad || (gas_trigger != 2 && gas_trigger != 3)) return;
    double gas = gamepad[gas_trigger] / 255.0, brake = brake_trigger == 2 || brake_trigger == 3 ? gamepad[brake_trigger] / 255.0 : 0.0;
    double c = cruise_throttle(gas, brake);
    if (c > gas) gamepad[gas_trigger] = (unsigned char)(c * 255.0 + 0.5);
}

static void put(char *text, unsigned *n, unsigned cap, const char *t) {
    while (*t && *n + 1 < cap) text[(*n)++] = *t++;
    text[*n] = 0;
}

static void put_uint(char *text, unsigned *n, unsigned cap, unsigned value) {
    char d[12];
    unsigned k = 0;
    do d[k++] = (char)('0' + value % 10); while ((value /= 10) && k < sizeof(d));
    while (k && *n + 1 < cap) text[(*n)++] = d[--k];
    text[*n] = 0;
}

/* "120 km/h" / "75 mph" */
static void put_speed(char *text, unsigned *n, unsigned cap, float metres_per_second, int imperial) {
    put_uint(text, n, cap, (unsigned)(metres_per_second * (imperial ? 2.236936f : 3.6f) + 0.5f));
    put(text, n, cap, imperial ? " mph" : " km/h");
}

/* the key as bound now: "K" (the controller combination is the same for everyone) */
static void put_key(char *text, unsigned *n, unsigned cap, int de) {
    int index = keyboard_binding_find(L"CruiseControl");
    unsigned char vk[4] = {0};
    if (index >= 0) keyboard_binding_vks((unsigned)index, vk);
    if (vk[0]) {
        wchar_t keys[32];
        vk[1] = vk[2] = vk[3] = 0;
        keyboard_keys_text(vk, de, keys, 32);
        int got = WideCharToMultiByte(CP_UTF8, 0, keys, -1, text + *n, (int)(cap - *n), NULL, NULL);
        if (got > 0) *n += (unsigned)got - 1;
        put(text, n, cap, " / ");
    }
    put(text, n, cap, de ? "LS + Steuerkreuz links" : "LS + D-pad left");
}

/* V7 (Marvin 06.11.): the off cards say why and how it comes back, not only "Crash" */
static void card(unsigned char *game, LONG what) {
    int de = game_language_is_german(game), imperial = game_units_imperial(game);
    char text[160];
    unsigned n = 0;
    text[0] = 0;
    if (what == 1) {
        put(text, &n, sizeof(text), de ? "Tempomat an: " : "Cruise control on: ");
        put_speed(text, &n, sizeof(text), target, imperial);
        put(text, &n, sizeof(text), de ? "\nAus: Bremsen oder " : "\nOff: brake or ");
        put_key(text, &n, sizeof(text), de);
        notify_show(text);
        return;
    }
    if (what == 4) {
        put(text, &n, sizeof(text), de ? "Tempomat noch nicht möglich\nErst schneller als " : "Cruise control not yet\nDrive faster than ");
        put_speed(text, &n, sizeof(text), min_speed(), imperial);
        put(text, &n, sizeof(text), de ? " fahren" : " first");
        notify_show_tinted(text, NOTIFY_COLOR_ORANGE, de ? "TEMPOMAT" : "CRUISE", 0);
        return;
    }
    const char *why = what == 3 ? (de ? "Tempomat aus: gebremst" : "Cruise control off: braked")
                    : what == 5 ? (de ? "Tempomat aus: Aufprall erkannt" : "Cruise control off: crash detected")
                    : what == 6 ? (de ? "Tempomat aus: fast stehen geblieben" : "Cruise control off: almost stopped")
                                : (de ? "Tempomat aus" : "Cruise control off");
    put(text, &n, sizeof(text), why);
    put(text, &n, sizeof(text), de ? "\nWieder an: " : "\nOn again: ");
    put_key(text, &n, sizeof(text), de);
    if (what == 3 || what == 5 || what == 6)
        notify_show_tinted(text, NOTIFY_COLOR_ORANGE, de ? "TEMPOMAT" : "CRUISE", 0);
    else
        notify_show(text);
}

void cruise_step(unsigned long long now, unsigned char *game) {
    (void)now;
    static int was_down;
    int down = keyboard_held(KEYBOARD_HOLD_CRUISE);
    int pressed = (down && !was_down) || InterlockedExchange(&toggle_request, 0);
    if (pressed) {
        if (active) {
            cruise_off();
            log_append("CRUISE: off (key)\r\n");
        } else if (!DRIVING()) {
            log_append("CRUISE: not now (noclip or menu)\r\n");
        } else if (speed_now < min_speed()) {
            log_append_value("CRUISE: too slow to switch on, km/h ", (int)(speed_now * 3.6f));
            InterlockedExchange(&want_card, 4);
        } else {
            target = speed_now;
            integral = 0.0f;
            since_on = 0.0f;
            set_throttle(BASE);
            InterlockedExchange(&active, 1);
            InterlockedExchange(&want_card, 1);
            log_append_value("CRUISE: on, km/h ", (int)(target * 3.6f + 0.5f));
        }
    }
    was_down = down;
    if (active && !DRIVING()) stop(2);
    LONG what = InterlockedExchange(&want_card, 0);
    if (what) {
        if (what == 3) log_append("CRUISE: off (brake)\r\n");
        if (what == 5) log_append("CRUISE: off (crash)\r\n");
        if (what == 6) log_append("CRUISE: off (slower than 8 km/h or backwards)\r\n");
        card(game, what);
    }
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_cruise.py: speed along the car (m/s) for dt seconds */
__declspec(dllexport) void Fh2TestCruiseSample(float dt, float speed) {
    float v[3] = {0.0f, 0.0f, speed}, f[3] = {0.0f, 0.0f, 1.0f};
    test_driving = 1;
    cruise_sample(dt, v, f);
}
__declspec(dllexport) void Fh2TestCruiseToggle(void) {
    cruise_request_toggle();
}
__declspec(dllexport) int Fh2TestCruiseStep(void) {
    test_driving = 1;
    cruise_step(0, NULL);
    return cruise_active();
}
/* the controller state: RT (gas) and LT (brake) in, RT out */
__declspec(dllexport) void Fh2TestCruiseOptions(int crash, int min_speed_kmh) {
    cruise_set_options(crash, min_speed_kmh);
}
__declspec(dllexport) void Fh2TestCruiseSlowOff(int on) {
    cruise_set_slow_off(on);
}
__declspec(dllexport) int Fh2TestCruisePad(int rt, int lt) {
    unsigned char pad[12] = {0};
    pad[3] = (unsigned char)rt, pad[2] = (unsigned char)lt;
    cruise_pad(pad, 3, 2);
    return pad[3];
}
#endif
