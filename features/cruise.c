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
#define KP 0.12f            /* throttle per m/s too slow */
#define KI 0.04f            /* throttle per m/s*s */
#define HARD_DECEL -4.5f    /* m/s^2: braking hard or a crash */

static volatile LONG active, want_card; /* want_card: 1 on, 2 off, 3 off by brake/crash, 4 too slow */
static float target, integral, last_speed, speed_now, smooth_accel;
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
        return;
    }
    if (along < MIN_SPEED * 0.6f) {
        stop(3);
        return;
    }
    if (smooth_accel < HARD_DECEL) {
        stop(3);
        return;
    }
    /* more throttle by the player: the faster speed is kept */
    if (player_gas && speed > target) target = speed, integral = 0.0f;
    float error = target - speed;
    integral += error * dt;
    if (integral > 15.0f) integral = 15.0f;
    if (integral < -5.0f) integral = -5.0f;
    float t = KP * error + KI * integral + 0.15f; /* a little throttle holds the speed on the flat */
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    set_throttle(t);
}

double cruise_throttle(double player_throttle, double player_brake) {
    InterlockedExchange(&player_gas, player_throttle > 0.5);
    if (!active) return 0.0;
    if (player_brake > 0.15) {
        stop(3);
        return 0.0;
    }
    LONG bits = throttle_bits;
    float t;
    __builtin_memcpy(&t, &bits, 4);
    return t;
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
        head = de ? "Tempomat\nErst schneller als 20 km/h fahren" : "Cruise control\nDrive faster than 20 km/h first";
    else
        head = de ? "Tempomat aus" : "Cruise control off";
    notify_show(head);
}

void cruise_step(unsigned long long now, unsigned char *game) {
    (void)now;
    static int was_down;
    int down = keyboard_held(KEYBOARD_HOLD_CRUISE);
    if (down && !was_down) {
        if (active) {
            cruise_off();
            log_append("CRUISE: off (key)\r\n");
        } else if (noclip_active() || !game_simulation_running()) {
            log_append("CRUISE: not now (noclip or menu)\r\n");
        } else if (speed_now < MIN_SPEED) {
            InterlockedExchange(&want_card, 4);
        } else {
            target = speed_now;
            integral = 0.0f;
            set_throttle(0.0f);
            InterlockedExchange(&active, 1);
            InterlockedExchange(&want_card, 1);
            log_append("CRUISE: on\r\n");
        }
    }
    was_down = down;
    if (active && (noclip_active() || !game_simulation_running())) stop(2);
    LONG what = InterlockedExchange(&want_card, 0);
    if (what) {
        if (what == 3) log_append("CRUISE: off (brake, crash or slow)\r\n");
        card(game, what);
    }
}
