/* Records, own sprints, telemetry CSV - see records.h. Own code. */
#include <xmmintrin.h>
#include "gripline.h"
#include "support.h"
#include "logging.h"
#include "records.h"
#include "notify.h"
#include "keyboard_pad.h"
#include "game_language.h"
#include "game_units.h"

#define FILE_LIMIT 8192u
#define GATE 15.0f          /* m: start and finish circles */
#define TOP_SETTLE_MS 3000u /* top speed card once the speed stopped rising */
#define CSV_EVERY_MS 100u
#define SPRINT_MAX 900.0f   /* s */

static unsigned char *game;
static wchar_t book_path[PATH_CAPACITY], csv_dir[PATH_CAPACITY];
static SRWLOCK lock = SRWLOCK_INIT;
static float best[RECORD_COUNT];
static float pending[RECORD_COUNT], before[RECORD_COUNT]; /* new records waiting for their card */
static ULONGLONG top_at;
static int dirty, started;

/* own sprint */
static float pos_now[3];
static int have_pos;
static float gate_start[3], gate_finish[3];
static int sprint_state;   /* 0 none, 1 start, 2 start + finish */
static int run_phase;      /* 0 away, 1 in the start circle, 2 running */
static float run_time;
static volatile LONG run_card; /* 1 started, 2 finished */
static float run_result;

/* csv */
static volatile LONG csv_on;
static HANDLE csv_file = INVALID_HANDLE_VALUE;
static ULONGLONG csv_next, csv_t0;

static float root(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
static int lower_better(unsigned kind) { return kind != RECORD_TOP; }

/* ------------------------------------------------------------------ text helpers */

static void put(char *out, size_t *n, size_t cap, const char *t) {
    while (*t && *n + 1 < cap) out[(*n)++] = *t++;
    out[*n] = 0;
}

static void put_int(char *out, size_t *n, size_t cap, long v) {
    if (v < 0) put(out, n, cap, "-"), v = -v;
    char d[24];
    unsigned k = 0;
    unsigned long u = (unsigned long)v;
    do d[k++] = (char)('0' + u % 10); while ((u /= 10) && k < sizeof(d));
    while (k && *n + 1 < cap) out[(*n)++] = d[--k];
    out[*n] = 0;
}

/* value with digits after the point ("3.21"), German comma when de */
static void put_fixed(char *out, size_t *n, size_t cap, float v, int digits, int de) {
    long scale = digits == 2 ? 100 : digits == 1 ? 10 : 1;
    long h = (long)(v * (float)scale + (v < 0 ? -0.5f : 0.5f));
    if (h < 0) put(out, n, cap, "-"), h = -h;
    put_int(out, n, cap, h / scale);
    if (digits) {
        put(out, n, cap, de ? "," : ".");
        if (digits == 2 && h % scale < 10) put(out, n, cap, "0");
        put_int(out, n, cap, h % scale);
    }
}

static const char *kind_name(unsigned kind, int de) {
    switch (kind) {
        case RECORD_SPRINT: return "0-100 km/h";
        case RECORD_SPRINT200: return "0-200 km/h";
        case RECORD_SPRINT_MPH: return "0-60 mph";
        case RECORD_SPRINT200_MPH: return "0-100 mph";
        case RECORD_QUARTER: return de ? "Viertelmeile" : "Quarter mile";
        case RECORD_BRAKE: return de ? "Bremsweg 100-0" : "Braking 100-0";
        case RECORD_BRAKE_MPH: return de ? "Bremsweg 60-0" : "Braking 60-0";
        case RECORD_TOP: return de ? "Höchsttempo" : "Top speed";
        case RECORD_OWN: return de ? "Eigener Sprint" : "Own sprint";
    }
    return "";
}

static void put_value(char *out, size_t *n, size_t cap, unsigned kind, float v, int de) {
    int imperial = game_units_imperial(game);
    if (kind == RECORD_TOP) {
        put_int(out, n, cap, (long)(v * (imperial ? 2.236936f : 3.6f) + 0.5f));
        put(out, n, cap, imperial ? " mph" : " km/h");
    } else if (kind == RECORD_BRAKE || kind == RECORD_BRAKE_MPH) {
        if (kind == RECORD_BRAKE_MPH) put_int(out, n, cap, (long)(v * 3.28084f + 0.5f)), put(out, n, cap, " ft");
        else put_fixed(out, n, cap, v, 1, de), put(out, n, cap, " m");
    } else {
        put_fixed(out, n, cap, v, 2, de);
        put(out, n, cap, " s");
    }
}

/* ------------------------------------------------------------------ file */

static char file_text[FILE_LIMIT + 1];

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

static long parse_int(const char **p, const char *end) {
    long v = 0;
    int neg = 0;
    while (*p < end && **p == ' ') ++*p;
    if (*p < end && **p == '-') neg = 1, ++*p;
    while (*p < end && **p >= '0' && **p <= '9') v = v * 10 + (*(*p)++ - '0');
    return neg ? -v : v;
}

/* "best <kind> <value * 1000>", "sprint <x y z of start, x y z of finish in cm>" (finish 0 0 0 0 = none) */
static void load(void) {
    HANDLE file = CreateFileW(book_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart <= FILE_LIMIT &&
             ReadFile(file, file_text, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart;
    CloseHandle(file);
    if (!ok) return;
    const char *p = file_text, *end = file_text + got;
    while (p < end) {
        if (end - p >= 5 && memory_equal(p, "best ", 5)) {
            p += 5;
            long k = parse_int(&p, end), v = parse_int(&p, end);
            if (k >= 0 && k < RECORD_COUNT && v > 0) best[k] = (float)v / 1000.0f;
        } else if (end - p >= 7 && memory_equal(p, "sprint ", 7)) {
            p += 7;
            long s = parse_int(&p, end);
            for (int i = 0; i < 3; ++i) gate_start[i] = (float)parse_int(&p, end) / 100.0f;
            for (int i = 0; i < 3; ++i) gate_finish[i] = (float)parse_int(&p, end) / 100.0f;
            sprint_state = s >= 1 && s <= 2 ? (int)s : 0;
        }
        while (p < end && *p != '\n') ++p;
        ++p;
    }
}

static void save(void) {
    static char out[FILE_LIMIT];
    size_t n = 0;
    AcquireSRWLockShared(&lock);
    put(out, &n, sizeof(out), "# Gripline records (value * 1000: s, m, m/s), own sprint (cm)\r\n");
    for (unsigned k = 0; k < RECORD_COUNT; ++k) {
        if (best[k] <= 0.0f) continue;
        put(out, &n, sizeof(out), "best ");
        put_int(out, &n, sizeof(out), (long)k);
        put(out, &n, sizeof(out), " ");
        put_int(out, &n, sizeof(out), (long)(best[k] * 1000.0f + 0.5f));
        put(out, &n, sizeof(out), "\r\n");
    }
    if (sprint_state) {
        put(out, &n, sizeof(out), "sprint ");
        put_int(out, &n, sizeof(out), sprint_state);
        for (int i = 0; i < 3; ++i) put(out, &n, sizeof(out), " "), put_int(out, &n, sizeof(out), (long)(gate_start[i] * 100.0f));
        for (int i = 0; i < 3; ++i) put(out, &n, sizeof(out), " "), put_int(out, &n, sizeof(out), (long)(gate_finish[i] * 100.0f));
        put(out, &n, sizeof(out), "\r\n");
    }
    ReleaseSRWLockShared(&lock);
    if (!write_text(book_path, out, n)) log_append("RECORDS: Gripline_Records.txt could not be written.\r\n");
    dirty = 0;
}

/* ------------------------------------------------------------------ records */

void records_start(unsigned char *game_image, const wchar_t *settings_directory) {
    game = game_image;
    if (!settings_directory || !path_join(book_path, settings_directory, L"Gripline_Records.txt") ||
        !path_join(csv_dir, settings_directory, L"Gripline_Telemetry"))
        return;
    load();
    started = 1;
}

void records_offer(unsigned kind, float value) {
    if (!started || kind >= RECORD_COUNT || !(value > 0.0f)) return;
    AcquireSRWLockExclusive(&lock);
    int better = best[kind] <= 0.0f || (lower_better(kind) ? value < best[kind] - 0.004f : value > best[kind] + 0.3f);
    if (better) {
        if (pending[kind] <= 0.0f) before[kind] = best[kind];
        best[kind] = value;
        pending[kind] = value;
        dirty = 1;
        if (kind == RECORD_TOP) top_at = GetTickCount64();
    }
    ReleaseSRWLockExclusive(&lock);
}

float records_best(unsigned kind) {
    if (kind >= RECORD_COUNT) return 0.0f;
    AcquireSRWLockShared(&lock);
    float v = best[kind];
    ReleaseSRWLockShared(&lock);
    return v;
}

void records_clear(void) {
    AcquireSRWLockExclusive(&lock);
    for (unsigned k = 0; k < RECORD_COUNT; ++k) best[k] = pending[k] = 0.0f;
    dirty = 1;
    ReleaseSRWLockExclusive(&lock);
    log_append("RECORDS: cleared from the menu.\r\n");
}

static void record_card(unsigned kind, float value, float old) {
    int de = game_language_is_german(game);
    char text[128];
    size_t n = 0;
    put(text, &n, sizeof(text), kind_name(kind, de));
    put(text, &n, sizeof(text), ": ");
    put_value(text, &n, sizeof(text), kind, value, de);
    put(text, &n, sizeof(text), "\n");
    if (old > 0.0f) {
        put(text, &n, sizeof(text), de ? "vorher " : "was ");
        put_value(text, &n, sizeof(text), kind, old, de);
    } else {
        put(text, &n, sizeof(text), de ? "erster Rekord" : "first record");
    }
    notify_show_tinted(text, NOTIFY_COLOR_YELLOW, de ? "NEUER REKORD" : "NEW RECORD", 0);
}

/* ------------------------------------------------------------------ own sprint */

static float distance(const float a[3], const float b[3]) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return root(x * x + y * y + z * z);
}

void records_sample(float dt, const float p[3], float speed) {
    if (!(dt > 0.0f) || dt > 0.25f) return;
    AcquireSRWLockExclusive(&lock);
    pos_now[0] = p[0], pos_now[1] = p[1], pos_now[2] = p[2];
    have_pos = 1;
    if (sprint_state == 2) {
        float to_start = distance(p, gate_start), to_finish = distance(p, gate_finish);
        if (run_phase == 2) {
            run_time += dt;
            if (to_finish < GATE && run_time > 3.0f) {
                run_result = run_time;
                run_phase = to_start < GATE ? 1 : 0;
                InterlockedExchange(&run_card, 2);
            } else if (run_time > SPRINT_MAX || speed > 150.0f) {
                run_phase = 0; /* too long, or a teleport */
            }
        } else if (to_start < GATE) {
            run_phase = 1;
        } else if (run_phase == 1 && speed > 1.0f) {
            run_phase = 2; /* out of the start circle: the clock runs */
            run_time = 0.0f;
            InterlockedExchange(&run_card, 1);
        } else {
            run_phase = 0;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

int records_set_start(void) {
    AcquireSRWLockExclusive(&lock);
    int ok = have_pos;
    if (ok) {
        gate_start[0] = pos_now[0], gate_start[1] = pos_now[1], gate_start[2] = pos_now[2];
        if (sprint_state == 0) sprint_state = 1;
        run_phase = 1;
        best[RECORD_OWN] = 0.0f; /* a new track: a new record */
        dirty = 1;
    }
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

int records_set_finish(void) {
    AcquireSRWLockExclusive(&lock);
    int ok = have_pos && sprint_state >= 1 && distance(pos_now, gate_start) > 2.0f * GATE;
    if (ok) {
        gate_finish[0] = pos_now[0], gate_finish[1] = pos_now[1], gate_finish[2] = pos_now[2];
        sprint_state = 2;
        run_phase = 0;
        best[RECORD_OWN] = 0.0f;
        dirty = 1;
    }
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

void records_clear_sprint(void) {
    AcquireSRWLockExclusive(&lock);
    sprint_state = 0;
    run_phase = 0;
    best[RECORD_OWN] = 0.0f;
    dirty = 1;
    ReleaseSRWLockExclusive(&lock);
}

int records_sprint_state(float *metres) {
    AcquireSRWLockShared(&lock);
    int s = sprint_state;
    *metres = s == 2 ? distance(gate_start, gate_finish) : 0.0f;
    ReleaseSRWLockShared(&lock);
    return s;
}

/* ------------------------------------------------------------------ CSV */

typedef struct {
    WORD year, month, weekday, day, hour, minute, second, ms;
} LocalTime;
typedef void(WINAPI *GetLocalTimeFn)(LocalTime *);

static void csv_close(void) {
    if (csv_file != INVALID_HANDLE_VALUE) CloseHandle(csv_file);
    csv_file = INVALID_HANDLE_VALUE;
}

static void two(wchar_t *out, size_t *n, unsigned v) {
    out[(*n)++] = (wchar_t)(L'0' + v / 10 % 10);
    out[(*n)++] = (wchar_t)(L'0' + v % 10);
}

static int csv_open(void) {
    if (!started) return 0;
    CreateDirectoryW(csv_dir, NULL);
    LocalTime lt = {0};
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    GetLocalTimeFn get = k32 ? (GetLocalTimeFn)(void (*)(void))GetProcAddress(k32, "GetLocalTime") : NULL;
    if (get) get(&lt);
    static wchar_t name[64], path[PATH_CAPACITY];
    size_t n = 0;
    for (const wchar_t *s = L"telemetry_"; *s; ++s) name[n++] = *s;
    two(name, &n, lt.year / 100), two(name, &n, lt.year % 100), name[n++] = L'-';
    two(name, &n, lt.month), name[n++] = L'-', two(name, &n, lt.day), name[n++] = L'_';
    two(name, &n, lt.hour), name[n++] = L'-', two(name, &n, lt.minute), name[n++] = L'-', two(name, &n, lt.second);
    for (const wchar_t *s = L".csv"; *s; ++s) name[n++] = *s;
    name[n] = 0;
    if (!path_join(path, csv_dir, name)) return 0;
    csv_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (csv_file == INVALID_HANDLE_VALUE) return 0;
    static const char head[] = "time_s;speed_kmh;long_g;side_g;slip_deg;turn_deg_s;slope_pct;x_m;y_m;z_m\r\n";
    DWORD w;
    WriteFile(csv_file, head, sizeof(head) - 1, &w, NULL);
    csv_t0 = GetTickCount64();
    log_append("RECORDS: CSV recording started in Gripline_Telemetry.\r\n");
    return 1;
}

void records_csv_set(int on) {
    InterlockedExchange(&csv_on, on ? 1 : 0);
}

int records_csv_on(void) { return csv_on != 0; }

void records_csv_line(unsigned long long now, float speed, float long_g, float side_g, float slip, float yaw, float slope) {
    if (!csv_on || now < csv_next) return;
    csv_next = now + CSV_EVERY_MS;
    if (csv_file == INVALID_HANDLE_VALUE && !csv_open()) {
        InterlockedExchange(&csv_on, 0);
        log_append("RECORDS: the CSV file could not be made - recording off.\r\n");
        return;
    }
    char line[192];
    size_t n = 0;
    put_fixed(line, &n, sizeof(line), (float)(now - csv_t0) / 1000.0f, 2, 0);
    put(line, &n, sizeof(line), ";");
    put_fixed(line, &n, sizeof(line), speed * 3.6f, 1, 0);
    put(line, &n, sizeof(line), ";");
    put_fixed(line, &n, sizeof(line), long_g, 2, 0);
    put(line, &n, sizeof(line), ";");
    put_fixed(line, &n, sizeof(line), side_g, 2, 0);
    put(line, &n, sizeof(line), ";");
    put_fixed(line, &n, sizeof(line), slip, 1, 0);
    put(line, &n, sizeof(line), ";");
    put_fixed(line, &n, sizeof(line), yaw, 1, 0);
    put(line, &n, sizeof(line), ";");
    put_fixed(line, &n, sizeof(line), slope, 1, 0);
    AcquireSRWLockShared(&lock);
    float p[3] = {pos_now[0], pos_now[1], pos_now[2]};
    ReleaseSRWLockShared(&lock);
    for (int i = 0; i < 3; ++i) put(line, &n, sizeof(line), ";"), put_fixed(line, &n, sizeof(line), p[i], 1, 0);
    put(line, &n, sizeof(line), "\r\n");
    DWORD w;
    WriteFile(csv_file, line, (DWORD)n, &w, NULL);
}

/* ------------------------------------------------------------------ watch thread */

void records_step(unsigned long long now) {
    if (!started) return;
    if (!csv_on && csv_file != INVALID_HANDLE_VALUE) {
        csv_close();
        log_append("RECORDS: CSV recording stopped.\r\n");
    }
    unsigned show = RECORD_COUNT;
    float value = 0.0f, old = 0.0f;
    AcquireSRWLockExclusive(&lock);
    for (unsigned k = 0; k < RECORD_COUNT && show == RECORD_COUNT; ++k) {
        if (pending[k] <= 0.0f) continue;
        if (k == RECORD_TOP && now - top_at < TOP_SETTLE_MS) continue;
        show = k, value = pending[k], old = before[k];
        pending[k] = 0.0f;
    }
    ReleaseSRWLockExclusive(&lock);
    if (show < RECORD_COUNT) {
        /* the first top speed after the install is no news: every drive would make one */
        if (!(show == RECORD_TOP && old <= 0.0f)) record_card(show, value, old);
    }
    LONG rc = InterlockedExchange(&run_card, 0);
    if (rc) {
        int de = game_language_is_german(game);
        if (rc == 1) {
            notify_show(de ? "Eigener Sprint\nLos! Die Zeit läuft" : "Own sprint\nGo! The clock runs");
        } else {
            float old_best = records_best(RECORD_OWN);
            records_offer(RECORD_OWN, run_result);
            if (old_best > 0.0f && run_result >= old_best - 0.004f) {
                char text[96];
                size_t n = 0;
                put(text, &n, sizeof(text), de ? "Eigener Sprint: " : "Own sprint: ");
                put_value(text, &n, sizeof(text), RECORD_OWN, run_result, de);
                put(text, &n, sizeof(text), de ? "\nRekord " : "\nRecord ");
                put_value(text, &n, sizeof(text), RECORD_OWN, old_best, de);
                notify_show(text);
            } /* else the record card follows */
        }
    }
    static ULONGLONG saved_at;
    if (dirty && now - saved_at > 5000) {
        saved_at = now;
        save();
    }
}
