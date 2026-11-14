/* Other mods that already do something Gripline does - see mod_partners.h. Own code. */
#include "gripline.h"
#include "mod_partners.h"
#include "logging.h"
#include "notify.h"
#include "support.h"
#include "game_language.h"

#define PART_KEYBOARD 1
#define PART_MENU_MOUSE 2
#define PART_DRIVE_LOOK 4

static volatile LONG yield_setting = 1;
static LONG mouse_parts;   /* parts FH2 Mouse & Keyboard has switched on (PART_*) */
static int mouse_found;    /* FH2 Mouse & Keyboard installed */
static char mouse_version[16];
static unsigned char *game;

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

const wchar_t *mod_partners_mouse_name(void) {
    return L"FH2 Mouse & Keyboard";
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
    mouse_parts = 0;
    if (mouse_found && path_join(path, azimuth_dir, L"FH2Mouse\\FH2Mouse.ini")) {
        /* a missing file or key reads as FH2Mouse's own default: on */
        if (GetPrivateProfileIntW(L"Mouse", L"Enabled", 1, path)) {
            mouse_parts |= PART_MENU_MOUSE;
            if (GetPrivateProfileIntW(L"Keyboard", L"Enabled", 1, path)) mouse_parts |= PART_KEYBOARD;
            if (GetPrivateProfileIntW(L"Mouse", L"FreeLook", 1, path)) mouse_parts |= PART_DRIVE_LOOK;
        }
    }
}

static int mouse_card(char *out, unsigned size) {
    int de = game && game_language_is_german(game);
    const char *text;
    if ((mouse_parts & (PART_KEYBOARD | PART_MENU_MOUSE)) == (PART_KEYBOARD | PART_MENU_MOUSE))
        text = de ? "FH2 Mouse & Keyboard erkannt\nTastatur und Maus kommen von dort" : "FH2 Mouse & Keyboard found\nKeyboard and mouse come from it";
    else if (mouse_parts & PART_KEYBOARD)
        text = de ? "FH2 Mouse & Keyboard erkannt\nTastatur kommt von dort" : "FH2 Mouse & Keyboard found\nThe keyboard comes from it";
    else
        text = de ? "FH2 Mouse & Keyboard erkannt\nMaus kommt von dort" : "FH2 Mouse & Keyboard found\nThe mouse comes from it";
    unsigned n = 0;
    while (text[n] && n + 1 < size) out[n] = text[n], ++n;
    out[n] = 0;
    return n > 0;
}

void mod_partners_start(unsigned char *game_image) {
    game = game_image;
    if (!mouse_found) return;
    log_append("PARTNERS: FH2 Mouse & Keyboard ");
    log_append(mouse_version[0] ? mouse_version : "(version unknown)");
    log_append(" is installed.\r\n");
    if (!mouse_parts) {
        log_append("PARTNERS: it is switched off (FH2Mouse.ini) - Gripline keeps its keyboard and mouse.\r\n");
        return;
    }
    if (!yield_setting) {
        log_append("PARTNERS: YieldToMods=0 - Gripline keeps its own keyboard and mouse next to it.\r\n");
        return;
    }
    if (mouse_parts & PART_KEYBOARD) log_append("PARTNERS: its keys drive - Gripline keyboard driving off.\r\n");
    if (mouse_parts & PART_MENU_MOUSE) log_append("PARTNERS: its cursor works the menus - Gripline menu mouse off.\r\n");
    if (mouse_parts & PART_DRIVE_LOOK) log_append("PARTNERS: its free look turns the camera - Gripline mouse look off.\r\n");
    log_append("PARTNERS: Gripline's own hotkeys (noclip, freecam, walking, F8 ...) keep working.\r\n");
    if (notify_kind_on(NOTIFY_KIND_START)) notify_show_deferred_call(mouse_card);
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
    return mod_partners_takes_keyboard() | mod_partners_takes_menu_mouse() << 1 | mod_partners_takes_drive_look() << 2;
}

__declspec(dllexport) unsigned Fh2TestPartnersReport(char *out, unsigned size) {
    return mod_partners_report(out, size);
}
#endif
