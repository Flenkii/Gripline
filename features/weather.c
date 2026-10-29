/* Fixed weather from the "Lenkrad & Verkehr" menu.
 *
 * The game has a weather control (stateflow behaviour "SetWeatherControl"): car meets, the
 * island introduction and races use it to pin a preset, then release it again. This file uses
 * the same two native calls on the game's WeatherManager:
 *   game+0x1981860  SetControl(manager, const WeatherControl *)
 *   game+0x1981c80  ReleaseControl(manager)
 * The manager is found the way the behaviour finds it: *(game+0x34aba60) +0x48 +0x98 +0x488,
 * checked against the WeatherManager vtable. Preset names are looked up in the manager's own
 * preset list, so only presets that exist on the current map (mainland / Storm Island) are used.
 *
 * Everything runs on the game thread (weather_tick is called from the local car's update).
 * The menu thread only stores the wanted value.
 *
 * Ownership: a menu change always applies. Afterwards the choice is only re-applied while the
 * control is released (free roam), so a race or car meet that pins its own weather keeps it;
 * after that event releases the control, the menu choice comes back. "Original" releases the
 * control only if it still holds our values. */
#include "weather.h"
#include "features.h"
#include "logging.h"
#include "support.h"

#define RVA_MANAGER_ROOT 0x34aba60u
#define RVA_MANAGER_VTABLE 0x2f64920u
#define RVA_SET_CONTROL 0x1981860u
#define RVA_RELEASE_CONTROL 0x1981c80u
#define CHECK_INTERVAL_MS 1000u

/* Layout copied by game+0x1981860 (0x3c bytes, stored at manager+0xe0). */
typedef struct {
    unsigned char has_preset, pad0[3];
    int preset;
    unsigned char has_severity, pad1[3];
    int severity;
    unsigned char has_atmosphere, pad2[3];
    float atmosphere;
    unsigned char has_environment, pad3[3];
    float environment;
    unsigned char has_precipitation, pad4[3];
    float precipitation;
    unsigned char has_extra, pad5[3];
    float extra;
    unsigned char flag30, flag31, flag32, pad6;
    float blend;
    unsigned char flag38, pad7[3];
} WeatherControl;
_Static_assert(sizeof(WeatherControl) == 0x3c, "weather control layout");

typedef void (*SetControl)(void *, const WeatherControl *);
typedef void (*ReleaseControl)(void *);

typedef struct {
    const char *preset, *fallback;
    float precipitation, environment;
    const char *name;
} WeatherChoice;

static const WeatherChoice choices[WEATHER_COUNT] = {
    [WEATHER_ORIGINAL] = {NULL, NULL, 0, 0, "Original"},
    [WEATHER_SUNNY] = {"Clear", NULL, 0.0f, 0.0f, "Sonnig"},
    [WEATHER_CLOUDY] = {"CloudyA", NULL, 0.0f, 0.0f, "Bewoelkt"},
    [WEATHER_THREATENING] = {"ThreateningStormACloudyA", NULL, 0.0f, 0.0f, "Gewitterwolken"},
    [WEATHER_RAIN] = {"StormBCloudyA", NULL, 1.0f, 1.0f, "Regen"},
    [WEATHER_FOG] = {"CloudyFog", "CloudyA", 0.0f, 0.0f, "Nebel"},
};

static unsigned char *game;
static volatile LONG wanted, generation;
static volatile LONG yielding; /* 1 = Extra Options' Weather Control sets the weather */
static LONG seen_generation = -1;
static int loaded;

/* state owned by the game thread */
static void *applied_manager;
static int applied_choice, applied_preset, applied_map = -1, waiting_logged;
static int failed_choice = -1, failed_map = -2;
static ULONGLONG checked_at;

int weather_setting(void) {
    return (int)wanted;
}

void weather_set_yield(int yield) {
    if (InterlockedExchange(&yielding, yield != 0) != (yield != 0)) {
        InterlockedIncrement(&generation);
    }
}

void weather_set_setting(int value) {
    if (!loaded || value < 0 || value >= WEATHER_COUNT) {
        return;
    }
    InterlockedExchange(&wanted, value);
    InterlockedIncrement(&generation);
}

void weather_start(unsigned char *game_image, const ModOptions *options) {
    game = game_image;
    int value = options ? options->weather : 0;
    wanted = value >= 0 && value < WEATHER_COUNT ? value : 0;
    loaded = game != NULL;
}

static void log_weather(const char *first, const char *second) {
    char line[200];
    size_t n = 0;
    for (const char *p = "WEATHER: "; *p; ++p) line[n++] = *p;
    for (const char *p = first; *p && n < sizeof(line) - 4; ++p) line[n++] = *p;
    for (const char *p = second; p && *p && n < sizeof(line) - 4; ++p) line[n++] = *p;
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    log_append(line);
}

static void *read_pointer(const void *base, unsigned offset) {
    void *value = NULL;
    if (!base || !memory_read_pointer((const unsigned char *)base + offset, &value)) {
        return NULL;
    }
    return value;
}

static void *find_manager(void) {
    void *root = read_pointer(game, RVA_MANAGER_ROOT);
    void *world = read_pointer(root, 0x48);
    void *scene = read_pointer(world, 0x98);
    void *manager = read_pointer(scene, 0x488);
    void *vtable = read_pointer(manager, 0);
    return vtable == game + RVA_MANAGER_VTABLE ? manager : NULL;
}

/* MSVC std::string at address: compares with a C string. */
static int string_equals(const unsigned char *address, const char *text) {
    size_t size = 0, capacity = 0;
    if (!memory_read(address + 0x10, &size, sizeof(size)) ||
        !memory_read(address + 0x18, &capacity, sizeof(capacity)) || size > 64) {
        return 0;
    }
    char buffer[65];
    const void *chars = address;
    if (capacity >= 16 && !memory_read_pointer(address, (void **)&chars)) {
        return 0;
    }
    if (!memory_read(chars, buffer, size)) {
        return 0;
    }
    size_t length = 0;
    while (text[length]) ++length;
    if (length != size) {
        return 0;
    }
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] != text[i]) return 0;
    }
    return 1;
}

/* The presets object (manager+0x158) starts with a std::vector of 0xc8-byte presets in file
   order: +0x00 name (std::string), +0x20 cloud scape, +0x40 lighting, +0x60 index, +0x64
   severity (checked against a crash dump of the real game). Returns -1 if the name is unknown. */
static int find_preset(void *manager, const char *name) {
    unsigned char *presets = read_pointer(manager, 0x158);
    unsigned char *begin = read_pointer(presets, 0x00), *end = read_pointer(presets, 0x08);
    if (!begin || end <= begin || (size_t)(end - begin) % 0xc8 || (size_t)(end - begin) > 0xc8 * 64) {
        return -1;
    }
    for (unsigned char *entry = begin; entry < end; entry += 0xc8) {
        if (string_equals(entry, name)) {
            int index = -1;
            if (!memory_read(entry + 0x60, &index, sizeof(index)) || index < 0 ||
                index != (int)((entry - begin) / 0xc8)) {
                return -1;
            }
            return index;
        }
    }
    return -1;
}

static int build_control(void *manager, int choice, WeatherControl *control) {
    const WeatherChoice *c = &choices[choice];
    int preset = find_preset(manager, c->preset);
    if (preset < 0 && c->fallback) {
        preset = find_preset(manager, c->fallback);
    }
    if (preset < 0) {
        return 0;
    }
    *control = (WeatherControl){0};
    control->has_preset = 1;
    control->preset = preset;
    control->has_environment = 1;
    control->environment = c->environment;
    control->has_precipitation = 1;
    control->precipitation = c->precipitation;
    control->flag32 = 1; /* as the stateflow behaviour fills it */
    control->blend = 0.999f;
    control->flag38 = 1;
    return 1;
}

static int read_control(void *manager, WeatherControl *control) {
    return memory_read((unsigned char *)manager + 0xe0, control, sizeof(*control));
}

static int control_released(const WeatherControl *c) {
    return !c->has_preset && !c->has_severity && !c->has_atmosphere && !c->has_environment &&
           !c->has_precipitation;
}

static int control_is_ours(void *manager, const WeatherControl *c) {
    return applied_choice && manager == applied_manager && c->has_preset &&
           c->preset == applied_preset && c->has_precipitation &&
           c->precipitation == choices[applied_choice].precipitation;
}

static int current_map(void *manager) {
    int map = -1;
    memory_read((unsigned char *)manager + 0x11c, &map, sizeof(map));
    return map;
}

static int apply(void *manager, int choice) {
    WeatherControl control;
    int map = current_map(manager);
    if (choice == failed_choice && map == failed_map) {
        return 0; /* already reported for this map */
    }
    if (!build_control(manager, choice, &control)) {
        failed_choice = choice;
        failed_map = map;
        log_weather("preset not found on this map, skipped: ", choices[choice].name);
        return 0;
    }
    __try {
        ((SetControl)(game + RVA_SET_CONTROL))(manager, &control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        failed_choice = choice;
        failed_map = map;
        log_weather("ERROR: exception while setting the weather: ", choices[choice].name);
        return 0;
    }
    applied_manager = manager;
    applied_choice = choice;
    applied_preset = control.preset;
    applied_map = map;
    waiting_logged = 0;
    failed_choice = -1;
    return 1;
}

static void release(void *manager) {
    __try {
        ((ReleaseControl)(game + RVA_RELEASE_CONTROL))(manager);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log_weather("ERROR: exception while releasing the weather.", "");
    }
}

void weather_tick(void) {
    if (!loaded) {
        return;
    }
    LONG current_generation = generation;
    int changed = current_generation != seen_generation;
    ULONGLONG now = GetTickCount64();
    if (!changed && now - checked_at < CHECK_INTERVAL_MS) {
        return;
    }
    void *manager = find_manager();
    if (!manager) {
        return; /* menus, loading screens: try again later */
    }
    checked_at = now;
    seen_generation = current_generation;
    int yield = (int)yielding;
    int choice = yield ? WEATHER_ORIGINAL : (int)wanted;
    static int yield_logged;
    if (yield != yield_logged) {
        if (yield && wanted != WEATHER_ORIGINAL) {
            log_weather("Extra Options' Weather Control sets the weather - Gripline's weather waits.", "");
        } else if (!yield && yield_logged) {
            log_weather("Weather Control is on Dynamic again - Gripline's weather applies.", "");
        }
        yield_logged = yield;
    }
    WeatherControl control;
    if (!read_control(manager, &control)) {
        return;
    }
    int ours = control_is_ours(manager, &control);
    if (choice == WEATHER_ORIGINAL) {
        if (ours) {
            release(manager);
            log_weather("back to the game's normal weather.", "");
        }
        applied_choice = 0;
        applied_manager = NULL;
        return;
    }
    if (!changed) {
        /* keep the choice: re-apply after the game released the control, or on a new map */
        if (ours && current_map(manager) == applied_map) {
            return;
        }
        if (!control_released(&control) && !ours) {
            if (!waiting_logged) {
                waiting_logged = 1;
                log_weather("the game controls the weather right now (event); waiting.", "");
            }
            return;
        }
    }
    if (apply(manager, choice)) {
        log_weather("set to ", choices[choice].name);
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestWeatherBuild(void *manager, int choice, void *out) {
    if (choice <= 0 || choice >= WEATHER_COUNT) {
        return 0;
    }
    return build_control(manager, choice, (WeatherControl *)out);
}
__declspec(dllexport) void *Fh2TestWeatherManager(void) {
    return find_manager();
}
__declspec(dllexport) void Fh2TestWeatherStart(unsigned char *image, int value) {
    ModOptions options = {0};
    options.weather = value;
    weather_start(image, &options);
    seen_generation = -1;
    applied_manager = NULL;
    applied_choice = 0;
    applied_map = -1;
    failed_choice = -1;
    checked_at = 0;
}
__declspec(dllexport) void Fh2TestWeatherSet(int value) {
    weather_set_setting(value);
}
__declspec(dllexport) void Fh2TestWeatherTick(void) {
    weather_tick();
}
#endif
