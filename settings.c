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
};

const size_t setting_count = ARRAY_COUNT(setting_definitions);
static char ini_buffer[INI_MAX_SIZE + 1];
static wchar_t ini_path[PATH_CAPACITY];

void settings_defaults(ModOptions *options) {
    *options = (ModOptions){
        .traffic_level = 4,
        .traffic_all_day = 1,
        .notifications = 1,
        .cockpit_view_forward = 35,
        .cockpit_view_up = -5,
        .card_style = 2, /* Overdrive */
    };
}

int settings_value(const ModOptions *options, const SettingDefinition *definition) {
    const unsigned char *fields = (const unsigned char *)options;
    return *(const int *)(fields + definition->field_offset);
}

static char *trim_whitespace(char *text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    size_t length = text_length(text);
    while (length &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) {
        text[--length] = 0;
    }
    return text;
}

static int known_section(const char *name) {
    for (size_t index = 0; index < setting_count; ++index) {
        if (text_equal_ignore_case(name, setting_definitions[index].section)) {
            return 1;
        }
    }
    return 0;
}

static const SettingDefinition *find_setting(const char *section, const char *key) {
    for (size_t index = 0; index < setting_count; ++index) {
        const SettingDefinition *definition = &setting_definitions[index];
        if (text_equal_ignore_case(section, definition->section) &&
            text_equal_ignore_case(key, definition->key)) {
            return definition;
        }
    }
    return NULL;
}

static int parse_value(const SettingDefinition *definition, const char *text, int *value) {
    if (definition->type == SETTING_CENTIMETERS) { /* -100 .. 100 */
        const char *digit = text[0] == '-' ? text + 1 : text;
        int number = 0;
        if (!*digit) {
            return 0;
        }
        for (; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || number > 100) {
                return 0;
            }
            number = number * 10 + (*digit - '0');
        }
        if (number > 100) {
            return 0;
        }
        *value = text[0] == '-' ? -number : number;
        return 1;
    }
    if (definition->type == SETTING_TRAFFIC_LEVEL || definition->type == SETTING_CARD_COLOR) {
        if (text[0] >= '0' && text[0] <= '7' && !text[1]) {
            *value = text[0] - '0';
            return 1;
        }
        return 0;
    }
    if (definition->type == SETTING_CARD_STYLE) {
        if (text[0] >= '0' && text[0] <= '2' && !text[1]) {
            *value = text[0] - '0';
            return 1;
        }
        return 0;
    }
    if (text_equal_ignore_case(text, "0") || text_equal_ignore_case(text, "1")) {
        *value = text[0] - '0';
        return 1;
    }
    return 0;
}

static int parse_ini(char *text, ModOptions *options) {
    char section[32] = {0};
    if ((unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
        (unsigned char)text[2] == 0xbf) {
        text += 3;
    }

    while (*text) {
        char *line = text;
        while (*text && *text != '\n') {
            ++text;
        }
        if (*text) {
            *text++ = 0;
        }
        for (char *cursor = line; *cursor; ++cursor) {
            if (*cursor == ';' || *cursor == '#') {
                *cursor = 0;
                break;
            }
        }
        line = trim_whitespace(line);
        if (!*line) {
            continue;
        }

        if (*line == '[') {
            char *closing = line;
            while (*closing && *closing != ']') {
                ++closing;
            }
            if (*closing != ']' || *trim_whitespace(closing + 1)) {
                goto invalid;
            }
            *closing = 0;
            char *name = trim_whitespace(line + 1);
            size_t length = text_length(name);
            if (length >= sizeof(section) || !known_section(name)) {
                goto invalid;
            }
            memory_copy(section, name, length + 1);
            continue;
        }

        char *separator = line;
        while (*separator && *separator != '=') {
            ++separator;
        }
        if (!*separator) {
            goto invalid;
        }
        *separator = 0;
        char *key = trim_whitespace(line);
        char *value = trim_whitespace(separator + 1);

        // Retired keys (V5: weather and time of day, now Extra Options) are ignored so an older INI still loads.
        if (text_equal_ignore_case(section, "Traffic") &&
            (text_equal_ignore_case(key, "Weather") || text_equal_ignore_case(key, "Wetter") ||
             text_equal_ignore_case(key, "TimeOfDay"))) {
            continue;
        }

        const SettingDefinition *definition = find_setting(section, key);
        int parsed;
        if (!definition || !parse_value(definition, value, &parsed)) {
            goto invalid;
        }
        int *field = (int *)((unsigned char *)options + definition->field_offset);
        *field = parsed;
    }
    return 1;

invalid:
    status_set("REFUSED: invalid INI section, option, or value. See the supplied "
               "Gripline.ini.");
    return 0;
}

int settings_read(const wchar_t *directory, ModOptions *options) {
    settings_defaults(options);
    if (!path_join(ini_path, directory, MOD_INI_NAME)) {
        status_set("REFUSED: INI path is too long.");
        return 0;
    }
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return 1;
        }
        status_set("REFUSED: cannot read Gripline.ini.");
        return 0;
    }
    LARGE_INTEGER size;
    DWORD bytes_read;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > INI_MAX_SIZE ||
        !ReadFile(file, ini_buffer, (DWORD)size.QuadPart, &bytes_read, NULL) ||
        bytes_read != size.QuadPart) {
        CloseHandle(file);
        status_set("REFUSED: INI read failed or file exceeds 8192 bytes.");
        return 0;
    }
    CloseHandle(file);
    ini_buffer[bytes_read] = 0;
    for (unsigned index = 0; index < bytes_read; ++index) {
        if (!ini_buffer[index]) {
            status_set("REFUSED: INI must use UTF-8 or ASCII, not UTF-16.");
            return 0;
        }
    }
    return parse_ini(ini_buffer, options);
}
