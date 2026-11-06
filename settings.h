#ifndef FH2_SETTINGS_H
#define FH2_SETTINGS_H

#include "gripline.h"

typedef enum { SETTING_BOOLEAN, SETTING_TRAFFIC_LEVEL, SETTING_CENTIMETERS, SETTING_CARD_COLOR, SETTING_CARD_STYLE, SETTING_MASK, SETTING_NO_DAMAGE } SettingType;

typedef struct {
    const char *section;
    const char *key;
    const char *log_name;
    size_t field_offset;
    SettingType type;
} SettingDefinition;

extern const SettingDefinition setting_definitions[];
extern const size_t setting_count;

void settings_defaults(ModOptions *options);
int settings_read(const wchar_t *directory, ModOptions *options);
int settings_value(const ModOptions *options, const SettingDefinition *definition);
/* lines of Gripline.ini the last settings_read skipped (bad key or value); the rest still loaded */
unsigned settings_ignored_lines(void);

#endif
