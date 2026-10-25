/* compat3: More Traffic - raises the game's own traffic/festival density caps.
 *
 * FH2 registers developer tunables as global objects in .data:
 *   +0x00 vtable, +0x10 value, +0x14 default, +0x18 min, +0x20 max.
 * The open-world spawner caps cars at "AI/Spawning/Density/MaxTraffic" (14),
 * "AI/Traffic/Density/MaxTraffic" (16), and the festival equivalents (10 / 7),
 * no matter what Densities.xml asks for. These are plain data writes - no game
 * code is changed. Every object is verified (vtable + untouched default value)
 * before it is written, and the values are re-applied periodically because the
 * objects are only constructed during the game's own static initialisation. */
#include "logging.h"
#include "rig_settings.h"
#include "support.h"

#define VT_FLOAT 0x2c5cb58u
#define VT_INT 0x2c81898u

typedef struct {
    unsigned rva;
    unsigned vtable;
    float original;               /* default stored at +0x14 */
    float levels[7];              /* TrafficLevel 0..5, 6 = no traffic (game values) */
    int raise_max;
    const char *name;
} Tunable;

static const Tunable tunables[] = {
    {0x36e8f18, VT_FLOAT, 14.0f, {14, 24, 36, 48, 64, 150, 14}, 1, "Spawning/MaxTraffic"},
    {0x36e6e88, VT_FLOAT, 16.0f, {16, 24, 36, 48, 64, 150, 16}, 1, "Traffic/MaxTraffic"},
    {0x36e8d10, VT_FLOAT, 10.0f, {10, 14, 20, 26, 32, 60, 10}, 1, "Spawning/MaxFestival"},
    {0x36e6dd0, VT_FLOAT, 7.0f, {7, 10, 14, 18, 24, 50, 7}, 1, "Traffic/MaxFestival"},
    {0x34c80f8, VT_FLOAT, 8.0f, {8, 8, 10, 12, 14, 16, 8}, 1, "MinDistanceFromOtherCars"}, /* more gap: no long convoys */
    {0x34c8578, VT_INT, 5.0f, {5, 8, 12, 16, 20, 30, 5}, 1, "InitialSpawnMaxCars"},
};

/* Time-of-day density curve: 0.5..23.5 h. "Rush hour all day" keeps max density. */
typedef struct {
    unsigned rva;
    float original, all_day;
} TimeTunable;

static const TimeTunable times[] = {
    {0x36e6d80, 7.0f, 0.5f},   /* StartTimeAtMaxDensity */
    {0x36e6e60, 19.0f, 23.5f}, /* EndTimeAtMaxDensity */
    {0x36e8ea0, 23.0f, 23.5f}, /* StartTimeAtMinDensity */
    {0x36e6df8, 5.0f, 0.5f},   /* EndTimeAtMinDensity */
};

static unsigned char *image;
/* wanted state (menu / INI) and what the watcher thread last applied */
static volatile LONG want_level, want_all_day, generation;
static LONG applied_generation = -1;
static int touched[ARRAY_COUNT(tunables)], times_touched;
static int loaded;

static int same_float(const void *address, float expected) {
    uint32_t a, b;
    memory_copy(&a, address, 4);
    memory_copy(&b, &expected, 4);
    return a == b;
}

static int object_ready(unsigned rva, unsigned vtable) {
    unsigned char *object = image + rva;
    uintptr_t vt;
    if (!memory_read(object, &vt, sizeof(vt))) {
        return 0;
    }
    return vt == (uintptr_t)(image + vtable) && memory_is_writable(object + 0x10, 0x14);
}

/* Returns how many objects are in the wanted state. Level 0 restores the game's own
   values, but only for objects this watcher changed before (the game is left alone
   otherwise, exactly like the original add-on with TrafficLevel=0). */
static int apply_once(int level, int all_day) {
    int done = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(tunables); ++i) {
        const Tunable *t = &tunables[i];
        if (!object_ready(t->rva, t->vtable)) {
            continue;
        }
        unsigned char *object = image + t->rva;
        if (level == 0 && !touched[i]) {
            ++done;
            continue;
        }
        float want = t->levels[level];
        if (t->vtable == VT_INT) {
            int def, cur, iwant = (int)want, max;
            memory_copy(&def, object + 0x14, 4);
            if (def != (int)t->original) {
                continue;
            }
            memory_copy(&cur, object + 0x10, 4);
            if (cur != iwant) {
                memory_copy(object + 0x10, &iwant, 4);
            }
            memory_copy(&max, object + 0x20, 4);
            if (t->raise_max && max < iwant) {
                memory_copy(object + 0x20, &iwant, 4);
            }
        } else {
            if (!same_float(object + 0x14, t->original)) {
                continue;
            }
            if (!same_float(object + 0x10, want)) {
                memory_copy(object + 0x10, &want, 4);
            }
            float max;
            memory_copy(&max, object + 0x20, 4);
            if (t->raise_max && max < want) {
                memory_copy(object + 0x20, &want, 4);
            }
        }
        touched[i] = level != 0;
        ++done;
    }
    if (all_day || times_touched) {
        int restored = 0;
        for (unsigned i = 0; i < ARRAY_COUNT(times); ++i) {
            const TimeTunable *t = &times[i];
            if (!object_ready(t->rva, VT_FLOAT) || !same_float(image + t->rva + 0x14, t->original)) {
                continue;
            }
            const float *value = all_day ? &t->all_day : &t->original;
            if (!same_float(image + t->rva + 0x10, *value)) {
                memory_copy(image + t->rva + 0x10, value, 4);
            }
            ++restored;
            ++done;
        }
        if (all_day) {
            times_touched = 1;
        } else if (restored == (int)ARRAY_COUNT(times)) {
            times_touched = 0;
        }
    }
    return done;
}

static DWORD WINAPI traffic_thread(LPVOID unused) {
    (void)unused;
    int logged = 0;
    unsigned since_change = 0;
    for (;;) {
        LONG current = generation;
        int level = (int)want_level, all_day = (int)want_all_day;
        if (current != applied_generation) {
            applied_generation = current;
            logged = 0;
            since_change = 0;
        }
        unsigned total = ARRAY_COUNT(tunables) + (all_day ? ARRAY_COUNT(times) : 0);
        int done = apply_once(level, all_day);
        if (!logged && done >= (int)total) {
            if (level == 6) {
                log_append("TRAFFIC: no traffic (density 0 in the traffic files).\r\n");
            } else if (level > 0) {
                log_append_value("TRAFFIC: density caps raised in memory, TrafficLevel=", level);
            } else {
                log_append("TRAFFIC: original density caps (TrafficLevel=0).\r\n");
            }
            if (all_day) {
                log_append("TRAFFIC: rush-hour density all day.\r\n");
            }
            logged = 1;
        } else if (!logged && since_change == 600) {
            log_append_value("TRAFFIC: WARNING only part of the tunables matched, applied=", done);
            logged = 1;
        }
        ++since_change;
        /* 100 ms until everything matched, then every 2 s - but react to menu changes at once */
        unsigned slices = since_change < 600 ? 1 : 20;
        for (unsigned slice = 0; slice < slices && generation == current; ++slice) {
            Sleep(100);
        }
    }
    return 0;
}

int traffic_setting(unsigned id) {
    return id == RIG_TRAFFIC_LEVEL ? (int)want_level : id == RIG_TRAFFIC_ALL_DAY ? (int)want_all_day : 0;
}

void traffic_set_setting(unsigned id, int value) {
    if (!loaded) {
        return;
    }
    if (id == RIG_TRAFFIC_LEVEL && value >= 0 && value <= 6) {
        InterlockedExchange(&want_level, value);
    } else if (id == RIG_TRAFFIC_ALL_DAY && (value == 0 || value == 1)) {
        InterlockedExchange(&want_all_day, value);
    } else {
        return;
    }
    InterlockedIncrement(&generation);
}

void traffic_tunables_start(unsigned char *game_image, const ModOptions *options) {
    if (!game_image) {
        return;
    }
    image = game_image;
    want_level = options->traffic_level >= 0 && options->traffic_level <= 6 ? options->traffic_level : 0;
    want_all_day = options->traffic_all_day != 0;
    loaded = 1;
    /* The watcher also runs at TrafficLevel=0, so the in-game menu can raise it later;
       with level 0 it only ever writes values it changed itself. */
    HANDLE thread = CreateThread(NULL, 0, traffic_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
        log_append("TRAFFIC: More Traffic add-on (unofficial build) - tunable watcher started.\r\n");
    } else {
        log_append("TRAFFIC: could not start tunable watcher.\r\n");
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestTrafficApply(void) {
    return apply_once((int)want_level, (int)want_all_day);
}
#endif
