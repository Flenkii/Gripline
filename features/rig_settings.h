#ifndef FH2_RIG_SETTINGS_H
#define FH2_RIG_SETTINGS_H
#include "extra_options.h"

/* In-game page "Lenkrad & Verkehr": wheel bridge and More Traffic settings.
   Values are kept in the units of the INI files (FH2_Wheel.ini / FH2_ExtraOptions.ini),
   so the launcher keeps reading and writing the very same keys. */
enum {
    RIG_WHEEL_ENABLED,   /* Aktiv            0/1, next game start */
    RIG_WHEEL_FFB,       /* ForceFeedback    0/1 */
    RIG_WHEEL_STRENGTH,  /* FFBStaerke       % */
    RIG_WHEEL_RUMBLE,    /* Vibration        % */
    RIG_WHEEL_DAMPING,   /* Daempfung        % */
    RIG_WHEEL_FRICTION,  /* Reibung          % */
    RIG_WHEEL_SMOOTHING, /* Glaettung        ms */
    RIG_WHEEL_MINFORCE,  /* Mindestkraft     % */
    RIG_WHEEL_OUTWARD,   /* KraftNachAussen  % */
    RIG_WHEEL_CENTERING, /* Zentrierung      % */
    RIG_WHEEL_LOCK,      /* Anschlag         degrees per side */
    RIG_WHEEL_CURVE,     /* Kurve            x10 */
    RIG_WHEEL_ROTATION,  /* Drehwinkel       degrees */
    RIG_WHEEL_SHIFTER,   /* Schaltknauf      0/1 */
    RIG_WHEEL_BUTTON1,   /* ZusatzFunktion1  WHEEL_FUNCTION_* of extra button 1 (RSB) */
    RIG_WHEEL_BUTTON2,   /* ZusatzFunktion2  extra button 2 (LSB) */
    RIG_WHEEL_BUTTON3,   /* ZusatzFunktion3  extra button 3 (Xbox button) */
    RIG_WHEEL_FFB_MODE,  /* FFBMode          0 = the game's wheel programs, 1 = own FFB */
    RIG_WHEEL_CLUTCH_BRAKE, /* ClutchAsBrake 0/1: the clutch pedal brakes */
    RIG_WHEEL_COUNT,
    RIG_TRAFFIC_LEVEL = RIG_WHEEL_COUNT, /* [Traffic] TrafficLevel   0..5 */
    RIG_TRAFFIC_ALL_DAY,                 /* [Traffic] RushHourAllDay 0/1 */
    RIG_WEATHER,                         /* [Traffic] Wetter         0..5 (weather.h) */
    RIG_KEY_FIRST,                       /* FH2_Keyboard.ini [Keyboard], keyboard_pad.h order */
    RIG_KEY_ENABLED = RIG_KEY_FIRST,     /* Aktiv                0/1, next game start */
    RIG_KEY_STEER,                       /* Lenkzeit             ms */
    RIG_KEY_RETURN,                      /* Rueckstellzeit       ms */
    RIG_KEY_PEDAL,                       /* Pedalzeit            ms */
    RIG_KEY_END,
    RIG_GRIP_FIRST = RIG_KEY_END,        /* FH2_ExtraOptions.ini [Gripline] */
    RIG_GRIP_NOTIFICATIONS = RIG_GRIP_FIRST, /* Notifications      0/1 */
    RIG_GRIP_STEERING,                   /* SteeringAnimation  0/1, next game start */
    RIG_GRIP_END,
    RIG_WHEEL_PROFILE = RIG_GRIP_END,     /* input profile: not stored, derived from wheel / keyboard / clutch-as-brake */
    RIG_TRAFFIC_PRESET,                  /* traffic preset: not stored, derived from level + rush hour */
    RIG_SETTING_COUNT
};

/* How a setting is shown in the menu. */
enum {
    RIG_KIND_TOGGLE,  /* SpinnerControl, ToggleChoices (0 = off, 1 = on) */
    RIG_KIND_TRAFFIC, /* SpinnerControl, TrafficChoices */
    RIG_KIND_PERCENT, /* Slider, value / 100 with the game's percent display */
    RIG_KIND_NUMBER,  /* Slider, value * scale with a plain number display */
    RIG_KIND_PROFILE, /* SpinnerControl, ProfileChoices */
    RIG_KIND_FUNCTION, /* SpinnerControl, FunctionChoices (free wheel buttons) */
    RIG_KIND_DENSITY,  /* SpinnerControl, DensityChoices (traffic presets) */
    RIG_KIND_WEATHER,  /* SpinnerControl, WeatherChoices */
    RIG_KIND_FFB_MODE, /* SpinnerControl, FfbModeChoices */
};

/* kinds shown as a list of choices (SpinnerControl) and the name of their collection */
int rig_kind_is_choice(unsigned kind);
const char *rig_kind_collection(unsigned kind);

#define RIG_PROFILE_COUNT 6 /* the last one starts the wheel setup assistant */
#define RIG_DENSITY_COUNT 6
#define RIG_WEATHER_COUNT 6
#define RIG_FFB_MODE_COUNT 2

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
/* [Gripline] values read from FH2_ExtraOptions.ini at the game start */
void rig_settings_gripline(int notifications, int steering_animation);
void rig_settings_flush(void);

/* Texts of the page (German or English, following the game language). */
const char *rig_setting_heading(unsigned char *game_image, unsigned id);
const char *rig_setting_description(unsigned char *game_image, unsigned id);
const wchar_t *rig_traffic_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_profile_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_density_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_weather_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_function_choice(unsigned char *game_image, unsigned index);
const wchar_t *rig_ffb_mode_choice(unsigned char *game_image, unsigned index);
/* Rows of the menu: rig_setting_order(0 .. rig_setting_menu_count() - 1) are setting ids.
   Settings that are not listed keep working from their INI files but are not shown. */
unsigned rig_setting_menu_count(void);
unsigned rig_setting_order(unsigned position);

/* Provided by the wheel bridge and the traffic watcher. */
int wheel_bridge_setting(unsigned id);
void wheel_bridge_set_setting(unsigned id, int value);
const wchar_t *wheel_bridge_ini_path(void);
/* Starts the setup assistant (cards ask for steering, pedals, buttons). 0 = no wheel running. */
int wheel_bridge_start_setup(void);
int traffic_setting(unsigned id);
void traffic_set_setting(unsigned id, int value);
#endif
