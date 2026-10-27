/* fh2-mods\Gripline.dll - Gripline as an FH2 Mod Platform mod (plugin build, FH2_GRIPLINE_PLUGIN).

   Since FH2MP 1.1.1 the launcher only starts the game with the official Extra Options 0.3.2
   (winmm.dll next to azimuth.exe, checked by its SHA-256). Gripline therefore no longer replaces
   Extra Options: it runs next to it as a platform mod.
   - The platform loads this DLL when Azimuth starts, so the keyboard (Azimuth's gamepad import) and
     the picture cards (DXGI import) are taken over before Azimuth sets them up.
   - Once the game is mapped, the Gripline features start on their own thread, as in runtime.c.
   - Settings are Gripline.ini ([Traffic], [Gripline]); Extra Options refuses unknown keys in its
     own INI. FH2_Wheel.ini and FH2_Keyboard.ini stay as they are.
   - Weather and time of day need a tick on the game thread: Extra Options' plugin
     mods\Gripline\plugins\GriplineTick.dll calls GriplineVehicleUpdate for the local car.
   - Steering animation and More Traffic files go to mods\Gripline\media, which Extra Options mounts
     over game:\media like any mod's media folder.
   - The in-game settings are rows on the game's Mods page (Pause > Settings > MODS). */
#include "extra_options.h"
#include "../third_party/fh2mod/fh2mod.h"
#include "logging.h"
#include "settings.h"
#include "support.h"
#include "features.h"
#include "features/autodrive_game.h"
#include "features/cockpit.h"
#include "features/cockpit_view.h"
#include "features/keyboard_pad.h"
#include "features/mod_scan.h"
#include "features/nitro_link.h"
#include "features/notify.h"
#include "features/report.h"
#include "features/rig_settings.h"
#include "features/traffic_files.h"
#include "features/weather.h"
#include "features/time_of_day.h"

void wheel_bridge_shutdown(void);
void keyboard_pad_game_start(unsigned char *game_image, const wchar_t *settings_directory);
void report_init(const wchar_t *settings_directory);

static const FH2ModApi *api;
static FH2Mod *mod;
static wchar_t directory[PATH_CAPACITY];   /* the Azimuth folder (next to azimuth.exe) */
static wchar_t game_path[PATH_CAPACITY];   /* ...\Mount\forza_durango_release_final.exe */
static wchar_t mount_dir[PATH_CAPACITY];   /* ...\Mount */
static wchar_t media_dir[PATH_CAPACITY];   /* Azimuth\mods\Gripline\media */
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

/* Every start, before Extra Options reads its INI (it does so when the game is mapped, the platform
   loads this DLL earlier): the [Traffic] and [Gripline] sections move out of FH2_ExtraOptions.ini,
   because Extra Options 0.3.2 refuses keys it does not know. They go to Gripline.ini when that does
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
            static const char defaults[] = "[Traffic]\r\nTrafficLevel=4\r\nRushHourAllDay=1\r\nWeather=0\r\nTimeOfDay=0\r\n"
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
    case RIG_KIND_PROFILE: return rig_profile_choice(game, index);
    case RIG_KIND_FUNCTION: return rig_function_choice(game, index);
    case RIG_KIND_DENSITY: return rig_density_choice(game, index);
    case RIG_KIND_WEATHER: return rig_weather_choice(game, index);
    case RIG_KIND_FFB_MODE: return rig_ffb_mode_choice(game, index);
    case RIG_KIND_TIME: return rig_time_choice(game, index);
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
    return id == RIG_WHEEL_ENABLED || id == RIG_KEY_ENABLED || id == RIG_GRIP_STEERING ||
           id == RIG_TRAFFIC_LEVEL || id == RIG_TRAFFIC_ALL_DAY || id == RIG_TRAFFIC_PRESET ||
           id == RIG_WHEEL_PROFILE;
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
    if (id == RIG_GRIP_REPORT || id == RIG_WHEEL_PROFILE || id == RIG_TRAFFIC_PRESET) sync_rows();
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
    if (row_ids[row] == RIG_WHEEL_PROFILE || row_ids[row] == RIG_TRAFFIC_PRESET) sync_rows();
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

/* ------------------------------------------------------------------ start */

static void start_features(void) {
    ModOptions options;
    settings_defaults(&options);
    log_reset();
    int read = settings_read(directory, &options);
    log_start(directory, &options);
    log_set_directory(directory);
    log_append("GRIPLINE: plugin build - FH2 Mod Platform mod next to the official Extra Options.\r\n");
    if (!read) {
        log_append("GRIPLINE: Gripline.ini could not be read; defaults are used.\r\n");
        platform_log("Gripline.ini could not be read - defaults used (see Gripline.log)");
    }
    autodrive_game_bind(game);
    traffic_tunables_start(game, &options);
    weather_start(game, &options);
    time_of_day_start(game, &options);
    wheel_bridge_start(game, game_path, directory);
    keyboard_pad_game_start(game, directory);
    nitro_link_start(game);
    cockpit_view_start(game);
    gripline_set_output_root(media_dir);
    traffic_files_start(mount_dir, options.traffic_level);
    cockpit_start(game, mount_dir, options.steering_animation, wheel_bridge_setting(RIG_WHEEL_ROTATION),
                  wheel_bridge_setting(RIG_WHEEL_CURVE));
    notify_set_enabled(options.notifications);
    mod_scan_start(directory);
    report_init(directory);
    rig_settings_gripline(options.notifications, options.steering_animation, options.cockpit_view_forward,
                          options.cockpit_view_up);
    rig_settings_start(directory);
    InterlockedExchange(&started, 1);
    if (row_count) sync_rows();
    /* Extra Options mounts mods\Gripline\media; the game's steering angle follows once the files are there */
    if (cockpit_wait_ready()) cockpit_patch_game();
    platform_log("Gripline features started");
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

/* Extra Options plugin GriplineTick.dll: the local car's update on the game thread */
__declspec(dllexport) void GriplineVehicleUpdate(void *car, unsigned input_enabled) {
    (void)car;
    (void)input_enabled;
    if (!started) return;
    weather_tick();
    time_of_day_tick();
}

/* creator mode hides Gripline's own cards here; the game's HUD stays (Extra Options owns those hooks) */
void autodrive_ui_set_creator(int hidden) {
    (void)hidden;
}

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi *platform) {
    if (!platform || mod || !platform->register_mod || !platform->wait_for_game) return 1;
    api = platform;
    static FH2ModInfo info;
    info.size = sizeof(info);
    info.name = "Gripline";
    info.version = GRIPLINE_VERSION;
    info.github_repo = "Flenkii/Gripline";
    info.platform_version = "2.7.1";
    info.check_updates = 0;
    info.description = "Wheel (Logitech G920), keyboard, traffic, weather and time of day for Forza Horizon 2. "
                       "Settings: Pause > Settings > MODS.";
    mod = api->register_mod(&info);
    if (!mod) return 1;
    if (!find_folders()) {
        platform_log("the platform did not name the Azimuth folder - Gripline stays off");
        return 0;
    }
    if (!mount_dir[0]) platform_log("the platform did not find the game folder - steering animation and traffic files off");
    if (path_join(media_dir, directory, L"mods\\Gripline\\media")) CreateDirectoryW(media_dir, NULL);
    move_gripline_sections();
    log_set_directory(directory);
    unsigned char *host = (unsigned char *)GetModuleHandleW(NULL);
    if (host) {
        keyboard_pad_arm(host, directory); /* before Azimuth sets up its gamepads */
        notify_install(host);              /* game picture cards */
    }
    add_rows();
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
        wheel_bridge_shutdown();
        rig_settings_flush();
    }
    return TRUE;
}
