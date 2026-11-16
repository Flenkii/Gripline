/* Re-sends "window active" to Azimuth's game window - see focus_guard.h. Own code. */
#include "gripline.h"
#include "focus_guard.h"
#include "keyboard_pad.h"
#include "logging.h"

#define WM_ACTIVATE_ 0x0006u
#define WA_ACTIVE_ 1u
#define TICK_MS 250u
#define SETTLE_MS 1500u /* in front this long before the "active" goes out */
#define LOG_LIMIT 8

#ifndef FH2_TWEAKS_TEST /* the test build has no game window */
typedef HWND(WINAPI *ForegroundFn)(void);
typedef DWORD(WINAPI *WindowPidFn)(HWND, DWORD *);
typedef int(WINAPI *ClassNameFn)(HWND, LPWSTR, int);
typedef BOOL(WINAPI *PostFn)(HWND, UINT, WPARAM, LPARAM);

static ForegroundFn foreground;
static WindowPidFn window_pid;
static ClassNameFn class_name;
static PostFn post;

static int load_user32(void) {
    HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!user32) return 0;
    foreground = (ForegroundFn)(uintptr_t)GetProcAddress(user32, "GetForegroundWindow");
    window_pid = (WindowPidFn)(uintptr_t)GetProcAddress(user32, "GetWindowThreadProcessId");
    class_name = (ClassNameFn)(uintptr_t)GetProcAddress(user32, "GetClassNameW");
    post = (PostFn)(uintptr_t)GetProcAddress(user32, "PostMessageW");
    return foreground && window_pid && class_name && post;
}

static int same_text(const wchar_t *a, const wchar_t *b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

/* Azimuth's game window when it is the foreground window, else NULL */
static HWND game_window_in_front(void) {
    HWND window = foreground();
    DWORD pid = 0;
    if (!window || !window_pid(window, &pid) || pid != GetCurrentProcessId()) return NULL;
    wchar_t name[64];
    int n = class_name(window, name, 64);
    if (n <= 0 || n >= 64) return NULL;
    name[n] = 0;
    return same_text(name, L"Azimuth.Game.Win32") ? window : NULL;
}

static int sent_logs;

static void send_active(HWND window, const char *why) {
    if (!post(window, WM_ACTIVATE_, WA_ACTIVE_, 0)) return;
    if (sent_logs < LOG_LIMIT) {
        ++sent_logs;
        log_append(why);
    }
}

static DWORD WINAPI guard_thread(void *unused) {
    (void)unused;
    HWND last = NULL;
    ULONGLONG since = 0;
    int sent_for_edge = 0, world_seen = 0;
    for (;;) {
        Sleep(TICK_MS);
        ULONGLONG now = GetTickCount64();
        HWND window = game_window_in_front();
        if (window != last) { /* came to the front (or another window): wait until it settles */
            last = window;
            since = now;
            sent_for_edge = 0;
        }
        if (!window) continue;
        if (!sent_for_edge && now - since >= SETTLE_MS) {
            sent_for_edge = 1;
            send_active(window, "FOCUS: game window in front - 'window active' sent to the game.\r\n");
        }
        if (!world_seen && game_simulation_running()) { /* the world is loaded: once more, after Azimuth's start */
            world_seen = 1;
            send_active(window, "FOCUS: world loaded - 'window active' sent to the game.\r\n");
        }
    }
}

#endif

void focus_guard_start(void) {
    static int started;
    if (started) return;
    started = 1;
#ifndef FH2_TWEAKS_TEST
    if (!load_user32()) {
        log_append("FOCUS: user32 functions missing - focus guard off.\r\n");
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, guard_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
        log_append("FOCUS: guard ready - the game gets 'window active' again when Azimuth missed it at the start.\r\n");
    } else {
        log_append("FOCUS: no thread - focus guard off.\r\n");
    }
#endif
}
