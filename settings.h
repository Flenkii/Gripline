#ifndef GRIPLINE_SETTINGS_H
#define GRIPLINE_SETTINGS_H
#include "gripline.h"

/* how a value in Gripline.ini is written and checked */
typedef enum {
    SETTING_BOOLEAN,       /* 0 or 1 */
    SETTING_TRAFFIC_LEVEL, /* 0..7 */
    SETTING_CENTIMETERS,   /* -100..100 */
    SETTING_CARD_COLOR,    /* 0..9 */
    SETTING_CARD_STYLE,    /* 0..2 */
    SETTING_MASK,          /* 0..65535 (bit sets and small numbers) */
    SETTING_NO_DAMAGE      /* 0..2 */
} SettingType;

typedef struct {
    const char *section;
    const char *key;
    const char *log_name; /* name printed in Gripline.log */
    size_t field_offset;  /* where the int lives in ModOptions */
    SettingType type;
} SettingDefinition;

extern const SettingDefinition setting_definitions[];
extern const size_t setting_count;

void settings_defaults(ModOptions *options);
/* Reads Gripline.ini of directory into options (defaults first); 0 when something was refused or skipped. */
int settings_read(const wchar_t *directory, ModOptions *options);
int settings_value(const ModOptions *options, const SettingDefinition *definition);
/* lines of Gripline.ini the last settings_read skipped (bad key or value); the rest still loaded */
unsigned settings_ignored_lines(void);
#endif
