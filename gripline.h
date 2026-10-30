#ifndef FH2_GRIPLINE_H
#define FH2_GRIPLINE_H
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

/* Gripline: an FH2 Mod Platform mod (fh2-mods\Gripline.dll) next to the official Extra Options. */
#define GRIPLINE_VERSION "V6.4"
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
} ModOptions;
#endif
