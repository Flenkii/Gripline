#include "features/ini_migrate.h"
#include "runtime.h"
#include "features.h"
#include "features/keyboard_pad.h"
#include "features/nitro_link.h"
#include "features/notify.h"
#include "features/cockpit.h"
#include "features/mod_scan.h"
#include "features/report.h"
#include "features/rig_settings.h"
#include "features/video_settings.h"
#include "logging.h"
#include "patch_engine.h"
#include "settings.h"
#include "support.h"

#ifdef FH2_AZIMUTH
#include "adapters/azimuth_host.h"
#endif

static void log_enabled_features(const wchar_t *settings_directory, const ModOptions *options) {
    log_set_directory(settings_directory);
    if (options->garage_from_pause) {
        log_append("GARAGE: XML parse hook installed; awaiting menu-flow resources.\r\n");
    }
    if (options->skip_ineligible_car_dialogue) {
        log_append("FRONTEND: race dialogue skip enabled; awaiting race activation "
                   "flow.\r\n");
    }
    if (options->skip_car_meet_on_startup) {
        log_append("STARTUP: mainland car-meet boot skipped; Storm Island retains "
                   "normal boot.\r\n");
    }
    if (options->spawn_at_festival && options->skip_car_meet_on_startup) {
        log_append("STARTUP: mainland festival spawn enabled; Storm Island retains "
                   "normal boot.\r\n");
    }
    if (options->media_overrides) {
        log_append("FILES: native override hook installed; folders are mounted on "
                   "the first "
                   "file lookup outside DLL initialization.\r\n");
    }
    if (options->autodrive) {
        log_append("AUTODRIVE: experimental F2 GPS/AI hooks installed; cinematic "
                   "delay is 3 "
                   "seconds.\r\n");
    }
    if (options->all_rims_category) {
        log_append("UPGRADES: virtual All Rims category enabled; database file "
                   "unchanged.\r\n");
    }
    log_append("VEHICLE: right-stick press toggles headlights; stationary "
               "2-second hold toggles "
               "supported roofs.\r\n");
    log_append("SETTINGS: Video and HUD menu hooks installed; awaiting menu "
               "resources.\r\n");
    if (options->custom_gamerpic) {
        log_append("GAMERPIC: converter hook installed; Gamerpic.png is read from "
                   "the settings "
                   "folder on the first local-player request.\r\n");
    }
}

int runtime_initialize(unsigned char *game_image, const wchar_t *executable_path,
                       const wchar_t *settings_directory) {
    ModOptions options;
    settings_defaults(&options);
    gamerpic_set_directory(settings_directory);
    log_reset();
    ini_migrate(settings_directory); /* German files of fork1-fork3 -> English ones */
    int initialized = settings_read(settings_directory, &options);
    if (initialized) {
        video_settings_load(settings_directory);
        video_settings_seed(&options);
        video_settings_apply(&options);
    }
    if (initialized && options.enabled) {
        initialized = verify_game_file(executable_path);
    }
    media_overrides_configure(options.media_overrides, options.steering_animation);
    if (initialized) {
        initialized = patch_engine_apply(game_image, &options);
    }
    log_start(settings_directory, &options);
    log_set_directory(settings_directory);
    ini_migrate_log();
    log_reset();
#ifdef FH2_AZIMUTH
    if (initialized && options.enabled) {
        log_set_directory(settings_directory);
        azimuth_apply_save_compatibility(options.disable_azimuth_save_checksums);
        azimuth_apply_loading_guard(game_image);
    }
#endif
    if (initialized && options.enabled) {
        log_set_directory(settings_directory);
        traffic_tunables_start(game_image, &options);
        weather_start(game_image, &options);
    }
    if (initialized && options.enabled) {
        wheel_bridge_start(game_image, executable_path, settings_directory);
        keyboard_pad_game_start(game_image, settings_directory);
        nitro_link_start(game_image);
        if (options.steering_animation) {
            static wchar_t mount_dir[PATH_CAPACITY];
            size_t n = 0, cut = 0;
            for (; executable_path[n] && n + 1 < PATH_CAPACITY; ++n) {
                mount_dir[n] = executable_path[n];
                if (executable_path[n] == L'\\' || executable_path[n] == L'/') cut = n;
            }
            mount_dir[cut] = 0; /* the Mount folder holds forza_durango_release_final.exe */
            cockpit_start(game_image, mount_dir, 1, wheel_bridge_setting(RIG_WHEEL_ROTATION),
                          wheel_bridge_setting(RIG_WHEEL_CURVE));
        }
        notify_set_enabled(options.notifications);
        mod_scan_start(settings_directory);
        report_init(settings_directory);
        rig_settings_gripline(options.notifications, options.steering_animation, options.cockpit_view_forward,
                              options.cockpit_view_up);
        rig_settings_start(settings_directory);
    }
    if (initialized && options.enabled) {
        log_enabled_features(settings_directory, &options);
    }
    return initialized;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestInitialize(void *base, const wchar_t *path,
                                            const wchar_t *directory) {
    return runtime_initialize(base, path, directory);
}

__declspec(dllexport) int Fh2TestVerifyFile(const wchar_t *path) {
    return verify_game_file(path);
}

__declspec(dllexport) int Fh2TestReadOptions(const wchar_t *directory, ModOptions *options) {
    return settings_read(directory, options);
}
#endif
