/* Wheel plugged in / unplugged - see wheel_watch.h. Own code. */
#include "gripline.h"
#include "support.h"
#include "logging.h"
#include "wheel_watch.h"
#include "notify.h"
#include "game_language.h"

#define EVERY_MS 2000u
#define MAX_DEVICES 64

typedef struct {
    unsigned short vendor, product;
    const char *name;
} Wheel;

/* product 0 = every product of that vendor (they only make wheels and pedals) */
static const Wheel wheels[] = {
    {0x046d, 0xc262, "Logitech G920"},    {0x046d, 0xc261, "Logitech G920"},
    {0x046d, 0xc24f, "Logitech G29"},     {0x046d, 0xc260, "Logitech G29"},
    {0x046d, 0xc266, "Logitech G923"},    {0x046d, 0xc267, "Logitech G923"},
    {0x046d, 0xc26d, "Logitech G923"},    {0x046d, 0xc26e, "Logitech G923"},
    {0x046d, 0xc29b, "Logitech G27"},     {0x046d, 0xc299, "Logitech G25"},
    {0x046d, 0xc29a, "Logitech Driving Force GT"}, {0x046d, 0xc272, "Logitech G PRO Racing Wheel"},
    {0x046d, 0xc268, "Logitech G PRO Racing Wheel"},
    {0x044f, 0, "Thrustmaster"},          {0x0eb7, 0, "Fanatec"},
    {0x346e, 0, "MOZA"},                  {0x3416, 0, "Cammus"},
    {0x16d0, 0x0d5a, "Simucube"},         {0x2433, 0, "Asetek"},
};

typedef UINT(WINAPI *GetRawInputDeviceListFn)(PRAWINPUTDEVICELIST, PUINT, UINT);
typedef UINT(WINAPI *GetRawInputDeviceInfoWFn)(HANDLE, UINT, LPVOID, PUINT);

static GetRawInputDeviceListFn list_devices;
static GetRawInputDeviceInfoWFn device_info;
static int loaded, primed;
static ULONGLONG next_check;
static unsigned char present[ARRAY_COUNT(wheels)];

static int load(void) {
    if (loaded) return list_devices && device_info;
    loaded = 1;
    HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!user32) return 0;
    list_devices = (GetRawInputDeviceListFn)(uintptr_t)GetProcAddress(user32, "GetRawInputDeviceList");
    device_info = (GetRawInputDeviceInfoWFn)(uintptr_t)GetProcAddress(user32, "GetRawInputDeviceInfoW");
    return list_devices && device_info;
}

static int wheel_of(unsigned vendor, unsigned product) {
    for (unsigned i = 0; i < ARRAY_COUNT(wheels); ++i)
        if (wheels[i].vendor == vendor && (!wheels[i].product || wheels[i].product == product)) return (int)i;
    return -1;
}

/* the same wheel can be listed under two ids (modes): one name, one card */
static int same_name(unsigned a, unsigned b) {
    const char *x = wheels[a].name, *y = wheels[b].name;
    while (*x && *x == *y) ++x, ++y;
    return *x == *y;
}

static void card(unsigned char *game, unsigned index, int plugged) {
    int de = game_language_is_german(game);
    char text[96];
    size_t n = 0;
    const char *first = plugged ? (de ? "Lenkrad erkannt\n" : "Wheel connected\n")
                                : (de ? "Lenkrad getrennt\n" : "Wheel disconnected\n");
    for (const char *p = first; *p; ++p) text[n++] = *p;
    for (const char *p = wheels[index].name; *p && n + 1 < sizeof(text); ++p) text[n++] = *p;
    text[n] = 0;
    if (plugged) notify_show_tinted(text, NOTIFY_COLOR_GREEN, NULL, 0);
    else notify_show_tinted(text, NOTIFY_COLOR_RED, de ? "WARNUNG" : "WARNING", 0);
}

static void check(unsigned char *game) {
    static RAWINPUTDEVICELIST devices[MAX_DEVICES];
    UINT count = MAX_DEVICES;
    UINT got = list_devices(devices, &count, sizeof(RAWINPUTDEVICELIST));
    if (got == (UINT)-1) return; /* more than MAX_DEVICES or an error: try again later */
    unsigned char now[ARRAY_COUNT(wheels)] = {0};
    for (UINT i = 0; i < got; ++i) {
        if (devices[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info;
        UINT size = sizeof(info);
        info.cbSize = sizeof(info);
        if (device_info(devices[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1) continue;
        /* generic desktop joystick / gamepad / multi-axis: the wheel itself, not its other HID parts */
        if (info.hid.usUsagePage != 1 || (info.hid.usUsage != 4 && info.hid.usUsage != 5 && info.hid.usUsage != 8))
            continue;
        int w = wheel_of(info.hid.dwVendorId, info.hid.dwProductId);
        if (w >= 0) now[w] = 1;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(wheels); ++i) {
        int before = 0, after = 0, first = 1;
        for (unsigned j = 0; j < ARRAY_COUNT(wheels); ++j) {
            if (!same_name(i, j)) continue;
            if (j < i) first = 0;
            before |= present[j];
            after |= now[j];
        }
        if (first && primed && before != after) {
            log_append(after ? "WHEEL WATCH: wheel connected.\r\n" : "WHEEL WATCH: wheel disconnected.\r\n");
            card(game, i, after);
        }
    }
    memory_copy(present, now, sizeof(present));
    primed = 1;
}

void wheel_watch_step(unsigned char *game, unsigned long long now) {
    if (now < next_check) return;
    next_check = now + EVERY_MS;
    if (!game || !load()) return;
    check(game);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestWheelOf(unsigned vendor, unsigned product) {
    return wheel_of(vendor, product);
}
#endif
