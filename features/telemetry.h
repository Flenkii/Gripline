#ifndef FH2_TELEMETRY_H
#define FH2_TELEMETRY_H
#include <stddef.h>

/* Live telemetry panel (V7, community idea): what the own car's body tells - speed, grip (slip angle),
   g-forces, turn rate, slope, 0-100 time, top speed, best values - at the right while driving.
   On/off: Mods page row "Gripline: Live-Telemetrie" or the Gripline menu (F8); which rows are shown:
   Gripline menu > Live-Telemetrie, saved as Gripline.ini [Gripline] TelemetryShow (bits below).
   The game keeps tyre temperatures in its own code where Gripline cannot read them. */

enum {
    TELEMETRY_SHOW_SPEED,
    TELEMETRY_SHOW_GRIP,
    TELEMETRY_SHOW_SIDE,
    TELEMETRY_SHOW_LONG,
    TELEMETRY_SHOW_YAW,
    TELEMETRY_SHOW_SLOPE,
    TELEMETRY_SHOW_SPRINT, /* 0-100 km/h (0-60 mph): last and best */
    TELEMETRY_SHOW_TOP,
    TELEMETRY_SHOW_BEST,   /* best side force and best drift */
    TELEMETRY_SHOW_QUARTER, /* quarter mile from standstill: time and end speed */
    TELEMETRY_SHOW_SPRINT200, /* 0-200 km/h (0-100 mph) */
    TELEMETRY_SHOW_BRAKING, /* braking distance 100-0 km/h (60-0 mph) */
    TELEMETRY_SHOW_COUNT
};
#define TELEMETRY_SHOW_DEFAULT                                                                                       \
    ((1 << TELEMETRY_SHOW_SPEED) | (1 << TELEMETRY_SHOW_GRIP) | (1 << TELEMETRY_SHOW_SIDE) | (1 << TELEMETRY_SHOW_LONG) | \
     (1 << TELEMETRY_SHOW_SPRINT) | (1 << TELEMETRY_SHOW_BEST) | (1 << TELEMETRY_SHOW_QUARTER) | (1 << TELEMETRY_SHOW_BRAKING))

void telemetry_start(unsigned char *game_image);
void telemetry_set_enabled(int on);
int telemetry_enabled(void);
/* -1 = default */
void telemetry_set_show(int mask);
int telemetry_show(void);
/* menu names of the rows (German / English) */
const wchar_t *telemetry_item_name(unsigned item, int german);

/* From the car update of the own car (game thread): dt in s, world velocity (m/s), body rows, angular velocity. */
void telemetry_sample(float dt, const float velocity[3], const float right[3], const float forward[3], const float spin[3]);

/* From the watch thread, a few times a second. */
void telemetry_step(unsigned long long now);

#endif
