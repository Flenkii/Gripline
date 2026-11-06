/* Gripline key menu (V7). See key_menu.h. */
#include "key_menu.h"
#include "keyboard_pad.h"
#include "notify.h"
#include "game_language.h"
#include "ini_edit.h"
#include "logging.h"
#include "telemetry.h"
#include "logbook.h"
#include "records.h"
#include "game_units.h"
#include "rig_settings.h"
#include "support.h"
#include "report.h"
#include "no_damage.h"
#include "update_check.h"
#include "cruise.h"

#define VK_UP_ 0x26
#define VK_LEFT_ 0x25
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28
#define VK_ENTER_ 0x0d
#define VK_ESC_ 0x1b
#define VK_BACK_ 0x08
#define REPEAT_DELAY_MS 350u
#define REPEAT_STEP_MS 90u
#define FILE_LIMIT 32768u
#define MAX_ROWS (NOTIFY_PANEL_ROWS - 1)

enum { LEVEL_CLOSED, LEVEL_GROUPS, LEVEL_LIST, LEVEL_BINDING, LEVEL_CAPTURE, LEVEL_RESET_ALL, LEVEL_STYLE, LEVEL_MOUSE, LEVEL_TELEMETRY, LEVEL_WORLD, LEVEL_LOGBOOK, LEVEL_RECORDS, LEVEL_DAMAGE,
       LEVEL_SETTINGS, LEVEL_COLOR, LEVEL_VIEW_FORWARD, LEVEL_VIEW_UP, LEVEL_TRAFFIC, LEVEL_CARDS, LEVEL_CRUISE, LEVEL_COUNT };

/* first page: rows after the binding pages */
enum { TOP_STYLE = 100, TOP_RESET = 101, TOP_MOUSE = 102, TOP_TELEMETRY = 103, TOP_WORLD = 104, TOP_LOGBOOK = 105, TOP_RECORDS = 106, TOP_REPORT = 108, TOP_DAMAGE = 109,
       TOP_SETTINGS = 110, TOP_CARDS = 111, TOP_TRAFFIC = 112, TOP_CRUISE = 113 };

/* settings page (V7): rows */
enum { SET_COLOR, SET_STEERING, SET_VIEW_FORWARD, SET_VIEW_UP, SET_UPDATE };
/* cockpit view 2 choices in cm (rig_settings.c ranges -20..80 and -20..30) */
#define VIEW_STEP 5
#define VIEW_FORWARD_MIN (-20)
#define VIEW_FORWARD_MAX 80
#define VIEW_UP_MIN (-20)
#define VIEW_UP_MAX 30
/* cards page: presets, then one row per card group (actions CARDS_KIND_FIRST + kind) */
enum { CARDS_PRESET_ALL, CARDS_PRESET_FEW, CARDS_PRESET_WARNINGS, CARDS_PRESET_COUNT };
#define CARDS_KIND_FIRST 10
static const int cards_presets[CARDS_PRESET_COUNT] = {0, NOTIFY_PRESET_FEW, NOTIFY_KINDS_ALL};
static const wchar_t *const preset_german[CARDS_PRESET_COUNT] = {L"Alles zeigen", L"Wenig", L"Nur Warnungen"};
static const wchar_t *const preset_english[CARDS_PRESET_COUNT] = {L"Show everything", L"Few", L"Warnings only"};
static const wchar_t *const kind_german[NOTIFY_KIND_COUNT] = {L"Startkarten", L"Neuer Rekord", L"Meilensteine",
                                                              L"Fahrt-Bilanz (Pause)", L"Live-Infos (Noclip, Maus)",
                                                              L"Lenkrad & Nitro", L"Bestätigungen (Mods-Seite)"};
static const wchar_t *const kind_english[NOTIFY_KIND_COUNT] = {L"Start cards", L"New record", L"Milestones",
                                                               L"Trip summary (pause)", L"Live info (noclip, mouse)",
                                                               L"Wheel & nitro", L"Confirmations (Mods page)"};
/* cruise control page: on from (km/h), actions CRUISE_FIRST + i; action 0 = crash on/off */
#define CRUISE_FIRST 10
#define CRUISE_CHOICES 8
static const int cruise_speeds[CRUISE_CHOICES] = {10, 20, 30, 50, 80, 100, 150, 200};
#define PREVIEW_TAG 0x9e7000u /* design / colour preview card: one card, new text in place */

/* mouse steering choices: FH2_Keyboard.ini MouseSteering values */
#define MOUSE_CHOICES 6
static const int mouse_values[MOUSE_CHOICES] = {0, 10, 25, 45, 70, 100};
static const wchar_t *const mouse_german[MOUSE_CHOICES] = {L"Aus", L"Sehr sanft", L"Sanft", L"Mittel", L"Schnell", L"Sehr schnell"};
static const wchar_t *const mouse_english[MOUSE_CHOICES] = {L"Off", L"Very gentle", L"Gentle", L"Medium", L"Fast", L"Very fast"};
/* back to the middle when the mouse rests: FH2_Keyboard.ini MouseCenter seconds (actions CENTER_FIRST + i) */
#define CENTER_FIRST 10
#define CENTER_CHOICES 5
static const int center_values[CENTER_CHOICES] = {0, 1, 2, 3, 5};
static const wchar_t *const center_german[CENTER_CHOICES] = {L"Nie", L"nach 1 Sekunde", L"nach 2 Sekunden",
                                                             L"nach 3 Sekunden", L"nach 5 Sekunden"};
/* dead zone the mouse skips: FH2_Keyboard.ini MouseDeadZone percent (actions DEAD_FIRST + i) */
#define DEAD_FIRST 20
#define DEAD_CHOICES 6
static const int dead_values[DEAD_CHOICES] = {0, 10, 20, 30, 40, 50};
static const wchar_t *const dead_names[DEAD_CHOICES] = {L"0 %", L"10 %", L"20 %", L"30 %", L"40 %", L"50 %"};
static const wchar_t *const center_english[CENTER_CHOICES] = {L"Never", L"after 1 second", L"after 2 seconds",
                                                              L"after 3 seconds", L"after 5 seconds"};

static const wchar_t *const style_names[NOTIFY_STYLE_COUNT] = {L"Classic", L"Enhanced", L"Overdrive"};

/* the pages of the first level; every binding is in exactly one (tests/test_key_menu.py) */
static const wchar_t *const group_driving[] = {L"Throttle", L"Brake", L"SteerLeft", L"SteerRight", L"Handbrake",
                                               L"ShiftUp", L"ShiftDown", L"Clutch", L"Rewind", L"Lights", L"Map",
                                               L"Telemetry", L"PhotoMode", L"RadioPrevious", L"RadioNext", NULL};
static const wchar_t *const group_camera[] = {L"Camera", L"LookBack", L"LookLeft", L"LookRight", L"CockpitView",
                                              L"ArrowUp", L"ArrowDown", L"ArrowLeft", L"ArrowRight", NULL};
static const wchar_t *const group_gripline[] = {L"Nitro", L"Noclip", L"NoclipUp", L"NoclipDown", L"NoclipFast",
                                                L"Freecam", L"Keybinds", L"KeyMenu", L"CruiseControl", NULL};
static const wchar_t *const group_menu[] = {L"Pause", L"Confirm", L"Back", L"ButtonX", L"ButtonY", L"TabLeft",
                                            L"TabRight", L"DpadUp", L"DpadDown", L"DpadLeft", L"DpadRight", NULL};
static const struct {
    const wchar_t *german, *english;
    const wchar_t *const *keys;
} groups[] = {
    {L"Fahren", L"Driving", group_driving},
    {L"Kamera & Blick", L"Camera & view", group_camera},
    {L"Gripline-Funktionen", L"Gripline features", group_gripline},
    {L"Menü & Controller-Tasten", L"Menu & controller buttons", group_menu},
};
#define GROUP_COUNT (sizeof(groups) / sizeof(groups[0]))

/* binding page actions */
enum { ACT_REPLACE, ACT_ADD, ACT_DEFAULT, ACT_CLEAR, ACT_COUNT };

static struct {
    int level;
    int group, binding;   /* page shown on LEVEL_LIST / LEVEL_BINDING */
    int sel[LEVEL_COUNT]; /* selected row per level */
    int add;              /* capture: 1 = add a key, 0 = replace */
    int armed;            /* capture: all keys were released once */
    unsigned char held[256]; /* fixed menu keys, last tick */
    int menu_key_held;
    ULONGLONG repeat_at;
    int german;
} m;

static wchar_t ini_path[PATH_CAPACITY];
static char file_text[FILE_LIMIT + 1], edit_text[2][FILE_LIMIT + 1];

/* ------------------------------------------------------------------ helpers */

static int group_size(int g) {
    int n = 0;
    while (groups[g].keys[n]) ++n;
    return n;
}

static int group_binding(int g, int i) {
    return keyboard_binding_find(groups[g].keys[i]);
}

static int menu_key_index(void) {
    return keyboard_binding_find(L"KeyMenu");
}

static unsigned wcopy(wchar_t *out, unsigned n, unsigned capacity, const wchar_t *text) {
    while (text && *text && n + 1 < capacity) out[n++] = *text++;
    out[n] = 0;
    return n;
}

static const wchar_t *tr(const wchar_t *german, const wchar_t *english) {
    return m.german ? german : english;
}

static int key_count(const unsigned char vk[4]) {
    int n = 0;
    while (n < 4 && vk[n]) ++n;
    return n;
}

/* keys the menu itself listens to: never the menu key (it would open and act at once) */
static int is_menu_key(unsigned char vk) {
    return vk == VK_UP_ || vk == VK_DOWN_ || vk == VK_ENTER_ || vk == VK_ESC_ || vk == VK_BACK_;
}

/* keys a binding can get in the menu: the names FH2_Keyboard.ini knows, without Esc (cancels) and
   the left mouse button (a click into the window) */
static const unsigned char capture_named[] = {0x20, 0x0d, 0x09, 0x08, 0xa0, 0xa1, 0xa2, 0xa3, 0x12, 0x26, 0x28,
                                              0x25, 0x27, 0x21, 0x22, 0x24, 0x23, 0x2d, 0x2e, 0xbd, 0xbb, 0xbc,
                                              0xbe, 0x02, 0x04, 0x05, 0x06};

static unsigned char capture_key(void) {
    for (unsigned vk = 'A'; vk <= 'Z'; ++vk)
        if (keyboard_key_down((unsigned char)vk)) return (unsigned char)vk;
    for (unsigned vk = '0'; vk <= '9'; ++vk)
        if (keyboard_key_down((unsigned char)vk)) return (unsigned char)vk;
    for (unsigned vk = 0x70; vk <= 0x7b; ++vk)
        if (keyboard_key_down((unsigned char)vk)) return (unsigned char)vk;
    for (unsigned vk = 0x60; vk <= 0x69; ++vk)
        if (keyboard_key_down((unsigned char)vk)) return (unsigned char)vk;
    for (unsigned i = 0; i < sizeof(capture_named); ++i)
        if (keyboard_key_down(capture_named[i])) return capture_named[i];
    return 0;
}

static int any_key_down(void) {
    return capture_key() || keyboard_key_down(VK_ESC_) || keyboard_key_down(0x01);
}

/* ------------------------------------------------------------------ FH2_Keyboard.ini */

static int read_file_at(const wchar_t *path, size_t *length) {
    *length = 0;
    file_text[0] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND; /* made new */
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart <= FILE_LIMIT &&
             ReadFile(file, file_text, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart;
    CloseHandle(file);
    if (!ok) return 0;
    file_text[got] = 0;
    *length = got;
    return 1;
}

static int write_file_at(const wchar_t *path, const char *data, size_t length) {
    static wchar_t temp[PATH_CAPACITY + 8];
    unsigned n = wcopy(temp, 0, PATH_CAPACITY, path);
    wcopy(temp, n, PATH_CAPACITY + 8, L".gltmp");
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp);
    return ok;
}

/* "W,Up" / "-" */
static void value_text(const unsigned char vk[4], char *out, unsigned capacity) {
    unsigned n = 0;
    for (unsigned k = 0; k < 4 && vk[k]; ++k) {
        char buffer[8];
        const char *name = keyboard_key_ini_name(vk[k], buffer);
        if (!name[0]) continue;
        if (n && n + 1 < capacity) out[n++] = ',';
        while (*name && n + 1 < capacity) out[n++] = *name++;
    }
    if (!n) out[n++] = '-';
    out[n] = 0;
}

static void ascii_key(const wchar_t *key, char *out, unsigned capacity) {
    unsigned n = 0;
    while (key[n] && n + 1 < capacity) {
        out[n] = (char)key[n];
        ++n;
    }
    out[n] = 0;
}

/* writes the current keys of bindings [first, end) into FH2_Keyboard.ini; the rest of the file stays */
static int save_bindings_now(unsigned first, unsigned end) {
    size_t length;
    if (!ini_path[0] || !read_file_at(ini_path, &length)) {
        log_append("KEYMENU: FH2_Keyboard.ini could not be read - change not saved.\r\n");
        return 0;
    }
    const char *source = file_text;
    unsigned slot = 0;
    for (unsigned i = first; i < end; ++i) {
        unsigned char vk[4];
        char key[32], value[64];
        keyboard_binding_vks(i, vk);
        value_text(vk, value, sizeof(value));
        ascii_key(keyboard_binding_key(i), key, sizeof(key));
        size_t n = ini_set_value(source, length, "Keyboard", key, value, edit_text[slot], FILE_LIMIT + 1);
        if (!n || n > FILE_LIMIT) {
            log_append("KEYMENU: FH2_Keyboard.ini would become too large - change not saved.\r\n");
            return 0;
        }
        source = edit_text[slot];
        length = n;
        slot ^= 1;
    }
    if (!write_file_at(ini_path, source, length)) {
        log_append("KEYMENU: FH2_Keyboard.ini could not be written - change not saved.\r\n");
        return 0;
    }
    return 1;
}

static int save_bindings(unsigned first, unsigned end) { /* one writer at a time (support.h) */
    settings_file_lock();
    int ok = save_bindings_now(first, end);
    settings_file_unlock();
    return ok;
}

/* Gripline.ini next to FH2_Keyboard.ini: [Gripline] <key> = one digit (CardStyle, NoDamage) */
static int save_gripline_digit_now(const char *key, int digit) {
    static wchar_t path[PATH_CAPACITY];
    unsigned n = wcopy(path, 0, PATH_CAPACITY, ini_path), cut = 0;
    for (unsigned i = 0; i < n; ++i)
        if (path[i] == L'\\' || path[i] == L'/') cut = i + 1;
    if (!cut) return 0;
    path[cut] = 0;
    wcopy(path, cut, PATH_CAPACITY, L"Gripline.ini");
    size_t length;
    if (!read_file_at(path, &length)) return 0;
    char value[2] = {(char)('0' + digit), 0};
    size_t written = ini_set_value(file_text, length, "Gripline", key, value, edit_text[0], FILE_LIMIT + 1);
    if (!written || written > FILE_LIMIT || !write_file_at(path, edit_text[0], written)) {
        log_append("KEYMENU: Gripline.ini could not be written - change not saved.\r\n");
        return 0;
    }
    return 1;
}

static int save_gripline_digit(const char *key, int digit) { /* one writer at a time (support.h) */
    settings_file_lock();
    int ok = save_gripline_digit_now(key, digit);
    settings_file_unlock();
    return ok;
}

static int save_style(int style) {
    return save_gripline_digit("CardStyle", style);
}

/* FH2_Keyboard.ini [Keyboard] MouseSteering */
static int save_mouse_now(const char *key, int value) {
    size_t length;
    char text[4];
    unsigned n = 0;
    if (value >= 100) text[n++] = '1', text[n++] = '0', text[n++] = '0';
    else {
        if (value >= 10) text[n++] = (char)('0' + value / 10);
        text[n++] = (char)('0' + value % 10);
    }
    text[n] = 0;
    if (!read_file_at(ini_path, &length)) return 0;
    size_t written = ini_set_value(file_text, length, "Keyboard", key, text, edit_text[0], FILE_LIMIT + 1);
    if (!written || written > FILE_LIMIT || !write_file_at(ini_path, edit_text[0], written)) {
        log_append("KEYMENU: FH2_Keyboard.ini could not be written - mouse steering not saved.\r\n");
        return 0;
    }
    return 1;
}

static int save_mouse(const char *key, int value) { /* one writer at a time (support.h) */
    settings_file_lock();
    int ok = save_mouse_now(key, value);
    settings_file_unlock();
    return ok;
}

static int mouse_choice(void) {
    int value = keyboard_mouse_steering(), best = 0;
    for (int i = 1; i < MOUSE_CHOICES; ++i) {
        int d = mouse_values[i] - value, b = mouse_values[best] - value;
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) best = i;
    }
    return value ? (best ? best : 1) : 0;
}

static int dead_choice(void) {
    int value = keyboard_mouse_dead_zone(), best = 0;
    for (int i = 1; i < DEAD_CHOICES; ++i) {
        int d = dead_values[i] - value, b = dead_values[best] - value;
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) best = i;
    }
    return best;
}

/* Gripline.ini [Gripline] key = number */
static int save_gripline_int_now(const char *key, int value) {
    static wchar_t path[PATH_CAPACITY];
    unsigned n = wcopy(path, 0, PATH_CAPACITY, ini_path), cut = 0;
    for (unsigned i = 0; i < n; ++i)
        if (path[i] == L'\\' || path[i] == L'/') cut = i + 1;
    if (!cut) return 0;
    path[cut] = 0;
    wcopy(path, cut, PATH_CAPACITY, L"Gripline.ini");
    size_t length;
    if (!read_file_at(path, &length)) return 0;
    char text[12];
    unsigned d = 0, v = value < 0 ? 0u : (unsigned)value;
    char digits[12];
    do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
    unsigned t = 0;
    while (d) text[t++] = digits[--d];
    text[t] = 0;
    size_t written = ini_set_value(file_text, length, "Gripline", key, text, edit_text[0], FILE_LIMIT + 1);
    return written && written <= FILE_LIMIT && write_file_at(path, edit_text[0], written);
}

static int save_gripline_int(const char *key, int value) { /* one writer at a time (support.h) */
    settings_file_lock();
    int ok = save_gripline_int_now(key, value);
    settings_file_unlock();
    return ok;
}

static int center_choice(void) {
    int value = keyboard_mouse_center();
    for (int i = 0; i < CENTER_CHOICES; ++i)
        if (center_values[i] == value) return i;
    return value ? CENTER_CHOICES - 1 : 0;
}

static void log_binding(unsigned index) {
    unsigned char vk[4];
    char line[128] = "KEYMENU: ", value[64];
    unsigned n = 9;
    keyboard_binding_vks(index, vk);
    value_text(vk, value, sizeof(value));
    for (const wchar_t *k = keyboard_binding_key(index); *k && n < 60; ++k) line[n++] = (char)*k;
    line[n++] = ' ', line[n++] = '=', line[n++] = ' ';
    for (const char *v = value; *v && n < 120; ++v) line[n++] = *v;
    line[n++] = '\r', line[n++] = '\n', line[n] = 0;
    log_append(line);
}

/* ------------------------------------------------------------------ cards */

static void utf8(const wchar_t *text, char *out, unsigned capacity, unsigned *n) {
    int got = WideCharToMultiByte(CP_UTF8, 0, text, -1, out + *n, (int)(capacity - *n), NULL, NULL);
    if (got > 0) *n += (unsigned)got - 1; /* without the terminator */
}

/* "Gas: Pfeil hoch" + saved / which other function has the key too */
static void change_card(unsigned index, int saved) {
    unsigned char vk[4];
    wchar_t keys[48], line[160];
    keyboard_binding_vks(index, vk);
    keyboard_keys_text(vk, m.german, keys, 48);
    unsigned n = wcopy(line, 0, 160, keyboard_binding_label(index, m.german));
    n = wcopy(line, n, 160, L": ");
    n = wcopy(line, n, 160, keys);
    n = wcopy(line, n, 160, L"\n");
    int other = -1;
    for (unsigned i = 0; i < keyboard_binding_count() && other < 0; ++i) {
        unsigned char ov[4];
        if (i == index) continue;
        keyboard_binding_vks(i, ov);
        for (int a = 0; a < 4 && vk[a] && other < 0; ++a)
            for (int b = 0; b < 4 && ov[b]; ++b)
                if (vk[a] == ov[b]) other = (int)i;
    }
    if (!saved) {
        n = wcopy(line, n, 160, tr(L"Nur bis zum Spielende (nicht gespeichert)", L"Until the game closes (not saved)"));
    } else if (other >= 0) {
        n = wcopy(line, n, 160, tr(L"Auch belegt: ", L"Also used by: "));
        n = wcopy(line, n, 160, keyboard_binding_label((unsigned)other, m.german));
    } else {
        n = wcopy(line, n, 160, tr(L"Gespeichert", L"Saved"));
    }
    char text[400];
    unsigned t = 0;
    utf8(line, text, sizeof(text), &t);
    text[t] = 0;
    if (other >= 0 || !saved) notify_show_tinted(text, NOTIFY_COLOR_ORANGE, m.german ? "HINWEIS" : "NOTE", 0);
    else notify_show(text);
}

/* ------------------------------------------------------------------ pages */

static NotifyPanelRow rows[NOTIFY_PANEL_ROWS];
static int row_action[NOTIFY_PANEL_ROWS]; /* -1 = not selectable */
static unsigned row_count;

static void row_add(const wchar_t *label, const wchar_t *keys, int style, int action) {
    if (row_count >= MAX_ROWS) return;
    wcopy(rows[row_count].label, 0, 32, label);
    wcopy(rows[row_count].keys, 0, 32, keys);
    rows[row_count].style = style;
    row_action[row_count] = action;
    ++row_count;
}

static int selectable_count(void) {
    int n = 0;
    for (unsigned i = 0; i < row_count; ++i) n += row_action[i] >= 0;
    return n;
}

/* the action of the selected row (-1: none) */
static int selected_action(void) {
    int k = 0;
    for (unsigned i = 0; i < row_count; ++i) {
        if (row_action[i] < 0) continue;
        if (k++ == m.sel[m.level]) return row_action[i];
    }
    return -1;
}

static void hint(int first_page) {
    /* keyboard and controller at once (A = Enter, B = Esc) */
    row_add(tr(L"↑↓ wählen · Enter/A OK", L"↑↓ choose · Enter/A OK"),
            first_page ? tr(L"Esc/B schließen", L"Esc/B close") : tr(L"Esc/B zurück", L"Esc/B back"), NOTIFY_ROW_HINT, -1);
}

/* digits of v at out[n]; returns the new length */
static unsigned wuint(wchar_t *out, unsigned n, unsigned v) {
    wchar_t d[12];
    unsigned k = 0;
    do d[k++] = (wchar_t)(L'0' + v % 10); while ((v /= 10) && k < 12);
    while (k && n + 1 < 32) out[n++] = d[--k];
    out[n] = 0;
    return n;
}

/* V7: telemetry panel look (notify.h). Left / right (arrow keys, D-pad, stick) change the value of the
   selected row, Enter steps on and starts again at the end; the card says what it is now */
enum { TELE_LOOK_POS = 60, TELE_LOOK_OPACITY, TELE_LOOK_SIZE };
static const wchar_t *const tele_pos_german[NOTIFY_TELE_POS_COUNT] = {L"Rechts Mitte", L"Rechts oben", L"Rechts unten",
                                                                      L"Links oben", L"Links Mitte", L"Links unten"};
static const wchar_t *const tele_pos_english[NOTIFY_TELE_POS_COUNT] = {L"Right middle", L"Right top", L"Right bottom",
                                                                       L"Left top", L"Left middle", L"Left bottom"};
static const int tele_opacities[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100}; /* background only */
static const int tele_sizes[] = {60, 70, 85, 100, 115, 130, 150, 175, 200};

static void percent_text(int value, wchar_t out[16]) {
    unsigned n = wuint(out, 0, (unsigned)value);
    out[n++] = L' ', out[n++] = L'%', out[n] = 0;
}

/* dir +1 / -1 stops at the ends; dir 2 (Enter) goes on and starts again at the end */
static int step_in(const int *list, int count, int now, int dir) {
    int at = 0;
    for (int i = 0; i < count; ++i)
        if (list[i] == now) at = i;
    if (dir == 2) return list[(at + 1) % count];
    at += dir;
    return list[at < 0 ? 0 : at >= count ? count - 1 : at];
}

static void tele_look_step(int action, int dir) {
    int pos = notify_telemetry_position(), opacity = notify_telemetry_opacity(), size = notify_telemetry_size();
    const char *key;
    int value;
    if (action == TELE_LOOK_POS)
        key = "TelemetryPosition", value = pos = (pos + (dir == -1 ? NOTIFY_TELE_POS_COUNT - 1 : 1)) % NOTIFY_TELE_POS_COUNT;
    else if (action == TELE_LOOK_OPACITY)
        key = "TelemetryOpacity", value = opacity = step_in(tele_opacities, (int)ARRAY_COUNT(tele_opacities), opacity, dir);
    else key = "TelemetrySize", value = size = step_in(tele_sizes, (int)ARRAY_COUNT(tele_sizes), size, dir);
    notify_set_telemetry_look(pos, opacity, size);
    if (!save_gripline_int(key, value)) log_append("KEYMENU: Gripline.ini could not be written - telemetry look not saved.\r\n");
    wchar_t line[96], v[16];
    unsigned n = wcopy(line, 0, 96, tr(L"Telemetrie: ", L"Telemetry: "));
    if (action == TELE_LOOK_POS) n = wcopy(line, n, 96, tr(tele_pos_german[pos], tele_pos_english[pos]));
    else {
        percent_text(value, v);
        n = wcopy(line, n, 96, action == TELE_LOOK_OPACITY ? tr(L"Hintergrund ", L"background ") : tr(L"Größe ", L"size "));
        n = wcopy(line, n, 96, v);
    }
    wcopy(line, n, 96, tr(L"\nZu sehen, wenn das Menü zu ist", L"\nShown once the menu is closed"));
    char text[192];
    unsigned t = 0;
    utf8(line, text, sizeof(text), &t);
    text[t] = 0;
    notify_show_step(text, 0x7e1e00u, 2500);
}

/* "12,3 km" below 100, else "1234 km" (miles when the game shows mph) */
static void distance_text(double metres, wchar_t out[32]) {
    int imperial = game_units_imperial(keyboard_game_image());
    double units = metres / (imperial ? 1609.344 : 1000.0);
    unsigned n;
    if (units < 100.0) {
        unsigned tenths = (unsigned)(units * 10.0 + 0.5);
        n = wuint(out, 0, tenths / 10);
        n = wcopy(out, n, 32, m.german ? L"," : L".");
        n = wuint(out, n, tenths % 10);
    } else {
        n = wuint(out, 0, (unsigned)(units + 0.5));
    }
    wcopy(out, n, 32, imperial ? L" mi" : L" km");
}

static int confirm_clear; /* records page: first Enter asks */

static const unsigned world_ids[3] = {RIG_GRIP_SPEED_CAMERAS, RIG_GRIP_CAR_LABELS, RIG_GRIP_EVENT_ICONS};
static const wchar_t *const world_german[3] = {L"Blitzer", L"Namen über Autos", L"Event-Symbole auf der Straße"};
static const wchar_t *const world_english[3] = {L"Speed cameras", L"Names above cars", L"Event icons on the road"};

static const wchar_t *const damage_german[NO_DAMAGE_COUNT] = {L"Aus (wie im Spiel)", L"Andere Autos", L"Alle Autos"};
static const wchar_t *const damage_english[NO_DAMAGE_COUNT] = {L"Off (as in the game)", L"Other cars", L"All cars"};
static const wchar_t *const damage_short_german[NO_DAMAGE_COUNT] = {L"Aus", L"Andere", L"Alle"};
static const wchar_t *const damage_short_english[NO_DAMAGE_COUNT] = {L"Off", L"Others", L"All"};

static int style_saved; /* the saved card design (the design page previews others while choosing) */

/* "35 cm" / "-5 cm" */
static void centimetres(int v, wchar_t out[32]) {
    unsigned n = 0;
    if (v < 0) out[n++] = L'-', v = -v;
    n = wuint(out, n, (unsigned)v);
    wcopy(out, n, 32, L" cm");
}

/* traffic preset names (rig_settings.c), 0 = own values */
static const wchar_t *traffic_name(int preset) {
    return preset > 0 ? rig_density_choice(keyboard_game_image(), (unsigned)preset) : tr(L"eigene Werte", L"own values");
}

static const wchar_t *cards_preset_name(void) {
    int hidden = notify_hidden_kinds();
    for (int p = 0; p < CARDS_PRESET_COUNT; ++p)
        if (hidden == cards_presets[p]) return tr(preset_german[p], preset_english[p]);
    return tr(L"eigene Auswahl", L"own choice");
}

/* km/h value in the game's unit: "20 km/h" / "12 mph" */
static void speed_text(int kmh, wchar_t out[32]) {
    int imperial = game_units_imperial(keyboard_game_image());
    unsigned n = wuint(out, 0, (unsigned)(imperial ? (kmh / 1.609344 + 0.5) : kmh));
    wcopy(out, n, 32, imperial ? L" mph" : L" km/h");
}

static void build_page(wchar_t *title) {
    row_count = 0;
    title[0] = 0;
    unsigned char vk[4];
    wchar_t keys[32];
    switch (m.level) {
        case LEVEL_GROUPS:
            wcopy(title, 0, 64, tr(L"Gripline-Menü", L"Gripline menu"));
            /* V7: four sections, so the long list reads at a glance */
            row_add(tr(L"Tasten & Steuerung", L"Keys & controls"), L"", NOTIFY_ROW_SECTION, -1);
            for (unsigned g = 0; g < GROUP_COUNT; ++g) row_add(tr(groups[g].german, groups[g].english), L"›", 0, (int)g);
            row_add(tr(L"Maus-Lenkung (experimentell)", L"Mouse steering (experimental)"), tr(mouse_german[mouse_choice()], mouse_english[mouse_choice()]), 0,
                    TOP_MOUSE);
            row_add(tr(L"Fahren & Daten", L"Driving & data"), L"", NOTIFY_ROW_SECTION, -1);
            row_add(tr(L"Live-Telemetrie", L"Live telemetry"), telemetry_enabled() ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0,
                    TOP_TELEMETRY);
            {
                double lm, ls;
                float lt;
                logbook_total(&lm, &ls, &lt);
                wchar_t dist[32];
                distance_text(lm, dist);
                row_add(tr(L"Fahrtenbuch", L"Logbook"), dist, 0, TOP_LOGBOOK);
            }
            {
                wchar_t v[32];
                unsigned n = wcopy(v, 0, 32, tr(L"ab ", L"from "));
                speed_text(cruise_min_speed(), v + n);
                row_add(tr(L"Tempomat (Cruise Control)", L"Cruise control"), v, 0, TOP_CRUISE);
            }
            row_add(tr(L"Rekorde", L"Records"), L"›", 0, TOP_RECORDS);
            row_add(tr(L"Welt & Autos", L"World & cars"), L"", NOTIFY_ROW_SECTION, -1);
            row_add(tr(L"Welt: Blitzer, Namen, Symbole", L"World: cameras, names, icons"), L"›", 0, TOP_WORLD);
            row_add(tr(L"Verkehr", L"Traffic"), traffic_name(rig_setting_get(RIG_TRAFFIC_PRESET)), 0, TOP_TRAFFIC);
            row_add(tr(L"Kein Schaden an Autos", L"No damage on cars"),
                    tr(damage_short_german[no_damage_mode()], damage_short_english[no_damage_mode()]), 0, TOP_DAMAGE);
            row_add(L"Gripline", L"", NOTIFY_ROW_SECTION, -1);
            row_add(tr(L"Einstellungen", L"Settings"), L"›", 0, TOP_SETTINGS);
            row_add(tr(L"Karten & Hinweise", L"Cards & notices"), cards_preset_name(), 0, TOP_CARDS);
            row_add(tr(L"Karten-Design", L"Card design"), style_names[style_saved], 0, TOP_STYLE);
            row_add(tr(L"Problem-Bericht erstellen", L"Create problem report"), L"ZIP", 0, TOP_REPORT);
            row_add(tr(L"Alle Tasten auf Standard", L"Reset all keys to default"), L"›", 0, TOP_RESET); /* keys only, not the other settings */
            hint(1);
            break;
        case LEVEL_SETTINGS: {
            wcopy(title, 0, 64, tr(L"Einstellungen", L"Settings"));
            row_add(tr(L"Kartenfarbe", L"Card colour"), rig_color_choice(keyboard_game_image(), (unsigned)rig_setting_get(RIG_GRIP_CARD_COLOR)), 0, SET_COLOR);
            row_add(tr(L"Lenkrad-Animation 1:1", L"Steering animation 1:1"), rig_setting_get(RIG_GRIP_STEERING) ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0,
                    SET_STEERING);
            wchar_t cm[32];
            centimetres(rig_setting_get(RIG_GRIP_VIEW_FORWARD), cm);
            row_add(tr(L"Cockpit-Ansicht 2: nach vorn", L"Cockpit view 2: forward"), cm, 0, SET_VIEW_FORWARD);
            centimetres(rig_setting_get(RIG_GRIP_VIEW_UP), cm);
            row_add(tr(L"Cockpit-Ansicht 2: Höhe", L"Cockpit view 2: height"), cm, 0, SET_VIEW_UP);
            row_add(tr(L"Update-Hinweis beim Start", L"Update notice at start"), update_check_enabled() ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0,
                    SET_UPDATE);
            row_add(tr(L"Lenkrad-Animation, Update:", L"Steering animation, update:"), L"", NOTIFY_ROW_INFO, -1);
            row_add(tr(L"wirken nach Spiel-Neustart", L"take effect after a restart"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        }
        case LEVEL_COLOR:
            wcopy(title, 0, 64, tr(L"Kartenfarbe", L"Card colour"));
            for (int i = 0; i < NOTIFY_COLOR_COUNT; ++i)
                row_add(rig_color_choice(keyboard_game_image(), (unsigned)i), i == rig_setting_get(RIG_GRIP_CARD_COLOR) ? tr(L"✓ aktiv", L"✓ active") : L"",
                        0, i);
            row_add(tr(L"Vorschau beim Wählen", L"Preview while choosing"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        case LEVEL_VIEW_FORWARD:
        case LEVEL_VIEW_UP: {
            int forward = m.level == LEVEL_VIEW_FORWARD;
            int now = rig_setting_get(forward ? RIG_GRIP_VIEW_FORWARD : RIG_GRIP_VIEW_UP);
            wcopy(title, 0, 64, forward ? tr(L"Cockpit 2: nach vorn", L"Cockpit 2: forward") : tr(L"Cockpit 2: Höhe", L"Cockpit 2: height"));
            int low = forward ? VIEW_FORWARD_MIN : VIEW_UP_MIN, high = forward ? VIEW_FORWARD_MAX : VIEW_UP_MAX;
            int standard = forward ? 35 : -5;
            for (int v = low; v <= high; v += VIEW_STEP) {
                wchar_t cm[32];
                centimetres(v, cm);
                row_add(cm, v == now ? tr(L"✓ aktiv", L"✓ active") : v == standard ? tr(L"Standard", L"default") : L"", 0, v - low);
            }
            hint(0);
            break;
        }
        case LEVEL_TRAFFIC: {
            wcopy(title, 0, 64, tr(L"Verkehr", L"Traffic"));
            int now = rig_setting_get(RIG_TRAFFIC_PRESET);
            for (int p = 1; p < RIG_DENSITY_COUNT; ++p)
                row_add(traffic_name(p), p == now ? tr(L"✓ aktiv", L"✓ active") : L"", 0, p);
            if (!now) row_add(tr(L"Jetzt: eigene Werte", L"Now: own values (Mods page)"), L"", NOTIFY_ROW_INFO, -1);
            row_add(tr(L"Wirkt nach Spiel-Neustart", L"Takes effect after a restart"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        }
        case LEVEL_CRUISE: {
            wcopy(title, 0, 64, tr(L"Tempomat", L"Cruise control"));
            row_add(tr(L"Bei Aufprall ausschalten", L"Switch off on a crash"), cruise_crash_off() ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0, 0);
            row_add(tr(L"Einschalten ab", L"Switch on from"), L"", NOTIFY_ROW_SECTION, -1);
            int now = cruise_min_speed();
            for (int i = 0; i < CRUISE_CHOICES; ++i) {
                wchar_t v[32];
                speed_text(cruise_speeds[i], v);
                row_add(v, cruise_speeds[i] == now ? tr(L"✓ aktiv", L"✓ active") : cruise_speeds[i] == CRUISE_MIN_DEFAULT ? tr(L"Standard", L"default") : L"",
                        0, CRUISE_FIRST + i);
            }
            row_add(tr(L"Nach oben offen, Bremsen = aus", L"No top limit, braking = off"), L"", NOTIFY_ROW_INFO, -1);
            row_add(tr(L"Controller: LS + Steuerkreuz ◄", L"Controller: LS + D-pad ◄"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        }
        case LEVEL_CARDS: {
            wcopy(title, 0, 64, tr(L"Karten & Hinweise", L"Cards & notices"));
            int hidden = notify_hidden_kinds();
            row_add(tr(L"Voreinstellung", L"Preset"), L"", NOTIFY_ROW_SECTION, -1);
            for (int p = 0; p < CARDS_PRESET_COUNT; ++p)
                row_add(tr(preset_german[p], preset_english[p]), hidden == cards_presets[p] ? tr(L"✓ aktiv", L"✓ active") : L"", 0, p);
            row_add(tr(L"Einzeln (Enter = an/aus)", L"One by one (Enter = on/off)"), L"", NOTIFY_ROW_SECTION, -1);
            for (int k = 0; k < NOTIFY_KIND_COUNT; ++k)
                row_add(tr(kind_german[k], kind_english[k]), (hidden & (1 << k)) ? tr(L"aus", L"off") : tr(L"✓ an", L"✓ on"), 0, CARDS_KIND_FIRST + k);
            row_add(tr(L"Warnungen kommen immer", L"Warnings always show"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        }
        case LEVEL_LIST:
            wcopy(title, 0, 64, tr(groups[m.group].german, groups[m.group].english));
            for (int i = 0; i < group_size(m.group); ++i) {
                int b = group_binding(m.group, i);
                if (b < 0) continue;
                keyboard_binding_vks((unsigned)b, vk);
                keyboard_keys_text(vk, m.german, keys, 32);
                row_add(keyboard_binding_label((unsigned)b, m.german), keys, 0, b);
            }
            hint(0);
            break;
        case LEVEL_BINDING:
            wcopy(title, 0, 64, keyboard_binding_label((unsigned)m.binding, m.german));
            keyboard_binding_vks((unsigned)m.binding, vk);
            keyboard_keys_text(vk, m.german, keys, 32);
            row_add(tr(L"Jetzt", L"Now"), keys, NOTIFY_ROW_INFO, -1);
            keyboard_binding_default_vks((unsigned)m.binding, vk);
            keyboard_keys_text(vk, m.german, keys, 32);
            row_add(tr(L"Standard", L"Default"), keys, NOTIFY_ROW_INFO, -1);
            row_add(tr(L"Neue Taste festlegen", L"Set a new key"), tr(L"ersetzt", L"replaces"), 0, ACT_REPLACE);
            row_add(tr(L"Weitere Taste dazu", L"Add another key"), tr(L"bis zu 4", L"up to 4"), 0, ACT_ADD);
            row_add(tr(L"Standard wiederherstellen", L"Restore default"), L"", 0, ACT_DEFAULT);
            if (m.binding != menu_key_index()) row_add(tr(L"Keine Taste", L"No key"), L"", 0, ACT_CLEAR);
            hint(0);
            break;
        case LEVEL_CAPTURE:
            wcopy(title, 0, 64, keyboard_binding_label((unsigned)m.binding, m.german));
            row_add(m.add ? tr(L"Drücke die zusätzliche Taste", L"Press the extra key")
                          : tr(L"Drücke jetzt die neue Taste", L"Press the new key now"),
                    L"", NOTIFY_ROW_INFO, -1);
            row_add(tr(L"Abbrechen", L"Cancel"), L"Esc", NOTIFY_ROW_HINT, -1);
            break;
        case LEVEL_STYLE:
            wcopy(title, 0, 64, tr(L"Karten-Design", L"Card design"));
            for (int i = 0; i < NOTIFY_STYLE_COUNT; ++i)
                row_add(style_names[i], i == style_saved ? tr(L"✓ aktiv", L"✓ active") : L"", 0, i);
            hint(0);
            break;
        case LEVEL_MOUSE:
            wcopy(title, 0, 64, tr(L"Maus-Lenkung (experimentell)", L"Mouse steering (experimental)"));
            row_add(tr(L"Empfindlichkeit", L"Sensitivity"), L"", NOTIFY_ROW_SECTION, -1);
            for (int i = 0; i < MOUSE_CHOICES; ++i)
                row_add(tr(mouse_german[i], mouse_english[i]), i == mouse_choice() ? tr(L"✓ aktiv", L"✓ active") : L"", 0, i);
            row_add(tr(L"Totzone überspringen", L"Skip dead zone"), L"", NOTIFY_ROW_SECTION, -1);
            for (int i = 0; i < DEAD_CHOICES; ++i)
                row_add(dead_names[i], i == dead_choice() ? tr(L"✓ aktiv", L"✓ active") : (i == 3 ? tr(L"Standard", L"default") : L""), 0,
                        DEAD_FIRST + i);
            row_add(tr(L"Zur Mitte, wenn Maus ruht", L"Centre when mouse rests"), L"", NOTIFY_ROW_SECTION, -1);
            for (int i = 0; i < CENTER_CHOICES; ++i)
                row_add(tr(center_german[i], center_english[i]), i == center_choice() ? tr(L"✓ aktiv", L"✓ active") : L"", 0,
                        CENTER_FIRST + i);
            hint(0);
            break;
        case LEVEL_TELEMETRY: {
            wcopy(title, 0, 64, tr(L"Live-Telemetrie", L"Live telemetry"));
            row_add(tr(L"Telemetrie-Tafel", L"Telemetry panel"), telemetry_enabled() ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0, 0);
            row_add(tr(L"Als CSV aufzeichnen", L"Record as CSV"), records_csv_on() ? tr(L"● läuft", L"● on") : tr(L"Aus", L"Off"), 0, 50);
            row_add(tr(L"Anzeigen (Enter = an/aus)", L"Shown (Enter = on/off)"), L"", NOTIFY_ROW_SECTION, -1);
            int mask = telemetry_show();
            for (unsigned i = 0; i < TELEMETRY_SHOW_COUNT; ++i)
                row_add(telemetry_item_name(i, m.german), (mask & (1 << i)) ? tr(L"✓ an", L"✓ on") : tr(L"aus", L"off"), 0,
                        1 + (int)i);
            row_add(tr(L"Aussehen (◄ ► ändern)", L"Look (◄ ► change)"), L"", NOTIFY_ROW_SECTION, -1);
            wchar_t v[16];
            row_add(tr(L"Position", L"Position"), tr(tele_pos_german[notify_telemetry_position()], tele_pos_english[notify_telemetry_position()]), 0, TELE_LOOK_POS);
            percent_text(notify_telemetry_opacity(), v);
            row_add(tr(L"Hintergrund-Deckkraft", L"Background opacity"), v, 0, TELE_LOOK_OPACITY);
            percent_text(notify_telemetry_size(), v);
            row_add(tr(L"Größe", L"Size"), v, 0, TELE_LOOK_SIZE);
            hint(0);
            break;
        }
        case LEVEL_LOGBOOK: {
            wcopy(title, 0, 64, tr(L"Fahrtenbuch", L"Logbook"));
            double lm, ls;
            float lt;
            logbook_total(&lm, &ls, &lt);
            wchar_t text[32], name[32];
            distance_text(lm, text);
            row_add(tr(L"Gesamt gefahren", L"Driven in total"), text, 0, -1);
            unsigned minutes = (unsigned)(ls / 60.0 + 0.5), n = 0;
            n = wuint(text, 0, minutes / 60);
            n = wcopy(text, n, 32, L" h ");
            if (minutes % 60 < 10) n = wcopy(text, n, 32, L"0");
            n = wuint(text, n, minutes % 60);
            wcopy(text, n, 32, L" min");
            row_add(tr(L"Fahrzeit", L"Driving time"), text, 0, -1);
            int imperial = game_units_imperial(keyboard_game_image());
            n = wuint(text, 0, (unsigned)(lt * (imperial ? 2.236936f : 3.6f) + 0.5f));
            wcopy(text, n, 32, imperial ? L" mph" : L" km/h");
            row_add(tr(L"Höchsttempo", L"Top speed"), text, 0, -1);
            double cm;
            logbook_current(name, 32, &cm);
            if (name[0]) {
                row_add(tr(L"Jetzt im Auto", L"Car now"), L"", NOTIFY_ROW_SECTION, -1);
                distance_text(cm, text);
                row_add(name, text, 0, -1);
            }
            row_add(game_units_imperial(keyboard_game_image()) ? tr(L"Meiste Meilen", L"Most miles") : tr(L"Meiste Kilometer", L"Most kilometres"), L"",
                    NOTIFY_ROW_SECTION, -1);
            int any = 0;
            for (unsigned r = 0; r < 5; ++r) {
                double m;
                if (!logbook_top(r, name, 32, &m)) break;
                distance_text(m, text);
                row_add(name, text, 0, -1);
                any = 1;
            }
            if (!any) row_add(tr(L"Noch kein Auto über 100 m", L"No car past 100 m yet"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        }
        case LEVEL_RECORDS: {
            wcopy(title, 0, 64, tr(L"Rekorde", L"Records"));
            int imperial = game_units_imperial(keyboard_game_image());
            static const unsigned kinds_metric[5] = {RECORD_SPRINT, RECORD_SPRINT200, RECORD_QUARTER, RECORD_BRAKE, RECORD_TOP};
            static const unsigned kinds_imperial[5] = {RECORD_SPRINT_MPH, RECORD_SPRINT200_MPH, RECORD_QUARTER, RECORD_BRAKE_MPH, RECORD_TOP};
            static const wchar_t *const names_de[5] = {L"0-100 km/h", L"0-200 km/h", L"Viertelmeile", L"Bremsweg 100-0", L"Höchsttempo"};
            static const wchar_t *const names_mph[5] = {L"0-60 mph", L"0-100 mph", L"Quarter mile", L"Braking 60-0", L"Top speed"};
            static const wchar_t *const names_en[5] = {L"0-100 km/h", L"0-200 km/h", L"Quarter mile", L"Braking 100-0", L"Top speed"};
            static const wchar_t *const names_de_mph[5] = {L"0-60 mph", L"0-100 mph", L"Viertelmeile", L"Bremsweg 60-0", L"Höchsttempo"};
            for (unsigned i = 0; i < 5; ++i) {
                unsigned k = imperial ? kinds_imperial[i] : kinds_metric[i];
                float v = records_best(k);
                wchar_t text[32];
                text[0] = 0;
                if (v <= 0.0f) wcopy(text, 0, 32, L"–");
                else if (k == RECORD_TOP) {
                    unsigned n = wuint(text, 0, (unsigned)(v * (imperial ? 2.236936f : 3.6f) + 0.5f));
                    wcopy(text, n, 32, imperial ? L" mph" : L" km/h");
                } else if (k == RECORD_BRAKE_MPH) {
                    unsigned n = wuint(text, 0, (unsigned)(v * 3.28084f + 0.5f));
                    wcopy(text, n, 32, L" ft");
                } else {
                    int tenth = k == RECORD_BRAKE;
                    unsigned h = (unsigned)(v * (tenth ? 10.0f : 100.0f) + 0.5f), n;
                    n = wuint(text, 0, h / (tenth ? 10 : 100));
                    n = wcopy(text, n, 32, m.german ? L"," : L".");
                    if (!tenth && h % 100 < 10) n = wcopy(text, n, 32, L"0");
                    n = wuint(text, n, h % (tenth ? 10 : 100));
                    wcopy(text, n, 32, tenth ? L" m" : L" s");
                }
                row_add(m.german ? (imperial ? names_de_mph[i] : names_de[i]) : (imperial ? names_mph[i] : names_en[i]), text, 0, -1);
            }
            row_add(confirm_clear ? tr(L"Sicher? Nochmal Enter", L"Sure? Enter again") : tr(L"Alle Rekorde löschen", L"Clear all records"),
                    L"", 0, 0);
            hint(0);
            break;
        }
        case LEVEL_WORLD:
            wcopy(title, 0, 64, tr(L"Welt", L"World"));
            for (unsigned i = 0; i < 3; ++i)
                row_add(tr(world_german[i], world_english[i]), rig_setting_get(world_ids[i]) ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0,
                        (int)i);
            row_add(tr(L"Wirkt nach Spiel-Neustart", L"Takes effect after a restart"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        case LEVEL_DAMAGE:
            wcopy(title, 0, 64, tr(L"Kein Schaden an Autos", L"No damage on cars"));
            for (int i = 0; i < NO_DAMAGE_COUNT; ++i)
                row_add(tr(damage_german[i], damage_english[i]), i == no_damage_mode() ? tr(L"✓ aktiv", L"✓ active") : L"", 0, i);
            row_add(tr(L"Andere = Drivatare und Verkehr", L"Others = Drivatars and traffic"), L"", NOTIFY_ROW_INFO, -1);
            row_add(tr(L"Nur sichtbarer Schaden, sofort", L"Visible damage only, at once"), L"", NOTIFY_ROW_INFO, -1);
            hint(0);
            break;
        case LEVEL_RESET_ALL:
            wcopy(title, 0, 64, tr(L"Alle Tasten auf Standard?", L"All keys to default?"));
            row_add(tr(L"Nein, zurück", L"No, back"), L"", 0, 0);
            row_add(tr(L"Ja, alle zurücksetzen", L"Yes, reset all"), L"", 0, 1);
            hint(0);
            break;
    }
    int selectable = selectable_count();
    if (m.sel[m.level] >= selectable) m.sel[m.level] = selectable ? selectable - 1 : 0;
    if (m.sel[m.level] < 0) m.sel[m.level] = 0;
    int k = 0;
    for (unsigned i = 0; i < row_count; ++i) {
        if (row_action[i] < 0) continue;
        if (k++ == m.sel[m.level]) rows[i].style = NOTIFY_ROW_SELECTED;
    }
}

static void show(void) {
    wchar_t title[64];
    build_page(title);
    notify_panel_show(title, rows, row_count, 1);
}

static void open_menu(void) {
    m.german = game_language_is_german(keyboard_game_image());
    m.level = LEVEL_GROUPS;
    for (unsigned i = 0; i < LEVEL_COUNT; ++i) m.sel[i] = 0;
    style_saved = notify_style();
    keyboard_block_input(1);
    show();
    log_append("KEYMENU: opened\r\n");
}

static volatile LONG pad_swallow; /* after closing: the controller's buttons stay away from the game until released */

static void close_menu(void) {
    if (m.level == LEVEL_STYLE) notify_set_style(style_saved); /* F8 closed during the preview */
    if (m.level == LEVEL_COLOR) notify_set_color(rig_setting_get(RIG_GRIP_CARD_COLOR));
    InterlockedExchange(&pad_swallow, 1);
    m.level = LEVEL_CLOSED;
    keyboard_block_input(0);
    keyboard_key_list_restore();
    log_append("KEYMENU: closed\r\n");
}

static void back(void) {
    switch (m.level) {
        case LEVEL_GROUPS: close_menu(); return;
        case LEVEL_LIST: m.level = LEVEL_GROUPS; break;
        case LEVEL_RESET_ALL: m.level = LEVEL_GROUPS; break;
        case LEVEL_STYLE: notify_set_style(style_saved); m.level = LEVEL_GROUPS; break; /* end of the preview */
        case LEVEL_SETTINGS: m.level = LEVEL_GROUPS; break;
        case LEVEL_CARDS: m.level = LEVEL_GROUPS; break;
        case LEVEL_CRUISE: m.level = LEVEL_GROUPS; break;
        case LEVEL_TRAFFIC: m.level = LEVEL_GROUPS; break;
        case LEVEL_COLOR: notify_set_color(rig_setting_get(RIG_GRIP_CARD_COLOR)); m.level = LEVEL_SETTINGS; break;
        case LEVEL_VIEW_FORWARD: m.level = LEVEL_SETTINGS; break;
        case LEVEL_VIEW_UP: m.level = LEVEL_SETTINGS; break;
        case LEVEL_MOUSE: m.level = LEVEL_GROUPS; break;
        case LEVEL_TELEMETRY: m.level = LEVEL_GROUPS; break;
        case LEVEL_WORLD: m.level = LEVEL_GROUPS; break;
        case LEVEL_DAMAGE: m.level = LEVEL_GROUPS; break;
        case LEVEL_LOGBOOK: m.level = LEVEL_GROUPS; break;
        case LEVEL_RECORDS: confirm_clear = 0; m.level = LEVEL_GROUPS; break;
        case LEVEL_BINDING: m.level = LEVEL_LIST; break;
        case LEVEL_CAPTURE: m.level = LEVEL_BINDING; break;
    }
    show();
}

/* design and colour pages: the row the arrows stand on is shown at once (menu and a sample card);
   Enter keeps it, Esc goes back to the saved one (back(), close_menu()) */
static void preview(void) {
    if (m.level != LEVEL_STYLE && m.level != LEVEL_COLOR) return;
    int i = selected_action();
    if (i < 0) return;
    wchar_t line[96];
    unsigned n;
    if (m.level == LEVEL_STYLE) {
        notify_set_style(i);
        n = wcopy(line, 0, 96, tr(L"Vorschau: ", L"Preview: "));
        n = wcopy(line, n, 96, style_names[i]);
    } else {
        notify_set_color(i);
        n = wcopy(line, 0, 96, tr(L"Vorschau: ", L"Preview: "));
        n = wcopy(line, n, 96, rig_color_choice(keyboard_game_image(), (unsigned)i));
    }
    wcopy(line, n, 96, tr(L"\nEnter übernimmt, Esc zurück", L"\nEnter keeps it, Esc goes back"));
    char text[192];
    unsigned t = 0;
    utf8(line, text, sizeof(text), &t);
    text[t] = 0;
    notify_show_step(text, PREVIEW_TAG, 2500);
}

static void apply(unsigned index, const unsigned char vk[4]) {
    keyboard_binding_set(index, vk);
    int saved = save_bindings(index, index + 1);
    log_binding(index);
    change_card(index, saved);
}

static void choose(void) {
    int action = selected_action();
    if (action < 0) return;
    switch (m.level) {
        case LEVEL_GROUPS:
            if (action == TOP_REPORT) {
                report_create_lang(m.german);
            } else if (action == TOP_RESET) {
                m.level = LEVEL_RESET_ALL;
                m.sel[LEVEL_RESET_ALL] = 0;
            } else if (action == TOP_RECORDS) {
                m.level = LEVEL_RECORDS;
                m.sel[LEVEL_RECORDS] = 0;
                confirm_clear = 0;
            } else if (action == TOP_LOGBOOK) {
                m.level = LEVEL_LOGBOOK;
                m.sel[LEVEL_LOGBOOK] = 0;
            } else if (action == TOP_DAMAGE) {
                m.level = LEVEL_DAMAGE;
                m.sel[LEVEL_DAMAGE] = no_damage_mode();
            } else if (action == TOP_WORLD) {
                m.level = LEVEL_WORLD;
                m.sel[LEVEL_WORLD] = 0;
            } else if (action == TOP_TELEMETRY) {
                m.level = LEVEL_TELEMETRY;
                m.sel[LEVEL_TELEMETRY] = 0;
            } else if (action == TOP_MOUSE) {
                m.level = LEVEL_MOUSE;
                m.sel[LEVEL_MOUSE] = mouse_choice();
            } else if (action == TOP_STYLE) {
                m.level = LEVEL_STYLE;
                m.sel[LEVEL_STYLE] = style_saved;
            } else if (action == TOP_CRUISE) {
                m.level = LEVEL_CRUISE;
                m.sel[LEVEL_CRUISE] = 0;
            } else if (action == TOP_SETTINGS) {
                m.level = LEVEL_SETTINGS;
                m.sel[LEVEL_SETTINGS] = 0;
            } else if (action == TOP_CARDS) {
                m.level = LEVEL_CARDS;
                m.sel[LEVEL_CARDS] = 0;
            } else if (action == TOP_TRAFFIC) {
                int now = rig_setting_get(RIG_TRAFFIC_PRESET);
                m.level = LEVEL_TRAFFIC;
                m.sel[LEVEL_TRAFFIC] = now > 0 ? now - 1 : 0;
            } else {
                m.group = action;
                m.level = LEVEL_LIST;
                m.sel[LEVEL_LIST] = 0;
            }
            break;
        case LEVEL_LIST:
            m.binding = action;
            m.level = LEVEL_BINDING;
            m.sel[LEVEL_BINDING] = 0;
            break;
        case LEVEL_BINDING: {
            unsigned char vk[4];
            keyboard_binding_vks((unsigned)m.binding, vk);
            if (action == ACT_REPLACE || action == ACT_ADD) {
                if (action == ACT_ADD && key_count(vk) >= 4) {
                    notify_show(m.german ? "Schon 4 Tasten\nZuerst eine ersetzen" : "Already 4 keys\nReplace one first");
                    return;
                }
                m.add = action == ACT_ADD;
                m.armed = 0;
                m.level = LEVEL_CAPTURE;
            } else if (action == ACT_DEFAULT) {
                keyboard_binding_default_vks((unsigned)m.binding, vk);
                apply((unsigned)m.binding, vk);
            } else if (action == ACT_CLEAR) {
                unsigned char none[4] = {0, 0, 0, 0};
                apply((unsigned)m.binding, none);
            }
            break;
        }
        case LEVEL_TELEMETRY: {
            if (action >= TELE_LOOK_POS && action <= TELE_LOOK_SIZE) {
                tele_look_step(action, 2);
                break;
            }
            if (action == 50) {
                int on = !records_csv_on();
                if (on && !telemetry_enabled() && !rig_setting_set(RIG_GRIP_TELEMETRY, 1)) telemetry_set_enabled(1); /* the CSV needs the panel's values */
                records_csv_set(on);
                notify_show(on ? (m.german ? "CSV-Aufzeichnung an\nOrdner Azimuth\\Gripline_Telemetry" : "CSV recording on\nFolder Azimuth\\Gripline_Telemetry")
                               : (m.german ? "CSV-Aufzeichnung aus" : "CSV recording off"));
            } else if (action == 0) {
                int on = !telemetry_enabled();
                if (!rig_setting_set(RIG_GRIP_TELEMETRY, on)) telemetry_set_enabled(on); /* rig_settings saves it */
                log_append(on ? "KEYMENU: telemetry on\r\n" : "KEYMENU: telemetry off\r\n");
            } else {
                int mask = telemetry_show() ^ (1 << (action - 1));
                telemetry_set_show(mask);
                if (!save_gripline_int("TelemetryShow", mask)) log_append("KEYMENU: Gripline.ini could not be written - telemetry rows not saved.\r\n");
            }
            break;
        }
        case LEVEL_RECORDS:
            if (action == 0) {
                if (!confirm_clear) confirm_clear = 1;
                else {
                    confirm_clear = 0;
                    records_clear();
                    notify_show(m.german ? "Rekorde gelöscht" : "Records cleared");
                }
            }
            break;
        case LEVEL_DAMAGE: {
            if (action < 0 || action >= NO_DAMAGE_COUNT) break;
            no_damage_set_mode(action);
            int saved = save_gripline_digit("NoDamage", action);
            char text[192];
            unsigned t = 0;
            wchar_t line[128];
            unsigned n = wcopy(line, 0, 128, tr(L"Kein Schaden: ", L"No damage: "));
            n = wcopy(line, n, 128, tr(damage_german[action], damage_english[action]));
            if (!saved) wcopy(line, n, 128, tr(L"\nNur bis zum Spielende (nicht gespeichert)", L"\nUntil the game closes (not saved)"));
            utf8(line, text, sizeof(text), &t);
            text[t] = 0;
            notify_show(text);
            log_append_value("KEYMENU: no damage mode ", action);
            break;
        }
        case LEVEL_WORLD: {
            if (action >= 0 && action < 3) {
                unsigned id = world_ids[action];
                int on = !rig_setting_get(id);
                rig_setting_set(id, on); /* saves Gripline.ini and shows the restart card */
                log_append(on ? "KEYMENU: world option on\r\n" : "KEYMENU: world option off\r\n");
            }
            break;
        }
        case LEVEL_MOUSE: {
            if (action >= DEAD_FIRST) {
                int i = action - DEAD_FIRST;
                keyboard_set_mouse_dead_zone(dead_values[i]);
                int saved = save_mouse("MouseDeadZone", dead_values[i]);
                char text[192];
                unsigned t = 0;
                wchar_t line[128];
                unsigned n = wcopy(line, 0, 128, tr(L"Totzone überspringen: ", L"Skip dead zone: "));
                n = wcopy(line, n, 128, dead_names[i]);
                wcopy(line, n, 128, saved ? tr(L"\nHöher = Auto lenkt früher", L"\nHigher = the car steers sooner")
                                          : tr(L"\nNur bis zum Spielende (nicht gespeichert)", L"\nUntil the game closes (not saved)"));
                utf8(line, text, sizeof(text), &t);
                text[t] = 0;
                notify_show(text);
                log_append_value("KEYMENU: mouse dead zone percent ", dead_values[i]);
                break;
            }
            if (action >= CENTER_FIRST) {
                int i = action - CENTER_FIRST;
                keyboard_set_mouse_center(center_values[i]);
                int saved = save_mouse("MouseCenter", center_values[i]);
                char text[192];
                unsigned t = 0;
                wchar_t line[128];
                unsigned n = wcopy(line, 0, 128, tr(L"Lenkrad automatisch gerade: ", L"Wheel back to straight: "));
                n = wcopy(line, n, 128, tr(center_german[i], center_english[i]));
                wcopy(line, n, 128, saved ? tr(L"\nGilt, sobald die Maus ruht", L"\nApplies when the mouse rests")
                                          : tr(L"\nNur bis zum Spielende (nicht gespeichert)", L"\nUntil the game closes (not saved)"));
                utf8(line, text, sizeof(text), &t);
                text[t] = 0;
                notify_show(text);
                log_append_value("KEYMENU: mouse back to centre after seconds ", center_values[i]);
                break;
            }
            keyboard_set_mouse_steering(mouse_values[action]);
            int saved = save_mouse("MouseSteering", mouse_values[action]);
            char text[192];
            unsigned t = 0;
            wchar_t line[128];
            unsigned n = wcopy(line, 0, 128, tr(L"Maus-Lenkung: ", L"Mouse steering: "));
            n = wcopy(line, n, 128, tr(mouse_german[action], mouse_english[action]));
            wcopy(line, n, 128, !saved ? tr(L"\nNur bis zum Spielende (nicht gespeichert)", L"\nUntil the game closes (not saved)")
                               : action ? tr(L"\nMaus links/rechts lenkt beim Fahren", L"\nMove the mouse left/right while driving")
                                        : tr(L"\nLenken nur mit Tasten und Controller", L"\nSteering with keys and controller only"));
            utf8(line, text, sizeof(text), &t);
            text[t] = 0;
            notify_show(text);
            log_append_value("KEYMENU: mouse steering ", mouse_values[action]);
            break;
        }
        case LEVEL_STYLE: {
            notify_set_style(action);
            style_saved = action;
            int saved = save_style(action);
            char text[160];
            unsigned t = 0;
            wchar_t line[96];
            unsigned n = wcopy(line, 0, 96, tr(L"Karten-Design: ", L"Card design: "));
            n = wcopy(line, n, 96, style_names[action]);
            wcopy(line, n, 96, saved ? tr(L"\nSo sehen deine Karten jetzt aus", L"\nThis is how your cards look now")
                                     : tr(L"\nNur bis zum Spielende (nicht gespeichert)", L"\nUntil the game closes (not saved)"));
            utf8(line, text, sizeof(text), &t);
            text[t] = 0;
            notify_show(text);
            log_append(action == 0 ? "KEYMENU: card design Classic\r\n" : action == 1 ? "KEYMENU: card design Enhanced\r\n"
                                                                          : "KEYMENU: card design Overdrive\r\n");
            break;
        }
        case LEVEL_SETTINGS:
            if (action == SET_COLOR) {
                m.level = LEVEL_COLOR;
                m.sel[LEVEL_COLOR] = rig_setting_get(RIG_GRIP_CARD_COLOR);
            } else if (action == SET_STEERING) {
                rig_setting_set(RIG_GRIP_STEERING, !rig_setting_get(RIG_GRIP_STEERING)); /* saves, card "after a restart" */
            } else if (action == SET_VIEW_FORWARD || action == SET_VIEW_UP) {
                int forward = action == SET_VIEW_FORWARD;
                int low = forward ? VIEW_FORWARD_MIN : VIEW_UP_MIN, high = forward ? VIEW_FORWARD_MAX : VIEW_UP_MAX;
                int now = rig_setting_get(forward ? RIG_GRIP_VIEW_FORWARD : RIG_GRIP_VIEW_UP);
                if (now < low) now = low;
                if (now > high) now = high;
                m.level = forward ? LEVEL_VIEW_FORWARD : LEVEL_VIEW_UP;
                m.sel[m.level] = (now - low + VIEW_STEP / 2) / VIEW_STEP;
            } else if (action == SET_UPDATE) {
                int on = !update_check_enabled();
                int saved = save_gripline_digit("UpdateCheck", on);
                update_check_remember(on);
                notify_show(saved ? (on ? (m.german ? "Update-Hinweis an\nab dem nächsten Start" : "Update notice on\nfrom the next start")
                                        : (m.german ? "Update-Hinweis aus\nGripline fragt GitHub nicht mehr" : "Update notice off\nGripline no longer asks GitHub"))
                                  : (m.german ? "Update-Hinweis\nNicht gespeichert" : "Update notice\nNot saved"));
                log_append(on ? "KEYMENU: update check on\r\n" : "KEYMENU: update check off\r\n");
            }
            break;
        case LEVEL_COLOR:
            if (action >= 0 && action < NOTIFY_COLOR_COUNT) {
                if (!rig_setting_set(RIG_GRIP_CARD_COLOR, action)) notify_set_color(action); /* rig_settings saves it */
                log_append_value("KEYMENU: card colour ", action);
            }
            break;
        case LEVEL_VIEW_FORWARD:
        case LEVEL_VIEW_UP: {
            int forward = m.level == LEVEL_VIEW_FORWARD;
            int value = (forward ? VIEW_FORWARD_MIN : VIEW_UP_MIN) + action;
            rig_setting_set(forward ? RIG_GRIP_VIEW_FORWARD : RIG_GRIP_VIEW_UP, value); /* live, saved, card */
            log_append_value(forward ? "KEYMENU: cockpit view 2 forward cm " : "KEYMENU: cockpit view 2 up cm ", value);
            break;
        }
        case LEVEL_TRAFFIC:
            if (action >= 1 && action < RIG_DENSITY_COUNT) {
                rig_setting_set(RIG_TRAFFIC_PRESET, action); /* saved, card "after a restart" */
                log_append_value("KEYMENU: traffic preset ", action);
            }
            break;
        case LEVEL_CRUISE: {
            int crash = cruise_crash_off(), speed = cruise_min_speed();
            if (action == 0) crash = !crash;
            else if (action >= CRUISE_FIRST && action < CRUISE_FIRST + CRUISE_CHOICES) speed = cruise_speeds[action - CRUISE_FIRST];
            else break;
            cruise_set_options(crash, speed);
            int saved = save_gripline_digit("CruiseCrashOff", crash) && save_gripline_int("CruiseMinSpeed", speed);
            wchar_t line[128], v[32];
            unsigned n;
            if (action == 0) {
                n = wcopy(line, 0, 128, crash ? tr(L"Tempomat: aus bei Aufprall", L"Cruise control: off on a crash")
                                              : tr(L"Tempomat: bleibt bei Aufprall an", L"Cruise control: stays on in a crash"));
            } else {
                n = wcopy(line, 0, 128, tr(L"Tempomat: an ab ", L"Cruise control: on from "));
                speed_text(speed, v);
                n = wcopy(line, n, 128, v);
            }
            wcopy(line, n, 128, saved ? tr(L"\nGespeichert", L"\nSaved") : tr(L"\nNur bis zum Spielende (nicht gespeichert)", L"\nUntil the game closes (not saved)"));
            char text[192];
            unsigned t = 0;
            utf8(line, text, sizeof(text), &t);
            text[t] = 0;
            notify_show(text);
            log_append_value("KEYMENU: cruise control from km/h ", speed);
            log_append_value("KEYMENU: cruise control off on a crash ", crash);
            break;
        }
        case LEVEL_CARDS: {
            int hidden = notify_hidden_kinds();
            if (action >= 0 && action < CARDS_PRESET_COUNT) hidden = cards_presets[action];
            else if (action >= CARDS_KIND_FIRST && action < CARDS_KIND_FIRST + NOTIFY_KIND_COUNT) hidden ^= 1 << (action - CARDS_KIND_FIRST);
            else break;
            notify_set_hidden_kinds(hidden);
            int saved = save_gripline_int("CardsHidden", hidden);
            if (!saved) notify_show_tinted(m.german ? "Karten & Hinweise\nNur bis zum Spielende (nicht gespeichert)"
                                                    : "Cards & notices\nUntil the game closes (not saved)",
                                           NOTIFY_COLOR_ORANGE, m.german ? "HINWEIS" : "NOTE", 0);
            log_append_value("KEYMENU: hidden card groups ", hidden);
            break;
        }
        case LEVEL_RESET_ALL:
            if (action == 1) {
                unsigned count = keyboard_binding_count();
                for (unsigned i = 0; i < count; ++i) {
                    unsigned char vk[4];
                    keyboard_binding_default_vks(i, vk);
                    keyboard_binding_set(i, vk);
                }
                int saved = save_bindings(0, count);
                log_append("KEYMENU: all keys back to default\r\n");
                if (saved) notify_show(m.german ? "Alle Tasten auf Standard\nGespeichert" : "All keys back to default\nSaved");
                else notify_show_tinted(m.german ? "Alle Tasten auf Standard\nNicht gespeichert" : "All keys back to default\nNot saved",
                                        NOTIFY_COLOR_ORANGE, m.german ? "HINWEIS" : "NOTE", 0);
            }
            m.level = LEVEL_GROUPS;
            break;
    }
    show();
}

static void captured(unsigned char key) {
    unsigned index = (unsigned)m.binding;
    if ((int)index == menu_key_index() && is_menu_key(key)) {
        notify_show(m.german ? "Diese Taste braucht das Menü selbst\nBitte eine andere wählen"
                             : "The menu needs this key itself\nPlease pick another one");
        return; /* stays waiting */
    }
    unsigned char vk[4];
    keyboard_binding_vks(index, vk);
    if (m.add) {
        int n = key_count(vk), there = 0;
        for (int k = 0; k < n; ++k) there |= vk[k] == key;
        if (!there && n < 4) vk[n] = key;
    } else {
        vk[0] = key, vk[1] = vk[2] = vk[3] = 0;
    }
    apply(index, vk);
    m.level = LEVEL_BINDING;
    show();
}

/* ------------------------------------------------------------------ input */

/* controller (key_menu_pad_input): bits below, written on the game's input thread */
#define PAD_UP 1
#define PAD_DOWN 2
#define PAD_A 4
#define PAD_B 8
#define PAD_LEFT 16
#define PAD_RIGHT 32
static volatile LONG pad_now, pad_toggle;
static volatile LONG64 pad_tick;
static LONG pad_was;

/* Marvin 02.11. (after an FH2MP update): the menu opened from the controller but could neither be
   moved nor closed. The game's pad buffer may be read more than once per frame and we blank it
   while the menu is open, so the menu then saw only the blank. Now the menu reads the controller
   itself through XInput (loaded at run time: no new imports); the hook only keeps the game out. */
typedef struct {
    unsigned long packet;
    unsigned short buttons;
    unsigned char lt, rt;
    short lx, ly, rx, ry;
} XPadState;
typedef unsigned long(WINAPI *XInputGetStateFn)(unsigned long, XPadState *);
static XInputGetStateFn xinput_get;
static volatile LONG xinput_tried, xinput_ok;

static void xinput_load(void) {
    if (InterlockedExchange(&xinput_tried, 1)) return;
    static const wchar_t *names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (unsigned i = 0; i < 3 && !xinput_get; ++i) {
        HMODULE dll = LoadLibraryExW(names[i], NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dll) xinput_get = (XInputGetStateFn)(uintptr_t)GetProcAddress(dll, "XInputGetState");
    }
    if (xinput_get) InterlockedExchange(&xinput_ok, 1);
    log_append(xinput_get ? "KEYMENU: controller read through XInput.\r\n"
                          : "KEYMENU: XInput not found - controller read from the game.\r\n");
}

/* the menu's own controller reading: bits PAD_*, toggle on LS + D-pad right; 0 when none plugged in */
static LONG xinput_bits(int *toggle) {
    static unsigned short before;
    unsigned short buttons = 0;
    short ly = 0, lx = 0;
    for (unsigned long u = 0; u < 4; ++u) {
        XPadState st;
        memset(&st, 0, sizeof(st));
        if (xinput_get(u, &st) != 0) continue;
        buttons |= st.buttons;
        if ((st.ly < 0 ? -st.ly : st.ly) > (ly < 0 ? -ly : ly)) ly = st.ly;
        if ((st.lx < 0 ? -st.lx : st.lx) > (lx < 0 ? -lx : lx)) lx = st.lx;
    }
    unsigned short pressed = (unsigned short)(buttons & ~before);
    before = buttons;
    *toggle = (buttons & 0x0040u) && (pressed & 0x0008u);
    LONG bits = 0;
    if ((buttons & 0x0001u) || ly > 20000) bits |= PAD_UP;
    if ((buttons & 0x0002u) || ly < -20000) bits |= PAD_DOWN;
    if (buttons & 0x1000u) bits |= PAD_A;
    if (buttons & 0x2000u) bits |= PAD_B;
    if ((buttons & 0x0004u) || lx < -20000) bits |= PAD_LEFT;
    if (((buttons & 0x0008u) && !(buttons & 0x0040u)) || lx > 20000) bits |= PAD_RIGHT; /* LS + right = the menu toggle */
    return bits;
}

int key_menu_pad_input(unsigned char *gamepad) {
    if (!gamepad) return 0;
    unsigned short buttons;
    short ly;
    memory_copy(&buttons, gamepad, sizeof(buttons));
    memory_copy(&ly, gamepad + 6, sizeof(ly));
    static unsigned short before;
    unsigned short pressed = (unsigned short)(buttons & ~before);
    before = buttons;
    if (!xinput_ok && (buttons & 0x0040u) && (pressed & 0x0008u)) InterlockedExchange(&pad_toggle, 1); /* LS + D-pad right */
    LONG bits = 0;
    if ((buttons & 0x0001u) || ly > 20000) bits |= PAD_UP;
    if ((buttons & 0x0002u) || ly < -20000) bits |= PAD_DOWN;
    if (buttons & 0x1000u) bits |= PAD_A;
    if (buttons & 0x2000u) bits |= PAD_B;
    short lx;
    memory_copy(&lx, gamepad + 4, sizeof(lx));
    if ((buttons & 0x0004u) || lx < -20000) bits |= PAD_LEFT;
    if (((buttons & 0x0008u) && !(buttons & 0x0040u)) || lx > 20000) bits |= PAD_RIGHT;
    InterlockedExchange(&pad_now, bits);
    InterlockedExchange64(&pad_tick, (LONG64)GetTickCount64());
    int take = m.level != LEVEL_CLOSED || (buttons & 0x0040u);
    if (m.level != LEVEL_CLOSED) {
        unsigned char zero[12] = {0}; /* buttons, triggers, sticks: nothing for the game */
        memory_copy(gamepad, zero, sizeof(zero));
    } else if (pad_swallow) {
        if (buttons & 0x300fu) { /* A, B, D-pad still held from the menu */
            buttons &= (unsigned short)~0x300fu;
            memory_copy(gamepad, &buttons, sizeof(buttons));
        } else {
            InterlockedExchange(&pad_swallow, 0);
        }
    } else if (buttons & 0x0040u) {
        buttons &= (unsigned short)~0x0008u; /* the D-pad right of the combination stays away from the game */
        memory_copy(gamepad, &buttons, sizeof(buttons));
        take = 0;
    }
    return take;
}

static int edge(unsigned char vk, int now_down) {
    int was = m.held[vk];
    m.held[vk] = (unsigned char)now_down;
    return now_down && !was;
}

static void key_menu_tick(ULONGLONG now) {
    int menu_index = menu_key_index();
    unsigned char menu_vk[4] = {0};
    if (menu_index >= 0) keyboard_binding_vks((unsigned)menu_index, menu_vk);
    int menu_down = 0;
    for (int k = 0; k < 4 && menu_vk[k]; ++k) menu_down |= keyboard_key_down(menu_vk[k]);
    int menu_pressed = menu_down && !m.menu_key_held;
    m.menu_key_held = menu_down;

    /* the controller counts like the arrow keys, Enter and Esc */
    LONG pad = GetTickCount64() - (ULONGLONG)pad_tick < 300 ? pad_now : 0;
    if (xinput_ok) {
        int toggle = 0;
        pad = xinput_bits(&toggle);
        if (toggle) InterlockedExchange(&pad_toggle, 1);
    }
    LONG pad_pressed = pad & ~pad_was;
    pad_was = pad;
    if (InterlockedExchange(&pad_toggle, 0)) menu_pressed = 1;
    int up = keyboard_key_down(VK_UP_), down = keyboard_key_down(VK_DOWN_);
    int enter = edge(VK_ENTER_, keyboard_key_down(VK_ENTER_)) || (pad_pressed & PAD_A);
    int esc = edge(VK_ESC_, keyboard_key_down(VK_ESC_)) || (pad_pressed & PAD_B);
    int bs = edge(VK_BACK_, keyboard_key_down(VK_BACK_));
    int up_pressed = edge(VK_UP_, up) || (pad_pressed & PAD_UP), down_pressed = edge(VK_DOWN_, down) || (pad_pressed & PAD_DOWN);
    up = up || (pad & PAD_UP);
    down = down || (pad & PAD_DOWN);

    if (m.level == LEVEL_CLOSED) {
        if (menu_pressed && !notify_enabled()) {
            log_append("KEYMENU: not opened - Gripline cards are off (Notifications=0), the menu would not be seen.\r\n");
        } else if (menu_pressed) {
            open_menu();
        }
        return;
    }
    if (m.level == LEVEL_CAPTURE) {
        if (!m.armed) { /* the Enter that chose "set a new key" is still held */
            if (!any_key_down()) m.armed = 1;
            return;
        }
        if (esc) {
            back();
            return;
        }
        unsigned char key = capture_key();
        if (key) captured(key);
        return;
    }
    if (menu_pressed) {
        close_menu();
        return;
    }
    if (esc || bs) {
        back();
        return;
    }
    if (enter) {
        choose();
        return;
    }
    int left = edge(VK_LEFT_, keyboard_key_down(VK_LEFT_)) || (pad_pressed & PAD_LEFT);
    int right = edge(VK_RIGHT_, keyboard_key_down(VK_RIGHT_)) || (pad_pressed & PAD_RIGHT);
    if ((left || right) && m.level == LEVEL_TELEMETRY) {
        int action = selected_action();
        if (action >= TELE_LOOK_POS && action <= TELE_LOOK_SIZE) {
            tele_look_step(action, left ? -1 : 1);
            show();
            return;
        }
    }
    int step = 0;
    if (up_pressed || down_pressed) {
        step = up_pressed ? -1 : 1;
        m.repeat_at = now + REPEAT_DELAY_MS;
    } else if ((up || down) && now >= m.repeat_at) {
        step = up ? -1 : 1;
        m.repeat_at = now + REPEAT_STEP_MS;
    }
    if (step) {
        int n = selectable_count();
        if (n > 0) m.sel[m.level] = (m.sel[m.level] + step + n) % n;
        preview();
        show();
    }
}

/* start card of its own (plugin start): "Press F8 to change your keys" with the key bound now */
int key_menu_start_card(char *out, unsigned size) {
    int index = menu_key_index();
    unsigned char vk[4] = {0};
    if (index >= 0) keyboard_binding_vks((unsigned)index, vk);
    if (!vk[0] || size < 64) return 0;
    vk[1] = vk[2] = vk[3] = 0;
    int german = game_language_is_german(keyboard_game_image());
    wchar_t key[24], line[128];
    keyboard_keys_text(vk, german, key, 24);
    unsigned n = 0;
    if (german) {
        n = wcopy(line, n, 128, L"Tasten ändern: ");
        n = wcopy(line, n, 128, key);
        n = wcopy(line, n, 128, L" drücken\nTastenmenü mit Pfeilen, Enter und Esc");
    } else {
        n = wcopy(line, n, 128, L"Press ");
        n = wcopy(line, n, 128, key);
        n = wcopy(line, n, 128, L" to change your keys\nKey menu: arrows, Enter, Esc");
    }
    unsigned t = 0;
    utf8(line, out, size, &t);
    out[t] = 0;
    return 1;
}

static void set_path(void) {
    const wchar_t *path = keyboard_ini_path();
    if (path) wcopy(ini_path, 0, PATH_CAPACITY, path);
}

#ifndef FH2_TWEAKS_TEST
static DWORD WINAPI menu_thread(LPVOID unused) {
    (void)unused;
    xinput_load();
    for (;;) {
        key_menu_tick(GetTickCount64());
        Sleep(30);
    }
}
#endif

void key_menu_start(void) {
    static int started;
    if (started) return;
    started = 1;
    set_path();
#ifndef FH2_TWEAKS_TEST
    HANDLE thread = CreateThread(NULL, 0, menu_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else log_append("KEYMENU: no thread - key menu off.\r\n");
#endif
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestKeyMenuPad(unsigned buttons, int ly) {
    unsigned char pad[12] = {0};
    unsigned short b = (unsigned short)buttons;
    short y = (short)ly;
    memory_copy(pad, &b, 2);
    memory_copy(pad + 6, &y, 2);
    int taken = key_menu_pad_input(pad);
    unsigned short after;
    memory_copy(&after, pad, 2);
    return taken | (after ? 0 : 2); /* 2: nothing left for the game */
}
__declspec(dllexport) int Fh2TestKeyMenuXInput(void) {
    xinput_load();
    return (int)xinput_ok;
}
__declspec(dllexport) int Fh2TestKeyMenuTick(unsigned now_ms) {
    if (!ini_path[0]) set_path();
    key_menu_tick(now_ms);
    return m.level;
}
/* bindings in the menu's pages: count of distinct ones, -1 if one is listed twice or unknown */
__declspec(dllexport) int Fh2TestKeyMenuStartCard(char *out, unsigned size) {
    return key_menu_start_card(out, size);
}
__declspec(dllexport) int Fh2TestKeyMenuCover(void) {
    unsigned char seen[64] = {0};
    int n = 0;
    for (unsigned g = 0; g < GROUP_COUNT; ++g)
        for (int i = 0; i < group_size((int)g); ++i) {
            int b = group_binding((int)g, i);
            if (b < 0 || b >= 64 || seen[b]) return -1;
            seen[b] = 1;
            ++n;
        }
    return n;
}
__declspec(dllexport) int Fh2TestKeyMenuSelected(wchar_t *label) {
    for (unsigned i = 0; i < row_count; ++i)
        if (rows[i].style == NOTIFY_ROW_SELECTED) {
            memory_copy(label, rows[i].label, sizeof(rows[i].label));
            return (int)i;
        }
    return -1;
}
#endif
