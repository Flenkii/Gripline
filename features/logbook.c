/* Logbook and "what's new" cards - see logbook.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "support.h"
#include "logging.h"
#include "logbook.h"
#include "notify.h"
#include "keyboard_pad.h"
#include "game_language.h"
#include "game_units.h"
#include "ini_edit.h"

#define MAX_CARS 512        /* car files in media\cars (the game has about 340) */
#define MAX_KEPT 512        /* cars in the logbook */
#define STEM 40
#define MAX_SPEED 150.0f    /* m/s: faster is a teleport */
#define MOVING 1.0f
#define SAVE_EVERY_MS 60000u
#define SCAN_BYTES 0x6000u  /* the car object is searched this far for the name */
#define SCAN_INNER 0x200u   /* and each object it points to this far */
#define NEWS_VERSION GRIPLINE_NEWS_VERSION
#define NEWS_DELAY_MS 20000u /* after the start cards */
#define FILE_LIMIT 65536u

typedef struct {
    char stem[STEM];
    double metres, seconds;
    float top;
} Car;

static unsigned char *game;
static wchar_t book_path[PATH_CAPACITY], ini_path[PATH_CAPACITY];
static char stems[MAX_CARS][STEM];
static unsigned stem_count;
static Car cars[MAX_KEPT];
static unsigned car_count;
static double total_metres, total_seconds;
static float total_top;
static SRWLOCK lock = SRWLOCK_INIT; /* cars + totals (watch thread writes, menu reads) */

/* from the game thread */
static SRWLOCK pend_lock = SRWLOCK_INIT;
static double pend_metres, pend_seconds;
static float pend_top;
static void *pend_car;

static void *named_car;  /* the car object the name below belongs to */
static int current = -1; /* index in cars, -1 = unknown */
static int dirty, started, was_running;
static ULONGLONG saved_at;
/* where the name is: car + first (string or pointer); inner >= 0: *(car + first) + inner; wide text */
static int path_known, path_first, path_inner, path_wide;
static unsigned scans;

static int news_seen;
static ULONGLONG news_at;

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same_stem(const char *a, const char *b) {
    for (unsigned i = 0; i < STEM; ++i) {
        if (lower(a[i]) != lower(b[i])) return 0;
        if (!a[i]) return 1;
    }
    return 0;
}

static float root(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }

/* ------------------------------------------------------------------ names */

/* "NIS_GTR_12" -> "Nissan GTR '12" (brand table from the car files; the rest split at capitals) */
static const char *const brands[][2] = {
    {"ACU", "Acura"}, {"ALF", "Alfa Romeo"}, {"AMC", "AMC"}, {"ARI", "Ariel"}, {"ARM", "Terradyne"},
    {"AST", "Aston Martin"}, {"AUD", "Audi"}, {"BEN", "Bentley"}, {"BMW", "BMW"}, {"BOW", "Bowler"},
    {"BUG", "Bugatti"}, {"BUI", "Buick"}, {"CAD", "Cadillac"}, {"CAT", "Caterham"}, {"CHE", "Chevrolet"},
    {"CHR", "Plymouth"}, {"DOD", "Dodge"}, {"DON", "Donkervoort"}, {"FER", "Ferrari"}, {"FIA", "Fiat"},
    {"FOR", "Ford"}, {"HEN", "Hennessey"}, {"HON", "Honda"}, {"HUM", "Hummer"}, {"HYU", "Hyundai"},
    {"JAG", "Jaguar"}, {"JEE", "Jeep"}, {"KIA", "Kia"}, {"KOE", "Koenigsegg"}, {"KTM", "KTM"},
    {"LAM", "Lamborghini"}, {"LAN", "Lancia"}, {"LEX", "Lexus"}, {"LOC", "Local Motors"}, {"LOT", "Lotus"},
    {"LR", "Land Rover"}, {"MAS", "Maserati"}, {"MAZ", "Mazda"}, {"MCL", "McLaren"}, {"MER", "Mercedes"},
    {"MG", "MG"}, {"MIN", "Mini"}, {"MIT", "Mitsubishi"}, {"MRC", "Mercury"}, {"NIS", "Nissan"},
    {"NOB", "Noble"}, {"OLD", "Oldsmobile"}, {"PAG", "Pagani"}, {"PLY", "Plymouth"}, {"PON", "Pontiac"},
    {"POR", "Porsche"}, {"REN", "Renault"}, {"RUF", "RUF"}, {"SHE", "Shelby"}, {"SUB", "Subaru"},
    {"TES", "Tesla"}, {"TOY", "Toyota"}, {"TVR", "TVR"}, {"ULT", "Ultima"}, {"VAU", "Vauxhall"},
    {"VIP", "SRT"}, {"VOL", "Volvo"}, {"VW", "Volkswagen"},
};

static void pretty(const char *stem, wchar_t *out, size_t capacity) {
    size_t n = 0, i = 0;
    if (!capacity) return;
    while (stem[i] && stem[i] != '_') ++i;
    const char *brand = NULL;
    for (unsigned b = 0; b < ARRAY_COUNT(brands) && i; ++b) {
        size_t k = 0;
        while (brands[b][0][k] && k < i && lower(brands[b][0][k]) == lower(stem[k])) ++k;
        if (k == i && !brands[b][0][k]) brand = brands[b][1];
    }
    if (brand) {
        while (*brand && n + 1 < capacity) out[n++] = (wchar_t)*brand++;
    } else {
        for (size_t k = 0; k < i && n + 1 < capacity; ++k) out[n++] = (wchar_t)stem[k];
    }
    /* the last part is the year when it is two digits */
    size_t end = 0;
    while (stem[end]) ++end;
    size_t last = end;
    while (last > i && stem[last - 1] != '_') --last;
    int year = end - last == 2 && stem[last] >= '0' && stem[last] <= '9' && stem[last + 1] >= '0' && stem[last + 1] <= '9';
    size_t model_end = year && last > i ? last - 1 : end;
    char prev = '_';
    for (size_t k = i; k < model_end && n + 1 < capacity; ++k) {
        char c = stem[k];
        if (c == '_') {
            if (prev != '_' && n + 1 < capacity) out[n++] = L' ';
            prev = '_';
            continue;
        }
        if (prev == '_' && (n == 0 || out[n - 1] != L' ') && n + 1 < capacity) out[n++] = L' ';
        else if (c >= 'A' && c <= 'Z' && prev >= 'a' && prev <= 'z' && n + 1 < capacity) out[n++] = L' ';
        if (n + 1 < capacity) out[n++] = (wchar_t)c;
        prev = c;
    }
    if (year && n + 4 < capacity) {
        out[n++] = L' ';
        out[n++] = L'\'';
        out[n++] = (wchar_t)stem[last];
        out[n++] = (wchar_t)stem[last + 1];
    }
    if (n > 31) n = 31; /* menu rows hold 31 characters */
    out[n] = 0;
}

static void load_stems(const wchar_t *mount_dir) {
    static wchar_t pattern[PATH_CAPACITY];
    if (!mount_dir || !mount_dir[0] || !path_join(pattern, mount_dir, L"media\\cars\\*.zip")) return;
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        size_t len = 0;
        while (found.cFileName[len]) ++len;
        if (len < 5 || len - 4 >= STEM || stem_count >= MAX_CARS) continue;
        int ascii = 1;
        for (size_t k = 0; k < len - 4; ++k) {
            if (found.cFileName[k] > 126 || found.cFileName[k] < 33) ascii = 0;
            stems[stem_count][k] = (char)found.cFileName[k];
        }
        stems[stem_count][len - 4] = 0;
        if (ascii && !same_stem(stems[stem_count], "nul_car_00")) ++stem_count;
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

/* text at p (ASCII or UTF-16) -> the car file stem it names, or -1. Paths and extensions are cut. */
static int stem_at(const unsigned char *p, int wide) {
    unsigned char raw[2 * STEM + 16];
    if (!memory_read(p, raw, wide ? 2 * (STEM + 6) : STEM + 6)) return -1;
    char text[STEM + 8];
    unsigned n = 0;
    for (unsigned k = 0; k < STEM + 6; ++k) {
        unsigned c = wide ? (unsigned)(raw[2 * k] | raw[2 * k + 1] << 8) : raw[k];
        if (!c) break;
        if (c < 33 || c > 126) return -1;
        if (c == '/' || c == '\\') {
            n = 0;
            continue;
        }
        if (c == '.') break;
        if (n >= STEM - 1) return -1;
        text[n++] = (char)c;
    }
    text[n] = 0;
    /* quick filter: "ABC_" or "AB_" */
    if (n < 5 || !(text[2] == '_' || text[3] == '_')) return -1;
    for (unsigned s = 0; s < stem_count; ++s)
        if (same_stem(text, stems[s])) return (int)s;
    return -1;
}

static int plausible(uintptr_t v) { return v >= 0x10000 && v < 0x7fffffffffffull; }

static int read_path(void *car, int first, int inner, int wide) {
    const unsigned char *at = (const unsigned char *)car + first;
    if (inner >= 0) {
        void *p;
        if (!memory_read_pointer(at, &p) || !plausible((uintptr_t)p)) return -1;
        at = (const unsigned char *)p + inner;
        void *q;
        if (!memory_read_pointer(at, &q) || !plausible((uintptr_t)q)) return -1;
        return stem_at(q, wide);
    }
    int s = stem_at(at, wide); /* inline text */
    if (s >= 0) return s;
    void *p;
    if (!memory_read_pointer(at, &p) || !plausible((uintptr_t)p)) return -1;
    return stem_at(p, wide);
}

static void log_path(const char *stem) {
    char line[160];
    unsigned n = 0;
    static const char hex[] = "0123456789abcdef";
    const char *head = "LOGBOOK: car name found at +0x";
    while (*head) line[n++] = *head++;
    for (int shift = 12; shift >= 0; shift -= 4) line[n++] = hex[(path_first >> shift) & 15];
    if (path_inner >= 0) {
        const char *mid = " -> +0x";
        while (*mid) line[n++] = *mid++;
        for (int shift = 8; shift >= 0; shift -= 4) line[n++] = hex[(path_inner >> shift) & 15];
    }
    const char *w = path_wide ? " (UTF-16): " : ": ";
    while (*w) line[n++] = *w++;
    while (*stem && n < sizeof(line) - 3) line[n++] = *stem++;
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    log_append(line);
}

static int find_name(void *car) {
    if (path_known) {
        int s = read_path(car, path_first, path_inner, path_wide);
        if (s >= 0) return s;
    }
    if (!stem_count || scans >= 8) return -1; /* the search runs a few times only */
    ++scans;
    for (int wide = 0; wide < 2; ++wide)
        for (int off = 0; off < (int)SCAN_BYTES; off += 8) {
            int s = read_path(car, off, -1, wide);
            if (s >= 0) {
                path_known = 1, path_first = off, path_inner = -1, path_wide = wide;
                log_path(stems[s]);
                return s;
            }
        }
    for (int wide = 0; wide < 2; ++wide)
        for (int off = 0; off < (int)SCAN_BYTES; off += 8) {
            void *p;
            if (!memory_read_pointer((const unsigned char *)car + off, &p) || !plausible((uintptr_t)p)) continue;
            for (int in = 0; in < (int)SCAN_INNER; in += 8) {
                int s = read_path(car, off, in, wide);
                if (s >= 0) {
                    path_known = 1, path_first = off, path_inner = in, path_wide = wide;
                    log_path(stems[s]);
                    return s;
                }
            }
        }
    log_append("LOGBOOK: the car's name was not found - kilometres count only in the total.\r\n");
    return -1;
}

/* ------------------------------------------------------------------ file */

static char file_text[FILE_LIMIT + 1];

static unsigned long parse_uint(const char **p) {
    unsigned long v = 0;
    while (**p == ' ') ++*p;
    while (**p >= '0' && **p <= '9') v = v * 10 + (unsigned long)(*(*p)++ - '0');
    return v;
}

static int car_index(const char *stem, int add) {
    for (unsigned i = 0; i < car_count; ++i)
        if (same_stem(cars[i].stem, stem)) return (int)i;
    if (!add || car_count >= MAX_KEPT) return -1;
    Car *c = &cars[car_count];
    unsigned k = 0;
    while (stem[k] && k < STEM - 1) c->stem[k] = stem[k], ++k;
    c->stem[k] = 0;
    c->metres = c->seconds = 0;
    c->top = 0;
    return (int)car_count++;
}

static int read_text(const wchar_t *path, size_t *length) {
    *length = 0;
    file_text[0] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart <= FILE_LIMIT &&
             ReadFile(file, file_text, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart;
    CloseHandle(file);
    if (!ok) return 0;
    file_text[got] = 0;
    *length = got;
    return 1;
}

static int write_text(const wchar_t *path, const char *data, size_t length) {
    static wchar_t temp[PATH_CAPACITY + 8];
    size_t n = 0;
    while (path[n] && n < PATH_CAPACITY - 1) temp[n] = path[n], ++n;
    const wchar_t *ext = L".gltmp";
    while (*ext) temp[n++] = *ext++;
    temp[n] = 0;
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp);
    return ok;
}

/* lines: "total <metres> <seconds> <top cm/s>" and "car <stem> <metres> <seconds> <top cm/s>" */
static void load_book(void) {
    size_t length;
    if (!read_text(book_path, &length)) return;
    const char *p = file_text, *end = file_text + length;
    while (p < end) {
        if (p + 6 <= end && memory_equal(p, "total ", 6)) {
            p += 6;
            total_metres = (double)parse_uint(&p);
            total_seconds = (double)parse_uint(&p);
            total_top = (float)parse_uint(&p) / 100.0f;
        } else if (p + 4 <= end && memory_equal(p, "car ", 4)) {
            p += 4;
            char stem[STEM];
            unsigned k = 0;
            while (p < end && *p != ' ' && *p != '\r' && *p != '\n') {
                if (k < STEM - 1) stem[k++] = *p;
                ++p;
            }
            stem[k] = 0;
            int i = k ? car_index(stem, 1) : -1;
            double m = (double)parse_uint(&p), s = (double)parse_uint(&p);
            float t = (float)parse_uint(&p) / 100.0f;
            if (i >= 0) cars[i].metres = m, cars[i].seconds = s, cars[i].top = t;
        }
        while (p < end && *p != '\n') ++p;
        ++p;
    }
}

static void put(char *out, size_t *n, size_t cap, const char *t) {
    while (*t && *n + 1 < cap) out[(*n)++] = *t++;
    out[*n] = 0;
}

static void put_uint(char *out, size_t *n, size_t cap, unsigned long v) {
    char d[24];
    unsigned k = 0;
    do d[k++] = (char)('0' + v % 10); while ((v /= 10) && k < sizeof(d));
    while (k && *n + 1 < cap) out[(*n)++] = d[--k];
    out[*n] = 0;
}

static void save_book(void) {
    static char out[FILE_LIMIT];
    size_t n = 0;
    AcquireSRWLockShared(&lock);
    put(out, &n, sizeof(out), "# Gripline logbook: metres, seconds, top speed cm/s\r\ntotal ");
    put_uint(out, &n, sizeof(out), (unsigned long)total_metres);
    put(out, &n, sizeof(out), " ");
    put_uint(out, &n, sizeof(out), (unsigned long)total_seconds);
    put(out, &n, sizeof(out), " ");
    put_uint(out, &n, sizeof(out), (unsigned long)(total_top * 100.0f));
    put(out, &n, sizeof(out), "\r\n");
    for (unsigned i = 0; i < car_count; ++i) {
        put(out, &n, sizeof(out), "car ");
        put(out, &n, sizeof(out), cars[i].stem);
        put(out, &n, sizeof(out), " ");
        put_uint(out, &n, sizeof(out), (unsigned long)cars[i].metres);
        put(out, &n, sizeof(out), " ");
        put_uint(out, &n, sizeof(out), (unsigned long)cars[i].seconds);
        put(out, &n, sizeof(out), " ");
        put_uint(out, &n, sizeof(out), (unsigned long)(cars[i].top * 100.0f));
        put(out, &n, sizeof(out), "\r\n");
    }
    ReleaseSRWLockShared(&lock);
    if (!write_text(book_path, out, n)) log_append("LOGBOOK: Gripline_Logbook.txt could not be written.\r\n");
    dirty = 0;
}

/* ------------------------------------------------------------------ cards */

static const unsigned milestones[] = {100, 500, 1000, 2500, 5000, 10000};

static void utf8_name(const char *stem, char *out, size_t cap, size_t *n) {
    wchar_t w[48];
    pretty(stem, w, 48);
    for (unsigned k = 0; w[k] && *n + 1 < cap; ++k) out[(*n)++] = (char)w[k]; /* ASCII only */
    out[*n] = 0;
}

static void milestone(const char *stem, double before, double after) {
    int imperial = game_units_imperial(game), de = game_language_is_german(game);
    double unit = imperial ? 1609.344 : 1000.0;
    for (unsigned i = 0; i < ARRAY_COUNT(milestones); ++i) {
        double at = milestones[i] * unit;
        if (before < at && after >= at) {
            char text[160];
            size_t n = 0;
            put_uint(text, &n, sizeof(text), milestones[i]);
            put(text, &n, sizeof(text), imperial ? " mi" : " km");
            put(text, &n, sizeof(text), de ? " gefahren!\nim " : " driven!\nin the ");
            utf8_name(stem, text, sizeof(text), &n);
            if (notify_kind_on(NOTIFY_KIND_MILESTONES)) notify_show_tinted(text, NOTIFY_COLOR_YELLOW, de ? "MEILENSTEIN" : "MILESTONE", 1);
            log_append("LOGBOOK: milestone card\r\n");
        }
    }
}

static int save_news_seen_now(void) {
    size_t length;
    if (!read_text(ini_path, &length)) return 0;
    static char out[FILE_LIMIT + 64];
    char value[8];
    size_t n = 0;
    put_uint(value, &n, sizeof(value), NEWS_VERSION);
    size_t written = ini_set_value(file_text, length, "Gripline", "NewsSeen", value, out, sizeof(out));
    return written && write_text(ini_path, out, written);
}

static int save_news_seen(void) { /* one writer at a time (support.h) */
    settings_file_lock();
    int ok = save_news_seen_now();
    settings_file_unlock();
    return ok;
}

static void show_news(void) {
    int de = game_language_is_german(game);
    if (!notify_kind_on(NOTIFY_KIND_START)) { /* start cards hidden: no what's-new either, counts as seen */
        if (save_news_seen()) log_append("LOGBOOK: what's-new cards hidden (CardsHidden), NewsSeen saved.\r\n");
        return;
    }
    const char *badge = de ? "NEU IN V7" : "NEW IN V7";
    /* the last one shown ends up on top */
    notify_show_tinted(de ? "Freecam & Fahrtenbuch\nF7 Freecam, km pro Auto in F8" : "Freecam & logbook\nF7 freecam, km per car in F8",
                       NOTIFY_COLOR_GREEN, badge, 1);
    notify_show_tinted(de ? "Live-Telemetrie\nViertelmeile, Bremsweg & mehr (F8)" : "Live telemetry\nQuarter mile, braking & more (F8)",
                       NOTIFY_COLOR_GREEN, badge, 1);
    notify_show_tinted(de ? "Das Gripline-Menü\nF8: Tasten, Kein Schaden, Welt" : "The Gripline menu\nF8: keys, no damage, world",
                       NOTIFY_COLOR_GREEN, badge, 1);
    if (save_news_seen()) log_append("LOGBOOK: what's-new cards shown, NewsSeen saved.\r\n");
    else log_append("LOGBOOK: what's-new cards shown, but Gripline.ini could not be written.\r\n");
}

/* ------------------------------------------------------------------ API */

void logbook_start(unsigned char *game_image, const wchar_t *settings_directory, const wchar_t *mount_dir,
                   int news) {
    game = game_image;
    news_seen = news;
    if (!settings_directory || !path_join(book_path, settings_directory, L"Gripline_Logbook.txt") ||
        !path_join(ini_path, settings_directory, L"Gripline.ini"))
        return;
    {   /* the first V7 test build used a German name */
        static wchar_t old[PATH_CAPACITY];
        if (path_join(old, settings_directory, L"Gripline_Fahrtenbuch.txt") &&
            MoveFileExW(old, book_path, 0))
            log_append("LOGBOOK: Gripline_Fahrtenbuch.txt renamed to Gripline_Logbook.txt.\r\n");
    }
    load_stems(mount_dir);
    load_book();
    started = 1;
    char line[96];
    size_t n = 0;
    put(line, &n, sizeof(line), "LOGBOOK: ");
    put_uint(line, &n, sizeof(line), car_count);
    put(line, &n, sizeof(line), " cars kept, ");
    put_uint(line, &n, sizeof(line), stem_count);
    put(line, &n, sizeof(line), " car files known.\r\n");
    log_append(line);
}

void logbook_sample(void *car, float dt, const float v[3]) {
    if (!started || !(dt > 0.0f) || dt > 0.25f) return;
    float speed = root(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(speed < MAX_SPEED)) return;
    AcquireSRWLockExclusive(&pend_lock);
    pend_car = car;
    pend_metres += (double)speed * dt;
    if (speed > MOVING) pend_seconds += dt;
    if (speed > pend_top) pend_top = speed;
    ReleaseSRWLockExclusive(&pend_lock);
}

void logbook_step(unsigned long long now) {
    if (!started) return;
    if (news_seen < NEWS_VERSION && game_simulation_running()) {
        if (!news_at) news_at = now + NEWS_DELAY_MS;
        else if (now >= news_at) {
            news_seen = NEWS_VERSION;
            show_news();
        }
    }
    AcquireSRWLockExclusive(&pend_lock);
    void *car = pend_car;
    double m = pend_metres, s = pend_seconds;
    float t = pend_top;
    pend_metres = pend_seconds = 0;
    pend_top = 0;
    ReleaseSRWLockExclusive(&pend_lock);
    if (car && car != named_car) {
        named_car = car;
        int st = find_name(car);
        AcquireSRWLockExclusive(&lock);
        current = st >= 0 ? car_index(stems[st], 1) : -1;
        ReleaseSRWLockExclusive(&lock);
    }
    if (m > 0.0 || s > 0.0) {
        char stem[STEM];
        double before = 0, after = 0;
        stem[0] = 0;
        AcquireSRWLockExclusive(&lock);
        total_metres += m;
        total_seconds += s;
        if (t > total_top) total_top = t;
        if (current >= 0) {
            Car *c = &cars[current];
            before = c->metres;
            c->metres += m;
            c->seconds += s;
            if (t > c->top) c->top = t;
            after = c->metres;
            memory_copy(stem, c->stem, STEM);
        }
        ReleaseSRWLockExclusive(&lock);
        dirty = 1;
        if (stem[0]) milestone(stem, before, after);
    }
    int running = game_simulation_running();
    int paused_now = was_running && !running;
    was_running = running;
    if (dirty && (paused_now || now - saved_at >= SAVE_EVERY_MS)) {
        saved_at = now;
        save_book();
    }
}

void logbook_total(double *metres, double *seconds, float *top) {
    AcquireSRWLockShared(&lock);
    *metres = total_metres;
    *seconds = total_seconds;
    *top = total_top;
    ReleaseSRWLockShared(&lock);
}

void logbook_current(wchar_t *name, size_t capacity, double *metres) {
    AcquireSRWLockShared(&lock);
    if (current >= 0) {
        pretty(cars[current].stem, name, capacity);
        *metres = cars[current].metres;
    } else {
        if (capacity) name[0] = 0;
        *metres = 0;
    }
    ReleaseSRWLockShared(&lock);
}

int logbook_top(unsigned rank, wchar_t *name, size_t capacity, double *metres) {
    int found = 0;
    AcquireSRWLockShared(&lock);
    /* rank-th largest by metres, ties by order */
    static unsigned char used[MAX_KEPT];
    for (unsigned i = 0; i < car_count; ++i) used[i] = 0;
    int pick = -1;
    for (unsigned r = 0; r <= rank; ++r) {
        pick = -1;
        for (unsigned i = 0; i < car_count; ++i)
            if (!used[i] && cars[i].metres >= 100.0 && (pick < 0 || cars[i].metres > cars[pick].metres)) pick = (int)i;
        if (pick < 0) break;
        used[pick] = 1;
    }
    if (pick >= 0) {
        pretty(cars[pick].stem, name, capacity);
        *metres = cars[pick].metres;
        found = 1;
    }
    ReleaseSRWLockShared(&lock);
    return found;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestLogbookPretty(const char *stem, wchar_t *out, unsigned capacity) {
    pretty(stem, out, capacity);
}
#endif
