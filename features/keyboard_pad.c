/* Keyboard as a gamepad (unofficial add-on). See keyboard_pad.h. */
#include "keyboard_pad.h"
#include "mod_scan.h"
#include "ini_migrate.h"
#include "game_addresses.h"
#include "logging.h"
#include "support.h"
#include "nitro_link.h"
#include "cockpit_view.h"
#include "notify.h"
#include "game_language.h"

#define KEY_INI L"FH2_Keyboard.ini"
#define MAX_PADS 8

typedef HRESULT(WINAPI *GetFactoryFn)(void *, const GUID *, void **);
typedef const wchar_t *(WINAPI *RawBufferFn)(void *, UINT32 *);
typedef HRESULT(WINAPI *CreateStringFn)(const wchar_t *, UINT32, void **);
typedef HRESULT(WINAPI *QIFn)(void *, const GUID *, void **);
typedef ULONG(WINAPI *RefFn)(void *);
typedef SHORT(WINAPI *AsyncKeyFn)(int);
typedef HWND(WINAPI *ForegroundFn)(void);
typedef DWORD(WINAPI *WindowPidFn)(HWND, LPDWORD);

#define VT(object) (*(void ***)(object))

typedef struct {
    UINT64 timestamp;
    UINT32 buttons, padding;
    double left_trigger, right_trigger, lx, ly, rx, ry;
} PadReading;
_Static_assert(sizeof(PadReading) == 64, "Windows.Gaming.Input.GamepadReading");

typedef struct {
    double left, right, left_trigger, right_trigger;
} PadVibration;

typedef HRESULT(WINAPI *ReadingFn)(void *, PadReading *);
typedef HRESULT(WINAPI *VibrationFn)(void *, const PadVibration *);
typedef HRESULT(WINAPI *GetVibrationFn)(void *, PadVibration *);
typedef HRESULT(WINAPI *GetPadsFn)(void *, void **);
typedef HRESULT(WINAPI *GetAtFn)(void *, UINT32, void **);
typedef HRESULT(WINAPI *SizeFn)(void *, UINT32 *);

/* Windows.Gaming.Input.GamepadButtons */
enum {
    PAD_MENU = 1, PAD_VIEW = 2, PAD_A = 4, PAD_B = 8, PAD_X = 16, PAD_Y = 32,
    PAD_UP = 64, PAD_DOWN = 128, PAD_LEFT = 256, PAD_RIGHT = 512,
    PAD_LB = 1024, PAD_RB = 2048, PAD_LS = 4096, PAD_RS = 8192
};

static const GUID IID_Unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_Inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_Agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID IID_GamepadStatics = {0x8bbce529, 0xd49c, 0x39e9, {0x95, 0x60, 0xe4, 0x7d, 0xde, 0x96, 0xb7, 0xc8}};
static const GUID IID_Gamepad = {0xbc7bb43c, 0x0a69, 0x3903, {0x9e, 0x9d, 0xa5, 0x0f, 0x86, 0xa4, 0x5d, 0xe5}};

/* ------------------------------------------------------------------ settings */

static wchar_t ini_path[PATH_CAPACITY];
static volatile LONG values[KEY_SETTING_COUNT];
static int loaded;
static const wchar_t *const setting_keys[KEY_SETTING_COUNT] = {L"Enabled", L"SteerTime", L"ReturnTime",
                                                               L"PedalTime"};
static const int setting_defaults[KEY_SETTING_COUNT] = {1, 150, 90, 60};
static const int setting_min[KEY_SETTING_COUNT] = {0, 0, 0, 0};
static const int setting_max[KEY_SETTING_COUNT] = {1, 1000, 1000, 1000};

enum {
    KIND_ACTION, /* game action, button/trigger/stick follows the controller layout */
    KIND_STEER,  /* left stick X: -1 / +1 */
    KIND_NAV,    /* arrows, 0 up, 1 down, 2 left, 3 right: left stick in menus, camera while driving */
    KIND_FIXED,  /* fixed controller input */
    KIND_NITRO,  /* NitroSystem nitro button, see nitro_link.h */
    KIND_COCKPIT_VIEW, /* second cockpit view on/off, see cockpit_view.h */
    KIND_KEYLIST, /* key list panel on/off (notify.h), the game keeps the key */
};

typedef struct {
    const wchar_t *key;
    unsigned char kind, target;
    const wchar_t *defaults;
    unsigned char vk[4];
} Binding;

/* controller inputs (wheel_buttons.c numbering) */
enum {
    IN_DPAD_UP = 0, IN_DPAD_DOWN = 1, IN_DPAD_LEFT = 2, IN_DPAD_RIGHT = 3, IN_MENU = 4, IN_VIEW = 5,
    IN_LS = 6, IN_RS = 7, IN_LB = 8, IN_RB = 9, IN_LT = 10, IN_RT = 11, IN_A = 12, IN_B = 13, IN_X = 14,
    IN_Y = 15, IN_LS_UP = 0x13, IN_LS_DOWN = 0x14, IN_RS_UP = 0x1a, IN_RS_DOWN = 0x1b, IN_RS_LEFT = 0x1c,
    IN_RS_RIGHT = 0x1d
};

static Binding bindings[] = {
    {L"Throttle", KIND_ACTION, 0, L"W", {0}},
    {L"Brake", KIND_ACTION, 1, L"S", {0}},
    {L"SteerLeft", KIND_STEER, 0, L"A", {0}},
    {L"SteerRight", KIND_STEER, 1, L"D", {0}},
    {L"Handbrake", KIND_ACTION, 3, L"Space", {0}},
    {L"ShiftUp", KIND_ACTION, 4, L"E", {0}},
    {L"ShiftDown", KIND_ACTION, 5, L"Q", {0}},
    {L"Clutch", KIND_ACTION, 6, L"LShift", {0}},
    {L"Camera", KIND_ACTION, 17, L"Tab", {0}},
    {L"Rewind", KIND_ACTION, 20, L"R", {0}},
    {L"Telemetry", KIND_ACTION, 14, L"T", {0}},
    {L"PhotoMode", KIND_ACTION, 15, L"P", {0}},
    {L"RadioPrevious", KIND_ACTION, 23, L"Minus", {0}},
    {L"RadioNext", KIND_ACTION, 24, L"Plus", {0}},
    {L"LookBack", KIND_ACTION, 10, L"C,Num2", {0}},
    {L"LookLeft", KIND_ACTION, 11, L"Num4", {0}},
    {L"LookRight", KIND_ACTION, 12, L"Num6", {0}},
    {L"Nitro", KIND_NITRO, 0, L"N", {0}},
    {L"CockpitView", KIND_COCKPIT_VIEW, 0, L"V", {0}},
    {L"Keybinds", KIND_KEYLIST, 0, L"F5", {0}},
    {L"Lights", KIND_FIXED, IN_RS, L"L", {0}},
    {L"Map", KIND_FIXED, IN_VIEW, L"M", {0}},
    {L"Pause", KIND_FIXED, IN_MENU, L"Esc", {0}},
    {L"Confirm", KIND_FIXED, IN_A, L"Enter", {0}},
    {L"Back", KIND_FIXED, IN_B, L"Backspace", {0}},
    {L"ButtonX", KIND_FIXED, IN_X, L"X", {0}},
    {L"ButtonY", KIND_FIXED, IN_Y, L"Y", {0}},
    {L"TabLeft", KIND_FIXED, IN_LB, L"PageUp", {0}},
    {L"TabRight", KIND_FIXED, IN_RB, L"PageDown", {0}},
    {L"ArrowUp", KIND_NAV, 0, L"Up", {0}},
    {L"ArrowDown", KIND_NAV, 1, L"Down", {0}},
    {L"ArrowLeft", KIND_NAV, 2, L"Left", {0}},
    {L"ArrowRight", KIND_NAV, 3, L"Right", {0}},
    {L"DpadUp", KIND_FIXED, IN_DPAD_UP, L"-", {0}},
    {L"DpadDown", KIND_FIXED, IN_DPAD_DOWN, L"-", {0}},
    {L"DpadLeft", KIND_FIXED, IN_DPAD_LEFT, L"-", {0}},
    {L"DpadRight", KIND_FIXED, IN_DPAD_RIGHT, L"-", {0}},
};

static const struct {
    const char *name;
    unsigned char vk;
} key_names[] = {
    {"LEERTASTE", 0x20}, {"SPACE", 0x20},     {"ENTER", 0x0d},      {"EINGABE", 0x0d},   {"ESC", 0x1b},
    {"TAB", 0x09},       {"SHIFT", 0x10},     {"UMSCHALT", 0x10},   {"LSHIFT", 0xa0},    {"RSHIFT", 0xa1},
    {"STRG", 0x11},      {"CTRL", 0x11},      {"LSTRG", 0xa2},      {"LCTRL", 0xa2},     {"RSTRG", 0xa3},
    {"RCTRL", 0xa3},     {"ALT", 0x12},       {"RUECK", 0x08},      {"BACKSPACE", 0x08}, {"HOCH", 0x26},
    {"UP", 0x26},        {"RUNTER", 0x28},    {"DOWN", 0x28},       {"LINKS", 0x25},     {"LEFT", 0x25},
    {"RECHTS", 0x27},    {"RIGHT", 0x27},     {"BILDHOCH", 0x21},   {"PAGEUP", 0x21},    {"BILDRUNTER", 0x22},
    {"PAGEDOWN", 0x22},  {"POS1", 0x24},      {"HOME", 0x24},       {"ENDE", 0x23},      {"END", 0x23},
    {"EINFG", 0x2d},     {"INSERT", 0x2d},    {"ENTF", 0x2e},       {"DELETE", 0x2e},    {"MINUS", 0xbd},
    {"PLUS", 0xbb},      {"KOMMA", 0xbc},     {"COMMA", 0xbc},      {"PUNKT", 0xbe},     {"PERIOD", 0xbe},
    {"MAUS1", 0x01},     {"MAUSLINKS", 0x01}, {"MAUS2", 0x02},      {"MAUSRECHTS", 0x02}, {"MAUS3", 0x04},
    {"MAUSMITTE", 0x04}, {"MAUS4", 0x05},     {"MAUS5", 0x06},      {"MOUSE1", 0x01},    {"MOUSE2", 0x02},
    {"MOUSE3", 0x04},    {"MOUSE4", 0x05},    {"MOUSE5", 0x06},
};

static int read_int(const wchar_t *key, int fallback) {
    wchar_t text[32];
    DWORD n = GetPrivateProfileStringW(L"Keyboard", key, L"", text, 32, ini_path);
    int sign = 1, value = 0, any = 0;
    DWORD i = 0;
    while (i < n && text[i] == L' ') ++i;
    if (i < n && text[i] == L'-') sign = -1, ++i;
    for (; i < n && text[i] >= L'0' && text[i] <= L'9'; ++i) {
        value = value * 10 + (text[i] - L'0');
        any = 1;
        if (value > 100000) break;
    }
    return any ? sign * value : fallback;
}

static unsigned char key_code(const wchar_t *name, size_t length) {
    if (length == 1) {
        wchar_t c = name[0];
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return (unsigned char)c;
        return 0;
    }
    char upper[16];
    if (length >= sizeof(upper)) return 0;
    for (size_t i = 0; i < length; ++i) {
        wchar_t c = name[i];
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
        if (c > 127) return 0;
        upper[i] = (char)c;
    }
    upper[length] = 0;
    if (upper[0] == 'F' && length <= 3) { /* F1..F12 */
        int number = 0;
        for (size_t i = 1; i < length; ++i) {
            if (upper[i] < '0' || upper[i] > '9') return 0;
            number = number * 10 + (upper[i] - '0');
        }
        return number >= 1 && number <= 12 ? (unsigned char)(0x70 + number - 1) : 0;
    }
    if (length == 4 && upper[0] == 'N' && upper[1] == 'U' && upper[2] == 'M' && upper[3] >= '0' && upper[3] <= '9')
        return (unsigned char)(0x60 + upper[3] - '0');
    for (unsigned i = 0; i < ARRAY_COUNT(key_names); ++i) {
        if (text_equal_ignore_case(upper, key_names[i].name)) return key_names[i].vk;
    }
    return 0;
}

static void parse_keys(Binding *b, const wchar_t *text) {
    unsigned count = 0;
    for (unsigned k = 0; k < 4; ++k) b->vk[k] = 0;
    while (*text && count < 4) {
        while (*text == L' ' || *text == L',' || *text == L';') ++text;
        const wchar_t *start = text;
        while (*text && *text != L',' && *text != L' ' && *text != L';') ++text;
        size_t length = (size_t)(text - start);
        if (!length) break;
        unsigned char vk = key_code(start, length);
        if (vk) b->vk[count++] = vk;
    }
}

static const char default_ini[] =
    "; FH2 keyboard (unofficial) - settings\r\n"
    "; The values at the top are also in the game: Options > HUD > bottom.\r\n"
    "; Changes in this file apply on the next game start.\r\n"
    "[Keyboard]\r\n"
    "; 1 = keyboard works as a gamepad, 0 = off (applies after a restart)\r\n"
    "Enabled=1\r\n"
    "; milliseconds to full steering lock / back to the centre / to full throttle\r\n"
    "SteerTime=150\r\n"
    "ReturnTime=90\r\n"
    "PedalTime=60\r\n"
    "\r\n"
    "; Key bindings: several keys separated by commas, - = none.\r\n"
    "; Key names: letters, digits, F1-F12, Num0-Num9, Space, Enter, Esc, Tab, Backspace,\r\n"
    "; LShift, RShift, LCtrl, RCtrl, Alt, Up, Down, Left, Right, PageUp, PageDown, Home, End,\r\n"
    "; Insert, Delete, Minus, Plus, Comma, Period, Mouse1 (left), Mouse2 (right), Mouse3 (middle),\r\n"
    "; Mouse4, Mouse5.\r\n"
    "; Driving functions follow the controller layout chosen in the game.\r\n"
    "; Menus: Enter = A, Backspace = B, Esc = Menu.\r\n"
    "; Arrow keys: in menus = left stick, while driving = camera (right stick).\r\n"
    "; Nitro: only with the NitroSystem mod (presses its NitroButton).\r\n"
    "; CockpitView: second cockpit view on/off (close to the dashboard, set on the MODS page).\r\n"
    "; Keybinds: shows this key list at the right of the picture, again = off (the game keeps running).\r\n";

/* Default file text including the key bindings (also used by ini_migrate.c). Returns the
   length, 0 if it does not fit. */
size_t keyboard_default_ini(char *out, size_t capacity) {
    size_t n = 0;
    for (size_t i = 0; i + 1 < sizeof(default_ini); ++i) {
        if (n + 1 >= capacity) return 0;
        out[n++] = default_ini[i];
    }
    for (unsigned i = 0; i < ARRAY_COUNT(bindings); ++i) {
        if (n + 100 >= capacity) return 0;
        for (const wchar_t *k = bindings[i].key; *k; ++k) out[n++] = (char)*k;
        out[n++] = '=';
        for (const wchar_t *k = bindings[i].defaults; *k; ++k) out[n++] = (char)*k;
        out[n++] = '\r';
        out[n++] = '\n';
    }
    out[n] = 0;
    return n;
}

static void write_default_ini(void) {
    static char text[4096];
    size_t length = keyboard_default_ini(text, sizeof(text));
    if (!length) return;
    HANDLE file = CreateFileW(ini_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

/* ------------------------------------------------------------------ key list panel */

static const wchar_t *const labels_german[ARRAY_COUNT(bindings)] = {
    L"Gas", L"Bremse", L"Links lenken", L"Rechts lenken", L"Handbremse", L"Hochschalten", L"Runterschalten",
    L"Kupplung", L"Kamera wechseln", L"Zurückspulen", L"Telemetrie (halten)", L"Fotomodus (halten)", L"Radio: zurück",
    L"Radio: weiter", L"Blick zurück", L"Blick links", L"Blick rechts", L"Nitro (NitroSystem)",
    L"Cockpit-Ansicht 2", L"Tastenliste ein/aus", L"Licht", L"Karte", L"Pause",
    L"Bestätigen (A)", L"Zurück (B)", L"Taste X", L"Taste Y", L"Reiter links (LB)", L"Reiter rechts (RB)",
    L"Menü/Kamera hoch", L"Menü/Kamera runter", L"Menü/Kamera links", L"Menü/Kamera rechts",
    L"Steuerkreuz hoch", L"Steuerkreuz runter", L"Steuerkreuz links", L"Steuerkreuz rechts"};
static const wchar_t *const labels_english[ARRAY_COUNT(bindings)] = {
    L"Throttle", L"Brake", L"Steer left", L"Steer right", L"Handbrake", L"Shift up", L"Shift down",
    L"Clutch", L"Change camera", L"Rewind", L"Telemetry (hold)", L"Photo mode (hold)", L"Radio: previous",
    L"Radio: next", L"Look back", L"Look left", L"Look right", L"Nitro (NitroSystem)",
    L"Cockpit view 2", L"Key list on/off", L"Lights", L"Map", L"Pause",
    L"Confirm (A)", L"Back (B)", L"Button X", L"Button Y", L"Tab left (LB)", L"Tab right (RB)",
    L"Menu/camera up", L"Menu/camera down", L"Menu/camera left", L"Menu/camera right",
    L"D-pad up", L"D-pad down", L"D-pad left", L"D-pad right"};
_Static_assert(ARRAY_COUNT(bindings) + 1 <= NOTIFY_PANEL_ROWS, "key list rows");

static unsigned char *game_base;
static HMODULE user32;
static volatile LONG key_list_held, key_list_on;

typedef int(WINAPI *MapKeyFn)(UINT, UINT);
typedef int(WINAPI *KeyNameFn)(LONG, LPWSTR, int);

/* Name of a key as printed on this keyboard (Windows keyboard layout), for the keys whose place
   differs between layouts (Minus, Plus, Comma, Period: e.g. "=" on a US keyboard). */
static int layout_key_name(unsigned char vk, wchar_t *out, int capacity) {
#ifdef FH2_TWEAKS_TEST
    (void)vk, (void)out, (void)capacity;
    return 0;
#else
    static MapKeyFn map_key;
    static KeyNameFn key_name;
    if (!key_name) {
        if (!user32) user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!user32) return 0;
        map_key = (MapKeyFn)(uintptr_t)GetProcAddress(user32, "MapVirtualKeyW");
        key_name = (KeyNameFn)(uintptr_t)GetProcAddress(user32, "GetKeyNameTextW");
        if (!map_key || !key_name) {
            key_name = NULL;
            return 0;
        }
    }
    int scan = map_key(vk, 0); /* MAPVK_VK_TO_VSC */
    return scan ? key_name((LONG)scan << 16, out, capacity) : 0;
#endif
}

static const struct {
    unsigned char vk;
    const wchar_t *german, *english;
} key_words[] = {
    {0x20, L"Leertaste", L"Space"},        {0x0d, L"Enter", L"Enter"},
    {0x1b, L"Esc", L"Esc"},                {0x09, L"Tab", L"Tab"},
    {0x08, L"Rücktaste", L"Backspace"},    {0x10, L"Umschalt", L"Shift"},
    {0xa0, L"Umschalt links", L"Left Shift"}, {0xa1, L"Umschalt rechts", L"Right Shift"},
    {0x11, L"Strg", L"Ctrl"},              {0xa2, L"Strg links", L"Left Ctrl"},
    {0xa3, L"Strg rechts", L"Right Ctrl"}, {0x12, L"Alt", L"Alt"},
    {0x26, L"Pfeil hoch", L"Up"},          {0x28, L"Pfeil runter", L"Down"},
    {0x25, L"Pfeil links", L"Left"},       {0x27, L"Pfeil rechts", L"Right"},
    {0x21, L"Bild hoch", L"Page Up"},      {0x22, L"Bild runter", L"Page Down"},
    {0x24, L"Pos1", L"Home"},              {0x23, L"Ende", L"End"},
    {0x2d, L"Einfg", L"Insert"},           {0x2e, L"Entf", L"Delete"},
    {0xbd, L"Minus", L"Minus"},            {0xbb, L"Plus", L"Plus"},
    {0xbc, L"Komma", L"Comma"},            {0xbe, L"Punkt", L"Period"},
    {0x01, L"Maus links", L"Left mouse"},  {0x02, L"Maus rechts", L"Right mouse"},
    {0x04, L"Mausrad-Klick", L"Middle mouse"}, {0x05, L"Maus 4", L"Mouse 4"},
    {0x06, L"Maus 5", L"Mouse 5"},
};

static unsigned append(wchar_t *out, unsigned n, unsigned capacity, const wchar_t *text) {
    while (*text && n + 1 < capacity) out[n++] = *text++;
    out[n] = 0;
    return n;
}

/* the keys of one binding as the player reads them, e.g. "C, Num 2" */
static void key_list_keys(const Binding *b, int german, wchar_t *out, unsigned capacity) {
    unsigned n = 0;
    out[0] = 0;
    for (unsigned k = 0; k < 4 && b->vk[k]; ++k) {
        unsigned char vk = b->vk[k];
        wchar_t name[24] = {0};
        if (k) n = append(out, n, capacity, L", ");
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
            name[0] = (wchar_t)vk;
        } else if (vk >= 0x70 && vk <= 0x7b) {
            int f = vk - 0x70 + 1;
            name[0] = L'F';
            name[1] = (wchar_t)(f >= 10 ? L'1' : L'0' + f);
            if (f >= 10) name[2] = (wchar_t)(L'0' + f - 10);
        } else if (vk >= 0x60 && vk <= 0x69) {
            append(name, 0, 24, L"Num ");
            name[4] = (wchar_t)(L'0' + vk - 0x60);
        } else if (vk >= 0xba && vk <= 0xe2 && layout_key_name(vk, name, 24) > 0) {
            /* name from the keyboard layout */
        } else {
            for (unsigned i = 0; i < ARRAY_COUNT(key_words); ++i)
                if (key_words[i].vk == vk) append(name, 0, 24, german ? key_words[i].german : key_words[i].english);
        }
        n = append(out, n, capacity, name);
    }
}

static void copy_text(wchar_t *out, const wchar_t *text, unsigned capacity) {
    unsigned n = 0;
    while (text[n] && n + 1 < capacity) {
        out[n] = text[n];
        ++n;
    }
    out[n] = 0;
}

static void key_list_show(void) {
    static NotifyPanelRow rows[NOTIFY_PANEL_ROWS];
    int german = game_language_is_german(game_base);
    unsigned count = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(bindings); ++i) {
        if (!bindings[i].vk[0]) continue;
        copy_text(rows[count].label, german ? labels_german[i] : labels_english[i], 32);
        key_list_keys(&bindings[i], german, rows[count].keys, 32);
        ++count;
    }
    notify_panel_set(german ? L"Tastenbelegung" : L"Key Bindings", rows, count);
}

static void key_list_input(int pressed) {
    LONG before = InterlockedExchange(&key_list_held, pressed ? 1 : 0);
    if (!pressed || before) return;
    LONG on = !key_list_on;
    InterlockedExchange(&key_list_on, on);
    if (on) {
        key_list_show();
    } else {
        notify_panel_set(NULL, NULL, 0);
    }
    log_append(on ? "KEYBOARD: key list shown\r\n" : "KEYBOARD: key list hidden\r\n");
}

static void settings_load(void) {
    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) write_default_ini();
    for (unsigned id = 0; id < KEY_SETTING_COUNT; ++id) {
        int v = read_int(setting_keys[id], setting_defaults[id]);
        if (v < setting_min[id] || v > setting_max[id]) v = setting_defaults[id];
        values[id] = v;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(bindings); ++i) {
        wchar_t text[96];
        GetPrivateProfileStringW(L"Keyboard", bindings[i].key, bindings[i].defaults, text, 96, ini_path);
        parse_keys(&bindings[i], text);
    }
    loaded = 1;
}

int keyboard_setting(unsigned id) {
    return id < KEY_SETTING_COUNT ? (int)values[id] : 0;
}

void keyboard_set_setting(unsigned id, int value) {
    if (id < KEY_SETTING_COUNT && value >= setting_min[id] && value <= setting_max[id])
        InterlockedExchange(&values[id], value);
}

const wchar_t *keyboard_ini_path(void) {
    return loaded ? ini_path : NULL;
}

/* ------------------------------------------------------------------ user32 (loaded at run time) */

static AsyncKeyFn async_key;
static ForegroundFn foreground;
static WindowPidFn window_pid;

static int user32_ready(void) {
    if (async_key) return 1;
    if (!user32) user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!user32) return 0;
    foreground = (ForegroundFn)(uintptr_t)GetProcAddress(user32, "GetForegroundWindow");
    window_pid = (WindowPidFn)(uintptr_t)GetProcAddress(user32, "GetWindowThreadProcessId");
    AsyncKeyFn keys = (AsyncKeyFn)(uintptr_t)GetProcAddress(user32, "GetAsyncKeyState");
    if (!foreground || !window_pid || !keys) return 0;
    async_key = keys;
    return 1;
}

static int game_has_focus(void) {
    static ULONGLONG checked;
    static int focused;
    ULONGLONG now = GetTickCount64();
    if (now - checked >= 50) {
        checked = now;
        HWND window = foreground();
        DWORD pid = 0;
        if (window) window_pid(window, &pid);
        focused = window && pid == GetCurrentProcessId();
    }
    return focused;
}

/* ------------------------------------------------------------------ driving or menu */

/* While the simulation runs (free roam, races) the arrows move the camera; in menus, the pause
   menu, loading screens and before the world is loaded they move the left stick. Same check
   as autodrive: player manager -> simulation -> paused flag. */

static int simulation_running(void) {
    void *manager, *simulation;
    unsigned char paused;
    return game_base && memory_read_pointer(game_base + GAME_RVA_PLAYER_MANAGER, &manager) && manager &&
           memory_read_pointer((unsigned char *)manager + 0x48, &simulation) && simulation &&
           memory_read((unsigned char *)simulation + 0x28, &paused, 1) && !paused;
}

static int driving_now(void) {
    static ULONGLONG checked;
    static int driving;
    ULONGLONG now = GetTickCount64();
    if (now - checked >= 100) {
        checked = now;
        driving = simulation_running();
    }
    return driving;
}

/* ------------------------------------------------------------------ keyboard -> reading */

static int down(const Binding *b) {
    for (unsigned k = 0; k < 4 && b->vk[k]; ++k) {
        if (async_key(b->vk[k]) & 0x8000) return 1;
    }
    return 0;
}

static double approach(double current, double target, double dt, int attack_ms, int release_ms) {
    int toward_zero = (target == 0.0) || (current > 0.0 && target < current) || (current < 0.0 && target > current);
    int ms = toward_zero ? release_ms : attack_ms;
    if (ms <= 0) return target;
    double step = dt * 1000.0 / (double)ms;
    if (current < target) {
        current += step;
        if (current > target) current = target;
    } else if (current > target) {
        current -= step;
        if (current < target) current = target;
    }
    return current;
}

static double clamp1(double v) {
    return v > 1.0 ? 1.0 : v < -1.0 ? -1.0 : v;
}

static void apply_input(PadReading *r, int input, double amount) {
    static const UINT32 bits[16] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_MENU, PAD_VIEW, PAD_LS, PAD_RS,
                                    PAD_LB, PAD_RB, 0,       0,         PAD_A,    PAD_B,    PAD_X,  PAD_Y};
    if (amount <= 0.0) return;
    if (input == IN_LT) {
        if (amount > r->left_trigger) r->left_trigger = amount;
    } else if (input == IN_RT) {
        if (amount > r->right_trigger) r->right_trigger = amount;
    } else if (input >= 0 && input < 16) {
        r->buttons |= bits[input];
    } else if (input == IN_LS_UP) {
        r->ly = 1.0;
    } else if (input == IN_LS_DOWN) {
        r->ly = -1.0;
    } else if (input == IN_RS_UP) {
        r->ry = 1.0;
    } else if (input == IN_RS_DOWN) {
        r->ry = -1.0;
    } else if (input == IN_RS_LEFT) {
        r->rx = -1.0;
    } else if (input == IN_RS_RIGHT) {
        r->rx = 1.0;
    }
}

static struct {
    double steer, pedal[ARRAY_COUNT(bindings)];
    LARGE_INTEGER last;
} kb;

static void keyboard_reading(PadReading *r) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    double dt = kb.last.QuadPart ? (double)(now.QuadPart - kb.last.QuadPart) / (double)frequency.QuadPart : 0.0;
    if (dt < 0.0 || dt > 0.25) dt = 0.0;
    kb.last = now;
    int focused = game_has_focus();
    double steer_target = 0.0, nav_x = 0.0, nav_y = 0.0;
    for (unsigned i = 0; i < ARRAY_COUNT(bindings); ++i) {
        const Binding *b = &bindings[i];
        int pressed = focused && b->vk[0] && down(b);
        switch (b->kind) {
            case KIND_STEER:
                if (pressed) steer_target += b->target ? 1.0 : -1.0;
                break;
            case KIND_NAV:
                if (pressed) {
                    if (b->target == 0) nav_y += 1.0;
                    if (b->target == 1) nav_y -= 1.0;
                    if (b->target == 2) nav_x -= 1.0;
                    if (b->target == 3) nav_x += 1.0;
                }
                break;
            case KIND_FIXED:
                apply_input(r, b->target, pressed ? 1.0 : 0.0);
                break;
            case KIND_NITRO:
                nitro_link_set(NITRO_SOURCE_KEYBOARD, pressed);
                break;
            case KIND_COCKPIT_VIEW:
                cockpit_view_input(COCKPIT_VIEW_KEYBOARD, pressed);
                break;
            case KIND_KEYLIST:
                key_list_input(pressed);
                break;
            case KIND_ACTION: {
                int input = controller_action_input(b->target);
                if (input == IN_LT || input == IN_RT) { /* analog pedal: ramp */
                    kb.pedal[i] = approach(kb.pedal[i], pressed ? 1.0 : 0.0, dt, (int)values[KEY_SETTING_PEDAL],
                                           (int)values[KEY_SETTING_PEDAL] / 2);
                    apply_input(r, input, kb.pedal[i]);
                } else {
                    kb.pedal[i] = 0.0;
                    apply_input(r, input, pressed ? 1.0 : 0.0);
                }
                break;
            }
        }
    }
    kb.steer = approach(kb.steer, clamp1(steer_target), dt, (int)values[KEY_SETTING_STEER],
                        (int)values[KEY_SETTING_RETURN]);
    if ((nav_x != 0.0 || nav_y != 0.0) && driving_now()) {
        /* camera: right stick, like looking around with the gamepad */
        if (nav_x != 0.0 && r->rx == 0.0) r->rx = clamp1(nav_x);
        if (nav_y != 0.0 && r->ry == 0.0) r->ry = clamp1(nav_y);
        nav_x = nav_y = 0.0;
    }
    double lx = clamp1(kb.steer + nav_x);
    if (lx != 0.0) r->lx = lx;
    if (nav_y != 0.0) r->ly = clamp1(nav_y);
}

/* ------------------------------------------------------------------ virtual gamepad */

static void *real_statics;
static GetPadsFn real_get_pads; /* set when the late hook patched the real statics' vtable */
static void *real_pads[MAX_PADS];
static unsigned real_count;
static ULONGLONG pads_refreshed;
static SRWLOCK pad_lock = SRWLOCK_INIT;
static PadVibration vibration;
static CreateStringFn create_string;

static ULONG WINAPI static_addref(void *self) {
    (void)self;
    return 2;
}
static ULONG WINAPI static_release(void *self) {
    (void)self;
    return 1;
}
static HRESULT WINAPI no_iids(void *self, ULONG *count, GUID **iids) {
    (void)self;
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI trust_level(void *self, int *level) {
    (void)self;
    if (level) *level = 0;
    return S_OK;
}
static HRESULT WINAPI gamepad_class_name(void *self, void **name) {
    (void)self;
    if (!name) return E_POINTER;
    *name = NULL;
    return create_string ? create_string(L"Windows.Gaming.Input.Gamepad", 28, name) : S_OK;
}

static int guid_eq(const GUID *a, const GUID *b) {
    return a && b && memory_equal(a, b, sizeof(GUID));
}

/* refreshes the list of real gamepads (exclusive lock held) */
static void refresh_pads(ULONGLONG now) {
    if (!real_statics || (pads_refreshed && now - pads_refreshed < 1000)) return;
    pads_refreshed = now;
    void *view = NULL;
    void *fresh[MAX_PADS];
    unsigned count = 0;
    if ((real_get_pads ? real_get_pads : (GetPadsFn)VT(real_statics)[10])(real_statics, &view) >= 0 && view) {
        UINT32 n = 0;
        ((SizeFn)VT(view)[7])(view, &n);
        for (UINT32 i = 0; i < n && count < MAX_PADS; ++i) {
            void *pad = NULL;
            if (((GetAtFn)VT(view)[6])(view, i, &pad) >= 0 && pad) fresh[count++] = pad;
        }
        ((RefFn)VT(view)[2])(view);
    }
    if (count != real_count) {
        char line[96] = "KEYBOARD: real gamepads connected: ";
        unsigned l = 35;
        line[l++] = (char)('0' + count);
        line[l++] = '\r';
        line[l++] = '\n';
        line[l] = 0;
        log_append(line);
    }
    for (unsigned i = 0; i < real_count; ++i) ((RefFn)VT(real_pads[i])[2])(real_pads[i]);
    for (unsigned i = 0; i < count; ++i) real_pads[i] = fresh[i];
    real_count = count;
    for (unsigned i = 0; i < real_count; ++i) { /* a new pad gets the current rumble too */
        ((VibrationFn)VT(real_pads[i])[7])(real_pads[i], &vibration);
    }
}

static double magnitude(double x, double y) {
    return x * x + y * y;
}

static struct {
    void **vtbl;
} pad;

static HRESULT WINAPI pad_qi(void *self, const GUID *iid, void **out) {
    if (!out) return E_POINTER;
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_Inspectable) || guid_eq(iid, &IID_Agile) ||
        guid_eq(iid, &IID_Gamepad)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI pad_get_vibration(void *self, PadVibration *out) {
    (void)self;
    if (!out) return E_POINTER;
    AcquireSRWLockShared(&pad_lock);
    *out = vibration;
    ReleaseSRWLockShared(&pad_lock);
    return S_OK;
}

static HRESULT WINAPI pad_put_vibration(void *self, const PadVibration *value) {
    (void)self;
    if (!value) return E_POINTER;
    AcquireSRWLockExclusive(&pad_lock);
    vibration = *value;
    for (unsigned i = 0; i < real_count; ++i) ((VibrationFn)VT(real_pads[i])[7])(real_pads[i], value);
    ReleaseSRWLockExclusive(&pad_lock);
    return S_OK;
}

static HRESULT WINAPI pad_reading(void *self, PadReading *out) {
    (void)self;
    if (!out) return E_POINTER;
    PadReading r = {0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    UINT64 timestamp = (UINT64)((double)counter.QuadPart * 1000000.0 / (double)frequency.QuadPart);
    AcquireSRWLockExclusive(&pad_lock);
    refresh_pads(GetTickCount64());
    for (unsigned i = 0; i < real_count; ++i) {
        PadReading p;
        if (((ReadingFn)VT(real_pads[i])[8])(real_pads[i], &p) < 0) continue;
        r.buttons |= p.buttons;
        if (p.left_trigger > r.left_trigger) r.left_trigger = p.left_trigger;
        if (p.right_trigger > r.right_trigger) r.right_trigger = p.right_trigger;
        if (magnitude(p.lx, p.ly) > magnitude(r.lx, r.ly)) r.lx = p.lx, r.ly = p.ly;
        if (magnitude(p.rx, p.ry) > magnitude(r.rx, r.ry)) r.rx = p.rx, r.ry = p.ry;
    }
    if (user32_ready()) {
        PadReading k = {0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        keyboard_reading(&k);
        r.buttons |= k.buttons;
        if (k.left_trigger > r.left_trigger) r.left_trigger = k.left_trigger;
        if (k.right_trigger > r.right_trigger) r.right_trigger = k.right_trigger;
        if (magnitude(k.lx, k.ly) > magnitude(r.lx, r.ly)) r.lx = k.lx, r.ly = k.ly;
        if (magnitude(k.rx, k.ry) > magnitude(r.rx, r.ry)) r.rx = k.rx, r.ry = k.ry;
    }
    ReleaseSRWLockExclusive(&pad_lock);
    r.timestamp = timestamp;
    *out = r;
    return S_OK;
}

static void *pad_vtbl[9] = {(void *)pad_qi,       (void *)static_addref,      (void *)static_release,
                            (void *)no_iids,      (void *)gamepad_class_name, (void *)trust_level,
                            (void *)pad_get_vibration, (void *)pad_put_vibration, (void *)pad_reading};

/* IVectorView<Gamepad> with our one pad */
static HRESULT WINAPI view_qi(void *self, const GUID *iid, void **out) {
    if (!out) return E_POINTER;
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_Inspectable) || guid_eq(iid, &IID_Agile)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI view_class_name(void *self, void **name) {
    (void)self;
    if (name) *name = NULL;
    return S_OK;
}
static HRESULT WINAPI view_get_at(void *self, UINT32 index, void **out) {
    (void)self;
    if (!out) return E_POINTER;
    if (index) {
        *out = NULL;
        return E_BOUNDS;
    }
    *out = &pad;
    return S_OK;
}
static HRESULT WINAPI view_size(void *self, UINT32 *out) {
    (void)self;
    if (!out) return E_POINTER;
    *out = 1;
    return S_OK;
}
static HRESULT WINAPI view_index_of(void *self, void *value, UINT32 *index, BOOLEAN *found) {
    (void)self;
    if (!index || !found) return E_POINTER;
    *found = value == (void *)&pad;
    *index = 0;
    return S_OK;
}
static HRESULT WINAPI view_get_many(void *self, UINT32 start, UINT32 capacity, void **items, UINT32 *actual) {
    (void)self;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (start == 0 && capacity >= 1 && items) {
        items[0] = &pad;
        *actual = 1;
    }
    return S_OK;
}
static void *view_vtbl[10] = {(void *)view_qi,     (void *)static_addref,  (void *)static_release,
                              (void *)no_iids,     (void *)view_class_name, (void *)trust_level,
                              (void *)view_get_at, (void *)view_size,      (void *)view_index_of,
                              (void *)view_get_many};
static struct {
    void **vtbl;
} view = {view_vtbl};

/* IGamepadStatics: Azimuth subscribes to added/removed events and asks for the list */
static void *handlers[4];
static HRESULT WINAPI statics_qi(void *self, const GUID *iid, void **out) {
    if (!out) return E_POINTER;
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_Inspectable) || guid_eq(iid, &IID_Agile) ||
        guid_eq(iid, &IID_GamepadStatics)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT add_handler(unsigned kind, void *handler, INT64 *token) {
    if (!handler || !token) return E_POINTER;
    unsigned slot = kind * 2 + (handlers[kind * 2] ? 1 : 0);
    if (handlers[slot]) return E_FAIL;
    ((RefFn)VT(handler)[1])(handler); /* events never fire: the pad list never changes */
    handlers[slot] = handler;
    *token = (INT64)(slot + 1);
    return S_OK;
}
static HRESULT remove_handler(INT64 token) {
    if (token < 1 || token > 4 || !handlers[token - 1]) return S_OK;
    void *handler = handlers[token - 1];
    handlers[token - 1] = NULL;
    ((RefFn)VT(handler)[2])(handler);
    return S_OK;
}
static HRESULT WINAPI statics_add_added(void *self, void *handler, INT64 *token) {
    (void)self;
    return add_handler(0, handler, token);
}
static HRESULT WINAPI statics_remove(void *self, INT64 token) {
    (void)self;
    return remove_handler(token);
}
static HRESULT WINAPI statics_add_removed(void *self, void *handler, INT64 *token) {
    (void)self;
    return add_handler(1, handler, token);
}
static HRESULT WINAPI statics_get_gamepads(void *self, void **out) {
    (void)self;
    if (!out) return E_POINTER;
    *out = &view;
    return S_OK;
}
static void *statics_vtbl[11] = {(void *)statics_qi,         (void *)static_addref,      (void *)static_release,
                                 (void *)no_iids,            (void *)gamepad_class_name, (void *)trust_level,
                                 (void *)statics_add_added,  (void *)statics_remove,     (void *)statics_add_removed,
                                 (void *)statics_remove,     (void *)statics_get_gamepads};
static struct {
    void **vtbl;
} statics = {statics_vtbl};

/* ------------------------------------------------------------------ Azimuth's import */

static GetFactoryFn original_get_factory;
static RawBufferFn raw_buffer;
static volatile LONG wrapped;

static int is_gamepad_class(void *name) {
    if (!raw_buffer) {
        HMODULE combase = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (combase) {
            raw_buffer = (RawBufferFn)(uintptr_t)GetProcAddress(combase, "WindowsGetStringRawBuffer");
            create_string = (CreateStringFn)(uintptr_t)GetProcAddress(combase, "WindowsCreateString");
        }
        if (!raw_buffer) return 0;
    }
    UINT32 length = 0;
    const wchar_t *text = raw_buffer(name, &length);
    static const wchar_t wanted[] = L"Windows.Gaming.Input.Gamepad";
    if (!text || length != ARRAY_COUNT(wanted) - 1) return 0;
    for (UINT32 i = 0; i < length; ++i) {
        if (text[i] != wanted[i]) return 0;
    }
    return 1;
}

static HRESULT WINAPI hook_get_factory(void *name, const GUID *iid, void **out) {
    if (!out || !guid_eq(iid, &IID_GamepadStatics) || !is_gamepad_class(name)) {
        return original_get_factory(name, iid, out);
    }
    if (!loaded) settings_load();
    if (!values[KEY_SETTING_ENABLED]) {
        log_append("KEYBOARD: keyboard off (FH2_Keyboard.ini Enabled=0); gamepads unchanged.\r\n");
        return original_get_factory(name, iid, out);
    }
    if (mod_scan_mapper_running()) {
        log_append("KEYBOARD: Azimuth Mapper is running; Gripline keyboard off, gamepads unchanged.\r\n");
        mod_scan_announce_mapper();
        return original_get_factory(name, iid, out);
    }
    if (!real_statics) {
        void *real = NULL;
        HRESULT hr = original_get_factory(name, iid, &real);
        real_statics = hr >= 0 ? real : NULL;
        log_append(real_statics ? "KEYBOARD: keyboard active; real gamepads are merged into one "
                                  "controller.\r\n"
                                : "KEYBOARD: keyboard active; Windows.Gaming.Input unavailable, "
                                  "keyboard only.\r\n");
    }
    pad.vtbl = pad_vtbl;
    InterlockedExchange(&wrapped, 1);
    *out = &statics;
    return S_OK;
}

/* the IAT slot of RoGetActivationFactory in the host (azimuth.exe), found through its imports */
static void *volatile *find_factory_import(unsigned char *host) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)host;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(host + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(host + dir.VirtualAddress); d->Name; ++d) {
        const char *dll = (const char *)(host + d->Name);
        if (!text_equal_ignore_case(dll, "api-ms-win-core-winrt-l1-1-0.dll") &&
            !text_equal_ignore_case(dll, "combase.dll"))
            continue;
        if (!d->OriginalFirstThunk) continue;
        uint64_t *names = (uint64_t *)(host + d->OriginalFirstThunk);
        void **slots = (void **)(host + d->FirstThunk);
        for (unsigned k = 0; names[k]; ++k) {
            if (names[k] >> 63) continue;
            if (text_equal_ignore_case((const char *)(host + (names[k] & 0x7fffffff) + 2), "RoGetActivationFactory"))
                return (void *volatile *)&slots[k];
        }
    }
    return NULL;
}

int keyboard_pad_arm(unsigned char *host_image, const wchar_t *settings_directory) {
    if (!host_image || !settings_directory || !path_join(ini_path, settings_directory, KEY_INI)) return 0;
    ini_migrate(settings_directory); /* German files of fork1-fork3 -> English ones */
    void *volatile *slot = find_factory_import(host_image);
    void *current = slot ? *slot : NULL;
    if (!current || current == (void *)(uintptr_t)hook_get_factory) return 0;
    /* only take it over while it still points into the system (no other mod chained in) */
    HMODULE owner = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)current, &owner) ||
        owner != GetModuleHandleW(L"combase.dll"))
        return 0;
    DWORD protection, ignored;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &protection)) return 0;
    original_get_factory = (GetFactoryFn)current;
    void *previous = InterlockedCompareExchangePointer(slot, (void *)(uintptr_t)hook_get_factory, current);
    VirtualProtect((void *)slot, sizeof(void *), protection, &ignored);
    return previous == current;
}

/* plugin build: the platform may load Gripline after Azimuth asked for its gamepads, so the import
   swap above comes too late. Then the real IGamepadStatics (a cached singleton, the same object
   Azimuth holds) gets its get_Gamepads entry pointed at the merged list instead. */
int keyboard_pad_late(void) {
    if (wrapped) return 1;
    if (!ini_path[0]) return 0;
    if (!loaded) settings_load();
    if (!values[KEY_SETTING_ENABLED] || mod_scan_mapper_running()) return 0;
    HMODULE combase = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!combase) return 0;
    GetFactoryFn get_factory = original_get_factory ? original_get_factory
                                                    : (GetFactoryFn)(uintptr_t)GetProcAddress(combase, "RoGetActivationFactory");
    if (!create_string) create_string = (CreateStringFn)(uintptr_t)GetProcAddress(combase, "WindowsCreateString");
    if (!get_factory || !create_string) return 0;
    void *name = NULL, *real = NULL;
    if (create_string(L"Windows.Gaming.Input.Gamepad", 28, &name) < 0) return 0;
    HRESULT hr = get_factory(name, &IID_GamepadStatics, &real);
    typedef HRESULT(WINAPI * DeleteStringFn)(void *);
    DeleteStringFn delete_string = (DeleteStringFn)(uintptr_t)GetProcAddress(combase, "WindowsDeleteString");
    if (delete_string) delete_string(name);
    if (hr < 0 || !real) return 0;
    void **slot = &VT(real)[10];
    DWORD protection, ignored;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &protection)) return 0;
    real_statics = real;
    real_get_pads = (GetPadsFn)*slot;
    pad.vtbl = pad_vtbl;
    InterlockedExchangePointer(slot, (void *)(uintptr_t)statics_get_gamepads);
    VirtualProtect((void *)slot, sizeof(void *), protection, &ignored);
    InterlockedExchange(&wrapped, 1);
    log_append("KEYBOARD: keyboard active (late hook); real gamepads are merged into one controller.\r\n");
    return 1;
}

void keyboard_pad_game_start(unsigned char *game_image, const wchar_t *settings_directory) {
    if (!ini_path[0] && (!settings_directory || !path_join(ini_path, settings_directory, KEY_INI))) return;
    ini_migrate(settings_directory);
    if (!loaded) settings_load();
    game_base = game_image;
    if (values[KEY_SETTING_ENABLED] && !mod_scan_mapper_running()) wheel_buttons_install(game_image); /* follows the controller layout */
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestKeyboardFactoryHook(void) {
    return (void *)hook_get_factory;
}
__declspec(dllexport) void Fh2TestKeyboardSetup(GetFactoryFn original, const wchar_t *directory) {
    original_get_factory = original;
    path_join(ini_path, directory, KEY_INI);
    settings_load();
}
__declspec(dllexport) void Fh2TestKeyboardAsync(AsyncKeyFn keys, ForegroundFn fg, WindowPidFn pid, RawBufferFn raw) {
    async_key = keys;
    foreground = fg;
    window_pid = pid;
    raw_buffer = raw;
}
__declspec(dllexport) void Fh2TestKeyboardGame(unsigned char *game_image) {
    game_base = game_image;
}
__declspec(dllexport) int Fh2TestKeyboardBinding(unsigned index, unsigned k) {
    return index < ARRAY_COUNT(bindings) && k < 4 ? bindings[index].vk[k] : -1;
}
#endif
