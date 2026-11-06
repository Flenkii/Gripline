/* Update notice at game start - see update_check.h. Own code. */
#include "gripline.h"
#include "support.h"
#include "logging.h"
#include "update_check.h"
#include "notify.h"
#include "game_language.h"
#include <winhttp.h>

#define RESPONSE_CAPACITY 65536u
#define CARD_CAPACITY 256u

typedef HINTERNET(WINAPI *OpenFn)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL(WINAPI *TimeoutsFn)(HINTERNET, int, int, int, int);
typedef HINTERNET(WINAPI *ConnectFn)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET(WINAPI *RequestFn)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL(WINAPI *SendFn)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI *ReceiveFn)(HINTERNET, LPVOID);
typedef BOOL(WINAPI *QueryFn)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL(WINAPI *ReadFn)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI *CloseFn)(HINTERNET);

static unsigned char *game;
static char response[RESPONSE_CAPACITY];
static char card[CARD_CAPACITY];
/* 0 = asking, 1 = answer ready (card or none), 2 = the start cards were shown before the answer came */
static volatile LONG state;
static int have_card;

/* ------------------------------------------------------------------ text helpers */

static void put(char *out, size_t *n, size_t cap, const char *text) {
    while (*text && *n + 1 < cap) out[(*n)++] = *text++;
    out[*n] = 0;
}

static void put_number(char *out, size_t *n, size_t cap, unsigned v) {
    char digits[12];
    unsigned d = 0;
    do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
    while (d && *n + 1 < cap) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

/* "V7", "V7.1" or "V7.0.1" */
static void put_version(char *out, size_t *n, size_t cap, unsigned major, unsigned minor, unsigned patch) {
    put(out, n, cap, "V");
    put_number(out, n, cap, major);
    if (minor || patch) {
        put(out, n, cap, ".");
        put_number(out, n, cap, minor);
    }
    if (patch) {
        put(out, n, cap, ".");
        put_number(out, n, cap, patch);
    }
}

static const char *find(const char *text, size_t length, const char *what) {
    size_t n = text_length(what);
    for (size_t i = 0; n && i + n <= length; ++i)
        if (memory_equal(text + i, what, n)) return text + i;
    return NULL;
}

/* the string value of "key": "..." in the JSON answer (no escapes needed for tags and names) */
static size_t json_string(const char *json, size_t length, const char *key, char *out, size_t cap) {
    const char *p = find(json, length, key), *end = json + length;
    if (!p) return 0;
    p += text_length(key);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end || *p != '"') return 0;
    ++p;
    size_t n = 0;
    while (p < end && *p != '"' && n + 1 < cap) out[n++] = *p++;
    out[n] = 0;
    return p < end && *p == '"' ? n : 0;
}

/* the numbers after marker, e.g. "fork6.4.1" -> 6, 4, 1 (minor/patch 0 when missing) */
static int version_after(const char *text, const char *marker, unsigned *major, unsigned *minor, unsigned *patch) {
    const char *p = find(text, text_length(text), marker);
    if (!p) return 0;
    p += text_length(marker);
    if (*p < '0' || *p > '9') return 0;
    unsigned part[3] = {0, 0, 0};
    for (unsigned i = 0; i < 3; ++i) {
        while (*p >= '0' && *p <= '9' && part[i] < 100000) part[i] = part[i] * 10 + (unsigned)(*p++ - '0');
        if (i == 2 || *p != '.' || p[1] < '0' || p[1] > '9') break;
        ++p;
    }
    *major = part[0], *minor = part[1], *patch = part[2];
    return 1;
}

/* the card for GitHub's answer: 1 = newer release (card in out), 0 = up to date or not understood */
static int make_card(const char *json, size_t length, int de, char *out, size_t cap) {
    char tag[128], name[256];
    unsigned major = 0, minor = 0, patch = 0;
    out[0] = 0;
    int have_tag = json_string(json, length, "\"tag_name\"", tag, sizeof(tag)) != 0;
    int have_name = json_string(json, length, "\"name\"", name, sizeof(name)) != 0;
    /* tags are v0.2.98-fork6, v0.2.98-fork6.4, ...; the release title "Gripline V6.4 - ..." as fallback */
    if (!(have_tag && version_after(tag, "fork", &major, &minor, &patch)) &&
        !(have_name && version_after(name, "Gripline V", &major, &minor, &patch))) {
        log_append("UPDATE: newest release not understood - no card.\r\n");
        return 0;
    }
    /* V6.4.1 was a real release: the third number counts too (V7.0.1 is newer than V7) */
    if (major != GRIPLINE_RELEASE_MAJOR ? major < GRIPLINE_RELEASE_MAJOR
        : minor != GRIPLINE_RELEASE_MINOR ? minor < GRIPLINE_RELEASE_MINOR
                                          : patch <= GRIPLINE_RELEASE_PATCH) {
        log_append("UPDATE: this Gripline is the newest version.\r\n");
        return 0;
    }
    size_t n = 0;
    put(out, &n, cap, "Gripline ");
    put_version(out, &n, cap, major, minor, patch);
    put(out, &n, cap, de ? " ist draußen\nBitte nutzen, " : " is out\nPlease use it, ");
    put_version(out, &n, cap, GRIPLINE_RELEASE_MAJOR, GRIPLINE_RELEASE_MINOR, GRIPLINE_RELEASE_PATCH);
    put(out, &n, cap, de ? " ist veraltet" : " is outdated");
    log_append("UPDATE: ");
    log_append(out);
    log_append(" - github.com/Flenkii/Gripline/releases\r\n");
    return 1;
}

/* ------------------------------------------------------------------ GitHub request */

/* GitHub's answer for the newest release into response; its length, 0 = no answer */
static size_t ask_github(void) {
    HMODULE winhttp = LoadLibraryExW(L"winhttp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!winhttp) {
        log_append("UPDATE: winhttp.dll not available - no update check.\r\n");
        return 0;
    }
    OpenFn open = (OpenFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpOpen");
    TimeoutsFn timeouts = (TimeoutsFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpSetTimeouts");
    ConnectFn connect = (ConnectFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpConnect");
    RequestFn request_open = (RequestFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpOpenRequest");
    SendFn send = (SendFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpSendRequest");
    ReceiveFn receive = (ReceiveFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpReceiveResponse");
    QueryFn query = (QueryFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpQueryHeaders");
    ReadFn read = (ReadFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpReadData");
    CloseFn close = (CloseFn)(uintptr_t)GetProcAddress(winhttp, "WinHttpCloseHandle");
    size_t got = 0;
    if (!open || !timeouts || !connect || !request_open || !send || !receive || !query || !read || !close) return 0;
    /* automatic proxy (Windows 8.1+), else the system default */
    HINTERNET session = open(L"Gripline-UpdateCheck", 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */, NULL, NULL, 0);
    if (!session) session = open(L"Gripline-UpdateCheck", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    HINTERNET connection = NULL, request = NULL;
    if (session) {
        timeouts(session, 4000, 4000, 4000, 5000); /* resolve, connect, send, receive (ms) */
        connection = connect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    }
    if (connection)
        request = request_open(connection, L"GET", L"/repos/Flenkii/Gripline/releases/latest", NULL, NULL, NULL,
                               WINHTTP_FLAG_SECURE);
    DWORD status = 0, status_size = sizeof(status);
    if (request &&
        send(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) &&
        receive(request, NULL) &&
        query(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &status_size, NULL) &&
        status == 200) {
        for (;;) {
            DWORD part = 0;
            if (got + 1 >= sizeof(response) || !read(request, response + got, (DWORD)(sizeof(response) - 1 - got), &part) ||
                !part)
                break;
            got += part;
        }
    } else {
        log_append(status ? "UPDATE: GitHub did not answer with the release (HTTP status not 200) - no card.\r\n"
                          : "UPDATE: GitHub not reachable (offline or blocked) - no card.\r\n");
    }
    response[got] = 0;
    if (request) close(request);
    if (connection) close(connection);
    if (session) close(session);
    /* winhttp.dll stays loaded (no FreeLibrary import, same imports as before) */
    return got;
}

static void show(void) {
    if (have_card) notify_show_tinted(card, NOTIFY_COLOR_ORANGE, "UPDATE", 1);
}

static DWORD WINAPI check_thread(LPVOID unused) {
    (void)unused;
    size_t length = ask_github();
    have_card = length && make_card(response, length, game_language_is_german(game), card, sizeof(card));
    /* the start cards already went: show it now; else the deferred call below shows it with them */
    if (InterlockedCompareExchange(&state, 1, 0) == 2) show();
    return 0;
}

/* with the other start cards; when GitHub has not answered yet, the thread shows it later */
static int start_card(char *out, unsigned size) {
    (void)out, (void)size;
    if (InterlockedCompareExchange(&state, 2, 0) == 1) show();
    return 0;
}

void update_check_start(unsigned char *game_image, int enabled) {
    game = game_image;
    if (!enabled) {
        log_append("UPDATE: update check off (Gripline.ini UpdateCheck=0).\r\n");
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (!thread) return;
    CloseHandle(thread);
    notify_show_deferred_call(start_card);
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_update_check.py: the card for a GitHub answer */
__declspec(dllexport) int Fh2TestUpdateCard(const char *json, int german, char *out, unsigned size) {
    return make_card(json, text_length(json), german, out, size);
}
#endif
