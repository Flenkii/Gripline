/* fh2-mods\Gripline.dll - Gripline as an FH2 Mod Platform mod (plugin build, FH2_GRIPLINE_PLUGIN).

   Since FH2MP 1.1.1 the launcher only starts the game with the official Extra Options (0.3.2 then,
   0.3.9 with FH2MP 1.1.2; winmm.dll next to azimuth.exe, checked by its SHA-256). Gripline therefore no longer replaces
   Extra Options: it runs next to it as a platform mod.
   - The platform loads this DLL when Azimuth starts, so the keyboard (Azimuth's gamepad import) and
     the picture cards (DXGI import) are taken over before Azimuth sets them up.
   - Once the game is mapped, the Gripline features start on their own thread, as in runtime.c.
   - Settings are Gripline.ini ([Traffic], [Gripline]); Extra Options refuses unknown keys in its
     own INI. FH2_Keyboard.ini stays as it is.
   - V6 leaves to the others what they already do: the wheel and its force feedback to Azimuth (its
     launcher), weather, time of day and hiding the HUD to Extra Options. Gripline only reads Azimuth's
     wheel rotation for the cockpit animation.
   - Steering animation and More Traffic files go to mods\Gripline\media, which Extra Options mounts
     over game:\media like any mod's media folder.
   - The in-game settings are rows on Gripline's page under Pause > MODS > YOUR MODS (platform 2.9.0; Pause > Settings > MODS before). */
#include "gripline.h"
#include "../third_party/fh2mod/fh2mod.h"
#include "logging.h"
#include "settings.h"
#include "support.h"
#include "features.h"
#include "features/cockpit.h"
#include "features/cockpit_view.h"
#include "features/keyboard_pad.h"
#include "features/key_menu.h"
#include "features/mod_scan.h"
#include "features/mod_partners.h"
#include "features/anna_link.h"
#include "features/nitro_link.h"
#include "features/notify.h"
#include "features/ini_edit.h"
#include "features/report.h"
#include "features/rig_settings.h"
#include "features/traffic_files.h"
#include "features/traffic_live.h"
#include "features/world_files.h"
#include "features/telemetry.h"
#include "features/logbook.h"
#include "features/drive_look.h"
#include "features/dashcam_cars.h"
#include "features/records.h"
#include "features/game_language.h"
#include "features/noclip.h"
#include "features/freecam_drive.h"
#include "features/walking.h"
#include "features/summon.h"
#include "features/menu_mouse.h"
#include "features/focus_guard.h"
#include "features/no_damage.h"
#include "features/version_check.h"
#include "features/update_check.h"
#include "features/ini_migrate.h"
#include "features/cruise.h"
#include "features/pause_tile.h"
#include "features/blinker.h"

void keyboard_pad_game_start(unsigned char *game_image, const wchar_t *settings_directory);
void report_init(const wchar_t *settings_directory);

static const FH2ModApi *api;
static FH2Mod *mod;
static wchar_t directory[PATH_CAPACITY];   /* the Azimuth folder (next to azimuth.exe) */
static wchar_t game_path[PATH_CAPACITY];   /* ...\Mount\forza_durango_release_final.exe */
static wchar_t mount_dir[PATH_CAPACITY];   /* ...\Mount */
static wchar_t media_dir[PATH_CAPACITY];   /* Azimuth\mods\Forza Horizon 2\Gripline\media (EO 0.6.0) or mods\Gripline\media (older) */
static unsigned char *game;
static volatile LONG started;

__declspec(dllexport) const char *Fh2TweaksStatus(void) {
    return status_get();
}

static void platform_log(const char *line) {
    if (api && mod) api->log(mod, line);
}

/* ------------------------------------------------------------------ paths */

/* the platform knows both: Azimuth's folder and the game's Mount folder */
static int find_folders(void) {
    if (!api->azimuth_dir || !api->azimuth_dir[0] || lstrlenW(api->azimuth_dir) >= PATH_CAPACITY) return 0;
    lstrcpynW(directory, api->azimuth_dir, PATH_CAPACITY);
    if (api->game_dir && api->game_dir[0] && lstrlenW(api->game_dir) < PATH_CAPACITY) {
        lstrcpynW(mount_dir, api->game_dir, PATH_CAPACITY);
        path_join(game_path, mount_dir, GAME_EXE_NAME);
    }
    return 1;
}

/* ------------------------------------------------------------------ Gripline.ini */

static int write_whole(const wchar_t *path, const char *data, size_t length, DWORD disposition) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(file);
    return ok;
}

/* V7.2 (Marvin 15.11.2026): the Mods row "Notifications" (every card off) is gone; F8 / Gripline's page
   "Cards & notices" has presets for it. Notifications=0 becomes the preset "Warnings only" once, saved in
   Gripline.ini (Notifications=1, CardsHidden), so the player keeps a quiet screen and can change it there. */
static void retire_notifications_switch(ModOptions *o) {
    if (o->notifications) return;
    o->notifications = 1;
    o->cards_hidden |= NOTIFY_KINDS_ALL;
    static wchar_t ini[PATH_CAPACITY];
    static char text[16384], step[16384], out[16384];
    int saved = 0;
    if (path_join(ini, directory, MOD_INI_NAME)) {
        settings_file_lock();
        DWORD got = 0;
        HANDLE file = CreateFileW(ini, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            if (!ReadFile(file, text, sizeof(text) - 1, &got, NULL)) got = 0;
            CloseHandle(file);
        }
        char number[12];
        size_t n = got ? ini_set_value(text, got, "Gripline", "Notifications", "1", step, sizeof(step)) : 0;
        n = n ? ini_set_value(step, n, "Gripline", "CardsHidden", ini_format_int(o->cards_hidden, number), out, sizeof(out)) : 0;
        if (n) saved = write_whole(ini, out, n, CREATE_ALWAYS);
        settings_file_unlock();
    }
    log_append(saved ? "CARDS: Notifications=0 is now the preset 'Warnings only' (CardsHidden), saved.\r\n"
                     : "CARDS: Notifications=0 is now the preset 'Warnings only' - Gripline.ini could not be written.\r\n");
}
#ifdef FH2_TWEAKS_TEST
/* the start thread waits for the game in the tests: the switch's retirement on its own */
__declspec(dllexport) int Fh2TestRetireNotifications(void) {
    static ModOptions o;
    settings_read(directory, &o);
    retire_notifications_switch(&o);
    return o.cards_hidden;
}
#endif

/* Every start, before Extra Options reads its INI (it does so when the game is mapped, the platform
   loads this DLL earlier): the [Traffic] and [Gripline] sections move out of FH2_ExtraOptions.ini,
   because Extra Options (0.3.2 and later) refuses keys it does not know. They go to Gripline.ini when that does
   not exist yet; the INI as it was is kept once as FH2_ExtraOptions.ini.before-gripline. */
static void move_gripline_sections(void) {
    static wchar_t ini[PATH_CAPACITY], old[PATH_CAPACITY], backup[PATH_CAPACITY];
    static char text[16384], ours[16384], rest[16384];
    if (!path_join(ini, directory, MOD_INI_NAME) || !path_join(old, directory, L"FH2_ExtraOptions.ini") ||
        !path_join(backup, directory, L"FH2_ExtraOptions.ini.before-gripline"))
        return;
    DWORD got = 0;
    HANDLE file = CreateFileW(old, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        if (!ReadFile(file, text, sizeof(text) - 1, &got, NULL)) got = 0;
        CloseHandle(file);
    }
    size_t used = 0, kept = 0;
    int mine = 0;
    for (DWORD i = 0; i < got;) {
        DWORD start = i;
        while (i < got && text[i] != '\n') ++i;
        if (i < got) ++i;
        const char *line = text + start;
        DWORD length = i - start, k = 0;
        while (k < length && (line[k] == ' ' || line[k] == '\t')) ++k;
        if (k < length && line[k] == '[') {
            mine = (length - k >= 9 && memory_equal(line + k, "[Traffic]", 9)) ||
                   (length - k >= 10 && memory_equal(line + k, "[Gripline]", 10));
        }
        char *out = mine ? ours : rest;
        size_t *count = mine ? &used : &kept;
        if (*count + length < sizeof(ours)) {
            memory_copy(out + *count, line, length);
            *count += length;
        }
    }
    int have_ini = GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES;
    if (!have_ini) {
        if (used) {
            write_whole(ini, ours, used, CREATE_NEW);
        } else {
            static const char defaults[] = "[Traffic]\r\nTrafficLevel=0\r\nRushHourAllDay=0\r\n"
                                           "[Gripline]\r\nNotifications=1\r\nSteeringAnimation=0\r\n"
                                           "CockpitViewForward=35\r\nCockpitViewUp=-5\r\n";
            write_whole(ini, defaults, sizeof(defaults) - 1, CREATE_NEW);
        }
    }
    if (used) {
        write_whole(backup, text, got, CREATE_NEW); /* only the first time */
        if (write_whole(old, rest, kept, CREATE_ALWAYS))
            platform_log("moved [Traffic]/[Gripline] from FH2_ExtraOptions.ini (Extra Options refuses them)");
    }
}

/* ------------------------------------------------------------------ Mods page rows */

#define MAX_ROWS 40
static unsigned row_ids[MAX_ROWS], row_count;
static char row_keys[MAX_ROWS][8];
static char row_descriptions[MAX_ROWS][768];

static int is_slider(unsigned id) {
    const RigSettingInfo *info = rig_setting_info(id);
    return info && !(info->kind == RIG_KIND_TOGGLE);
}

static void append(char *out, size_t capacity, size_t *used, const char *text) {
    while (*text && *used + 1 < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void append_wide(char *out, size_t capacity, size_t *used, const wchar_t *text) {
    char utf8[128];
    int n = text ? WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL) : 0;
    if (n > 0) append(out, capacity, used, utf8);
}

static void append_number(char *out, size_t capacity, size_t *used, int value) {
    char digits[16];
    unsigned n = 0, v = value < 0 ? (unsigned)-value : (unsigned)value;
    do digits[n++] = (char)('0' + v % 10); while ((v /= 10) && n < sizeof(digits));
    if (value < 0 && *used + 1 < capacity) out[(*used)++] = '-';
    while (n && *used + 1 < capacity) out[(*used)++] = digits[--n];
    out[*used] = 0;
}

/* choices are shown as numbers on a slider; the description names them */
static const wchar_t *choice_name(unsigned kind, unsigned index) {
    switch (kind) {
    case RIG_KIND_TRAFFIC: return rig_traffic_choice(game, index);
    case RIG_KIND_DENSITY: return rig_density_choice(game, index);
    case RIG_KIND_COLOR: return rig_color_choice(game, index);
    }
    return NULL;
}

static void describe(unsigned row) {
    unsigned id = row_ids[row];
    const RigSettingInfo *info = rig_setting_info(id);
    char *out = row_descriptions[row];
    size_t used = 0, capacity = sizeof(row_descriptions[row]);
    out[0] = 0;
    append(out, capacity, &used, rig_setting_description(game, id));
    if (info && rig_kind_is_choice(info->kind)) {
        append(out, capacity, &used, "\n");
        for (int v = info->minimum; v <= info->maximum; ++v) {
            const wchar_t *name = choice_name(info->kind, (unsigned)v);
            if (!name) break;
            append_number(out, capacity, &used, v);
            append(out, capacity, &used, " = ");
            append_wide(out, capacity, &used, name);
            append(out, capacity, &used, v < info->maximum ? ", " : "");
        }
    }
}

static int needs_restart(unsigned id) {
    return id == RIG_KEY_ENABLED || id == RIG_GRIP_STEERING || id == RIG_TRAFFIC_LEVEL ||
           id == RIG_TRAFFIC_ALL_DAY || id == RIG_TRAFFIC_PRESET || id == RIG_GRIP_SPEED_CAMERAS ||
           id == RIG_GRIP_CAR_LABELS || id == RIG_GRIP_EVENT_ICONS;
}

static int row_of(const char *key) {
    for (unsigned i = 0; i < row_count; ++i)
        if (text_equal_ignore_case(row_keys[i], key)) return (int)i;
    return -1;
}

static void sync_rows(void);

static void on_change(void *user, const char *key, int value) {
    (void)user;
    int row = row_of(key);
    if (row < 0 || !started) return;
    unsigned id = row_ids[row];
    rig_setting_set(id, value);
    if (id == RIG_GRIP_REPORT || id == RIG_TRAFFIC_PRESET) sync_rows();
}

static void on_slide(void *user, const char *key, float value) {
    (void)user;
    int row = row_of(key);
    if (row < 0 || !started) return;
    const RigSettingInfo *info = rig_setting_info(row_ids[row]);
    float scale = info && info->kind == RIG_KIND_NUMBER && info->scale > 0 ? info->scale : 1.0f;
    float units = value / scale;
    int rounded = (int)(units < 0 ? units - 0.5f : units + 0.5f);
    rig_setting_set(row_ids[row], rounded);
    if (row_ids[row] == RIG_TRAFFIC_PRESET) sync_rows();
}

/* values known once the features have read their INI files */
static void sync_rows(void) {
    for (unsigned i = 0; i < row_count; ++i) {
        unsigned id = row_ids[i];
        const RigSettingInfo *info = rig_setting_info(id);
        int value = rig_setting_get(id);
        if (is_slider(id)) {
            float scale = info->kind == RIG_KIND_NUMBER && info->scale > 0 ? info->scale : 1.0f;
            api->set_slider(mod, row_keys[i], (float)value * scale);
        } else {
            api->set_setting(mod, row_keys[i], value ? 1 : 0);
        }
    }
}

static void add_rows(void) {
    if (api->api_version < 5 || !api->add_setting) {
        platform_log("platform older than 2.4.0: no settings rows, use the INI files");
        return;
    }
    unsigned count = rig_setting_menu_count();
    for (unsigned position = 0; position < count && row_count < MAX_ROWS; ++position) {
        unsigned id = rig_setting_order(position);
        const RigSettingInfo *info = rig_setting_info(id);
        if (!info) continue;
        unsigned row = row_count;
        row_ids[row] = id;
        row_keys[row][0] = 'G';
        unsigned n = 1, v = id;
        char digits[4];
        unsigned d = 0;
        do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
        while (d) row_keys[row][n++] = digits[--d];
        row_keys[row][n] = 0;
        describe(row);
        FH2Setting setting;
        memory_copy(&setting, &(FH2Setting){0}, sizeof(setting));
        setting.size = sizeof(setting);
        setting.key = row_keys[row];
        setting.label = rig_setting_heading(game, id);
        setting.description = row_descriptions[row];
        setting.needs_restart = needs_restart(id);
        setting.on_change = on_change;
        setting.on_slide = on_slide;
        if (is_slider(id)) {
            float scale = info->kind == RIG_KIND_NUMBER && info->scale > 0 ? info->scale : 1.0f;
            setting.kind = FH2_SETTING_SLIDER;
            setting.min = (float)info->minimum * scale;
            setting.max = (float)info->maximum * scale;
            if (setting.min > setting.max) {
                float t = setting.min;
                setting.min = setting.max;
                setting.max = t;
            }
            setting.step = rig_kind_is_choice(info->kind) ? 1.0f
                           : info->kind == RIG_KIND_PERCENT ? 5.0f
                                                            : 0.0f;
            setting.slider_value = (float)rig_setting_get(id) * scale;
            setting.decimals = info->kind == RIG_KIND_NUMBER ? info->digits : 0;
        } else {
            setting.value = rig_setting_get(id) ? 1 : 0;
        }
        if (api->add_setting(mod, &setting)) ++row_count;
    }
}

/* ------------------------------------------------------------------ Traffic page */

/* Preset and amount as a list of names (MODS > YOUR MODS > Gripline (Installed mods before platform 2.9.0) > Traffic): A picks one.
   The Mods page itself only knows On/Off rows and numbered sliders. */
#define TRAFFIC_PAGE "Traffic"
#define PAGE_ROWS (2 + RIG_DENSITY_COUNT + RIG_TRAFFIC_COUNT)
static char page_labels[PAGE_ROWS][64];
static char page_keys[PAGE_ROWS][8];

static const char *const preset_help_en[RIG_DENSITY_COUNT] = {
    "", "The game's own traffic.",
    "100 traffic cars at any time of day, coming from further away.",
    "Nobody drives (offline).", "Drivatars instead of civilian traffic."};
static const char *const preset_help_de[RIG_DENSITY_COUNT] = {
    "", "Der Verkehr des Spiels.",
    "100 Verkehrsautos zu jeder Uhrzeit, aus größerer Entfernung.",
    "Offline fährt niemand.", "Drivatare statt normalem Verkehr."};

static void build_traffic_page(void) {
    if (!api || api->api_version < 8 || !api->set_page_rows) return;
    int german = game && game_language_is_german(game);
    static FH2PageRow rows[PAGE_ROWS];
    unsigned n = 0;
    int preset = rig_setting_get(RIG_TRAFFIC_PRESET), level = rig_setting_get(RIG_TRAFFIC_LEVEL);
    const char *picked = german ? "Ausgewählt" : "Selected";
    const char *restart = german ? "Wirkt nach einem Neustart von Azimuth." : "Applies after restarting Azimuth.";
    memset(rows, 0, sizeof(rows));
    rows[n].size = sizeof(FH2PageRow);
    rows[n].label = german ? "Voreinstellung" : "Preset";
    rows[n++].style = FH2_ROW_HEADING;
    for (unsigned p = 1; p < RIG_DENSITY_COUNT; ++p, ++n) {
        size_t used = 0;
        page_labels[n][0] = 0;
        append_wide(page_labels[n], sizeof(page_labels[n]), &used, rig_density_choice(game, p));
        page_keys[n][0] = 'P';
        page_keys[n][1] = (char)('0' + p);
        page_keys[n][2] = 0;
        rows[n].size = sizeof(FH2PageRow);
        rows[n].key = page_keys[n];
        rows[n].label = page_labels[n];
        rows[n].value = preset == (int)p ? picked : NULL;
        rows[n].description = german ? preset_help_de[p] : preset_help_en[p];
        rows[n].footer = restart;
    }
    rows[n].size = sizeof(FH2PageRow);
    rows[n].label = german ? "Menge (eigene Werte)" : "Amount (custom)";
    rows[n++].style = FH2_ROW_HEADING;
    for (unsigned v = 0; v < RIG_TRAFFIC_COUNT; ++v, ++n) {
        if (v == 5) { /* was 200 cars, now the same as 100 cars: not listed twice */
            --n;
            continue;
        }
        size_t used = 0;
        page_labels[n][0] = 0;
        append_wide(page_labels[n], sizeof(page_labels[n]), &used, rig_traffic_choice(game, v));
        page_keys[n][0] = 'A';
        page_keys[n][1] = (char)('0' + v);
        page_keys[n][2] = 0;
        rows[n].size = sizeof(FH2PageRow);
        rows[n].key = page_keys[n];
        rows[n].label = page_labels[n];
        rows[n].value = level == (int)v ? picked : NULL;
        rows[n].description = rig_setting_description(game, RIG_TRAFFIC_LEVEL);
        rows[n].footer = restart;
    }
    api->set_page_rows(mod, TRAFFIC_PAGE, rows, n);
}

static void on_page_open(void *user, const char *page_key) {
    (void)user;
    (void)page_key;
    build_traffic_page();
}

static void on_page_select(void *user, const char *page_key, const char *row_key) {
    (void)user;
    (void)page_key;
    if (!started || !row_key || !row_key[0] || row_key[1] < '0' || row_key[1] > '9') return;
    unsigned value = (unsigned)(row_key[1] - '0');
    if (row_key[0] == 'P' && value < RIG_DENSITY_COUNT) rig_setting_set(RIG_TRAFFIC_PRESET, (int)value);
    if (row_key[0] == 'A' && value < RIG_TRAFFIC_COUNT) rig_setting_set(RIG_TRAFFIC_LEVEL, (int)value);
    build_traffic_page();
    sync_rows();
}

static void add_traffic_page(void) {
    if (api->api_version < 8 || !api->add_page || !api->set_page_rows) {
        platform_log("platform older than 2.7.0: traffic preset only in Gripline.ini");
        return;
    }
    static FH2Page page;
    page.size = sizeof(page);
    page.key = TRAFFIC_PAGE;
    page.title = "Gripline Traffic";
    page.label = "Traffic: Preset and Amount";
    page.description = "Original, Much More Traffic, No Traffic or Drivatars Only - by name. "
                       "Press A on one. Applies after restarting Azimuth.";
    page.on_open = on_page_open;
    page.on_select = on_page_select;
    if (!api->add_page(mod, &page)) platform_log("could not add the Traffic page");
}

/* ------------------------------------------------------------------ F8 pages (V7.1) */

/* The F8 menu's pages under MODS > YOUR MODS > Gripline (Installed mods before platform 2.9.0) (key_menu.c keeps rows and actions,
   so both menus stay the same). Titles are fixed at the start (English), rows follow the game language. */
#define MIRROR_PAGE_ROWS 48
static const char *const mirror_keys[KEY_MENU_MIRROR_PAGES] = {"F8Mouse", "F8Walking", "F8Driving", "F8Summon", "F8Cards", "F8Logbook",
                                                                 "F8World" /* Gripline's page only */};
static KeyMenuMirrorRow mirror_rows[MIRROR_PAGE_ROWS];

static int mirror_page_index(const char *page_key) {
    for (unsigned p = 0; p < KEY_MENU_MIRROR_PAGES; ++p) {
        const char *a = mirror_keys[p], *b = page_key;
        while (*a && *a == *b) ++a, ++b;
        if (!*a && !*b) return (int)p;
    }
    return -1;
}

static void build_mirror_page(unsigned page) {
    if (!api || api->api_version < 8 || !api->set_page_rows || page >= KEY_MENU_MIRROR_PAGES) return;
    static FH2PageRow rows[MIRROR_PAGE_ROWS];
    unsigned count = started ? key_menu_mirror_rows(page, mirror_rows, MIRROR_PAGE_ROWS) : 0;
    int german = game && game_language_is_german(game);
    const char *now = german ? "Wird gespeichert - dieselbe Einstellung wie im F8-Menü." : "Saved - the same setting as in the F8 menu.";
    memset(rows, 0, sizeof(rows));
    for (unsigned i = 0; i < count; ++i) {
        rows[i].size = sizeof(FH2PageRow);
        rows[i].key = mirror_rows[i].key[0] ? mirror_rows[i].key : NULL;
        rows[i].label = mirror_rows[i].label;
        rows[i].value = mirror_rows[i].value[0] ? mirror_rows[i].value : NULL;
        rows[i].style = mirror_rows[i].style == KEY_MENU_MIRROR_HEADING ? FH2_ROW_HEADING
                      : mirror_rows[i].style == KEY_MENU_MIRROR_DIM   ? FH2_ROW_DIM
                                                                      : FH2_ROW_NORMAL;
        if (rows[i].key) {
            rows[i].description = key_menu_mirror_description_shown(page);
            rows[i].footer = now;
        }
    }
    if (!count) { /* the game is still loading */
        rows[0].size = sizeof(FH2PageRow);
        rows[0].label = german ? "Gripline startet noch ..." : "Gripline is still starting ...";
        rows[0].style = FH2_ROW_DIM;
        count = 1;
    }
    api->set_page_rows(mod, mirror_keys[page], rows, count);
}

static void on_mirror_open(void *user, const char *page_key) {
    (void)user;
    int page = mirror_page_index(page_key);
    if (page >= 0) build_mirror_page((unsigned)page);
}

static void on_mirror_select(void *user, const char *page_key, const char *row_key) {
    (void)user;
    int page = mirror_page_index(page_key);
    if (page < 0 || !started || !row_key) return;
    key_menu_mirror_choose(row_key);
    build_mirror_page((unsigned)page);
    sync_rows(); /* telemetry and others are rows on the Mods page too */
}

static void add_mirror_pages(void) {
    if (api->api_version < 8 || !api->add_page || !api->set_page_rows) {
        platform_log("platform older than 2.7.0: the F8 pages only in the F8 menu");
        return;
    }
    static FH2Page pages[KEY_MENU_MIRROR_PAGES];
    for (unsigned p = 0; p < KEY_MENU_MIRROR_PAGES; ++p) {
        if (!key_menu_mirror_on_mods(p)) continue; /* its rows are settings rows there already */
        pages[p].size = sizeof(FH2Page);
        pages[p].key = mirror_keys[p];
        pages[p].title = key_menu_mirror_title(p);
        pages[p].label = key_menu_mirror_label(p);
        pages[p].description = key_menu_mirror_description(p);
        pages[p].on_open = on_mirror_open;
        pages[p].on_select = on_mirror_select;
        if (!api->add_page(mod, &pages[p])) platform_log("could not add an F8 page");
    }
}

/* ------------------------------------------------------------------ start */

/* Azimuth 1.0.3 drives the wheel (launcher, data\azimuth.ini [launcher] wheel_rotation); the cockpit
   wheel turns as far as that. 900 when no wheel is set there. */
static int azimuth_rotation(void) {
    static wchar_t path[PATH_CAPACITY];
    if (!path_join(path, directory, L"data\\azimuth.ini")) return 900;
    int rotation = (int)GetPrivateProfileIntW(L"launcher", L"wheel_rotation", 900, path);
    return rotation >= 180 && rotation <= 2520 ? rotation : 900;
}

static FH2Hook *noclip_hook;
static FH2Hook *tile_hooks[6];
static unsigned tile_hook_count;

/* pause_tile.c: the platform's shared hooks (switched on after pause_tile_start kept the originals) */
static void *tile_hook_function(const char *name, void *target, void *detour, void **original) {
    if (api->api_version < 3 || !api->hook_function || !api->enable_hooks || tile_hook_count >= 6) return NULL;
    FH2Hook *hook = api->hook_function(mod, name, target, detour, original);
    if (hook) tile_hooks[tile_hook_count++] = hook;
    return hook;
}

/* V7.2 (platform 2.9.0): the platform checks the code of the pause menu's wiring (game+0xDC6E30) and of the
   functions around it (binding a tile game+0xD9F9F0, the HUD options page game+0xC7AD40 ...) once, as its MODS tab starts, and leaves the tab out when a
   hook is already there. So ALL of the tile's and page's hooks go in only after the platform's own hook is at
   game+0xDC6E30 (its first byte is then a jump), or after 90 s when it never comes (no MODS tab this time). */
static DWORD WINAPI tile_bind_later(LPVOID unused) {
    (void)unused;
    unsigned char first = 0;
    int platform_in = 0;
    for (unsigned waited = 0; waited < 900; ++waited) {
        if (memory_read(game + 0xDC6E30u, &first, 1) && (first == 0xE9 || first == 0xFF)) {
            platform_in = 1;
            break;
        }
        Sleep(100);
    }
    Sleep(500);
    log_append(platform_in ? "PAUSETILE: the platform's MODS tab is in - Gripline tile and page hooks switched on.\r\n"
                           : "PAUSETILE: no platform hook on the pause menu after 90 s - Gripline tile and page hooks switched on.\r\n");
    for (unsigned i = 0; i < tile_hook_count; ++i)
        if (!api->enable_hooks(mod, tile_hooks[i])) log_append("PAUSETILE: the platform could not switch a hook on.\r\n");
    return 0;
}

static void pause_tile_install(void) {
    if (!pause_tile_start(game, media_dir, tile_hook_function)) return; /* nothing switched on: details in the log */
    if (tile_hook_count) {
        HANDLE thread = CreateThread(NULL, 0, tile_bind_later, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
        else tile_bind_later(NULL);
    }
}

static int noclip_switch(int on) {
    return on ? api->enable_hooks(mod, noclip_hook) : api->disable_hooks(mod, noclip_hook);
}

/* Noclip: the platform hooks the car update (inline, so it also runs behind Extra Options' own
   car-update hook); if that place is taken, the ForzaCar vtable slot instead. */
static void noclip_install(void) {
    if (api->api_version < 3 || !api->hook_function || !api->hook_pointer || !api->enable_hooks) {
        log_append("NOCLIP: the platform has no hooks (too old) - noclip off.\r\n");
        return;
    }
    void *original = NULL;
    FH2Hook *hook = api->hook_function(mod, "ForzaCar::Update (Gripline noclip)", game + NOCLIP_CAR_UPDATE_RVA,
                                       (void *)noclip_car_update, &original);
    if (!hook) {
        original = NULL;
        hook = api->hook_pointer(mod, "ForzaCar vtable update (Gripline noclip)",
                                 (void **)(game + NOCLIP_CAR_UPDATE_SLOT_RVA), (void *)noclip_car_update, &original);
    }
    if (!hook || !original) {
        log_append("NOCLIP: could not hook the car update - noclip off.\r\n");
        platform_log("noclip: car update not hooked (see the platform log)");
        return;
    }
    noclip_hook = hook; /* noclip_start switches it on */
    noclip_start(game, original, noclip_switch);
    log_append("NOCLIP: car update hook ready - key Noclip (F6) offline, trip card in the pause menu.\r\n");
}

/* V7.3 live traffic: the platform hooks the Densities*.xml parser, Gripline then changes the loaded tables */
static void traffic_live_install(void) {
    void *target = traffic_live_target(game);
    if (!target) return;
    if (api->api_version < 3 || !api->hook_function || !api->enable_hooks) {
        log_append("TRAFFIC LIVE: the platform has no hooks (too old) - traffic changes apply after a restart.\r\n");
        return;
    }
    void *original = NULL;
    FH2Hook *hook = api->hook_function(mod, "Densities parser (Gripline live traffic)", target,
                                       (void *)traffic_live_parse_detour(), &original);
    if (!hook || !original) {
        log_append("TRAFFIC LIVE: could not hook the density parser - traffic changes apply after a restart.\r\n");
        return;
    }
    traffic_live_start(original);
    if (!api->enable_hooks(mod, hook)) log_append("TRAFFIC LIVE: the platform could not switch the hook on.\r\n");
}

/* "Gripline V7 active" + "F5 keys, F6 noclip, F7 freecam, F8 menu" (short: the card is narrow) */
/* V7.2 calm start: ONE card, "Gripline V7.x active" and the key of the Gripline menu (as bound now; the
   key menu can change it). The other keys are in that menu; partner mods and nitro only go to the log. */
static int start_card(char *out, unsigned size) {
    int de = game && game_language_is_german(game);
    char key[8] = {0};
    const char *key_name = NULL;
    int index = keyboard_binding_find(L"KeyMenu");
    if (index >= 0) {
        unsigned char vk[4] = {0};
        keyboard_binding_vks((unsigned)index, vk);
        if (vk[0]) key_name = keyboard_key_ini_name(vk[0], key);
    }
    unsigned n = 0;
    const char *parts[4] = {"Gripline " GRIPLINE_VERSION, de ? " aktiv" : " active", NULL, NULL};
    if (key_name && *key_name) {
        parts[2] = "\n";
        parts[3] = key_name;
    }
    for (unsigned p = 0; p < 4; ++p)
        for (const char *t = parts[p]; t && *t && n + 1 < size; ++t) out[n++] = *t;
    const char *tail = key_name && *key_name ? (de ? ": Gripline-Menü" : ": Gripline menu") : "";
    for (const char *t = tail; *t && n + 1 < size; ++t) out[n++] = *t;
    out[n] = 0;
    return 1;
}

/* the load guard put the traffic back to Original (traffic_files.c) */
static int load_guard_card(char *out, unsigned size) {
    (void)out;
    (void)size;
    int de = game && game_language_is_german(game);
    notify_show_big(de ? "Verkehr auf Original gesetzt\nLetzter Start hing beim Laden"
                       : "Traffic set to Original\nLast start hung while loading",
                    de ? "Der letzte Spielstart blieb beim Laden hängen. Zur Sicherheit fährt jetzt nur der normale "
                         "Verkehr des Spiels.\n\nMehr Verkehr kannst du im Gripline-Menü wieder einstellen."
                       : "The last game start got stuck while loading. To be safe, only the game's normal traffic "
                         "drives now.\n\nYou can turn more traffic back on in the Gripline menu.",
                    NOTIFY_COLOR_ORANGE, de ? "HINWEIS" : "NOTE", 0);
    return 0;
}

/* Gripline.ini had lines that were skipped (settings.c) */
static int ini_lines_card(char *out, unsigned size) {
    (void)out;
    (void)size;
    int de = game && game_language_is_german(game);
    notify_show_big(de ? "Gripline.ini: eine Zeile übersprungen" : "Gripline.ini: one line skipped",
                    de ? "Eine Zeile in Gripline.ini war unverständlich und wurde ausgelassen. Alle anderen "
                         "Einstellungen gelten.\n\nWelche Zeile: ganz oben in Gripline.log (IGNORED)."
                       : "One line in Gripline.ini could not be read and was left out. All other settings "
                         "are used.\n\nWhich line: at the top of Gripline.log (IGNORED).",
                    NOTIFY_COLOR_ORANGE, de ? "HINWEIS" : "NOTE", 0);
    return 0;
}

static void start_features(void) {
    ModOptions options;
    settings_defaults(&options);
    log_reset();
    int read = settings_read(directory, &options);
    log_start(directory, &options);
    log_set_directory(directory);
    log_append("GRIPLINE: plugin build - FH2 Mod Platform mod next to the official Extra Options.\r\n");
    if (!read && settings_ignored_lines()) {
        log_append("GRIPLINE: Gripline.ini has lines that were not understood; they were skipped, all other "
                   "settings are used (see the IGNORED line at the top).\r\n");
        platform_log("Gripline.ini: some lines skipped, the rest loaded (see Gripline.log)");
        notify_show_deferred_call(ini_lines_card);
    } else if (!read) {
        log_append("GRIPLINE: Gripline.ini could not be read; defaults are used.\r\n");
        platform_log("Gripline.ini could not be read - defaults used (see Gripline.log)");
    }
    if (traffic_load_guard(directory, options.traffic_level)) {
        options.traffic_level = 0;
        options.traffic_all_day = 0;
        notify_show_deferred_call(load_guard_card);
    }
    traffic_tunables_start(game, &options);
    keyboard_pad_game_start(game, directory);
    ini_migrate_log(); /* notes of the fork1-fork3 file migration (were never written to the log) */
    nitro_online_remember(options.nitro_online);
    nitro_link_start(game);
    cockpit_view_start(game);
    walking_start(game);
    summon_start(game); /* V7.1: the car drives to the walker (GPS + the game's AI) */
    summon_set_style(options.summon_style);
    summon_set_sounds(options.summon_sounds);
    walking_set_handheld(options.walk_handheld);
    noclip_set_walk_door(options.walk_door);
    /* V7.2 (Marvin 15.11.2026): the freecam always lets the car drive on; FreecamDrive=0 (the old one) is
       read but no longer used (the old one stays only as the fallback when walking is not ready) */
    noclip_set_freecam_drive(1);
    if (!options.freecam_drive) log_append("FREECAM: FreecamDrive=0 in Gripline.ini is no longer used - the car drives on.\r\n");
    freecam_drive_set_range(options.freecam_range);
    drive_look_set_head_turn(options.head_turn); /* V7.2 head into the turn */
    drive_look_set_head_value(DRIVE_LOOK_HEAD_ANGLE, options.head_angle); /* its own values (15.11.2026) */
    drive_look_set_head_value(DRIVE_LOOK_HEAD_SENSITIVITY, options.head_sensitivity);
    drive_look_set_head_value(DRIVE_LOOK_HEAD_SPEED, options.head_speed);
    drive_look_set_head_value(DRIVE_LOOK_HEAD_RETURN, options.head_return);
    drive_look_set_head_value(DRIVE_LOOK_HEAD_FROM, options.head_from);
    walk_gauge_set_style(options.walk_gauge);
    blinker_set_arrows(options.blinker_arrows); /* V7.2 turn signal arrows + click (blinker.h) */
    blinker_set_sound(options.blinker_sound);
    blinker_set_volume(options.blinker_volume);
    menu_mouse_start(game); /* after walking: both chain the UI frame */
    focus_guard_start();    /* Azimuth may miss "window active" at the start -> pause menu again and again */
    no_damage_bind(game);
    no_damage_set_mode(options.no_damage);
    noclip_install();
    pause_tile_install(); /* V7.2: Gripline tile in the pause menu (CARS tab) */
    gripline_set_output_root(media_dir);
    traffic_files_start(mount_dir, options.traffic_level);
    traffic_live_install(); /* V7.3: after the files (it needs the originals), before the world loads */
    world_files_start(mount_dir, options.speed_cameras, options.car_labels, options.event_icons, options.calm_drivatars);
    cockpit_start(game, mount_dir, options.steering_animation, azimuth_rotation(), 10 /* linear */);
    retire_notifications_switch(&options); /* V7.2: before the cards and rows read it */
    notify_set_enabled(options.notifications);
    notify_set_style(options.card_style);
    notify_set_hidden_kinds(options.cards_hidden);
    cruise_set_options(options.cruise_crash_off, options.cruise_min_speed);
    cruise_set_slow_off(options.cruise_slow_off);
    notify_set_telemetry_look(options.telemetry_pos, options.telemetry_opacity, options.telemetry_size);
    mod_scan_start(directory);
    mod_partners_start(game);
    anna_link_set_game(game);
    version_check_start(game, directory);
    update_check_start(game, options.update_check);
    report_init(directory);
    rig_settings_set_game(game);
    rig_settings_gripline(options.notifications, options.steering_animation, options.cockpit_view_forward,
                          options.cockpit_view_up, options.card_color);
    telemetry_set_show(options.telemetry_show);
    logbook_start(game, directory, mount_dir, options.news_seen);
    records_start(game, directory);
    rig_settings_world(options.speed_cameras, options.car_labels, options.telemetry, options.event_icons,
                       options.calm_drivatars);
    rig_settings_start(directory);
    dashcam_cars_start(directory); /* V7.2 Dashboard Cam per car */
    /* the platform may have loaded Gripline after Azimuth set up gamepads and swap chain */
    if (!keyboard_pad_late()) platform_log("keyboard: not active (off in FH2_Keyboard.ini, Mapper running, FH2 Mouse & Keyboard drives, or no hook)");
    if (!notify_install_late()) platform_log("cards: could not hook the game picture");
    if (notify_kind_on(NOTIFY_KIND_START)) {
        notify_show_deferred_call(start_card); /* V7.2: the only start card; the menu key as bound now */
    }
    InterlockedExchange(&started, 1);
    if (row_count) sync_rows();
    /* Extra Options mounts mods\Gripline\media; the game's steering angle follows once the files are there */
    cockpit_wait_ready(); /* up to 60 s; files that take longer are patched in when they are ready */
    cockpit_patch_when_ready();
    platform_log("Gripline features started");
}

/* Where Extra Options mounts Gripline's media. Extra Options 0.6.0 (FH2MP 1.3.3 and later) reads its packages
   from mods\Forza Horizon 2\<name>; 0.3.x/0.4.x read mods\<name>. New layout when mods\Forza Horizon 2 exists or
   there is no old mods\loadorder.txt; the old layout only while that list is still there and the new folder is not
   (an older Extra Options, or 0.6.0 before its first start moved the packages). */
static int dir_exists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void make_dirs(const wchar_t *path) {
    static wchar_t partial[PATH_CAPACITY]; /* static: a big stack frame needs ___chkstk_ms, not linked */
    lstrcpynW(partial, path, PATH_CAPACITY);
    for (wchar_t *p = partial + 3; *p; ++p) {
        if (*p != L'\\') continue;
        *p = 0;
        CreateDirectoryW(partial, NULL);
        *p = L'\\';
    }
    CreateDirectoryW(partial, NULL);
}

static void choose_media_dir(void) {
    static wchar_t horizon[PATH_CAPACITY], order[PATH_CAPACITY];
    int new_layout = 1;
    if (path_join(horizon, directory, L"mods\\Forza Horizon 2") && path_join(order, directory, L"mods\\loadorder.txt"))
        new_layout = dir_exists(horizon) || GetFileAttributesW(order) == INVALID_FILE_ATTRIBUTES;
    if (path_join(media_dir, directory, new_layout ? L"mods\\Forza Horizon 2\\Gripline\\media" : L"mods\\Gripline\\media")) {
        make_dirs(media_dir);
        platform_log(new_layout ? "media folder: mods\\Forza Horizon 2\\Gripline (Extra Options 0.6.0 layout)"
                                : "media folder: mods\\Gripline (older Extra Options layout)");
    }
}

static DWORD WINAPI start_thread(LPVOID unused) {
    (void)unused;
    game = api->api_version >= 3 && api->wait_for_code ? (unsigned char *)api->wait_for_code(120000)
                              : (unsigned char *)api->wait_for_game(120000);
    if (!game) {
        platform_log("the game was not found within 2 minutes - Gripline stays off");
        return 0;
    }
    start_features();
    return 0;
}

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi *platform) {
    if (!platform || mod || !platform->register_mod || !platform->wait_for_game) return 1;
    api = platform;
    static FH2ModInfo info;
    info.size = sizeof(info);
    info.name = "Gripline";
    info.version = GRIPLINE_VERSION;
    info.github_repo = "Flenkii/Gripline";
    info.platform_version = "2.7.4";
    info.check_updates = 0;
    info.description = "Immersion for Forza Horizon 2: walk on foot, dashboard cam and 1:1 wheel, keyboard and mouse, "
                       "live telemetry, world options. Runs next to Extra Options and FH2MP. "
                       "Settings here and on the pages below (the same as the F8 menu in the game, except key bindings).";
    mod = api->register_mod(&info);
    if (!mod) return 1;
    if (!find_folders()) {
        platform_log("the platform did not name the Azimuth folder - Gripline stays off");
        return 0;
    }
    if (!mount_dir[0]) platform_log("the platform did not find the game folder - steering animation and traffic files off");
    choose_media_dir();
    move_gripline_sections();
    log_set_directory(directory);
    /* V7.2: before the keyboard is armed - another mod may already drive with the keyboard */
    mod_partners_detect(directory, api->api_version >= 2 && api->mod_version ? api->mod_version : NULL);
    unsigned char *host = (unsigned char *)GetModuleHandleW(NULL);
    if (host) {
        int keys = keyboard_pad_arm(host, directory); /* before Azimuth sets up its gamepads */
        notify_prefer_window();                       /* the window has the walking gauge and the fade */
        int cards = notify_install(host);             /* game picture cards */
        platform_log(keys ? "early keyboard hook: yes" : "early keyboard hook: no");
        platform_log(cards ? "early card hook: yes" : "early card hook: no");
        /* V7.2: the pause menu file, before the game first opens it */
        platform_log(pause_tile_arm(host, mount_dir) ? "Gripline tile (CARS tab): menu file watched" : "Gripline tile (CARS tab): off");
    }
    /* V7.2: Gripline's list in FH2 ANNA's menu (FH2ANNA.dll started before Gripline.dll, its interface is there) */
    anna_link_add(api->api_version >= 2 && api->get_interface ? api->get_interface("FH2ANNA", 4) : NULL);
    add_rows();
    add_traffic_page();
    add_mirror_pages();
    HANDLE thread = CreateThread(NULL, 0, start_thread, NULL, 0, NULL);
    if (!thread) {
        platform_log("could not start - no thread");
        return 0;
    }
    CloseHandle(thread);
    api->log(mod, "Gripline " GRIPLINE_VERSION " registered (plugin build, next to Extra Options)");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        rig_settings_flush();
    }
    return TRUE;
}
