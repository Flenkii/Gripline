#ifndef FH2_RIG_SETTINGS_H
#define FH2_RIG_SETTINGS_H
#include "gripline.h"

/* Gripline's rows on the Mods page: keyboard, More Traffic, cockpit and Gripline settings.
   Values are kept in the units of the INI files (FH2_Keyboard.ini / Gripline.ini).
   The wheel belongs to Azimuth (its launcher), weather and time of day to Extra Options. */
enum {
    RIG_TRAFFIC_LEVEL,                   /* [Traffic] TrafficLevel   0..7 */
    RIG_TRAFFIC_ALL_DAY,                 /* [Traffic] RushHourAllDay 0/1 */
    RIG_TRAFFIC_END,
    RIG_KEY_FIRST = RIG_TRAFFIC_END,     /* FH2_Keyboard.ini [Keyboard], keyboard_pad.h order */
    RIG_KEY_ENABLED = RIG_KEY_FIRST,     /* Enabled    0/1, next game start */
    RIG_KEY_STEER,                       /* SteerTime  ms */
    RIG_KEY_RETURN,                      /* ReturnTime ms */
    RIG_KEY_PEDAL,                       /* PedalTime  ms */
    RIG_KEY_END,
    RIG_GRIP_FIRST = RIG_KEY_END,        /* Gripline.ini [Gripline] */
    RIG_GRIP_NOTIFICATIONS = RIG_GRIP_FIRST, /* Notifications      0/1 */
    RIG_GRIP_STEERING,                   /* SteeringAnimation  0/1, next game start */
    RIG_GRIP_VIEW_FORWARD,               /* CockpitViewForward cm, second cockpit view */
    RIG_GRIP_VIEW_UP,                    /* CockpitViewUp      cm */
    RIG_GRIP_CARD_COLOR,                 /* CardColor          0..7, notify.h NOTIFY_COLOR_* */
    RIG_GRIP_END,
    RIG_TRAFFIC_PRESET = RIG_GRIP_END,   /* traffic preset: not stored, derived from level + rush hour */
    RIG_GRIP_REPORT,                     /* problem report: an action (On = write Gripline_Report.zip), not stored */
    RIG_SETTING_COUNT
};

/* How a setting is shown in the menu. */
enum {
    RIG_KIND_TOGGLE,  /* SpinnerControl, ToggleChoices (0 = off, 1 = on) */
    RIG_KIND_TRAFFIC, /* SpinnerControl, TrafficChoices */
    RIG_KIND_PERCENT, /* Slider, value / 100 with the game's percent display */
    RIG_KIND_NUMBER,  /* Slider, value * scale with a plain number display */
    RIG_KIND_DENSITY,  /* SpinnerControl, DensityChoices (traffic presets) */
    RIG_KIND_COLOR,    /* numbered slider, card colours (rig_color_choice) */
};

/* kinds shown as a list of choices (SpinnerControl) and the name of their collection */
int rig_kind_is_choice(unsigned kind);
const char *rig_kind_collection(unsigned kind);

#define RIG_DENSITY_COUNT 5 /* own values, Original, Much More Traffic, No Traffic, Drivatars Only */
#define RIG_TRAFFIC_COUNT 8 /* TrafficLevel 0..5, 6 = no traffic, 7 = Drivatars only */

typedef struct {
    const char *property; /* binding name on the options page */
    const char *key;      /* INI key */
    int minimum, maximum; /* INI units */
    unsigned char kind, digits;
    float scale;        /* menu value = INI value * scale */
    const char *style;  /* slider style key (sliders only) */
} RigSettingInfo;

const RigSettingInfo *rig_setting_info(unsigned id);
int rig_setting_get(unsigned id);
/* Returns 1 when the value changed. Out-of-range values are refused. */
int rig_setting_set(unsigned id, int value);
void rig_settings_start(const wchar_t *settings_directory);
/* [Gripline] values read from Gripline.ini at the game start */
void rig_settings_gripline(int notifications, int steering_animation, int view_forward, int view_up, int card_color);
/* game image for the language of the confirmation cards (a card names every change made in the menu) */
void rig_settings_set_game(unsigned char *game_image);
void rig_settings_flush(void);

/* Texts of the page (German or English, following the game language). */
const char *rig_setting_heading(unsigned char *game_image, unsigned id);
const char *rig_setting_description(unsigned char *game_image, unsigned id);
const wchar_t *rig_traffic_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_density_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_color_choice(unsigned char *game_image, unsigned index);
/* Rows of the menu: rig_setting_order(0 .. rig_setting_menu_count() - 1) are setting ids.
   Settings that are not listed keep working from their INI files but are not shown. */
unsigned rig_setting_menu_count(void);
unsigned rig_setting_order(unsigned position);

/* Provided by the traffic watcher. */
int traffic_setting(unsigned id);
void traffic_set_setting(unsigned id, int value);
#endif
