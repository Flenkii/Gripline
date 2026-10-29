/* Fixed time of day from the Gripline menu.
 *
 * The game keeps one time-of-day clock (0x410 bytes, built by game+0x1975660). Race and story
 * scripts drive it with the stateflow behaviours SetTimeOfDay and StopTimeOfDay; this file does
 * the same two things:
 *   game+0x197aec0  SetTime(clock, float seconds, int remap)   (SetTimeOfDay, remap = 1)
 *   clock+0x24      stopped flag                               (StopTimeOfDay writes it)
 * The clock is found the way the behaviours find it: *(game+0x34aba60) +0x48 +0x98 +0x480.
 * Fields used (from the constructor and the update game+0x197b170):
 *   +0x10 float time in seconds (0..86400)   +0x18 float clock speed (48 = 1 s real : 48 s game)
 *   +0x24 stopped   +0x25 time fixed by the game's config   +0x27 stop fixed by the config
 *
 * Everything runs on the game thread (time_of_day_tick is called from the local car's update).
 * The menu thread only stores the wanted value.
 *
 * Ownership: the choice only applies in free roam offline. In races, car meets and other
 * events, and in an online session (FH2MP: the time comes from the session), the clock is
 * handed back to the game as it was (running, normal speed); in free roam the choice returns. */
#include "time_of_day.h"
#include "autodrive_game.h"
#include "features.h"
#include "logging.h"
#include "support.h"

#define RVA_WORLD_ROOT 0x34aba60u
#define RVA_SET_TIME 0x197aec0u
/* FH2's network connection object: state +0x10 is 1 while a network session is up
   (same test as nitro_link.c). */
#define RVA_NET_CONNECTION 0x36e24f8u
#define CHECK_INTERVAL_MS 1000u
#define DAY_SECONDS 86400.0f
#define SLOW_FACTOR 4.0f

typedef void (*SetTime)(void *, float, int);

typedef struct {
    float seconds; /* time the race scripts would use (hours * 3600 + minutes * 60) */
    const char *name;
} TimeChoice;

/* Times like the game's own events: dawn races start around 4:45, sunset races 19:20-19:40,
   night races 20:23 and later (AnthemEventRaces.TimeOfDayStart). */
static const TimeChoice choices[TIME_OF_DAY_COUNT] = {
    [TIME_OF_DAY_ORIGINAL] = {0, "Original"},
    [TIME_OF_DAY_MORNING] = {8 * 3600.0f, "morning 08:00"},
    [TIME_OF_DAY_MIDDAY] = {13 * 3600.0f, "midday 13:00"},
    [TIME_OF_DAY_SUNSET] = {19 * 3600.0f + 35 * 60.0f, "sunset 19:35"},
    [TIME_OF_DAY_NIGHT] = {22 * 3600.0f + 30 * 60.0f, "night 22:30"},
    [TIME_OF_DAY_SLOW] = {0, "slow day (4x slower)"},
};

typedef struct {
    float time, speed;
    unsigned char stopped, time_locked, stop_locked;
} ClockState;

static unsigned char *game;
static volatile LONG wanted, generation;
static volatile LONG yielding; /* 1 = Extra Options' Weather Control locks the clock */
static LONG seen_generation = -1;
static int loaded;
static int (*in_free_roam)(void) = autodrive_game_is_freeroam;

/* state owned by the game thread */
static unsigned char *held_clock;   /* clock we changed, NULL = the game's own */
static int held_choice;
static float held_time;             /* time read back after our SetTime (remapped) */
static float held_speed, game_speed; /* our slow speed and the speed it replaced */
static int held_stop, held_slow, online_logged = -1, locked_logged;
static ULONGLONG checked_at;

int time_of_day_setting(void) {
    return (int)wanted;
}

void time_of_day_set_yield(int yield) {
    if (InterlockedExchange(&yielding, yield != 0) != (yield != 0)) {
        InterlockedIncrement(&generation);
    }
}

void time_of_day_set_setting(int value) {
    if (!loaded || value < 0 || value >= TIME_OF_DAY_COUNT) {
        return;
    }
    InterlockedExchange(&wanted, value);
    InterlockedIncrement(&generation);
}

void time_of_day_start(unsigned char *game_image, const ModOptions *options) {
    game = game_image;
    int value = options ? options->time_of_day : 0;
    wanted = value >= 0 && value < TIME_OF_DAY_COUNT ? value : 0;
    loaded = game != NULL;
}

static void log_time(const char *first, const char *second) {
    char line[200];
    size_t n = 0;
    for (const char *p = "TIME OF DAY: "; *p; ++p) line[n++] = *p;
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

static int is_flag(unsigned char value) {
    return value <= 1;
}

/* Reads the clock and checks that it looks like one (no vtable to compare against). */
static int read_clock(const unsigned char *clock, ClockState *state) {
    unsigned char flags[6];
    if (!clock || !memory_read(clock + 0x10, &state->time, 4) ||
        !memory_read(clock + 0x18, &state->speed, 4) || !memory_read(clock + 0x24, flags, 6)) {
        return 0;
    }
    for (int i = 0; i < 6; ++i) {
        if (!is_flag(flags[i])) return 0;
    }
    if (!(state->time >= 0.0f && state->time <= DAY_SECONDS) ||
        !(state->speed >= 0.0f && state->speed <= 100000.0f)) {
        return 0;
    }
    state->stopped = flags[0];
    state->time_locked = flags[1];
    state->stop_locked = flags[3];
    return 1;
}

static unsigned char *find_clock(void) {
    void *root = read_pointer(game, RVA_WORLD_ROOT);
    void *world = read_pointer(root, 0x48);
    void *scene = read_pointer(world, 0x98);
    return read_pointer(scene, 0x480);
}

static int session_online(void) {
    void *connection = read_pointer(game, RVA_NET_CONNECTION);
    int state = 0;
    return connection && memory_read((unsigned char *)connection + 0x10, &state, sizeof(state)) &&
           state == 1;
}

static void write_byte(unsigned char *address, unsigned char value) {
    __try {
        *(volatile unsigned char *)address = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void write_float(unsigned char *address, float value) {
    __try {
        *(volatile float *)address = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* Gives the clock back: running again and at the game's speed, but only what is still ours. */
static void release(const char *why) {
    unsigned char *clock = held_clock;
    ClockState state;
    if (clock && find_clock() == clock && read_clock(clock, &state)) {
        if (held_stop && state.stopped && !state.stop_locked) {
            write_byte(clock + 0x24, 0);
        }
        if (held_slow && state.speed == held_speed) {
            write_float(clock + 0x18, game_speed);
        }
        if (why) log_time(why, "");
    }
    held_clock = NULL;
    held_choice = 0;
    held_stop = held_slow = 0;
}

static int hold_fixed(unsigned char *clock, int choice, const ClockState *state) {
    if (held_clock == clock && held_choice == choice && held_stop && state->stopped &&
        state->time == held_time) {
        return 0; /* still ours */
    }
    if (state->time_locked || state->stop_locked) {
        if (!locked_logged) {
            locked_logged = 1;
            log_time("the game's config fixes the time; skipped.", "");
        }
        return 0;
    }
    __try {
        ((SetTime)(game + RVA_SET_TIME))(clock, choices[choice].seconds, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log_time("ERROR: exception while setting the time: ", choices[choice].name);
        return 0;
    }
    write_byte(clock + 0x24, 1);
    ClockState after;
    if (!read_clock(clock, &after)) {
        return 0;
    }
    held_clock = clock;
    held_choice = choice;
    held_time = after.time;
    held_stop = 1;
    return 1;
}

static int hold_slow(unsigned char *clock, const ClockState *state) {
    if (held_clock == clock && held_choice == TIME_OF_DAY_SLOW && held_slow &&
        state->speed == held_speed && !state->stopped) {
        return 0;
    }
    if (state->stopped || state->speed <= 0.0f) {
        return 0; /* the game stopped its clock: nothing to slow down */
    }
    game_speed = state->speed;
    held_speed = state->speed / SLOW_FACTOR;
    write_float(clock + 0x18, held_speed);
    held_clock = clock;
    held_choice = TIME_OF_DAY_SLOW;
    held_slow = 1;
    return 1;
}

void time_of_day_tick(void) {
    if (!loaded) {
        return;
    }
    LONG current_generation = generation;
    int changed = current_generation != seen_generation;
    ULONGLONG now = GetTickCount64();
    if (!changed && now - checked_at < CHECK_INTERVAL_MS) {
        return;
    }
    checked_at = now;
    seen_generation = current_generation;
    unsigned char *clock = find_clock();
    ClockState state;
    if (held_clock && held_clock != clock) {
        held_clock = NULL; /* loading screen or a new world: the old clock is gone */
        held_choice = 0;
        held_stop = held_slow = 0;
    }
    if (!clock || !read_clock(clock, &state)) {
        return;
    }
    int online = session_online();
    if (online != online_logged) {
        if (online) log_time("online session - the time comes from the session.", "");
        else if (online_logged == 1) log_time("offline again.", "");
        online_logged = online;
    }
    int yield = (int)yielding;
    int choice = yield ? TIME_OF_DAY_ORIGINAL : (int)wanted;
    static int yield_logged;
    if (yield != yield_logged) {
        if (yield && wanted != TIME_OF_DAY_ORIGINAL) {
            log_time("Extra Options' Weather Control locks the clock - Gripline's time of day waits.", "");
        } else if (!yield && yield_logged) {
            log_time("Weather Control's time lock is off again - Gripline's time of day applies.", "");
        }
        yield_logged = yield;
    }
    if (held_clock && held_choice != choice) {
        release(choice == TIME_OF_DAY_ORIGINAL ? "back to the game's normal day and night." : NULL);
    }
    if (choice == TIME_OF_DAY_ORIGINAL) {
        return;
    }
    if (online) {
        release(held_clock ? "handed back for the online session." : NULL);
        return;
    }
    if (!in_free_roam()) {
        if (held_clock) release("event or menu - the game's clock runs again.");
        return;
    }
    if (!read_clock(clock, &state)) { /* again: release() may have started the clock */
        return;
    }
    int applied = choice == TIME_OF_DAY_SLOW ? hold_slow(clock, &state)
                                             : hold_fixed(clock, choice, &state);
    if (applied) {
        log_time("set to ", choices[choice].name);
    }
}

#ifdef FH2_TWEAKS_TEST
static int test_free_roam = 1;
static int test_in_free_roam(void) {
    return test_free_roam;
}
__declspec(dllexport) void Fh2TestTimeStart(unsigned char *image, int value) {
    ModOptions options = {0};
    options.time_of_day = value;
    time_of_day_start(image, &options);
    in_free_roam = test_in_free_roam;
    seen_generation = -1;
    held_clock = NULL;
    held_choice = held_stop = held_slow = 0;
    online_logged = -1;
    locked_logged = 0;
    checked_at = 0;
}
__declspec(dllexport) void Fh2TestTimeSet(int value) {
    time_of_day_set_setting(value);
}
__declspec(dllexport) void Fh2TestTimeFreeRoam(int value) {
    test_free_roam = value;
}
__declspec(dllexport) void *Fh2TestTimeClock(void) {
    return find_clock();
}
__declspec(dllexport) void Fh2TestTimeTick(void) {
    time_of_day_tick();
}
#endif
