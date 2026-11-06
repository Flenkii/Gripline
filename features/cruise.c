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

#define MIN_SPEED 5.5556f   /* 20 km/h */
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
    if (along < MIN_SPEED * 0.6f) {
        stop(6);
        return;
    }
    if (since_on > SETTLE_S && smooth_accel < CRASH_DECEL) {
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

static void card(unsigned char *game, LONG what) {
    int de = game_language_is_german(game), imperial = game_units_imperial(game);
    char text[96];
    unsigned n = 0;
    const char *head;
    if (what == 1) {
        head = de ? "Tempomat an: " : "Cruise control on: ";
        while (*head) text[n++] = *head++;
        unsigned value = (unsigned)(target * (imperial ? 2.236936f : 3.6f) + 0.5f);
        char d[8];
        unsigned k = 0;
        do d[k++] = (char)('0' + value % 10); while ((value /= 10) && k < 8);
        while (k) text[n++] = d[--k];
        const char *tail = imperial ? " mph\n" : " km/h\n";
        while (*tail) text[n++] = *tail++;
        tail = de ? "Bremsen oder Taste = aus" : "Brake or key again = off";
        while (*tail) text[n++] = *tail++;
        text[n] = 0;
        notify_show(text);
        return;
    }
    if (what == 4)
        head = imperial ? (de ? "Tempomat\nErst schneller als 12 mph fahren" : "Cruise control\nDrive faster than 12 mph first")
                        : (de ? "Tempomat\nErst schneller als 20 km/h fahren" : "Cruise control\nDrive faster than 20 km/h first");
    else if (what == 3)
        head = de ? "Tempomat aus\nGebremst" : "Cruise control off\nBraked";
    else if (what == 5)
        head = de ? "Tempomat aus\nAufprall" : "Cruise control off\nCrash";
    else if (what == 6)
        head = de ? "Tempomat aus\nZu langsam" : "Cruise control off\nToo slow";
    else
        head = de ? "Tempomat aus" : "Cruise control off";
    notify_show(head);
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
        } else if (speed_now < MIN_SPEED) {
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
        if (what == 6) log_append("CRUISE: off (slower than 12 km/h or backwards)\r\n");
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
__declspec(dllexport) int Fh2TestCruisePad(int rt, int lt) {
    unsigned char pad[12] = {0};
    pad[3] = (unsigned char)rt, pad[2] = (unsigned char)lt;
    cruise_pad(pad, 3, 2);
    return pad[3];
}
#endif
