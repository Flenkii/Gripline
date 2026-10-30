/* Gripline's settings rows (Pause > Settings > MODS).
 *
 * Holds the menu description of the keyboard, More-Traffic, cockpit and Gripline settings,
 * forwards changes to the live modules and persists them in the INI files:
 *   FH2_Keyboard.ini  [Keyboard]  Enabled, SteerTime, ReturnTime, PedalTime
 *   Gripline.ini      [Traffic]   TrafficLevel, RushHourAllDay
 *   Gripline.ini      [Gripline]  Notifications, SteeringAnimation, CockpitViewForward, CockpitViewUp, CardColor
 * Saving happens on a background thread shortly after the last change, by editing only the
 * affected lines (comments and every other key stay untouched). When the launcher changes
 * one of the files while the game runs, the new values are picked up within a second. */
#include "rig_settings.h"
#include "ini_edit.h"
#include "report.h"
#include "cockpit_view.h"
#include "ini_migrate.h"
#include "logging.h"
#include "support.h"
#include "game_language.h"
#include "keyboard_pad.h"
#include "notify.h"

#define TRAFFIC_SECTION "Traffic"
#define KEYBOARD_SECTION "Keyboard"
#define GRIPLINE_SECTION "Gripline"
#define EO_INI_LIMIT 8192u
#define FILE_LIMIT 16384u
#define SAVE_DELAY_MS 600u

static const RigSettingInfo infos[RIG_SETTING_COUNT] = {
    [RIG_TRAFFIC_LEVEL] = {"EoRigTrafficLevel", "TrafficLevel", 0, RIG_TRAFFIC_COUNT - 1, RIG_KIND_TRAFFIC, 0, 1,
                           NULL},
    [RIG_TRAFFIC_ALL_DAY] = {"EoRigRushHour", "RushHourAllDay", 0, 1, RIG_KIND_TOGGLE, 0, 1,
                             NULL},
    [RIG_TRAFFIC_PRESET] = {"EoRigDensity", "", 0, RIG_DENSITY_COUNT - 1, RIG_KIND_DENSITY, 0, 1,
                            NULL},
    [RIG_KEY_ENABLED] = {"EoKeyEnabled", "Enabled", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_KEY_STEER] = {"EoKeySteer", "SteerTime", 0, 1000, RIG_KIND_NUMBER, 0, 1, "EO_KeyTime"},
    [RIG_KEY_RETURN] = {"EoKeyReturn", "ReturnTime", 0, 1000, RIG_KIND_NUMBER, 0, 1,
                        "EO_KeyTime"},
    [RIG_KEY_PEDAL] = {"EoKeyPedal", "PedalTime", 0, 1000, RIG_KIND_NUMBER, 0, 1, "EO_KeyTime"},
    [RIG_GRIP_NOTIFICATIONS] = {"EoGripNotify", "Notifications", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_GRIP_STEERING] = {"EoGripSteering", "SteeringAnimation", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_GRIP_REPORT] = {"EoGripReport", "", 0, 1, RIG_KIND_TOGGLE, 0, 1, NULL},
    [RIG_GRIP_VIEW_FORWARD] = {"EoGripViewForward", "CockpitViewForward", -20, 80, RIG_KIND_NUMBER, 0, 1, "EO_GripViewForward"},
    [RIG_GRIP_VIEW_UP] = {"EoGripViewUp", "CockpitViewUp", -20, 30, RIG_KIND_NUMBER, 0, 1, "EO_GripViewUp"},
    [RIG_GRIP_CARD_COLOR] = {"EoGripCardColor", "CardColor", 0, NOTIFY_COLOR_COUNT - 1, RIG_KIND_COLOR, 0, 1, NULL},
};

/* Rows of the menu. */
static const unsigned char order[] = {
    RIG_KEY_ENABLED,     RIG_KEY_STEER,     RIG_KEY_RETURN,     RIG_KEY_PEDAL,
    RIG_TRAFFIC_ALL_DAY, /* preset and amount: the Traffic page (plugin), with names instead of numbers */
    RIG_GRIP_STEERING,   RIG_GRIP_VIEW_FORWARD, RIG_GRIP_VIEW_UP, RIG_GRIP_NOTIFICATIONS, RIG_GRIP_CARD_COLOR,
    RIG_GRIP_REPORT,
};
_Static_assert(sizeof(order) <= RIG_SETTING_COUNT, "menu rows");

int rig_kind_is_choice(unsigned kind) {
    return kind == RIG_KIND_TOGGLE || kind == RIG_KIND_TRAFFIC || kind == RIG_KIND_DENSITY || kind == RIG_KIND_COLOR;
}

const char *rig_kind_collection(unsigned kind) {
    switch (kind) {
        case RIG_KIND_TRAFFIC: return "TrafficChoices";
        case RIG_KIND_DENSITY: return "DensityChoices";
        case RIG_KIND_COLOR: return "ColorChoices";
    }
    return "ToggleChoices";
}

unsigned rig_setting_menu_count(void) {
    return (unsigned)sizeof(order);
}

unsigned rig_setting_order(unsigned position) {
    return position < sizeof(order) ? order[position] : 0;
}

/* ------------------------------------------------------------------ traffic presets */

/* Traffic presets: whichever one matches TrafficLevel + RushHourAllDay is shown. */
static const unsigned char density_presets[RIG_DENSITY_COUNT][2] = {
    {0, 0}, /* 0 = own values (not a preset) */
    {0, 0}, /* 1 = Original */
    {5, 1}, /* 2 = Deutlich mehr Verkehr (the most the game spawns), all day */
    {6, 0}, /* 3 = Kein Verkehr */
    {7, 1}, /* 4 = Nur Drivatare */
};

static int density_current(void) {
    for (unsigned p = 1; p < RIG_DENSITY_COUNT; ++p) {
        if (traffic_setting(RIG_TRAFFIC_LEVEL) == density_presets[p][0] &&
            traffic_setting(RIG_TRAFFIC_ALL_DAY) == density_presets[p][1]) {
            return (int)p;
        }
    }
    return 0;
}

const RigSettingInfo *rig_setting_info(unsigned id) {
    return id < RIG_SETTING_COUNT ? &infos[id] : NULL;
}

/* ------------------------------------------------------------------ texts */

typedef struct {
    const char *heading, *description;
} RigText;

static const RigText german[RIG_SETTING_COUNT] = {
    [RIG_TRAFFIC_LEVEL] = {"Verkehr: Menge",
                           "Höchstzahl der Verkehrsautos (Kein Verkehr = offline fährt niemand, Nur Drivatare = statt Verkehr viele Drivatare). Wird sofort gespeichert, wirkt aber erst "
                           "nach einem Neustart von Azimuth (Spiel ganz schließen und neu starten)."},
    [RIG_TRAFFIC_ALL_DAY] = {"Verkehr: Rushhour rund um die Uhr",
                             "Volle Verkehrsdichte zu jeder Uhrzeit statt nur von 7 bis 19 Uhr. "
                             "Wirkt sofort."},
    [RIG_TRAFFIC_PRESET] = {"Verkehr: Voreinstellung",
                            "Stellt Menge und Rushhour mit einem Klick ein: Original, Deutlich mehr "
                            "Verkehr (so viel das Spiel erzeugt, rund um die Uhr), Kein Verkehr oder Nur Drivatare. Änderst du danach Menge oder Rushhour, steht hier Eigene "
                            "Werte. Wird sofort gespeichert, die Menge wirkt aber erst nach einem "
                            "Neustart von Azimuth."},
    [RIG_KEY_ENABLED] = {"Tastatur: Tastatur",
                         "Die Tastatur steuert das Spiel wie ein Gamepad: W/S Gas und Bremse, A/D "
                         "lenken, Leertaste Handbremse, E/Q schalten, Pfeiltasten = Kamera beim "
                         "Fahren und Blättern in Menüs, Enter/Rücktaste/Esc in Menüs. Belegung "
                         "in FH2_Keyboard.ini. Controller funktionieren weiter. Wirkt nach einem "
                         "Neustart des Spiels."},
    [RIG_KEY_STEER] = {"Tastatur: Lenkzeit (ms)",
                       "So lange dauert es bis zum vollen Lenkeinschlag, wenn du A oder D hältst. "
                       "Höher = sanfter. Wirkt sofort."},
    [RIG_KEY_RETURN] = {"Tastatur: Rückstellzeit (ms)",
                        "So lange dauert es, bis die Lenkung nach dem Loslassen wieder gerade steht. "
                        "Wirkt sofort."},
    [RIG_KEY_PEDAL] = {"Tastatur: Pedalzeit (ms)",
                       "So lange dauert es bis Vollgas oder Vollbremsung mit W oder S. 0 = sofort. "
                       "Wirkt sofort."},
    [RIG_GRIP_STEERING] = {"Cockpit: Volle Lenkrad-Animation",
                           "Im Cockpit sind die Hände ausgeblendet und das Lenkrad dreht so weit "
                           "wie dein echtes (Drehwinkel aus dem Azimuth-Launcher). Aus = Original-Hände und -Lenkrad. Wirkt "
                           "nach einem Neustart des Spiels."},
    [RIG_GRIP_NOTIFICATIONS] = {"Gripline: Benachrichtigungen",
                                "Kleine Gripline-Karten oben rechts im Bild, z. B. wenn etwas "
                                "gespeichert wurde oder Nitro online aus ist. Auch im Vollbild. "
                                "Wirkt sofort."},
    [RIG_GRIP_VIEW_FORWARD] = {"Cockpit-Ansicht 2: vor (cm)",
                            "Zweite Cockpit-Ansicht wie in FH5: so viele Zentimeter weiter vorne am "
                            "Tacho. Kamera-Taste: Cockpit, Cockpit 2, nächste Kamera (auch V). "
                            "Das Lenkrad verschwindet dort. "
                            "Wirkt sofort."},
    [RIG_GRIP_VIEW_UP] = {"Cockpit-Ansicht 2: hoch (cm)",
                          "So viele Zentimeter sitzt du in der zweiten Cockpit-Ansicht höher "
                          "(negativ = tiefer). Wirkt sofort."},
    [RIG_GRIP_CARD_COLOR] = {"Gripline: Kartenfarbe",
                             "Farbe der Gripline-Karten (Streifen, Logo und Schrift) und der Tastenliste. "
                             "Wirkt sofort."},
    [RIG_GRIP_REPORT] = {"Gripline: Fehlerbericht erstellen",
                         "Auf An stellen: packt Logs und Einstellungen in Gripline_Report.zip "
                         "neben azimuth.exe. Die Datei bei einem Problem auf GitHub unter Issues "
                         "anhängen. Es wird nichts verschickt."},
};

static const RigText english[RIG_SETTING_COUNT] = {
    [RIG_TRAFFIC_LEVEL] = {"Traffic: Amount",
                           "Maximum number of traffic cars (No Traffic = nobody drives, offline; Drivatars Only = many Drivatars instead of traffic). Saved immediately, but only applies after "
                           "restarting Azimuth (close the game completely and start it again)."},
    [RIG_TRAFFIC_ALL_DAY] = {"Traffic: Rush Hour All Day",
                             "Full traffic density at any time of day instead of 7:00-19:00 only. "
                             "Applies immediately."},
    [RIG_TRAFFIC_PRESET] = {"Traffic: Preset",
                            "Sets amount and rush hour in one go: Original, Much More Traffic (as "
                            "much as the game spawns, all day), No Traffic or Drivatars Only. Changing amount or "
                            "rush hour afterwards shows Custom. Saved immediately, the amount only "
                            "applies after restarting Azimuth."},
    [RIG_KEY_ENABLED] = {"Keyboard: Keyboard",
                         "The keyboard controls the game like a gamepad: W/S throttle and brake, "
                         "A/D steer, Space handbrake, E/Q shift, arrow keys = camera while driving "
                         "and scrolling in menus, Enter/Backspace/Esc in menus. Bindings in "
                         "FH2_Keyboard.ini. Controllers keep working. Restart the game to "
                         "apply."},
    [RIG_KEY_STEER] = {"Keyboard: Steering Time (ms)",
                       "Time to full lock while you hold A or D. Higher = smoother. Applies "
                       "immediately."},
    [RIG_KEY_RETURN] = {"Keyboard: Return Time (ms)",
                        "Time until the steering is straight again after letting go. Applies "
                        "immediately."},
    [RIG_KEY_PEDAL] = {"Keyboard: Pedal Time (ms)",
                       "Time to full throttle or full brake with W or S. 0 = instant. Applies "
                       "immediately."},
    [RIG_GRIP_STEERING] = {"Cockpit: Full Steering Animation",
                           "In the cockpit the hands are hidden and the steering wheel turns as "
                           "far as your real one (rotation set in the Azimuth launcher). Off = original hands and wheel. "
                           "Restart the game to apply."},
    [RIG_GRIP_NOTIFICATIONS] = {"Gripline: Notifications",
                                "Small Gripline cards at the top right of the picture, e.g. when "
                                "something was saved or nitro is off online. Also in fullscreen. "
                                "Applies immediately."},
    [RIG_GRIP_VIEW_FORWARD] = {"Cockpit View 2: Forward (cm)",
                            "Second cockpit view like in FH5: this many centimetres closer to the "
                            "gauges. Camera button: cockpit, cockpit 2, next camera (also V). "
                            "The steering wheel disappears there. "
                            "Applies immediately."},
    [RIG_GRIP_VIEW_UP] = {"Cockpit View 2: Up (cm)",
                          "You sit this many centimetres higher in the second cockpit view "
                          "(negative = lower). Applies immediately."},
    [RIG_GRIP_CARD_COLOR] = {"Gripline: Card Colour",
                             "Colour of the Gripline cards (bar, logo and title) and of the key list. "
                             "Applies immediately."},
    [RIG_GRIP_REPORT] = {"Gripline: Create Problem Report",
                         "Set to On: packs logs and settings into Gripline_Report.zip next to "
                         "azimuth.exe. Attach that file to a GitHub issue when something goes "
                         "wrong. Nothing is sent anywhere."},
};

static const wchar_t *const density_german[RIG_DENSITY_COUNT] = {
    L"Eigene Werte", L"Original", L"Deutlich mehr Verkehr", L"Kein Verkehr", L"Nur Drivatare"};
static const wchar_t *const density_english[RIG_DENSITY_COUNT] = {
    L"Custom", L"Original", L"Much More Traffic", L"No Traffic", L"Drivatars Only"};

const wchar_t *rig_density_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_DENSITY_COUNT) {
        return L"";
    }
    return game_language_is_german(game_image) ? density_german[index]
                                                     : density_english[index];
}

static const wchar_t *const color_german[NOTIFY_COLOR_COUNT] = {
    L"Blau (Original)", L"Rot", L"Grün", L"Lila", L"Orange", L"Gelb", L"Pink", L"Weiß"};
static const wchar_t *const color_english[NOTIFY_COLOR_COUNT] = {
    L"Blue (original)", L"Red", L"Green", L"Purple", L"Orange", L"Yellow", L"Pink", L"White"};

const wchar_t *rig_color_choice(unsigned char *game_image, unsigned index) {
    if (index >= NOTIFY_COLOR_COUNT) return L"";
    return game_language_is_german(game_image) ? color_german[index] : color_english[index];
}

static const wchar_t *const traffic_german[RIG_TRAFFIC_COUNT] = {L"Original (~14)", L"30 Autos", L"45 Autos",
                                                  L"70 Autos",       L"100 Autos", L"200 Autos", L"Kein Verkehr", L"Nur Drivatare"};
static const wchar_t *const traffic_english[RIG_TRAFFIC_COUNT] = {L"Original (~14)", L"30 Cars", L"45 Cars",
                                                   L"70 Cars",        L"100 Cars", L"200 Cars", L"No Traffic", L"Drivatars Only"};

static const RigText *texts(unsigned char *game_image) {
    return game_language_is_german(game_image) ? german : english;
}

const char *rig_setting_heading(unsigned char *game_image, unsigned id) {
    return id < RIG_SETTING_COUNT ? texts(game_image)[id].heading : "";
}

const char *rig_setting_description(unsigned char *game_image, unsigned id) {
    return id < RIG_SETTING_COUNT ? texts(game_image)[id].description : "";
}

const wchar_t *rig_traffic_choice(unsigned char *game_image, unsigned index) {
    if (index >= RIG_TRAFFIC_COUNT) {
        return L"";
    }
    return game_language_is_german(game_image) ? traffic_german[index]
                                                     : traffic_english[index];
}

/* ------------------------------------------------------------------ values */

static wchar_t traffic_ini[PATH_CAPACITY];
static volatile LONG64 dirty_traffic, dirty_keyboard, dirty_gripline;
static volatile ULONGLONG save_after;
static SRWLOCK file_lock = SRWLOCK_INIT;
static int started;

static int is_keyboard(unsigned id) {
    return id >= RIG_KEY_FIRST && id < RIG_KEY_END;
}

static int is_gripline(unsigned id) {
    return id >= RIG_GRIP_FIRST && id < RIG_GRIP_END;
}

static volatile LONG gripline_values[RIG_GRIP_END - RIG_GRIP_FIRST] = {1, 0, 35, -5, NOTIFY_COLOR_BLUE};

void rig_settings_gripline(int notifications, int steering_animation, int view_forward, int view_up, int card_color) {
    gripline_values[RIG_GRIP_NOTIFICATIONS - RIG_GRIP_FIRST] = notifications ? 1 : 0;
    gripline_values[RIG_GRIP_STEERING - RIG_GRIP_FIRST] = steering_animation ? 1 : 0;
    gripline_values[RIG_GRIP_VIEW_FORWARD - RIG_GRIP_FIRST] = view_forward;
    gripline_values[RIG_GRIP_VIEW_UP - RIG_GRIP_FIRST] = view_up;
    gripline_values[RIG_GRIP_CARD_COLOR - RIG_GRIP_FIRST] =
        card_color >= 0 && card_color < NOTIFY_COLOR_COUNT ? card_color : NOTIFY_COLOR_BLUE;
    cockpit_view_set_offset(view_forward, view_up);
    notify_set_color((int)gripline_values[RIG_GRIP_CARD_COLOR - RIG_GRIP_FIRST]);
}

static int stored_value(unsigned id) {
    if (is_keyboard(id)) {
        return keyboard_setting(id - RIG_KEY_FIRST);
    }
    if (is_gripline(id)) {
        return (int)gripline_values[id - RIG_GRIP_FIRST];
    }
    return traffic_setting(id);
}

static volatile LONG64 *dirty_mask(unsigned id) {
    return is_keyboard(id) ? &dirty_keyboard : is_gripline(id) ? &dirty_gripline : &dirty_traffic;
}

int rig_setting_get(unsigned id) {
    if (id >= RIG_SETTING_COUNT) {
        return 0;
    }
    if (id == RIG_TRAFFIC_PRESET) {
        return density_current();
    }
    if (id == RIG_GRIP_REPORT) {
        return 0;
    }
    int value = stored_value(id);
    /* the INI may hold values outside the menu range; show them clamped */
    if (value < infos[id].minimum) {
        value = infos[id].minimum;
    }
    if (value > infos[id].maximum) {
        value = infos[id].maximum;
    }
    return value;
}

static void forward(unsigned id, int value) {
    if (is_keyboard(id)) {
        keyboard_set_setting(id - RIG_KEY_FIRST, value);
    } else if (is_gripline(id)) {
        LONG before = InterlockedExchange(&gripline_values[id - RIG_GRIP_FIRST], value);
        (void)before;
        if (id == RIG_GRIP_NOTIFICATIONS) {
            notify_set_enabled(value);
        } else if (id == RIG_GRIP_VIEW_FORWARD || id == RIG_GRIP_VIEW_UP) {
            cockpit_view_set_offset((int)gripline_values[RIG_GRIP_VIEW_FORWARD - RIG_GRIP_FIRST],
                                    (int)gripline_values[RIG_GRIP_VIEW_UP - RIG_GRIP_FIRST]);
        } else if (id == RIG_GRIP_CARD_COLOR) {
            notify_set_color(value);
        }
    } else {
        traffic_set_setting(id, value);
    }
}

/* ------------------------------------------------------------------ confirmation cards */

static unsigned char *card_game;
static int card_quiet; /* the preset sets level and rush hour: one card for the preset */

void rig_settings_set_game(unsigned char *game_image) {
    card_game = game_image;
}

static void card_append(char *out, size_t capacity, size_t *used, const char *text) {
    while (*text && *used + 1 < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void card_append_wide(char *out, size_t capacity, size_t *used, const wchar_t *text) {
    char utf8[96];
    int n = text ? WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL) : 0;
    if (n > 0) card_append(out, capacity, used, utf8);
}

/* "Verkehr: Voreinstellung" / "Rushhour" - the setting's heading and its new value, in the game
   language. A tagged card: sliding through values replaces the text instead of stacking cards. */
static void confirm_text(unsigned id, int value, char *text, size_t capacity) {
    int de = game_language_is_german(card_game);
    char number[12];
    size_t used = 0;
    text[0] = 0;
    card_append(text, capacity, &used, rig_setting_heading(card_game, id));
    card_append(text, capacity, &used, "\n");
    switch (infos[id].kind) {
        case RIG_KIND_TOGGLE:
            card_append(text, capacity, &used, value ? (de ? "An" : "On") : (de ? "Aus" : "Off"));
            break;
        case RIG_KIND_TRAFFIC: card_append_wide(text, capacity, &used, rig_traffic_choice(card_game, (unsigned)value)); break;
        case RIG_KIND_DENSITY: card_append_wide(text, capacity, &used, rig_density_choice(card_game, (unsigned)value)); break;
        case RIG_KIND_COLOR: card_append_wide(text, capacity, &used, rig_color_choice(card_game, (unsigned)value)); break;
        default: card_append(text, capacity, &used, ini_format_int(value, number)); break;
    }
    if (id == RIG_KEY_ENABLED || id == RIG_GRIP_STEERING || id == RIG_TRAFFIC_LEVEL || id == RIG_TRAFFIC_PRESET)
        card_append(text, capacity, &used, de ? " - nach Neustart" : " - after a restart");
}

static void confirm_change(unsigned id, int value) {
    if (card_quiet || !card_game) return;
    char text[192];
    confirm_text(id, value, text, sizeof(text));
    notify_show_step(text, 0x5e7000u + id, 3000);
}

int rig_setting_set(unsigned id, int value) {
    if (id >= RIG_SETTING_COUNT || value < infos[id].minimum || value > infos[id].maximum) {
        return 0;
    }
    if (id == RIG_GRIP_REPORT) { /* an action, not a setting: the row goes back to Off */
        if (value == 1) report_create();
        return 0;
    }
    if (id == RIG_TRAFFIC_PRESET) {
        if (value == 0 || value == density_current()) {
            return 0;
        }
        card_quiet = 1;
        int changed = rig_setting_set(RIG_TRAFFIC_LEVEL, density_presets[value][0]);
        changed |= rig_setting_set(RIG_TRAFFIC_ALL_DAY, density_presets[value][1]);
        card_quiet = 0;
        if (changed) confirm_change(id, value);
        return changed;
    }
    int current = stored_value(id);
    if (current == value) {
        return 0;
    }
    forward(id, value);
    confirm_change(id, value);
    save_after = GetTickCount64() + SAVE_DELAY_MS;
    InterlockedOr64(dirty_mask(id), (LONG64)(1ull << id));
    return 1;
}

/* ------------------------------------------------------------------ files */

static char file_buffer[FILE_LIMIT + 1], edit_buffer[2][FILE_LIMIT + 1];
static wchar_t temp_path[PATH_CAPACITY];

static int read_file(const wchar_t *path, char *buffer, size_t capacity, size_t *length,
                     int *missing) {
    *length = 0;
    *missing = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *missing = GetLastError() == ERROR_FILE_NOT_FOUND;
        return *missing;
    }
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
             (ULONGLONG)size.QuadPart < capacity &&
             ReadFile(file, buffer, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(file);
    if (ok) {
        *length = got;
        buffer[got] = 0;
    }
    return ok;
}

static int modified_time(const wchar_t *path, ULONGLONG *time) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!path || !path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        *time = 0;
        return 0;
    }
    *time = ((ULONGLONG)data.ftLastWriteTime.dwHighDateTime << 32) |
            data.ftLastWriteTime.dwLowDateTime;
    return 1;
}

static int write_file(const wchar_t *path, const char *data, size_t length) {
    size_t n = 0;
    while (path[n] && n < PATH_CAPACITY - 8) {
        temp_path[n] = path[n];
        ++n;
    }
    static const wchar_t suffix[] = L".eotmp";
    for (unsigned i = 0; i < ARRAY_COUNT(suffix); ++i) {
        temp_path[n + i] = suffix[i];
    }
    HANDLE file = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length &&
             FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) {
        ok = MoveFileExW(temp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) {
        DeleteFileW(temp_path);
    }
    return ok;
}

typedef struct {
    const wchar_t *path;
    const char *section;
    unsigned first, end; /* setting ids stored in this file */
    size_t limit;
    int create_missing;
    ULONGLONG known_time;
    const char *name;
    int error_logged;
} RigFile;

static RigFile files[3] = {
    /* never create Gripline.ini here: its absence means "all defaults" */
    {traffic_ini, TRAFFIC_SECTION, RIG_TRAFFIC_LEVEL, RIG_TRAFFIC_END, EO_INI_LIMIT, 0, 0, "Gripline.ini", 0},
    {NULL, KEYBOARD_SECTION, RIG_KEY_FIRST, RIG_KEY_END, FILE_LIMIT, 1, 0, "FH2_Keyboard.ini", 0},
    {traffic_ini, GRIPLINE_SECTION, RIG_GRIP_FIRST, RIG_GRIP_END, EO_INI_LIMIT, 0, 0, "Gripline.ini", 0},
};

static void log_line(const char *first, const char *second);

/* A failed save is retried a few seconds later; it is only logged once. */
static int save_failed(RigFile *f, const char *message) {
    if (!f->error_logged) {
        f->error_logged = 1;
        log_line(message, f->name);
    }
    return 0;
}

static void log_line(const char *first, const char *second) {
    char line[160];
    size_t n = 0;
    for (const char *p = first; *p && n < sizeof(line) - 4; ++p) line[n++] = *p;
    for (const char *p = second; *p && n < sizeof(line) - 4; ++p) line[n++] = *p;
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    log_append(line);
}

/* Writes the current values of the dirty settings into the file. */
static int save_file(RigFile *f, LONG64 dirty) {
    if (!f->path || !f->path[0] || !dirty) {
        return 1;
    }
    size_t length;
    int missing;
    if (!read_file(f->path, file_buffer, f->limit + 1, &length, &missing)) {
        return save_failed(f, "MENU: could not read (retrying) ");
    }
    if (missing && !f->create_missing) {
        return 1;
    }
    const char *source = file_buffer;
    size_t source_length = length;
    unsigned slot = 0;
    for (unsigned id = f->first; id < f->end; ++id) {
        if (!(dirty & (LONG64)(1ull << id))) {
            continue;
        }
        char number[12];
        int value = stored_value(id);
        size_t written = ini_set_value(source, source_length, f->section, infos[id].key,
                                       ini_format_int(value, number), edit_buffer[slot],
                                       f->limit + 1);
        if (!written || written > f->limit) {
            log_line("MENU: not saved, the file would become too large: ", f->name);
            return 1; /* permanent: do not retry */
        }
        source = edit_buffer[slot];
        source_length = written;
        slot ^= 1;
    }
    if (source == file_buffer) {
        return 1;
    }
    if (!write_file(f->path, source, source_length)) {
        return save_failed(f, "MENU: could not save (retrying) ");
    }
    modified_time(f->path, &f->known_time);
    if (f->error_logged) {
        f->error_logged = 0;
        log_line("MENU: saved again: ", f->name);
    }
    return 1;
}

/* The launcher (or an editor) changed the file: take over its values, except for settings
   that were just changed in the menu and still wait to be saved. */
static void reload_file(RigFile *f, LONG64 pending) {
    size_t length;
    int missing;
    if (!f->path || !read_file(f->path, file_buffer, FILE_LIMIT + 1, &length, &missing) ||
        missing) {
        return;
    }
    int changed = 0;
    for (unsigned id = f->first; id < f->end; ++id) {
        int value;
        if ((pending & (LONG64)(1ull << id)) ||
            !ini_find_int(file_buffer, length, f->section, infos[id].key, &value) ||
            value < infos[id].minimum || value > infos[id].maximum) {
            continue;
        }
        int current = stored_value(id);
        if (current != value) {
            forward(id, value);
            changed = 1;
        }
    }
    if (changed) {
        log_line("MENU: values reloaded from ", f->name);
    }
}

static ULONGLONG last_check;

static void saver_step(ULONGLONG now) {
    AcquireSRWLockExclusive(&file_lock);
    volatile LONG64 *masks[3] = {&dirty_traffic, &dirty_keyboard, &dirty_gripline};
    _Static_assert(ARRAY_COUNT(files) == 3, "one dirty mask per file");
    if ((dirty_traffic || dirty_keyboard || dirty_gripline) && now >= save_after) {
        for (unsigned i = 0; i < ARRAY_COUNT(files); ++i) {
            LONG64 dirty = InterlockedExchange64(masks[i], 0);
            if (!save_file(&files[i], dirty)) {
                InterlockedOr64(masks[i], dirty);
                save_after = now + 5000; /* retry later, e.g. while the file is locked */
            }
        }
    }
    if (now - last_check >= 1000) {
        last_check = now;
        for (unsigned i = 0; i < ARRAY_COUNT(files); ++i) {
            ULONGLONG time;
            LONG64 pending = *masks[i];
            if (modified_time(files[i].path, &time) && time != files[i].known_time) {
                files[i].known_time = time;
                reload_file(&files[i], pending);
            }
        }
    }
    ReleaseSRWLockExclusive(&file_lock);
}

static DWORD WINAPI saver_thread(LPVOID unused) {
    (void)unused;
    for (;;) {
        Sleep(100);
        saver_step(GetTickCount64());
    }
    return 0;
}

/* Process exit: save changes from the last moments (the saver thread is gone already). */
void rig_settings_flush(void) {
    if (!started) {
        return;
    }
    save_file(&files[0], InterlockedExchange64(&dirty_traffic, 0));
    save_file(&files[1], InterlockedExchange64(&dirty_keyboard, 0));
    save_file(&files[2], InterlockedExchange64(&dirty_gripline, 0));
}

void rig_settings_start(const wchar_t *settings_directory) {
    if (started || !settings_directory ||
        !path_join(traffic_ini, settings_directory, MOD_INI_NAME)) {
        return;
    }
    files[1].path = keyboard_ini_path();
    for (unsigned i = 0; i < ARRAY_COUNT(files); ++i) {
        modified_time(files[i].path, &files[i].known_time);
    }
    HANDLE thread = CreateThread(NULL, 0, saver_thread, NULL, 0, NULL);
    if (!thread) {
        log_append("MENU: Gripline - could not start the save thread; menu changes stay "
                   "until the game closes.\r\n");
        return;
    }
    CloseHandle(thread);
    started = 1;
    log_append("MENU: Gripline rows ready (Mods page); changes apply live and are "
               "saved to FH2_Keyboard.ini / Gripline.ini.\r\n");
}

#ifdef FH2_TWEAKS_TEST
#include "features.h"
#include "settings.h"
__declspec(dllexport) int Fh2TestRigStart(unsigned char *image, const wchar_t *executable,
                                          const wchar_t *directory) {
    ModOptions options;
    ini_migrate(directory);
    int ok = settings_read(directory, &options);
    log_set_directory(directory);
    traffic_tunables_start(image, &options);
    (void)executable;
    keyboard_pad_game_start(image, directory);
    rig_settings_start(directory);
    return ok ? (started ? 1 : 2) : 0;
}
__declspec(dllexport) void Fh2TestRigStep(ULONGLONG now) {
    saver_step(now);
}
__declspec(dllexport) void Fh2TestRigFlush(void) {
    rig_settings_flush();
}
__declspec(dllexport) int Fh2TestRigGet(unsigned id) {
    return rig_setting_get(id);
}
__declspec(dllexport) int Fh2TestRigSet(unsigned id, int value) {
    return rig_setting_set(id, value);
}
__declspec(dllexport) void Fh2TestConfirmText(unsigned id, int value, char *out, unsigned size) {
    confirm_text(id, value, out, size);
}
#endif
