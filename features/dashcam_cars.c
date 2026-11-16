/* Dashboard Cam per car - see dashcam_cars.h. Own code. */
#include "gripline.h"
#include "dashcam_cars.h"
#include "logbook.h"
#include "logging.h"
#include "support.h"
#include "ini_edit.h"
#include "rig_settings.h"

#define STEM_CAP 40
#define DC_FILE_LIMIT 16384u
#define CHECK_MS 500u

static wchar_t ini_path[PATH_CAPACITY];
static char car_now[STEM_CAP];      /* the car the values belong to ("" = none known yet) */
static volatile LONG own_values;    /* the car has its own line */
static unsigned long long next_check;
static char text[DC_FILE_LIMIT + 1], edited[DC_FILE_LIMIT + 1];
static SRWLOCK lock = SRWLOCK_INIT;

void dashcam_cars_start(const wchar_t *settings_directory) {
    ini_path[0] = 0;
    if (settings_directory) path_join(ini_path, settings_directory, L"Gripline.ini");
}

static int same(const char *a, const char *b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

/* "40 -10" -> 1 */
static int parse_pair(const wchar_t *v, int *forward, int *up) {
    int values[2], k = 0;
    while (k < 2) {
        while (*v == L' ' || *v == L'\t' || *v == L',') ++v;
        int sign = 1, any = 0, n = 0;
        if (*v == L'-') sign = -1, ++v;
        while (*v >= L'0' && *v <= L'9' && n < 10000) n = n * 10 + (*v++ - L'0'), any = 1;
        if (!any) return 0;
        values[k++] = sign * n;
    }
    *forward = values[0], *up = values[1];
    return 1;
}

static int read_car(const char *stem, int *forward, int *up) {
    if (!ini_path[0] || !stem[0]) return 0;
    wchar_t key[STEM_CAP], value[32];
    unsigned i = 0;
    for (; stem[i] && i + 1 < STEM_CAP; ++i) key[i] = (wchar_t)(unsigned char)stem[i];
    key[i] = 0;
    GetPrivateProfileStringW(L"DashCamCars", key, L"", value, 32, ini_path);
    return parse_pair(value, forward, up);
}

static void go_live(const char *stem) {
    int forward, up, own = read_car(stem, &forward, &up);
    if (!own) rig_settings_view_general(&forward, &up);
    rig_settings_view_live(forward, up);
    InterlockedExchange(&own_values, own);
    log_append(own ? "DASHCAM: this car has its own Dashboard Cam place: " : "DASHCAM: this car uses the general Dashboard Cam place: ");
    log_append(stem);
    log_append("\r\n");
}

void dashcam_cars_step(unsigned long long now) {
    if (now < next_check) return;
    next_check = now + CHECK_MS;
    char stem[STEM_CAP];
    if (!logbook_current_stem(stem, sizeof(stem))) return;
    AcquireSRWLockExclusive(&lock);
    if (!same(stem, car_now)) {
        unsigned i = 0;
        for (; stem[i] && i + 1 < STEM_CAP; ++i) car_now[i] = stem[i];
        car_now[i] = 0;
        go_live(car_now);
    }
    ReleaseSRWLockExclusive(&lock);
}

static int read_ini(size_t *length) {
    *length = 0;
    text[0] = 0;
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    LARGE_INTEGER size;
    DWORD got = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart <= DC_FILE_LIMIT &&
             ReadFile(file, text, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart;
    CloseHandle(file);
    if (!ok) return 0;
    text[got] = 0;
    *length = got;
    return 1;
}

static int write_ini(const char *data, size_t length) {
    static wchar_t temp[PATH_CAPACITY + 8];
    unsigned n = 0;
    while (ini_path[n] && n < PATH_CAPACITY - 1) temp[n] = ini_path[n], ++n;
    const wchar_t *ext = L".gltmp";
    while (*ext) temp[n++] = *ext++;
    temp[n] = 0;
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(file, data, (DWORD)length, &written, NULL) && written == length && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp, ini_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp);
    return ok;
}

int dashcam_cars_take(int forward_cm, int up_cm) {
    AcquireSRWLockExclusive(&lock);
    int taken = 0;
    if (car_now[0] && ini_path[0]) {
        char value[32], number[12];
        unsigned n = 0;
        for (const char *t = ini_format_int(forward_cm, number); *t; ++t) value[n++] = *t;
        value[n++] = ' ';
        for (const char *t = ini_format_int(up_cm, number); *t; ++t) value[n++] = *t;
        value[n] = 0;
        settings_file_lock();
        size_t length;
        if (read_ini(&length)) {
            size_t written = ini_set_value(text, length, "DashCamCars", car_now, value, edited, sizeof(edited));
            taken = written && written <= DC_FILE_LIMIT && write_ini(edited, written);
        }
        settings_file_unlock();
        if (taken) {
            InterlockedExchange(&own_values, 1);
            log_append("DASHCAM: Dashboard Cam place kept for ");
            log_append(car_now);
            log_append("\r\n");
        } else {
            log_append("DASHCAM: Gripline.ini could not be written - the place counts for all cars.\r\n");
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return taken;
}

int dashcam_cars_own(void) {
    return (int)own_values;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestDashcamCar(const char *stem) {
    AcquireSRWLockExclusive(&lock);
    unsigned i = 0;
    for (; stem && stem[i] && i + 1 < STEM_CAP; ++i) car_now[i] = stem[i];
    car_now[i] = 0;
    if (car_now[0]) go_live(car_now);
    ReleaseSRWLockExclusive(&lock);
}
__declspec(dllexport) void Fh2TestDashcamStart(const wchar_t *directory) {
    dashcam_cars_start(directory);
}
#endif
