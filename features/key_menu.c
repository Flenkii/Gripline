/* Gripline key menu (V7). See key_menu.h. */
#include "key_menu.h"
#include "keyboard_pad.h"
#include "notify.h"
#include "game_language.h"
#include "ini_edit.h"
#include "logging.h"
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

enum { LEVEL_CLOSED, LEVEL_GROUPS, LEVEL_LIST, LEVEL_BINDING, LEVEL_CAPTURE, LEVEL_RESET_ALL };

/* the pages of the first level; every binding is in exactly one (tests/test_key_menu.py) */
static const wchar_t *const group_driving[] = {L"Throttle", L"Brake", L"SteerLeft", L"SteerRight", L"Handbrake",
                                               L"ShiftUp", L"ShiftDown", L"Clutch", L"Rewind", L"Lights", L"Map",
                                               L"Telemetry", L"PhotoMode", L"RadioPrevious", L"RadioNext", NULL};
static const wchar_t *const group_camera[] = {L"Camera", L"LookBack", L"LookLeft", L"LookRight", L"CockpitView",
                                              L"ArrowUp", L"ArrowDown", L"ArrowLeft", L"ArrowRight", NULL};
static const wchar_t *const group_gripline[] = {L"Nitro", L"Noclip", L"NoclipUp", L"NoclipDown", L"NoclipFast",
                                                L"Freecam", L"Keybinds", L"KeyMenu", NULL};
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
    int sel[6];           /* selected row per level */
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

static int read_ini(size_t *length) {
    *length = 0;
    file_text[0] = 0;
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
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

static int write_ini(const char *data, size_t length) {
    static wchar_t temp[PATH_CAPACITY + 8];
    unsigned n = wcopy(temp, 0, PATH_CAPACITY, ini_path);
    wcopy(temp, n, PATH_CAPACITY + 8, L".gltmp");
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp, ini_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
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
    if (!ini_path[0] || !read_ini(&length)) {
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
    if (!write_ini(source, length)) {
        log_append("KEYMENU: FH2_Keyboard.ini could not be written - change not saved.\r\n");
        return 0;
    }
    return 1;
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

static void build_page(wchar_t *title) {
    row_count = 0;
    title[0] = 0;
    unsigned char vk[4];
    wchar_t keys[32];
    switch (m.level) {
        case LEVEL_GROUPS:
            wcopy(title, 0, 64, tr(L"Tastenbelegung ändern", L"Change key bindings"));
            for (unsigned g = 0; g < GROUP_COUNT; ++g) row_add(tr(groups[g].german, groups[g].english), L"›", 0, (int)g);
            row_add(tr(L"Alle auf Standard", L"Reset all to default"), L"›", 0, (int)GROUP_COUNT);
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
    for (unsigned i = 0; i < 6; ++i) m.sel[i] = 0;
    keyboard_block_input(1);
    show();
    log_append("KEYMENU: opened\r\n");
}

static void close_menu(void) {
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
            if (action == (int)GROUP_COUNT) {
                m.level = LEVEL_RESET_ALL;
                m.sel[LEVEL_RESET_ALL] = 0;
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

    int up = keyboard_key_down(VK_UP_), down = keyboard_key_down(VK_DOWN_);
    int enter = edge(VK_ENTER_, keyboard_key_down(VK_ENTER_));
    int esc = edge(VK_ESC_, keyboard_key_down(VK_ESC_));
    int bs = edge(VK_BACK_, keyboard_key_down(VK_BACK_));
    int up_pressed = edge(VK_UP_, up), down_pressed = edge(VK_DOWN_, down);

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
