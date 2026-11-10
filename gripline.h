#ifndef FH2_GRIPLINE_H
#define FH2_GRIPLINE_H
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

/* Gripline: an FH2 Mod Platform mod (fh2-mods\Gripline.dll) next to the official Extra Options. */
#define GRIPLINE_VERSION "V7"
/* this build as a release number: the update notice compares it with the newest GitHub release */
#define GRIPLINE_RELEASE_MAJOR 7
#define GRIPLINE_RELEASE_MINOR 0
#define GRIPLINE_RELEASE_PATCH 0 /* third number of the release tag (fork6.4.1), update_check.c */
/* NewsSeen value of this version (70 = V7.0): what's-new cards and the F8 hint card show until then */
#define GRIPLINE_NEWS_VERSION (GRIPLINE_RELEASE_MAJOR * 10 + GRIPLINE_RELEASE_MINOR)
#define MOD_INI_NAME L"Gripline.ini"
#define MOD_LOG_NAME L"Gripline.log"
#define GAME_EXE_NAME L"forza_durango_release_final.exe"
#define PATH_CAPACITY 32768
#define GAME_IMAGE_SIZE 0x3a18000u
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* Gripline.ini */
typedef struct {
    int traffic_level, traffic_all_day;        /* [Traffic] */
    int notifications, steering_animation;     /* [Gripline] */
    int cockpit_view_forward, cockpit_view_up; /* [Gripline] second cockpit view, cm */
    int card_color;                            /* [Gripline] 0 blue (original) .. 7, notify.h */
    int card_style; /* NOTIFY_STYLE_*: 0 Classic, 1 Enhanced, 2 Overdrive (V7) */
    int update_check; /* [Gripline] UpdateCheck: ask GitHub for a newer release at start (V7) */
    int speed_cameras, car_labels, telemetry, event_icons;
    int news_seen; /* [Gripline] NewsSeen: version whose what's-new cards were shown (70 = V7.0), logbook.c */
    int nitro_online; /* [Gripline] NitroOnline: 1 nitro key also online (default), 0 off online (nitro_link.h) */
    int no_damage; /* [Gripline] NoDamage: 0 off, 1 other cars, 2 all cars (V7, no_damage.h) */
    int telemetry_pos, telemetry_opacity, telemetry_size; /* [Gripline] TelemetryPosition 0..5, TelemetryOpacity %, TelemetrySize % (V7, notify.h) */
    int cruise_crash_off, cruise_min_speed, cruise_slow_off; /* [Gripline] CruiseCrashOff 0/1, CruiseMinSpeed km/h (V7, cruise.h) */
    int cards_hidden; /* [Gripline] CardsHidden: bits of notify.h NOTIFY_KIND_* that are not shown (V7) */
    int telemetry_show; /* [Gripline] TelemetryShow: bits of telemetry.h TELEMETRY_SHOW_*, -1 = default */ /* [Gripline] SpeedCameras / CarLabels: 0 = off (V7, world_files.c) */
} ModOptions;
#endif
