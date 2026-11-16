/* Other mods that already do something Gripline does - see mod_partners.h. Own code. */
#include "gripline.h"
#include "mod_partners.h"
#include "logging.h"
#include "notify.h"
#include "support.h"
#include "game_language.h"
#include "noclip.h"
#include "cockpit_view.h"

#define PART_KEYBOARD 1
#define PART_MENU_MOUSE 2
#define PART_DRIVE_LOOK 4
#define PART_SIGNAL_SOUND 8   /* Extra Options: its own turn signal sound (V7.2) */
#define PART_SIGNAL_ARROWS 16 /* Extra Options: its own turn signal arrows / HUD lamp (V7.2) */

static volatile LONG yield_setting = 1;
static LONG mouse_parts;   /* parts FH2 Mouse & Keyboard has switched on (PART_*) */
static int mouse_found;    /* FH2 Mouse & Keyboard installed */
static char mouse_version[16];
static unsigned char *game;
static wchar_t mouse_ini[PATH_CAPACITY];
static void walk_keys_start(void);
static int lower(int c);
static LONG eo_parts;           /* PART_SIGNAL_* Extra Options has switched on */
static char eo_sound_key[48], eo_arrows_key[48];

int mod_partners_yield(void) {
    return (int)yield_setting;
}

void mod_partners_remember_yield(int yield) {
    InterlockedExchange(&yield_setting, yield ? 1 : 0);
}

static int taken(LONG part) {
    return yield_setting && (mouse_parts & part) != 0;
}

int mod_partners_takes_keyboard(void) {
    return taken(PART_KEYBOARD);
}

int mod_partners_takes_menu_mouse(void) {
    return taken(PART_MENU_MOUSE);
}

int mod_partners_takes_drive_look(void) {
    return taken(PART_DRIVE_LOOK);
}

int mod_partners_takes_signal_sound(void) {
    return yield_setting && (eo_parts & PART_SIGNAL_SOUND) != 0;
}

int mod_partners_takes_signal_arrows(void) {
    return yield_setting && (eo_parts & PART_SIGNAL_ARROWS) != 0;
}

/* ------------------------------------------------------------------ Extra Options' turn signals

   V7.2 (Marvin 15.11.2026: if Extra Options brings its own turn signal sound or display one day, Gripline's
   goes away). Extra Options 0.6.1 has none: its INI only has the keys for switching them (SignalLeftXbox,
   HazardsKeyboard ...). Its option name is not known yet, so FH2_ExtraOptions.ini is read for any switched
   on key that names a turn signal AND a sound or a display: "IndicatorSound=1", "TurnSignalHud=On",
   "HazardArrows=true" ... Keys only about buttons (Xbox, Keyboard, Hold, Dpad) are not such a key. */

static int has_word(const char *key, const char *const *words) {
    for (; *words; ++words) {
        const char *w = *words;
        for (const char *k = key; *k; ++k) {
            unsigned i = 0;
            while (w[i] && k[i] && lower(k[i]) == w[i]) ++i;
            if (!w[i]) return 1;
        }
    }
    return 0;
}

static int value_on(const char *v) {
    while (*v == ' ' || *v == '\t') ++v;
    if (!*v || *v == ';' || *v == '#') return 0;
    static const char *const off[] = {"0", "off", "false", "no", "none", "disabled", NULL};
    for (const char *const *o = off; *o; ++o) {
        unsigned i = 0;
        while ((*o)[i] && lower(v[i]) == (*o)[i]) ++i;
        if (!(*o)[i] && (!v[i] || v[i] == ' ' || v[i] == '\t' || v[i] == ';' || v[i] == '\r' || v[i] == '\n')) return 0;
    }
    return 1;
}

static void eo_signals_scan(const char *text, unsigned length) {
    static const char *const signal[] = {"indicator", "turnsignal", "signal", "blinker", "hazard", NULL};
    static const char *const sound[] = {"sound", "audio", "click", "tick", "chime", "relay", "volume", NULL};
    static const char *const shown[] = {"hud", "arrow", "icon", "dashboard", "display", "lamp", "overlay", NULL};
    static const char *const button[] = {"xbox", "keyboard", "hold", "dpad", "button", "key", "material", NULL};
    unsigned i = 0;
    while (i < length) {
        unsigned start = i;
        while (i < length && text[i] != '\n') ++i;
        unsigned end = i++;
        while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
        if (start >= end || text[start] == ';' || text[start] == '#' || text[start] == '[') continue;
        unsigned eq = start;
        while (eq < end && text[eq] != '=') ++eq;
        if (eq >= end || eq - start >= 48) continue;
        char key[48], value[32];
        unsigned k = 0;
        for (unsigned j = start; j < eq; ++j)
            if (text[j] != ' ' && text[j] != '\t' && text[j] != '_') key[k++] = text[j];
        key[k] = 0;
        unsigned v = 0;
        for (unsigned j = eq + 1; j < end && v + 1 < sizeof(value); ++j) value[v++] = text[j];
        value[v] = 0;
        if (!has_word(key, signal) || has_word(key, button) || !value_on(value)) continue;
        if (has_word(key, sound) && !(eo_parts & PART_SIGNAL_SOUND)) {
            eo_parts |= PART_SIGNAL_SOUND;
            memory_copy(eo_sound_key, key, k + 1);
        }
        if (has_word(key, shown) && !(eo_parts & PART_SIGNAL_ARROWS)) {
            eo_parts |= PART_SIGNAL_ARROWS;
            memory_copy(eo_arrows_key, key, k + 1);
        }
    }
}

static void eo_signals_detect(const wchar_t *azimuth_dir) {
    static wchar_t path[PATH_CAPACITY];
    static char text[65536];
    eo_parts = 0, eo_sound_key[0] = eo_arrows_key[0] = 0;
    if (!path_join(path, azimuth_dir, L"FH2_ExtraOptions.ini")) return;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD got = 0;
    if (!ReadFile(f, text, sizeof(text) - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    text[got] = 0;
    eo_signals_scan(text, got);
}

static void copy_version(const char *version) {
    unsigned n = 0;
    if (version)
        for (; version[n] && n + 1 < sizeof(mouse_version); ++n) mouse_version[n] = version[n];
    mouse_version[n] = 0;
}

void mod_partners_detect(const wchar_t *azimuth_dir, ModVersionFn version_of) {
    static wchar_t path[PATH_CAPACITY];
    if (!azimuth_dir) return;
    if (path_join(path, azimuth_dir, L"Gripline.ini"))
        mod_partners_remember_yield(GetPrivateProfileIntW(L"Gripline", L"YieldToMods", 1, path) != 0);
    /* mods start in alphabetical order of their DLL: FH2Mouse.dll has registered before Gripline.dll */
    const char *version = version_of ? version_of("FH2 Mouse & Keyboard") : NULL;
    mouse_found = version != NULL || GetModuleHandleW(L"FH2Mouse.dll") != NULL;
    copy_version(version);
    eo_signals_detect(azimuth_dir);
    mouse_parts = 0;
    mouse_ini[0] = 0;
    if (mouse_found && path_join(path, azimuth_dir, L"FH2Mouse\\FH2Mouse.ini")) {
        memory_copy(mouse_ini, path, sizeof(mouse_ini));
        /* a missing file or key reads as FH2Mouse's own default: on */
        if (GetPrivateProfileIntW(L"Mouse", L"Enabled", 1, path)) {
            mouse_parts |= PART_MENU_MOUSE;
            if (GetPrivateProfileIntW(L"Keyboard", L"Enabled", 1, path)) mouse_parts |= PART_KEYBOARD;
            if (GetPrivateProfileIntW(L"Mouse", L"FreeLook", 1, path)) mouse_parts |= PART_DRIVE_LOOK;
        }
    }
}

void mod_partners_start(unsigned char *game_image) {
    game = game_image;
    if (eo_parts) {
        if (eo_parts & PART_SIGNAL_SOUND) {
            log_append("PARTNERS: Extra Options has its own turn signal sound (");
            log_append(eo_sound_key);
            log_append(yield_setting ? ") - Gripline's click off.\r\n" : ") - YieldToMods=0, Gripline's click stays.\r\n");
        }
        if (eo_parts & PART_SIGNAL_ARROWS) {
            log_append("PARTNERS: Extra Options shows its own turn signals (");
            log_append(eo_arrows_key);
            log_append(yield_setting ? ") - Gripline's arrows off.\r\n" : ") - YieldToMods=0, Gripline's arrows stay.\r\n");
        }
    }
    if (!mouse_found) return;
    log_append("PARTNERS: FH2 Mouse & Keyboard ");
    log_append(mouse_version[0] ? mouse_version : "(version unknown)");
    log_append(" is installed.\r\n");
    if (!mouse_parts) {
        log_append("PARTNERS: it is switched off (FH2Mouse.ini) - Gripline keeps its keyboard and mouse.\r\n");
        return;
    }
    if (mouse_parts & PART_KEYBOARD) walk_keys_start();
    if (!yield_setting) {
        log_append("PARTNERS: YieldToMods=0 - Gripline keeps its own keyboard and mouse next to it.\r\n");
        return;
    }
    if (mouse_parts & PART_KEYBOARD) log_append("PARTNERS: its keys drive - Gripline keyboard driving off.\r\n");
    if (mouse_parts & PART_MENU_MOUSE) log_append("PARTNERS: its cursor works the menus - Gripline menu mouse off.\r\n");
    if (mouse_parts & PART_DRIVE_LOOK) log_append("PARTNERS: its free look turns the camera - Gripline mouse look off.\r\n");
    log_append("PARTNERS: Gripline's own hotkeys (noclip, freecam, walking, F8 ...) keep working.\r\n");
    /* V7.2 calm start: no own card any more (Marvin 14.11.2026), the log above says what moved */
}

/* ------------------------------------------------------------------ walking with FH2Mouse's keys

   V7.2 (Marvin 14.11.2026: "wenn ich mit wasd laufe, lenkt und gibt gas das Auto im stehen"): FH2 Mouse &
   Keyboard reads its keys with GetAsyncKeyState and drives the car whenever the game thinks the player
   drives - also while Gripline walks, where the parked car then steers and revs on W A S D. While walking,
   its own driving keys (from its ini) read as up for it; Gripline's walking reads the keys itself and is
   not touched. Keys it also uses in the menus (Accept, Up, ButtonRT ...) are left alone, so the pause
   menu keeps working while walking. Only FH2Mouse.dll's own import slot is changed. */

#define WALK_KEY_COUNT 256
static unsigned char walk_blocked[WALK_KEY_COUNT];
static unsigned walk_blocked_count;
static int camera_vk = 'V'; /* its Camera key: pressed for getting out without a controller */
typedef SHORT(WINAPI *KeyStateFn)(int);
static KeyStateFn original_key_state;
static volatile LONG walk_keys_seen;

static int lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int name_is(const wchar_t *name, unsigned length, const char *want) {
    unsigned i = 0;
    for (; i < length && want[i]; ++i)
        if (lower(name[i]) != lower((unsigned char)want[i])) return 0;
    return i == length && !want[i];
}

/* one key name of FH2Mouse.ini -> virtual-key code (0 = none / unknown) */
static int key_code(const wchar_t *name, unsigned length) {
    static const struct { const char *name; unsigned char vk; } named[] = {
        {"Left", 0x25}, {"Right", 0x27}, {"Up", 0x26}, {"Down", 0x28}, {"Space", 0x20}, {"Enter", 0x0d},
        {"Escape", 0x1b}, {"Tab", 0x09}, {"Backspace", 0x08}, {"Insert", 0x2d}, {"Delete", 0x2e},
        {"Home", 0x24}, {"End", 0x23}, {"PageUp", 0x21}, {"PageDown", 0x22}, {"Shift", 0x10},
        {"LShift", 0xa0}, {"RShift", 0xa1}, {"Ctrl", 0x11}, {"LCtrl", 0xa2}, {"RCtrl", 0xa3},
        {"Alt", 0x12}, {"LAlt", 0xa4}, {"RAlt", 0xa5}, {"Comma", 0xbc}, {"Period", 0xbe},
        {"Minus", 0xbd}, {"Equals", 0xbb}, {"Semicolon", 0xba}, {"Slash", 0xbf}, {"Grave", 0xc0},
        {"LBracket", 0xdb}, {"Backslash", 0xdc}, {"RBracket", 0xdd}, {"Quote", 0xde}};
    if (!length) return 0;
    if (length == 1) {
        int c = name[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
        return 0;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(named); ++i)
        if (name_is(name, length, named[i].name)) return named[i].vk;
    if (length > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        int v = 0;
        for (unsigned i = 2; i < length; ++i) {
            int c = lower(name[i]);
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0 || v > 0xff) return 0;
            v = v * 16 + d;
        }
        return v > 0 && v < WALK_KEY_COUNT ? v : 0;
    }
    if ((name[0] == 'F' || name[0] == 'f') && length <= 3) {
        int v = 0;
        for (unsigned i = 1; i < length; ++i) {
            if (name[i] < '0' || name[i] > '9') return 0;
            v = v * 10 + (name[i] - '0');
        }
        return v >= 1 && v <= 24 ? 0x6f + v : 0;
    }
    if (length == 4 && name_is(name, 3, "Num") && name[3] >= '0' && name[3] <= '9') return 0x60 + (name[3] - '0');
    return 0;
}

/* marks every key of one [Keyboard] action (fallback = FH2Mouse's own default when the ini has none) */
static void action_keys(const wchar_t *ini, const wchar_t *action, const wchar_t *fallback, unsigned char mark) {
    wchar_t value[128];
    GetPrivateProfileStringW(L"Keyboard", action, fallback, value, ARRAY_COUNT(value), ini);
    for (unsigned i = 0; value[i];) {
        while (value[i] == L' ' || value[i] == L'\t') ++i;
        unsigned start = i;
        while (value[i] && value[i] != L' ' && value[i] != L'\t') ++i;
        int vk = key_code(value + start, i - start);
        if (vk > 0 && vk < WALK_KEY_COUNT) walk_blocked[vk] = mark;
    }
}

static void read_walk_keys(const wchar_t *ini) {
    static const wchar_t *const drive[][2] = {
        {L"Gas", L"W"}, {L"Brake", L"S"}, {L"SteerLeft", L"A"}, {L"SteerRight", L"D"}, {L"Handbrake", L"Space"},
        {L"Clutch", L""}, {L"ShiftUp", L""}, {L"ShiftDown", L""}, {L"LookBack", L""}, {L"LookLeft", L""},
        {L"LookRight", L""}, {L"Rewind", L""}, {L"Nitro", L""}, {L"Horn", L""}};
    static const wchar_t *const menu[][2] = {
        {L"Pause", L"Escape"}, {L"Accept", L"Enter"}, {L"Back", L"Escape"}, {L"Up", L"Up"}, {L"Down", L"Down"},
        {L"Left", L"Left"}, {L"Right", L"Right"}, {L"PreviousTab", L""}, {L"NextTab", L""}, {L"ButtonX", L""},
        {L"ButtonY", L""}, {L"ButtonView", L""}, {L"ButtonLT", L""}, {L"ButtonRT", L""}, {L"ButtonLS", L""},
        {L"ButtonRS", L""}};
    for (unsigned i = 0; i < WALK_KEY_COUNT; ++i) walk_blocked[i] = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(drive); ++i) action_keys(ini, drive[i][0], drive[i][1], 1);
    for (unsigned i = 0; i < ARRAY_COUNT(menu); ++i) action_keys(ini, menu[i][0], menu[i][1], 0); /* menus keep these */
    walk_blocked_count = 0;
    for (unsigned i = 0; i < WALK_KEY_COUNT; ++i) walk_blocked_count += walk_blocked[i];
    { /* the first key of its Camera action */
        wchar_t value[64];
        GetPrivateProfileStringW(L"Keyboard", L"Camera", L"V", value, ARRAY_COUNT(value), ini);
        unsigned i = 0, start;
        while (value[i] == L' ' || value[i] == L'\t') ++i;
        start = i;
        while (value[i] && value[i] != L' ' && value[i] != L'\t') ++i;
        camera_vk = key_code(value + start, i - start);
    }
}

#ifdef FH2_TWEAKS_TEST
static int test_walking = -1;
#endif

static int walking_now(void) {
#ifdef FH2_TWEAKS_TEST
    if (test_walking >= 0) return test_walking;
#endif
    return noclip_walking();
}

static SHORT WINAPI walk_key_state(int vk) {
    if (vk == camera_vk && vk > 0 && cockpit_view_key_press()) return (SHORT)0x8000; /* getting out / in: next camera */
    if (vk > 0 && vk < WALK_KEY_COUNT && walk_blocked[vk] && walking_now()) {
        if (!InterlockedExchange(&walk_keys_seen, 1))
            log_append("PARTNERS: walking - FH2 Mouse & Keyboard's driving keys held back from the car.\r\n");
        return 0;
    }
    return original_key_state ? original_key_state(vk) : 0;
}

/* IAT slot of USER32!GetAsyncKeyState in a loaded module */
static void *volatile *key_state_slot(unsigned char *module) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return NULL;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(module + dir.VirtualAddress); d->Name; ++d) {
        if (!text_equal_ignore_case((const char *)(module + d->Name), "USER32.dll")) continue;
        IMAGE_THUNK_DATA64 *names =
            (IMAGE_THUNK_DATA64 *)(module + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        void **slots = (void **)(module + d->FirstThunk);
        for (unsigned i = 0; names[i].u1.AddressOfData; ++i) {
            ULONGLONG entry = names[i].u1.AddressOfData;
            if (entry & IMAGE_ORDINAL_FLAG64) continue;
            IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(module + (DWORD)entry);
            if (text_equal_ignore_case((const char *)by_name->Name, "GetAsyncKeyState")) return (void *volatile *)&slots[i];
        }
    }
    return NULL;
}

static int link_walk_keys(HMODULE module, const wchar_t *ini) {
    void *volatile *slot = key_state_slot((unsigned char *)module);
    if (!slot) {
        log_append("PARTNERS: FH2 Mouse & Keyboard reads its keys differently - its driving keys stay on while walking.\r\n");
        return 0;
    }
    read_walk_keys(ini);
    if (*slot == (void *)walk_key_state) return 1;
    DWORD old;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    original_key_state = (KeyStateFn)*slot;
    InterlockedExchangePointer((void *volatile *)slot, (void *)walk_key_state);
    VirtualProtect((void *)slot, sizeof(void *), old, &old);
    log_append_value("PARTNERS: walking holds back FH2 Mouse & Keyboard's driving keys from the car - keys: ",
                     (int)walk_blocked_count);
    return 1;
}

static void walk_keys_start(void) {
    if (!mouse_ini[0]) return;
    HMODULE module = GetModuleHandleW(L"FH2Mouse.dll");
    if (module) link_walk_keys(module, mouse_ini);
}

static void put(char *out, unsigned size, unsigned *n, const char *text) {
    while (*text && *n + 1 < size) out[(*n)++] = *text++;
    out[*n] = 0;
}

unsigned mod_partners_report(char *out, unsigned size) {
    unsigned n = 0;
    if (!size) return 0;
    out[0] = 0;
    put(out, size, &n, "Yield to other mods: ");
    put(out, size, &n, yield_setting ? "on" : "off (YieldToMods=0)");
    put(out, size, &n, "\r\nExtra Options turn signals: ");
    put(out, size, &n, eo_parts & PART_SIGNAL_SOUND ? eo_sound_key : "no own sound");
    put(out, size, &n, ", ");
    put(out, size, &n, eo_parts & PART_SIGNAL_ARROWS ? eo_arrows_key : "no own display");
    put(out, size, &n, "\r\nFH2 Mouse & Keyboard: ");
    if (!mouse_found) {
        put(out, size, &n, "not installed\r\n");
        return n;
    }
    put(out, size, &n, mouse_version[0] ? mouse_version : "installed");
    put(out, size, &n, mouse_parts & PART_KEYBOARD ? ", keys on" : ", keys off");
    put(out, size, &n, mouse_parts & PART_MENU_MOUSE ? ", cursor on" : ", mod off");
    put(out, size, &n, mouse_parts & PART_DRIVE_LOOK ? ", free look on" : ", free look off");
    put(out, size, &n, "\r\nLeft to it: ");
    if (!mouse_parts || !yield_setting) put(out, size, &n, "nothing");
    else {
        int first = 1;
        if (mouse_parts & PART_KEYBOARD) put(out, size, &n, "keyboard driving"), first = 0;
        if (mouse_parts & PART_MENU_MOUSE) put(out, size, &n, first ? "menu mouse" : ", menu mouse"), first = 0;
        if (mouse_parts & PART_DRIVE_LOOK) put(out, size, &n, first ? "mouse look" : ", mouse look");
    }
    put(out, size, &n, "\r\n");
    return n;
}

#ifdef FH2_TWEAKS_TEST
static const char *test_version;
static const char *test_version_of(const char *name) {
    return text_equal_ignore_case(name, "FH2 Mouse & Keyboard") ? test_version : NULL;
}

/* installed: NULL = FH2 Mouse & Keyboard not there, else its version */
__declspec(dllexport) void Fh2TestPartnersDetect(const wchar_t *directory, const char *installed) {
    test_version = installed;
    mod_partners_detect(directory, test_version_of);
}

__declspec(dllexport) int Fh2TestPartnersTaken(void) {
    return mod_partners_takes_keyboard() | mod_partners_takes_menu_mouse() << 1 | mod_partners_takes_drive_look() << 2 |
           mod_partners_takes_signal_sound() << 3 | mod_partners_takes_signal_arrows() << 4;
}

/* V7.2: Extra Options' INI text -> PART_SIGNAL_* found */
__declspec(dllexport) int Fh2TestPartnersEoScan(const char *text, unsigned length) {
    eo_parts = 0;
    eo_signals_scan(text, length);
    return (int)eo_parts;
}

/* a fake module's GetAsyncKeyState slot is linked; walking = noclip's walking flag for the test */
__declspec(dllexport) int Fh2TestPartnersWalkLink(void *module, const wchar_t *ini) {
    return link_walk_keys((HMODULE)module, ini);
}

__declspec(dllexport) void Fh2TestPartnersWalking(int walking) {
    test_walking = walking;
}

__declspec(dllexport) int Fh2TestPartnersKeyState(int vk) {
    return walk_key_state(vk);
}

__declspec(dllexport) void *Fh2TestPartnersKeyHook(void) {
    return (void *)walk_key_state;
}

__declspec(dllexport) int Fh2TestPartnersCameraKey(void) {
    return camera_vk;
}

__declspec(dllexport) int Fh2TestPartnersWalkBlocked(int vk) {
    return vk > 0 && vk < WALK_KEY_COUNT ? walk_blocked[vk] : 0;
}

__declspec(dllexport) unsigned Fh2TestPartnersReport(char *out, unsigned size) {
    return mod_partners_report(out, size);
}
#endif
