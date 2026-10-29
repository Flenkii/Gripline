/* Takes the German settings files of fork1-fork3 over into the English ones - see ini_migrate.h. */
#include "ini_migrate.h"
#include "ini_edit.h"
#include "logging.h"
#include "support.h"

#include <windows.h>

#define FILE_LIMIT 16384u
#define BACKUP_FOLDER L"FH2_Settings_Backup"

typedef struct {
    const char *old_key, *new_key;
} KeyName;

/* the first four are numbers, the rest key bindings */
#define KEYBOARD_NUMBERS 4
static const KeyName keyboard_keys[] = {
    {"Aktiv", "Enabled"},           {"Lenkzeit", "SteerTime"},
    {"Rueckstellzeit", "ReturnTime"}, {"Pedalzeit", "PedalTime"},
    {"Gas", "Throttle"},            {"Bremse", "Brake"},
    {"LenkenLinks", "SteerLeft"},   {"LenkenRechts", "SteerRight"},
    {"Handbremse", "Handbrake"},    {"Hochschalten", "ShiftUp"},
    {"Runterschalten", "ShiftDown"}, {"Kupplung", "Clutch"},
    {"Kamera", "Camera"},           {"Zurueckspulen", "Rewind"},
    {"Telemetrie", "Telemetry"},    {"Fotomodus", "PhotoMode"},
    {"RadioZurueck", "RadioPrevious"}, {"RadioWeiter", "RadioNext"},
    {"BlickZurueck", "LookBack"},   {"BlickLinks", "LookLeft"},
    {"BlickRechts", "LookRight"},   {"Licht", "Lights"},
    {"Karte", "Map"},               {"Pause", "Pause"},
    {"Bestaetigen", "Confirm"},     {"Zurueck", "Back"},
    {"TasteX", "ButtonX"},          {"TasteY", "ButtonY"},
    {"TabLinks", "TabLeft"},        {"TabRechts", "TabRight"},
    {"PfeilHoch", "ArrowUp"},       {"PfeilRunter", "ArrowDown"},
    {"PfeilLinks", "ArrowLeft"},    {"PfeilRechts", "ArrowRight"},
    {"SteuerkreuzHoch", "DpadUp"},  {"SteuerkreuzRunter", "DpadDown"},
    {"SteuerkreuzLinks", "DpadLeft"}, {"SteuerkreuzRechts", "DpadRight"},
};

/* German key names in bindings (all still understood, only translated for the reader) */
static const KeyName key_words[] = {
    {"Leertaste", "Space"},   {"Eingabe", "Enter"},     {"Umschalt", "Shift"},
    {"Strg", "Ctrl"},         {"LStrg", "LCtrl"},       {"RStrg", "RCtrl"},
    {"Rueck", "Backspace"},   {"Hoch", "Up"},           {"Runter", "Down"},
    {"Links", "Left"},        {"Rechts", "Right"},      {"BildHoch", "PageUp"},
    {"BildRunter", "PageDown"}, {"Pos1", "Home"},       {"Ende", "End"},
    {"Einfg", "Insert"},      {"Entf", "Delete"},       {"Komma", "Comma"},
    {"Punkt", "Period"},      {"Maus1", "Mouse1"},      {"MausLinks", "Mouse1"},
    {"Maus2", "Mouse2"},      {"MausRechts", "Mouse2"}, {"Maus3", "Mouse3"},
    {"MausMitte", "Mouse3"},  {"Maus4", "Mouse4"},      {"Maus5", "Mouse5"},
};

static char old_text[FILE_LIMIT + 1], edit[2][FILE_LIMIT + 1];
static char report[1024];
static size_t report_length;
static volatile LONG done;

static void note(const char *a, const char *b) {
    for (const char *p = a; *p && report_length + 3 < sizeof(report); ++p) report[report_length++] = *p;
    for (const char *p = b; p && *p && report_length + 3 < sizeof(report); ++p) report[report_length++] = *p;
    report[report_length++] = '\r';
    report[report_length++] = '\n';
    report[report_length] = 0;
}

static int lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int same_word(const char *a, size_t length, const char *b) {
    size_t i = 0;
    for (; i < length && b[i]; ++i)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    return i == length && !b[i];
}

static int exists(const wchar_t *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static int read_all(const wchar_t *path, char *out, size_t limit, size_t *length) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart <= (LONGLONG)limit &&
             ReadFile(file, out, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(file);
    if (!ok) return 0;
    out[got] = 0;
    *length = got;
    return 1;
}

/* new file only (never overwrites) */
static int write_new(const wchar_t *path, const char *data, size_t length) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) DeleteFileW(path);
    return ok;
}

/* replaces an existing file through a temporary file */
static wchar_t backup_dir[PATH_CAPACITY];

static int backup_path(wchar_t out[PATH_CAPACITY], const wchar_t *name) {
    if (!exists(backup_dir) && !CreateDirectoryW(backup_dir, NULL)) return 0;
    return path_join(out, backup_dir, name);
}

/* moves a file of the settings folder into the backup folder (an older copy there stays) */
static int move_to_backup(const wchar_t *directory, const wchar_t *name) {
    static wchar_t from[PATH_CAPACITY], to[PATH_CAPACITY];
    if (!path_join(from, directory, name) || !exists(from) || !backup_path(to, name)) return 0;
    if (exists(to)) return 0;
    return MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
}

/* "Hoch,Num8" -> "Up,Num8" */
static void translate_keys(const char *value, char *out, size_t capacity) {
    size_t n = 0;
    const char *p = value;
    while (*p && n + 1 < capacity) {
        if (*p == ',' || *p == ' ' || *p == ';') {
            out[n++] = *p++;
            continue;
        }
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != ';') ++p;
        size_t length = (size_t)(p - start);
        const char *word = NULL;
        for (unsigned i = 0; i < ARRAY_COUNT(key_words) && !word; ++i)
            if (same_word(start, length, key_words[i].old_key)) word = key_words[i].new_key;
        if (word) {
            for (const char *w = word; *w && n + 1 < capacity; ++w) out[n++] = *w;
        } else {
            for (size_t i = 0; i < length && n + 1 < capacity; ++i) out[n++] = start[i];
        }
    }
    out[n] = 0;
}

/* Builds the new file: default text + the values of the old file. Returns its length. */
static size_t fill(const char *template_text, size_t template_length, const char *old_section,
                   const char *new_section, const KeyName *names, unsigned count, unsigned bindings_from,
                   size_t old_length, const char **result) {
    const char *current = template_text;
    size_t length = template_length;
    unsigned slot = 0;
    for (unsigned i = 0; i < count; ++i) {
        char value[160], translated[200];
        if (!ini_find_text(old_text, old_length, old_section, names[i].old_key, value, sizeof(value))) continue;
        const char *v = value;
        if (i >= bindings_from) {
            translate_keys(value, translated, sizeof(translated));
            v = translated;
        }
        size_t n = ini_set_value(current, length, new_section, names[i].new_key, v, edit[slot], FILE_LIMIT + 1);
        if (!n) continue;
        current = edit[slot];
        length = n;
        slot ^= 1;
    }
    *result = current;
    return length;
}

static void migrate_file(const wchar_t *directory, const wchar_t *old_name, const wchar_t *new_name,
                         const char *old_section, const char *new_section, const KeyName *names,
                         unsigned count, unsigned bindings_from, const char *template_text,
                         size_t template_length, const char *label) {
    static wchar_t old_path[PATH_CAPACITY], new_path[PATH_CAPACITY];
    size_t old_length;
    if (!path_join(old_path, directory, old_name) || !path_join(new_path, directory, new_name)) return;
    if (exists(new_path) || !exists(old_path)) return;
    if (!template_length || !read_all(old_path, old_text, FILE_LIMIT, &old_length)) {
        note("SETTINGS: could not read the old file, please check it: ", label);
        return;
    }
    const char *text;
    size_t length = fill(template_text, template_length, old_section, new_section, names, count, bindings_from,
                         old_length, &text);
    if (!write_new(new_path, text, length)) {
        note("SETTINGS: could not create the English file for ", label);
        return;
    }
    if (move_to_backup(directory, old_name)) {
        note("SETTINGS: your settings were moved into the English file; the old file is in "
             "FH2_Settings_Backup: ", label);
    } else {
        note("SETTINGS: your settings were copied into the English file (old file left in place): ", label);
    }
}

void ini_migrate(const wchar_t *directory) {
    if (!directory || InterlockedExchange(&done, 1)) return;
    if (!path_join(backup_dir, directory, BACKUP_FOLDER)) return;
    static char keyboard_text[4096];
    size_t keyboard_length = keyboard_default_ini(keyboard_text, sizeof(keyboard_text));
    migrate_file(directory, L"FH2_Tastatur.ini", L"FH2_Keyboard.ini", "Tastatur", "Keyboard", keyboard_keys,
                 ARRAY_COUNT(keyboard_keys), KEYBOARD_NUMBERS, keyboard_text, keyboard_length, "FH2_Tastatur.ini");
}

void ini_migrate_log(void) {
    if (report_length) log_append(report);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestIniMigrate(const wchar_t *directory) {
    done = 0;
    report_length = 0;
    report[0] = 0;
    ini_migrate(directory);
}
__declspec(dllexport) const char *Fh2TestIniMigrateReport(void) {
    return report;
}
#endif
