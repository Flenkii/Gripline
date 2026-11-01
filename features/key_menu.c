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

#define VK_UP_ 0x26
#define VK_DOWN_ 0x28
#define VK_ENTER_ 0x0d
#define VK_ESC_ 0x1b
#define VK_BACK_ 0x08
#define REPEAT_DELAY_MS 350u
#define REPEAT_STEP_MS 90u
#define FILE_LIMIT 32768u
#define MAX_ROWS (NOTIFY_PANEL_ROWS - 1)

enum { LEVEL_CLOSED, LEVEL_GROUPS, LEVEL_LIST, LEVEL_BINDING, LEVEL_CAPTURE, LEVEL_RESET_ALL, LEVEL_STYLE, LEVEL_MOUSE, LEVEL_TELEMETRY, LEVEL_WORLD, LEVEL_LOGBOOK, LEVEL_RECORDS, LEVEL_SPRINT, LEVEL_COUNT };

/* first page: rows after the binding pages */
enum { TOP_STYLE = 100, TOP_RESET = 101, TOP_MOUSE = 102, TOP_TELEMETRY = 103, TOP_WORLD = 104, TOP_LOGBOOK = 105, TOP_RECORDS = 106, TOP_SPRINT = 107 };

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
static int save_bindings(unsigned first, unsigned end) {
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

/* Gripline.ini next to FH2_Keyboard.ini: [Gripline] CardStyle */
static int save_style(int style) {
    static wchar_t path[PATH_CAPACITY];
    unsigned n = wcopy(path, 0, PATH_CAPACITY, ini_path), cut = 0;
    for (unsigned i = 0; i < n; ++i)
        if (path[i] == L'\\' || path[i] == L'/') cut = i + 1;
    if (!cut) return 0;
    path[cut] = 0;
    wcopy(path, cut, PATH_CAPACITY, L"Gripline.ini");
    size_t length;
    if (!read_file_at(path, &length)) return 0;
    char value[2] = {(char)('0' + style), 0};
    size_t written = ini_set_value(file_text, length, "Gripline", "CardStyle", value, edit_text[0], FILE_LIMIT + 1);
    if (!written || written > FILE_LIMIT || !write_file_at(path, edit_text[0], written)) {
        log_append("KEYMENU: Gripline.ini could not be written - card design not saved.\r\n");
        return 0;
    }
    return 1;
}

/* FH2_Keyboard.ini [Keyboard] MouseSteering */
static int save_mouse(const char *key, int value) {
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
static int save_gripline_int(const char *key, int value) {
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
    row_add(tr(L"↑↓ wählen · Enter OK", L"↑↓ choose · Enter OK"),
            first_page ? tr(L"Esc schließen", L"Esc close") : tr(L"Esc zurück", L"Esc back"), NOTIFY_ROW_HINT, -1);
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

static void build_page(wchar_t *title) {
    row_count = 0;
    title[0] = 0;
    unsigned char vk[4];
    wchar_t keys[32];
    switch (m.level) {
        case LEVEL_GROUPS:
            wcopy(title, 0, 64, tr(L"Gripline-Menü", L"Gripline menu"));
            for (unsigned g = 0; g < GROUP_COUNT; ++g) row_add(tr(groups[g].german, groups[g].english), L"›", 0, (int)g);
            row_add(tr(L"Maus-Lenkung (experimentell)", L"Mouse steering (experimental)"), tr(mouse_german[mouse_choice()], mouse_english[mouse_choice()]), 0,
                    TOP_MOUSE);
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
            row_add(tr(L"Rekorde", L"Records"), L"›", 0, TOP_RECORDS);
            {
                float sm;
                int ss = records_sprint_state(&sm);
                row_add(tr(L"Eigene Sprints", L"Own sprints"), ss == 2 ? tr(L"aktiv", L"active") : ss == 1 ? tr(L"Ziel fehlt", L"no finish") : L"›",
                        0, TOP_SPRINT);
            }
            row_add(tr(L"Welt: Blitzer, Namen, Symbole", L"World: cameras, names, icons"), L"›", 0, TOP_WORLD);
            row_add(tr(L"Karten-Design", L"Card design"), style_names[notify_style()], 0, TOP_STYLE);
            row_add(tr(L"Alle auf Standard", L"Reset all to default"), L"›", 0, TOP_RESET);
            hint(1);
            break;
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
                row_add(style_names[i], i == notify_style() ? tr(L"✓ aktiv", L"✓ active") : L"", 0, i);
            hint(0);
            break;
        case LEVEL_MOUSE:
            wcopy(title, 0, 64, tr(L"Maus-Lenkung (experimentell)", L"Mouse steering (experimental)"));
            row_add(tr(L"Empfindlichkeit", L"Sensitivity"), L"", NOTIFY_ROW_INFO, -1);
            for (int i = 0; i < MOUSE_CHOICES; ++i)
                row_add(tr(mouse_german[i], mouse_english[i]), i == mouse_choice() ? tr(L"✓ aktiv", L"✓ active") : L"", 0, i);
            row_add(tr(L"Totzone überspringen", L"Skip dead zone"), L"", NOTIFY_ROW_INFO, -1);
            for (int i = 0; i < DEAD_CHOICES; ++i)
                row_add(dead_names[i], i == dead_choice() ? tr(L"✓ aktiv", L"✓ active") : (i == 3 ? tr(L"Standard", L"default") : L""), 0,
                        DEAD_FIRST + i);
            row_add(tr(L"Zur Mitte, wenn Maus ruht", L"Centre when mouse rests"), L"", NOTIFY_ROW_INFO, -1);
            for (int i = 0; i < CENTER_CHOICES; ++i)
                row_add(tr(center_german[i], center_english[i]), i == center_choice() ? tr(L"✓ aktiv", L"✓ active") : L"", 0,
                        CENTER_FIRST + i);
            hint(0);
            break;
        case LEVEL_TELEMETRY: {
            wcopy(title, 0, 64, tr(L"Live-Telemetrie", L"Live telemetry"));
            row_add(tr(L"Telemetrie-Tafel", L"Telemetry panel"), telemetry_enabled() ? tr(L"An", L"On") : tr(L"Aus", L"Off"), 0, 0);
            row_add(tr(L"Als CSV aufzeichnen", L"Record as CSV"), records_csv_on() ? tr(L"● läuft", L"● on") : tr(L"Aus", L"Off"), 0, 50);
            row_add(tr(L"Anzeigen (Enter = an/aus)", L"Shown (Enter = on/off)"), L"", NOTIFY_ROW_INFO, -1);
            int mask = telemetry_show();
            for (unsigned i = 0; i < TELEMETRY_SHOW_COUNT; ++i)
                row_add(telemetry_item_name(i, m.german), (mask & (1 << i)) ? tr(L"✓ an", L"✓ on") : tr(L"aus", L"off"), 0,
                        1 + (int)i);
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
                row_add(tr(L"Jetzt im Auto", L"Car now"), L"", NOTIFY_ROW_INFO, -1);
                distance_text(cm, text);
                row_add(name, text, 0, -1);
            }
            row_add(tr(L"Meiste Kilometer", L"Most kilometres"), L"", NOTIFY_ROW_INFO, -1);
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
            static const unsigned kinds_metric[6] = {RECORD_SPRINT, RECORD_SPRINT200, RECORD_QUARTER, RECORD_BRAKE, RECORD_TOP, RECORD_OWN};
            static const unsigned kinds_imperial[6] = {RECORD_SPRINT_MPH, RECORD_SPRINT200_MPH, RECORD_QUARTER, RECORD_BRAKE_MPH, RECORD_TOP, RECORD_OWN};
            static const wchar_t *const names_de[6] = {L"0-100 km/h", L"0-200 km/h", L"Viertelmeile", L"Bremsweg 100-0", L"Höchsttempo", L"Eigener Sprint"};
            static const wchar_t *const names_mph[6] = {L"0-60 mph", L"0-100 mph", L"Quarter mile", L"Braking 60-0", L"Top speed", L"Own sprint"};
            static const wchar_t *const names_en[6] = {L"0-100 km/h", L"0-200 km/h", L"Quarter mile", L"Braking 100-0", L"Top speed", L"Own sprint"};
            static const wchar_t *const names_de_mph[6] = {L"0-60 mph", L"0-100 mph", L"Viertelmeile", L"Bremsweg 60-0", L"Höchsttempo", L"Eigener Sprint"};
            for (unsigned i = 0; i < 6; ++i) {
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
        case LEVEL_SPRINT: {
            wcopy(title, 0, 64, tr(L"Eigene Sprints", L"Own sprints"));
            float sm;
            int ss = records_sprint_state(&sm);
            row_add(tr(L"Start hier setzen", L"Set start here"), ss >= 1 ? tr(L"✓ gesetzt", L"✓ set") : L"", 0, 0);
            row_add(tr(L"Ziel hier setzen", L"Set finish here"), ss == 2 ? tr(L"✓ gesetzt", L"✓ set") : L"", 0, 1);
            if (ss) row_add(tr(L"Sprint löschen", L"Delete sprint"), L"", 0, 2);
            if (ss == 2) {
                wchar_t text[32];
                distance_text((double)sm, text);
                row_add(tr(L"Luftlinie Start-Ziel", L"Start to finish"), text, NOTIFY_ROW_INFO, -1);
                row_add(tr(L"Durch den Start fahren = los", L"Leave the start = clock runs"), L"", NOTIFY_ROW_INFO, -1);
            } else {
                row_add(tr(L"Am Start halten, Start setzen,", L"Stop at the start, set it,"), L"", NOTIFY_ROW_INFO, -1);
                row_add(tr(L"zum Ziel fahren, Ziel setzen", L"drive to the end, set finish"), L"", NOTIFY_ROW_INFO, -1);
            }
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
    keyboard_block_input(1);
    show();
    log_append("KEYMENU: opened\r\n");
}

static volatile LONG pad_swallow; /* after closing: the controller's buttons stay away from the game until released */

static void close_menu(void) {
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
        case LEVEL_STYLE: m.level = LEVEL_GROUPS; break;
        case LEVEL_MOUSE: m.level = LEVEL_GROUPS; break;
        case LEVEL_TELEMETRY: m.level = LEVEL_GROUPS; break;
        case LEVEL_WORLD: m.level = LEVEL_GROUPS; break;
        case LEVEL_LOGBOOK: m.level = LEVEL_GROUPS; break;
        case LEVEL_RECORDS: confirm_clear = 0; m.level = LEVEL_GROUPS; break;
        case LEVEL_SPRINT: m.level = LEVEL_GROUPS; break;
        case LEVEL_BINDING: m.level = LEVEL_LIST; break;
        case LEVEL_CAPTURE: m.level = LEVEL_BINDING; break;
    }
    show();
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
            if (action == TOP_RESET) {
                m.level = LEVEL_RESET_ALL;
                m.sel[LEVEL_RESET_ALL] = 0;
            } else if (action == TOP_RECORDS) {
                m.level = LEVEL_RECORDS;
                m.sel[LEVEL_RECORDS] = 0;
                confirm_clear = 0;
            } else if (action == TOP_SPRINT) {
                m.level = LEVEL_SPRINT;
                m.sel[LEVEL_SPRINT] = 0;
            } else if (action == TOP_LOGBOOK) {
                m.level = LEVEL_LOGBOOK;
                m.sel[LEVEL_LOGBOOK] = 0;
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
                m.sel[LEVEL_STYLE] = notify_style();
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
        case LEVEL_SPRINT:
            if (action == 0) {
                if (records_set_start())
                    notify_show(m.german ? "Start gesetzt\nJetzt zum Ziel fahren und Ziel setzen" : "Start set\nNow drive to the finish and set it");
                else notify_show(m.german ? "Start nicht gesetzt\nErst losfahren (kein Auto erkannt)" : "Start not set\nDrive first (no car seen)");
            } else if (action == 1) {
                if (records_set_finish())
                    notify_show(m.german ? "Ziel gesetzt\nZum Start fahren, losfahren = Zeit läuft" : "Finish set\nGo to the start, leaving it starts the clock");
                else notify_show(m.german ? "Ziel nicht gesetzt\nErst Start setzen, Ziel 30 m+ weg" : "Finish not set\nSet the start first, 30 m+ away");
            } else if (action == 2) {
                records_clear_sprint();
                notify_show(m.german ? "Sprint gelöscht" : "Sprint deleted");
                m.sel[LEVEL_SPRINT] = 0;
            }
            break;
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
static volatile LONG pad_now, pad_toggle;
static volatile LONG64 pad_tick;
static LONG pad_was;

int key_menu_pad_input(unsigned char *gamepad) {
    if (!gamepad) return 0;
    unsigned short buttons;
    short ly;
    memory_copy(&buttons, gamepad, sizeof(buttons));
    memory_copy(&ly, gamepad + 6, sizeof(ly));
    static unsigned short before;
    unsigned short pressed = (unsigned short)(buttons & ~before);
    before = buttons;
    if ((buttons & 0x0040u) && (pressed & 0x0008u)) InterlockedExchange(&pad_toggle, 1); /* LS + D-pad right */
    LONG bits = 0;
    if ((buttons & 0x0001u) || ly > 20000) bits |= PAD_UP;
    if ((buttons & 0x0002u) || ly < -20000) bits |= PAD_DOWN;
    if (buttons & 0x1000u) bits |= PAD_A;
    if (buttons & 0x2000u) bits |= PAD_B;
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

int key_menu_open(void) {
    return m.level != LEVEL_CLOSED;
}

static void set_path(void) {
    const wchar_t *path = keyboard_ini_path();
    if (path) wcopy(ini_path, 0, PATH_CAPACITY, path);
}

#ifndef FH2_TWEAKS_TEST
static DWORD WINAPI menu_thread(LPVOID unused) {
    (void)unused;
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
