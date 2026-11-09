#include "settings.h"
#include "support.h"

#define INI_MAX_SIZE 8192
#define SETTING(section, key, field, type) {section, key, key, offsetof(ModOptions, field), type}

const SettingDefinition setting_definitions[] = {
    SETTING("Traffic", "TrafficLevel", traffic_level, SETTING_TRAFFIC_LEVEL),
    SETTING("Traffic", "RushHourAllDay", traffic_all_day, SETTING_BOOLEAN),
    SETTING("Gripline", "Notifications", notifications, SETTING_BOOLEAN),
    SETTING("Gripline", "SteeringAnimation", steering_animation, SETTING_BOOLEAN),
    SETTING("Gripline", "CockpitViewForward", cockpit_view_forward, SETTING_CENTIMETERS),
    SETTING("Gripline", "CockpitViewUp", cockpit_view_up, SETTING_CENTIMETERS),
    SETTING("Gripline", "CardColor", card_color, SETTING_CARD_COLOR),
    SETTING("Gripline", "CardStyle", card_style, SETTING_CARD_STYLE),
    SETTING("Gripline", "UpdateCheck", update_check, SETTING_BOOLEAN),
    SETTING("Gripline", "SpeedCameras", speed_cameras, SETTING_BOOLEAN),
    SETTING("Gripline", "CarLabels", car_labels, SETTING_BOOLEAN),
    SETTING("Gripline", "Telemetry", telemetry, SETTING_BOOLEAN),
    SETTING("Gripline", "TelemetryShow", telemetry_show, SETTING_MASK),
    SETTING("Gripline", "EventIcons", event_icons, SETTING_BOOLEAN),
    SETTING("Gripline", "NewsSeen", news_seen, SETTING_MASK),
    SETTING("Gripline", "NoDamage", no_damage, SETTING_NO_DAMAGE),
    SETTING("Gripline", "CardsHidden", cards_hidden, SETTING_MASK),
    SETTING("Gripline", "CruiseCrashOff", cruise_crash_off, SETTING_BOOLEAN),
    SETTING("Gripline", "CruiseMinSpeed", cruise_min_speed, SETTING_MASK),
    SETTING("Gripline", "CruiseSlowOff", cruise_slow_off, SETTING_BOOLEAN),
    SETTING("Gripline", "TelemetryPosition", telemetry_pos, SETTING_MASK),
    SETTING("Gripline", "TelemetryOpacity", telemetry_opacity, SETTING_MASK),
    SETTING("Gripline", "TelemetrySize", telemetry_size, SETTING_MASK),
    SETTING("Gripline", "WalkHandheld", walk_handheld, SETTING_MASK),
    SETTING("Gripline", "CalmDrivatars", calm_drivatars, SETTING_BOOLEAN),
    SETTING("Gripline", "WalkGauge", walk_gauge, SETTING_MASK),
    SETTING("Gripline", "SummonStyle", summon_style, SETTING_MASK),
    SETTING("Gripline", "SummonSounds", summon_sounds, SETTING_MASK),
};

const size_t setting_count = ARRAY_COUNT(setting_definitions);
static char ini_buffer[INI_MAX_SIZE + 1];
static wchar_t ini_path[PATH_CAPACITY];

void settings_defaults(ModOptions *options) {
    *options = (ModOptions){
        .traffic_level = 0, /* Original: 100 cars hung the first load on some PCs (V6.4.1) */
        .traffic_all_day = 0,
        .notifications = 1,
        .cockpit_view_forward = 35,
        .cockpit_view_up = -5,
        .card_style = 2, /* Overdrive */
        .update_check = 1,
        .speed_cameras = 1,
        .car_labels = 1,
        .event_icons = 1,
        .telemetry_show = -1, /* telemetry.c default */
        .walk_handheld = 2, /* medium (V7.1) */
        .summon_style = 1,  /* normal (V7.1) */
        .summon_sounds = 1, /* on (V7.1, Marvin 08.11.2026) */
        .walk_gauge = 3, /* step pulse (V7.1, Marvin 08.11.2026) */
        .cruise_crash_off = 1,
        .cruise_min_speed = 20,
        .cruise_slow_off = 1,
        .telemetry_opacity = 100,
        .telemetry_size = 100,
    };
}

int settings_value(const ModOptions *options, const SettingDefinition *definition) {
    int value;
    memory_copy(&value, (const unsigned char *)options + definition->field_offset, sizeof(value));
    return value;
}

/* ---------------------------------------------------------------- reading values */

/* digits only, no sign, no more than `limit`; returns 0 for anything else */
static int parse_count(const char *text, int limit, int *value) {
    if (!*text) return 0;
    int total = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return 0;
        total = total * 10 + (*text - '0');
        if (total > limit) return 0;
    }
    *value = total;
    return 1;
}

/* the highest allowed number of a type that is a single digit 0..top */
static int digit_top(SettingType type) {
    switch (type) {
    case SETTING_TRAFFIC_LEVEL: return 7;
    case SETTING_CARD_COLOR: return 9; /* 8 = black, 9 = rainbow (V7) */
    case SETTING_CARD_STYLE:
    case SETTING_NO_DAMAGE: return 2;
    default: return 1; /* on/off */
    }
}

static int parse_value(const SettingDefinition *definition, const char *text, int *value) {
    switch (definition->type) {
    case SETTING_CENTIMETERS: { /* -100 .. 100 */
        int negative = text[0] == '-';
        int magnitude;
        if (!parse_count(text + negative, 100, &magnitude)) return 0;
        *value = negative ? -magnitude : magnitude;
        return 1;
    }
    case SETTING_MASK: /* 0 .. 65535 */
        return parse_count(text, 65535, value);
    default: { /* one digit */
        int top = digit_top(definition->type);
        if (text[0] < '0' || text[0] > '0' + top || text[1]) return 0;
        *value = text[0] - '0';
        return 1;
    }
    }
}

/* ---------------------------------------------------------------- lines that were skipped */

static unsigned skipped_lines;

unsigned settings_ignored_lines(void) {
    return skipped_lines;
}

/* One bad line does not drop the file (V7): it is skipped and named in Gripline.log, the other
   settings still load. The first skip replaces "Not initialized" at the top of the log. */
static void skip_line(unsigned number, const char *what) {
    char note[128];
    size_t used = 0;
    const char *lead = skipped_lines ? ", line " : "IGNORED in Gripline.ini (other settings loaded): line ";
    while (*lead) note[used++] = *lead++;
    char digits[12];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number && count < sizeof(digits));
    while (count) note[used++] = digits[--count];
    note[used++] = ' ';
    note[used++] = '(';
    for (; *what && used + 3 < sizeof(note); ++what) note[used++] = *what;
    note[used++] = ')';
    note[used] = 0;
    ++skipped_lines;
    if (skipped_lines == 1) status_set(note);
    else if (skipped_lines <= 8) status_append(note);
}

/* ---------------------------------------------------------------- the INI text */

static char *strip(char *text) {
    while (*text == ' ' || *text == '\t') ++text;
    char *end = text + text_length(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
    return text;
}

static const SettingDefinition *lookup(const char *section, const char *key) {
    for (size_t i = 0; i < setting_count; ++i) {
        const SettingDefinition *entry = &setting_definitions[i];
        if (text_equal_ignore_case(section, entry->section) && (!key || text_equal_ignore_case(key, entry->key)))
            return entry;
    }
    return NULL;
}

/* keys that older versions wrote (weather and time of day moved to Extra Options in V5) */
static int retired_key(const char *section, const char *key) {
    if (!text_equal_ignore_case(section, "Traffic")) return 0;
    return text_equal_ignore_case(key, "Weather") || text_equal_ignore_case(key, "Wetter") ||
           text_equal_ignore_case(key, "TimeOfDay");
}

/* cut a line at the first ; or # */
static void drop_comment(char *line) {
    for (; *line; ++line) {
        if (*line == ';' || *line == '#') {
            *line = 0;
            return;
        }
    }
}

static int parse_ini(char *text, ModOptions *options) {
    char section[32] = {0};
    int in_known_section = 1;
    unsigned number = 0;
    skipped_lines = 0;

    if ((unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf) text += 3;

    while (*text) {
        char *line = text;
        ++number;
        while (*text && *text != '\n') ++text;
        if (*text) *text++ = 0;
        drop_comment(line);
        line = strip(line);
        if (!*line) continue;

        if (*line == '[') { /* [Section] */
            char *close = line;
            while (*close && *close != ']') ++close;
            if (*close != ']' || *strip(close + 1)) {
                skip_line(number, "section");
                in_known_section = 0;
                continue;
            }
            *close = 0;
            char *name = strip(line + 1);
            size_t length = text_length(name);
            if (length >= sizeof(section) || !lookup(name, NULL)) {
                skip_line(number, "unknown section");
                in_known_section = 0; /* the keys below it are skipped without a note */
                continue;
            }
            memory_copy(section, name, length + 1);
            in_known_section = 1;
            continue;
        }

        if (!in_known_section) continue;
        char *equals = line;
        while (*equals && *equals != '=') ++equals;
        if (!*equals) {
            skip_line(number, "no =");
            continue;
        }
        *equals = 0;
        char *key = strip(line);
        char *text_value = strip(equals + 1);
        if (retired_key(section, key)) continue;

        const SettingDefinition *entry = lookup(section, key);
        int value;
        if (!entry || !parse_value(entry, text_value, &value)) {
            skip_line(number, *key ? key : "?");
            continue;
        }
        memory_copy((unsigned char *)options + entry->field_offset, &value, sizeof(value));
    }
    return skipped_lines == 0;
}

/* ---------------------------------------------------------------- Gripline.ini on disk */

int settings_read(const wchar_t *directory, ModOptions *options) {
    settings_defaults(options);
    if (!path_join(ini_path, directory, MOD_INI_NAME)) {
        status_set("REFUSED: INI path is too long.");
        return 0;
    }
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            status_set("Gripline.ini not found - defaults used.");
            return 1;
        }
        status_set("REFUSED: cannot read Gripline.ini.");
        return 0;
    }
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart <= INI_MAX_SIZE &&
             ReadFile(file, ini_buffer, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(file);
    if (!ok) {
        status_set("REFUSED: INI read failed or file exceeds 8192 bytes.");
        return 0;
    }
    ini_buffer[got] = 0;
    for (DWORD i = 0; i < got; ++i) {
        if (!ini_buffer[i]) { /* UTF-16 text has zero bytes between the letters */
            status_set("REFUSED: INI must use UTF-8 or ASCII, not UTF-16.");
            return 0;
        }
    }
    status_set("Gripline.ini read.");
    return parse_ini(ini_buffer, options); /* a skipped line replaces this status (skip_line) */
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_settings.py: reads Gripline.ini of directory; values in setting_definitions order */
__declspec(dllexport) int Fh2TestSettingsRead(const wchar_t *directory, int *values, unsigned count,
                                              unsigned *ignored, char *status, unsigned status_size) {
    static ModOptions options;
    int ok = settings_read(directory, &options);
    for (unsigned i = 0; i < count && i < setting_count; ++i) values[i] = settings_value(&options, &setting_definitions[i]);
    *ignored = settings_ignored_lines();
    const char *s = status_get();
    unsigned n = 0;
    while (s[n] && n + 1 < status_size) status[n] = s[n], ++n;
    status[n] = 0;
    return ok;
}
#endif
