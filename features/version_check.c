/* Start check of Azimuth / Extra Options / FH2MP - see version_check.h. Own code. */
#include "gripline.h"
#include "support.h"
#include "logging.h"
#include "version_check.h"
#include "notify.h"
#include "game_language.h"

/* the newest Azimuth Gripline was tested with: 1.0.4 */
#define TESTED_MAJOR 1
#define TESTED_MINOR 0
#define TESTED_PATCH 4
#define LOG_TAIL 65536u

static unsigned char *game;
static wchar_t eo_log[PATH_CAPACITY], platform_log_path[PATH_CAPACITY];
static unsigned azimuth[3];
static int have_azimuth;

/* VS_FIXEDFILEINFO of the process image (azimuth.exe): searched in its mapped resource directory,
   so no resource functions are imported */
static int read_azimuth_version(void) {
    const unsigned char *exe = (const unsigned char *)GetModuleHandleW(NULL);
    DWORD pe, rva = 0, size = 0;
    if (!exe || !memory_read(exe + 0x3c, &pe, 4) || !memory_read(exe + pe + 24 + 112 + 2 * 8, &rva, 4) ||
        !memory_read(exe + pe + 24 + 112 + 2 * 8 + 4, &size, 4) || !rva || !size)
        return 0;
    const unsigned char *data = exe + rva;
    for (DWORD i = 0; i + 16 <= size; i += 4) {
        DWORD signature;
        if (!memory_read(data + i, &signature, 4)) return 0;
        if (signature != 0xfeef04bd) continue;
        DWORD ms, ls;
        if (!memory_read(data + i + 8, &ms, 4) || !memory_read(data + i + 12, &ls, 4)) return 0;
        azimuth[0] = ms >> 16, azimuth[1] = ms & 0xffff, azimuth[2] = ls >> 16;
        return 1;
    }
    return 0;
}

static int newer_than_tested(void) {
    static const unsigned tested[3] = {TESTED_MAJOR, TESTED_MINOR, TESTED_PATCH};
    for (unsigned i = 0; i < 3; ++i)
        if (azimuth[i] != tested[i]) return azimuth[i] > tested[i];
    return 0;
}

/* the end of a log file, as text */
static size_t read_tail(const wchar_t *path, char *out, size_t capacity) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size, start;
    DWORD got = 0;
    if (GetFileSizeEx(file, &size)) {
        start.QuadPart = size.QuadPart > (LONGLONG)(capacity - 1) ? size.QuadPart - (LONGLONG)(capacity - 1) : 0;
        if (SetFilePointerEx(file, start, NULL, FILE_BEGIN) && !ReadFile(file, out, (DWORD)(capacity - 1), &got, NULL))
            got = 0;
    }
    CloseHandle(file);
    out[got] = 0;
    return got;
}

static int contains(const char *text, size_t length, const char *what) {
    size_t n = text_length(what);
    for (size_t i = 0; n && i + n <= length; ++i)
        if (memory_equal(text + i, what, n)) return 1;
    return 0;
}

static void put(char *out, size_t *n, size_t cap, const char *text) {
    while (*text && *n + 1 < cap) out[(*n)++] = *text++;
    out[*n] = 0;
}

static void put_version(char *out, size_t *n, size_t cap) {
    for (unsigned i = 0; i < 3; ++i) {
        char digits[8];
        unsigned d = 0, v = azimuth[i];
        do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
        while (d && *n + 1 < cap) out[(*n)++] = digits[--d];
        if (i < 2 && *n + 1 < cap) out[(*n)++] = '.';
    }
    out[*n] = 0;
}

static char log_text[LOG_TAIL];

enum { FIT_OK, FIT_EO_AZIMUTH, FIT_EO_OFF, FIT_FH2MP, FIT_AZIMUTH_NEW };

static int find_problem(void) {
    size_t length = read_tail(eo_log, log_text, sizeof(log_text));
    if (contains(log_text, length, "REFUSED: unsupported Azimuth build")) return FIT_EO_AZIMUTH;
    if (contains(log_text, length, "REFUSED:")) return FIT_EO_OFF;
    length = read_tail(platform_log_path, log_text, sizeof(log_text));
    if (contains(log_text, length, "XboxUserId constant NOT found") ||
        contains(log_text, length, "not found in this azimuth.exe"))
        return FIT_FH2MP;
    if (have_azimuth && newer_than_tested()) return FIT_AZIMUTH_NEW;
    return FIT_OK;
}

static int make_card(char *out, unsigned size) {
    int problem = find_problem();
    if (problem == FIT_OK) {
        log_append("VERSIONS: Azimuth, Extra Options and FH2MP fit together.\r\n");
        return 0;
    }
    int de = game_language_is_german(game);
    size_t n = 0;
    out[0] = 0;
    switch (problem) {
        case FIT_EO_AZIMUTH:
            put(out, &n, size, de ? "Extra Options passt nicht zu Azimuth " : "Extra Options does not fit Azimuth ");
            put_version(out, &n, size);
            put(out, &n, size, de ? "\nExtra Options aktualisieren" : "\nUpdate Extra Options");
            break;
        case FIT_EO_OFF:
            put(out, &n, size, de ? "Extra Options ist aus\nGrund: FH2_ExtraOptions.log (REFUSED)"
                                  : "Extra Options is off\nReason: FH2_ExtraOptions.log (REFUSED)");
            break;
        case FIT_FH2MP:
            put(out, &n, size, de ? "FH2MP passt nicht zu Azimuth " : "FH2MP does not fit Azimuth ");
            put_version(out, &n, size);
            put(out, &n, size, de ? "\nAuf ein FH2MP-Update warten" : "\nWait for an FH2MP update");
            break;
        default:
            put(out, &n, size, "Azimuth ");
            put_version(out, &n, size);
            put(out, &n, size, de ? " ist neu\nGripline ist damit noch nicht getestet" : " is new\nGripline was not tested with it yet");
            break;
    }
    log_append("VERSIONS: ");
    log_append(out);
    log_append("\r\n");
    return 1;
}

/* the start check's card: red, with WARNUNG / WARNING in the title */
static int show_card(char *out, unsigned size) {
    if (make_card(out, size))
        notify_show_tinted(out, NOTIFY_COLOR_RED, game_language_is_german(game) ? "WARNUNG" : "WARNING", 1);
    return 0;
}

void version_check_start(unsigned char *game_image, const wchar_t *directory) {
    game = game_image;
    if (!directory || !path_join(eo_log, directory, L"FH2_ExtraOptions.log") ||
        !path_join(platform_log_path, directory, L"fh2-mods\\platform.log"))
        return;
    have_azimuth = read_azimuth_version();
    notify_show_deferred_call(show_card);
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_version_check.py: the card for the logs in directory and a given Azimuth version */
__declspec(dllexport) int Fh2TestVersionCard(unsigned char *game_image, const wchar_t *directory, unsigned major, unsigned minor, unsigned patch,
                                             char *out, unsigned size) {
    game = game_image;
    path_join(eo_log, directory, L"FH2_ExtraOptions.log");
    path_join(platform_log_path, directory, L"fh2-mods\\platform.log");
    azimuth[0] = major, azimuth[1] = minor, azimuth[2] = patch;
    have_azimuth = 1;
    return make_card(out, size);
}
#endif
