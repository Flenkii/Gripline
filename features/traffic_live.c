/* V7.3 live traffic: the traffic amount changes at once, without restarting the game.
 *
 * FH2 reads AI/Densities*.xml once, when the open world is loaded (event "LoadedNavigationWorld",
 * game+0x722990), into one density table per file. The parser game+0x18D3F90 (this = the table) appends
 * every <Density> to a vector per cartype at table+8+cartype*0x18 (begin, end, capacity; 2 traffic,
 * 1 festival). One entry is 0x28 bytes: +0 min, +4 max, +8 speed (mph * 0.44704), +0xC spawnAwayChance,
 * +0x10 spawnInDistance, +0x14 spawnOutDistance, +0x18 allowedVehicles, +0x20 road type.
 * The game reads min/max from the table at every density query (game+0x18DA2E0 / 0x18DA400) and only
 * blends them by the time of day, so values written into the table count from the next query on.
 *
 * Gripline hooks the parser (through the platform, no code of its own in the game), finds out which file a
 * new table came from (its values equal what traffic_files.c computes for the level the game loaded) and
 * then writes the values of the wanted level into it. Before every write the table must still hold exactly
 * what Gripline wrote last time; a table the game rebuilt or freed is dropped. CarLoadingSettings.xml (how
 * many cars and models at most) stays a start-up value. */
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "rig_settings.h"
#include "traffic_files.h"
#include "traffic_live.h"

#define ENTRY_SIZE 0x28u
#define MAX_ENTRIES 64u
#define MAX_TABLES 8u
#define MPH_TO_MS 0.44703999f

typedef void(__fastcall *ParseFn)(void *table, void *reader, void *tags);

typedef struct {
    unsigned char *table;
    unsigned file;
    int level; /* the level its entries hold now */
} Table;

static ParseFn parse_original;
static SRWLOCK lock = SRWLOCK_INIT;
static Table tables[MAX_TABLES];
static unsigned table_count;
static volatile LONG started;
static TrafficDensity want_values[MAX_ENTRIES], have_values[MAX_ENTRIES];

static int near_value(float a, float b) {
    float d = a - b, m = b < 0 ? -b : b;
    if (d < 0) d = -d;
    return d <= 0.0001f + m * 0.0001f;
}

/* begin and count of the table's vector for cartype (2 traffic, 1 festival); 0 if it does not look right */
static int vector_of(unsigned char *table, int cartype, unsigned char **begin, unsigned *count) {
    unsigned char *pair[2];
    if (!memory_read(table + 8 + (size_t)cartype * 0x18, pair, sizeof(pair))) return 0;
    if (!pair[0] && !pair[1]) { /* an empty list */
        *begin = NULL;
        *count = 0;
        return 1;
    }
    if (!pair[0] || pair[1] < pair[0] || (size_t)(pair[1] - pair[0]) % ENTRY_SIZE) return 0;
    size_t n = (size_t)(pair[1] - pair[0]) / ENTRY_SIZE;
    if (n > MAX_ENTRIES) return 0;
    *begin = pair[0];
    *count = (unsigned)n;
    return 1;
}

/* 1 when the table's cartype vector holds min/max of `level` for `file` */
static int holds(unsigned char *table, unsigned file, int level, int cartype) {
    unsigned want = traffic_density_values(file, level, cartype, want_values, MAX_ENTRIES), count;
    unsigned char *begin;
    if (!vector_of(table, cartype, &begin, &count) || count != want) return 0;
    for (unsigned i = 0; i < count; ++i) {
        float pair[2];
        if (!memory_read(begin + (size_t)i * ENTRY_SIZE, pair, sizeof(pair))) return 0;
        if (!near_value(pair[0], want_values[i].min) || !near_value(pair[1], want_values[i].max)) return 0;
    }
    return 1;
}

static int holds_both(unsigned char *table, unsigned file, int level) {
    return holds(table, file, level, 2) && holds(table, file, level, 1);
}

/* writes level `to` over level `from` for one cartype; only the values the levels differ in */
static int write_cartype(unsigned char *table, unsigned file, int from, int to, int cartype, int *written) {
    unsigned n = traffic_density_values(file, to, cartype, want_values, MAX_ENTRIES);
    unsigned m = traffic_density_values(file, from, cartype, have_values, MAX_ENTRIES), count;
    unsigned char *begin;
    if (n != m || !vector_of(table, cartype, &begin, &count) || count != n) return 0;
    if (count && !memory_is_writable(begin, (size_t)count * ENTRY_SIZE)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned char *e = begin + (size_t)i * ENTRY_SIZE;
        const TrafficDensity *w = &want_values[i], *h = &have_values[i];
        if (w->min != h->min) memory_copy(e + 0x0, &w->min, 4), ++*written;
        if (w->max != h->max) memory_copy(e + 0x4, &w->max, 4), ++*written;
        if (w->speed != h->speed) {
            float speed = MPH_TO_MS * w->speed;
            memory_copy(e + 0x8, &speed, 4), ++*written;
        }
        if (w->spawn_in != h->spawn_in) memory_copy(e + 0x10, &w->spawn_in, 4), ++*written;
        if (w->spawn_out != h->spawn_out) memory_copy(e + 0x14, &w->spawn_out, 4), ++*written;
    }
    return 1;
}

/* under the lock: every known table to `level` (drops tables that no longer hold what Gripline knows) */
static void apply_locked(int level) {
    for (unsigned t = 0; t < table_count;) {
        Table *table = &tables[t];
        if (table->level == level) {
            ++t;
            continue;
        }
        int ok = 0, written = 0;
        __try {
            ok = holds_both(table->table, table->file, table->level) &&
                 write_cartype(table->table, table->file, table->level, level, 2, &written) &&
                 write_cartype(table->table, table->file, table->level, level, 1, &written);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = 0;
        }
        if (!ok) {
            log_append("TRAFFIC LIVE: a density table changed or is gone (new load?) - no longer touched: ");
            log_append(traffic_density_file_name(table->file));
            log_append("\r\n");
            tables[t] = tables[--table_count];
            continue;
        }
        log_append("TRAFFIC LIVE: ");
        log_append(traffic_density_file_name(table->file));
        log_append_value(" now at TrafficLevel ", level);
        log_append_value("TRAFFIC LIVE: values written: ", written);
        table->level = level;
        ++t;
    }
}

static int wanted_level(void) {
    int level = traffic_setting(RIG_TRAFFIC_LEVEL);
    return level >= 0 && level <= 7 ? level : 0;
}

static int tables_taken_file(int file, unsigned char *except) {
    for (unsigned t = 0; t < table_count; ++t)
        if ((int)tables[t].file == file && tables[t].table != except) return 1;
    return 0;
}

/* the parser has filled `table`: which file was it? */
static void remember(unsigned char *table) {
    int loaded = traffic_files_loaded_level();
    if (loaded < 0) loaded = 0;
    int found = -1, found_level = loaded, matches = 0;
    for (int pass = 0; pass < 2 && found < 0; ++pass) {
        int level = pass ? 0 : loaded; /* pass 1: the game read its own files after all */
        if (pass && loaded == 0) break;
        for (unsigned f = 0; f < traffic_density_file_count(); ++f) {
            if (!holds_both(table, f, level)) continue;
            ++matches;
            /* the same values in two files (possible at high levels, where the caps meet): the file that
               has no table yet */
            int taken = 0;
            for (unsigned t = 0; t < table_count; ++t)
                if (tables[t].file == f && tables[t].table != table) taken = 1;
            if (found < 0 || (!taken && tables_taken_file(found, table))) found = (int)f, found_level = level;
        }
    }
    if (matches > 1) log_append_value("TRAFFIC LIVE: density table fits more than one file, matches=", matches);
    if (found < 0) {
        log_append("TRAFFIC LIVE: density table loaded that Gripline does not change (race or unknown file).\r\n");
        return;
    }
    for (unsigned t = 0; t < table_count; ++t)
        if (tables[t].table == table) tables[t] = tables[--table_count]; /* the same memory again */
    if (table_count >= MAX_TABLES) {
        log_append("TRAFFIC LIVE: too many density tables - the oldest is no longer touched.\r\n");
        for (unsigned t = 1; t < table_count; ++t) tables[t - 1] = tables[t];
        --table_count;
    }
    tables[table_count].table = table;
    tables[table_count].file = (unsigned)found;
    tables[table_count].level = found_level;
    ++table_count;
    log_append("TRAFFIC LIVE: density table found: ");
    log_append(traffic_density_file_name((unsigned)found));
    log_append_value(", loaded with TrafficLevel ", found_level);
}

static void __fastcall parse_detour(void *table, void *reader, void *tags) {
    parse_original(table, reader, tags);
    if (!table) return;
    AcquireSRWLockExclusive(&lock);
    __try {
        remember((unsigned char *)table);
        apply_locked(wanted_level());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log_append("TRAFFIC LIVE: error while reading a new density table - ignored.\r\n");
    }
    ReleaseSRWLockExclusive(&lock);
}

static DWORD WINAPI live_thread(LPVOID unused) {
    (void)unused;
    int last = -1;
    for (;;) {
        int level = wanted_level();
        if (level != last) {
            AcquireSRWLockExclusive(&lock);
            if (table_count) {
                log_append_value("TRAFFIC LIVE: menu TrafficLevel ", level);
                apply_locked(level);
            }
            ReleaseSRWLockExclusive(&lock);
            last = level;
        }
        Sleep(250);
    }
    return 0;
}

/* the first bytes of the parser in the known game exe (3ec04c61): mov rax,rsp; mov [rax+18h],r8; push ... */
static const unsigned char parse_start[] = {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x40, 0x18, 0x55, 0x56, 0x57,
                                            0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

void *traffic_live_target(unsigned char *game) {
    unsigned char now[sizeof(parse_start)];
    if (!game || !memory_read(game + TRAFFIC_LIVE_PARSE_RVA, now, sizeof(now))) return NULL;
    if (!memory_equal(now, parse_start, sizeof(now))) {
        log_append("TRAFFIC LIVE: the density parser is not where Gripline expects it (other game version or "
                   "another mod) - live traffic off, changes apply after a restart.\r\n");
        return NULL;
    }
    return game + TRAFFIC_LIVE_PARSE_RVA;
}

void *traffic_live_parse_detour(void) {
    return (void *)parse_detour;
}

void traffic_live_start(void *original) {
    if (!original || InterlockedExchange(&started, 1)) return;
    parse_original = (ParseFn)original;
    HANDLE thread = CreateThread(NULL, 0, live_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    log_append("TRAFFIC LIVE: density parser hooked - traffic amount changes without a restart.\r\n");
}

#ifdef FH2_TWEAKS_TEST
/* tests: a table the parser just filled (fake memory) and the menu level */
__declspec(dllexport) int Fh2TestTrafficLiveRemember(void *table, int level) {
    AcquireSRWLockExclusive(&lock);
    unsigned before = table_count;
    remember((unsigned char *)table);
    apply_locked(level);
    int kept = (int)table_count;
    ReleaseSRWLockExclusive(&lock);
    return kept > (int)before ? kept : -kept;
}
__declspec(dllexport) int Fh2TestTrafficLiveApply(int level) {
    AcquireSRWLockExclusive(&lock);
    apply_locked(level);
    int kept = (int)table_count;
    ReleaseSRWLockExclusive(&lock);
    return kept;
}
__declspec(dllexport) unsigned Fh2TestTrafficLiveValues(unsigned file, int level, int cartype, float *out,
                                                        unsigned capacity) {
    unsigned n = traffic_density_values(file, level, cartype, want_values, MAX_ENTRIES);
    for (unsigned i = 0; i < n && i * 5 + 4 < capacity; ++i) {
        out[i * 5] = want_values[i].min, out[i * 5 + 1] = want_values[i].max, out[i * 5 + 2] = want_values[i].speed;
        out[i * 5 + 3] = want_values[i].spawn_in, out[i * 5 + 4] = want_values[i].spawn_out;
    }
    return n;
}
#endif
