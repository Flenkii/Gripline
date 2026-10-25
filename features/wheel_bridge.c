/* compat4: Lenkrad-Bruecke (G920 & Co.) - unofficial add-on.
 *
 * FH2 ships with native support for the Xbox One racing wheel ("Bristol"):
 * the game asks Windows.Xbox.Input.Controller for its controllers and, when a
 * controller answers QueryInterface(IWheel), drives the car with the wheel
 * reading and its own wheel layouts/deadzones. Azimuth only exposes gamepads,
 * so a PC wheel never reaches the game.
 *
 * This module hooks the game's import of GetActivationFactoryByPCWSTR. When the
 * game asks for the Controller statics, the returned object is wrapped and
 * get_Controllers() gets one extra controller. That controller aggregates the
 * game's own Microsoft.Xbox.Input.Wheel.dll (mapped here by a small loader), so
 * every IWheel call is answered by Microsoft's original code. Game copies without
 * that DLL get the mod's own implementation of the class instead (wheel_soft.c). The raw device
 * reports that the console's input stack would deliver are synthesised from a
 * DirectInput wheel (G920, G29, G923, ...).
 *
 * Nothing on disk is modified. If no wheel is found, the game is left alone. */
#include "logging.h"
#include "ffb_vm.h"
#include "wheel_soft.h"
#include "ini_migrate.h"
#include "rig_settings.h"
#include "wheel_buttons.h"
#include "mod_scan.h"
#include "notify.h"
#include "ini_edit.h"
#include "support.h"

#define DIRECTINPUT_VERSION 0x0800
#include <objbase.h>
#include <dinput.h>

#define IAT_GET_FACTORY 0x1dd1330u
#define WHEEL_ID 0x000046dc26200920ull
#define WHEEL_DLL L"Microsoft.Xbox.Input.Wheel.dll"
#define WHEEL_INI L"FH2_Wheel.ini"
#define WHEEL_LOG L"FH2_Wheel.log"

typedef HRESULT(WINAPI *GetFactoryFn)(const wchar_t *, const GUID *, void **);
typedef HRESULT(WINAPI *QIFn)(void *, const GUID *, void **);
typedef ULONG(WINAPI *RefFn)(void *);
typedef HRESULT(WINAPI *Fn1)(void *, void *);
typedef HRESULT(WINAPI *Fn2)(void *, void *, void *);
typedef HRESULT(WINAPI *Fn3)(void *, void *, void *, void *);
typedef HRESULT(WINAPI *GetAtFn)(void *, UINT32, void **);
typedef HRESULT(WINAPI *SizeFn)(void *, UINT32 *);
typedef HRESULT(WINAPI *IndexOfFn)(void *, void *, UINT32 *, BOOLEAN *);
typedef HRESULT(WINAPI *GetManyFn)(void *, UINT32, UINT32, void **, UINT32 *);
typedef HRESULT(WINAPI *CreateInstanceFn)(void *, UINT64, void *, void **);
typedef HRESULT(WINAPI *OnInputFn)(void *, UINT32, const unsigned char *);
typedef HRESULT(WINAPI *DllGetFactoryFn)(void *, void **);
typedef HRESULT(WINAPI *CreateStringFn)(const wchar_t *, UINT32, void **);
typedef HRESULT(WINAPI *CoCreateFn)(const GUID *, void *, DWORD, const GUID *, void **);
typedef BOOL(WINAPI *DllMainFn)(HINSTANCE, DWORD, LPVOID);
typedef BOOLEAN(WINAPI *AddTableFn)(void *, DWORD, DWORD64);
typedef HRESULT(WINAPI *DI8CreateFn)(HINSTANCE, DWORD, const GUID *, void **, void *);
typedef void *(__cdecl *MallocFn)(size_t);
typedef void(__cdecl *FreeFn)(void *);

#define VT(object) (*(void ***)(object))

static const GUID IID_ControllerStatics = {0x7400624f, 0x144b, 0x47fc, {0x9f, 0x3b, 0x1d, 0xca, 0xfb, 0x48, 0x28, 0x73}};
static const GUID IID_UserStatics = {0xb4f23a93, 0x7fc6, 0x4a50, {0xb1, 0x53, 0x95, 0x00, 0x16, 0x38, 0x9e, 0x50}};
static const GUID IID_Controller = {0x9776ff56, 0x9bfd, 0x4581, {0xad, 0x45, 0xb6, 0x45, 0xbb, 0xa5, 0x26, 0xd6}};
static const GUID IID_VectorViewController = {0xd2e6422e, 0xa5c0, 0x56e0, {0xb2, 0xee, 0x55, 0x27, 0x39, 0xc2, 0xe0, 0xbc}};
static const GUID IID_Unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_Inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_Agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID IID_ControllerFactory = {0xbcba6051, 0x7e1a, 0x4e73, {0x83, 0x49, 0xc4, 0xb8, 0xa8, 0xb1, 0x33, 0x5d}};
static const GUID IID_InputHandler = {0x1c529a33, 0x97f3, 0x4a0f, {0x86, 0x6a, 0x87, 0xb1, 0xdb, 0x9c, 0xc1, 0x78}};
static const GUID IID_DeviceInfo = {0x568e38dc, 0xe9c8, 0x4f00, {0x92, 0x3c, 0x09, 0x01, 0x86, 0xde, 0x11, 0x44}};
static const GUID CLSID_GipSender = {0x9d25d1fc, 0x8da2, 0x455c, {0x94, 0xf1, 0xaa, 0x28, 0x02, 0x94, 0xb8, 0x13}};
static const GUID IID_GipSender = {0x495e5b96, 0x40b0, 0x4e92, {0x88, 0x3e, 0xfb, 0xa1, 0x45, 0x30, 0x03, 0xed}};
static const GUID IID_Wheel = {0x646979cf, 0x6b71, 0x4e96, {0x8d, 0xf9, 0x59, 0xe3, 0x98, 0xd7, 0x42, 0x0c}};
static const GUID IID_Gamepad = {0x082e402c, 0x07df, 0x45e1, {0xa5, 0xab, 0xa3, 0x12, 0x7a, 0xf1, 0x97, 0xb5}};
static const GUID IID_NavController = {0xb8f31fe7, 0x7386, 0x40e9, {0xa9, 0xf8, 0x2f, 0x21, 0x26, 0x3a, 0xcf, 0xb7}};
static const GUID IID_DI8W = {0xbf798031, 0x483a, 0x4da2, {0xaa, 0x99, 0x5d, 0x64, 0xed, 0x36, 0x97, 0x00}};
static const GUID G_XAxis = {0xa36d02e0, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_YAxis = {0xa36d02e1, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_ZAxis = {0xa36d02e2, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_RxAxis = {0xa36d02f4, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_RyAxis = {0xa36d02f5, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_RzAxis = {0xa36d02e3, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_Slider = {0xa36d02e4, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID G_POV = {0xa36d02f2, 0xc9f3, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

/* ------------------------------------------------------------------ helpers */

void *memset(void *destination, int value, size_t size);
void *memcpy(void *destination, const void *source, size_t size);
__attribute__((used)) void *memset(void *destination, int value, size_t size) {
    volatile unsigned char *d = destination;
    while (size--) {
        *d++ = (unsigned char)value;
    }
    return destination;
}
__attribute__((used)) void *memcpy(void *destination, const void *source, size_t size) {
    volatile unsigned char *d = destination;
    const unsigned char *s = source;
    while (size--) {
        *d++ = *s++;
    }
    return destination;
}

static void zero(void *p, size_t n) {
    memset(p, 0, n);
}

static int guid_eq(const GUID *a, const GUID *b) {
    return a && b && memory_equal(a, b, sizeof(GUID));
}

static int wide_eq(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *a == *b) {
        ++a, ++b;
    }
    return *a == *b;
}

static int ascii_eq_ci(const char *a, const char *b) {
    return text_equal_ignore_case(a, b);
}

static int wide_contains_ci(const wchar_t *hay, const wchar_t *needle) {
    if (!needle || !needle[0]) {
        return 0;
    }
    for (; *hay; ++hay) {
        const wchar_t *h = hay, *n = needle;
        while (*h && *n) {
            wchar_t a = *h, b = *n;
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) break;
            ++h, ++n;
        }
        if (!*n) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ logging */

static wchar_t log_path[PATH_CAPACITY], ini_path[PATH_CAPACITY], dll_path[PATH_CAPACITY];
static SRWLOCK log_lock = SRWLOCK_INIT;
static int log_enabled = 1;

static void fmt_put(char **o, char *end, char c) {
    if (*o < end) {
        *(*o)++ = c;
    }
}

static void fmt_uint(char **o, char *end, unsigned long long v, unsigned base, int width) {
    char tmp[24];
    int n = 0;
    do {
        unsigned d = (unsigned)(v % base);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v && n < 24);
    while (n < width && n < 24) {
        tmp[n++] = '0';
    }
    while (n) {
        fmt_put(o, end, tmp[--n]);
    }
}

/* %s %S(wide) %d %u %x %X(width 8) %p %c %f(3 decimals) */
static void wlog(const char *format, ...) {
    if (!log_enabled || !log_path[0]) {
        return;
    }
    char buffer[1024];
    char *o = buffer, *end = buffer + sizeof(buffer) - 2;
    __builtin_va_list args;
    __builtin_va_start(args, format);
    DWORD tick = (DWORD)GetTickCount64();
    fmt_uint(&o, end, tick / 1000, 10, 1);
    fmt_put(&o, end, '.');
    fmt_uint(&o, end, tick % 1000, 10, 3);
    fmt_put(&o, end, ' ');
    for (const char *f = format; *f; ++f) {
        if (*f != '%') {
            fmt_put(&o, end, *f);
            continue;
        }
        ++f;
        if (*f == 's') {
            const char *s = __builtin_va_arg(args, const char *);
            for (s = s ? s : "(null)"; *s; ++s) fmt_put(&o, end, *s);
        } else if (*f == 'S') {
            const wchar_t *s = __builtin_va_arg(args, const wchar_t *);
            for (s = s ? s : L"(null)"; *s; ++s) fmt_put(&o, end, *s < 128 ? (char)*s : '?');
        } else if (*f == 'd') {
            long long v = __builtin_va_arg(args, int);
            if (v < 0) {
                fmt_put(&o, end, '-');
                v = -v;
            }
            fmt_uint(&o, end, (unsigned long long)v, 10, 1);
        } else if (*f == 'u') {
            fmt_uint(&o, end, __builtin_va_arg(args, unsigned), 10, 1);
        } else if (*f == 'x') {
            fmt_uint(&o, end, __builtin_va_arg(args, unsigned), 16, 1);
        } else if (*f == 'X') {
            fmt_uint(&o, end, __builtin_va_arg(args, unsigned), 16, 8);
        } else if (*f == 'p') {
            fmt_put(&o, end, '0');
            fmt_put(&o, end, 'x');
            fmt_uint(&o, end, (unsigned long long)(uintptr_t)__builtin_va_arg(args, void *), 16, 1);
        } else if (*f == 'c') {
            fmt_put(&o, end, (char)__builtin_va_arg(args, int));
        } else if (*f == 'f') {
            double v = __builtin_va_arg(args, double);
            if (v < 0) {
                fmt_put(&o, end, '-');
                v = -v;
            }
            unsigned long long whole = (unsigned long long)v;
            fmt_uint(&o, end, whole, 10, 1);
            fmt_put(&o, end, '.');
            fmt_uint(&o, end, (unsigned long long)((v - (double)whole) * 1000.0), 10, 3);
        } else {
            fmt_put(&o, end, *f);
        }
    }
    __builtin_va_end(args);
    *o++ = '\r';
    *o++ = '\n';
    AcquireSRWLockExclusive(&log_lock);
    HANDLE file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, buffer, (DWORD)(o - buffer), &written, NULL);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&log_lock);
}

void wheel_log_text(const char *text) {
    wlog("%s", text);
}

static void log_hex(const char *label, const unsigned char *data, unsigned size) {
    char line[700];
    unsigned n = 0;
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < size && n + 3 < sizeof(line); ++i) {
        line[n++] = digits[data[i] >> 4];
        line[n++] = digits[data[i] & 15];
        line[n++] = ' ';
    }
    line[n] = 0;
    wlog("%s (%u): %s", label, size, line);
}

/* ------------------------------------------------------------------ settings */

typedef struct {
    int enabled, rotation, invert_steering, shifter, reverse_value, log;
    int axis_steer, axis_throttle, axis_brake, axis_clutch;
    int btn_a, btn_b, btn_x, btn_y, btn_paddle_right, btn_paddle_left, btn_menu, btn_view;
    int gear[7]; /* 1..6, reverse */
    int ffb, lock, centering, damping, ffb_csv, strength, rumble, ffb_sign, curve10, outward;
    int smoothing, friction, minforce;
    int ffb_mode; /* 0 = the game's own wheel programs (like on the Xbox), 1 = own FFB (old) */
    int wheel_dll; /* 1 = use Microsoft.Xbox.Input.Wheel.dll if it exists, 0 = always our own */
    int clutch_brake; /* 1 = the clutch pedal brakes, the clutch is off */
    int extra_button[WHEEL_EXTRA_BUTTONS], extra_function[WHEEL_EXTRA_BUTTONS];
    wchar_t device[128];
} WheelConfig;

static WheelConfig cfg;
static const char *const axis_names[] = {"X", "Y", "Z", "Rx", "Ry", "Rz", "Slider0", "Slider1"};

static int read_int(const wchar_t *key, int fallback) {
    wchar_t text[32];
    char ascii[32];
    DWORD n = GetPrivateProfileStringW(L"Wheel", key, L"", text, 32, ini_path);
    if (!n) {
        return fallback;
    }
    unsigned i = 0;
    for (; i < n && i < 31; ++i) ascii[i] = (char)text[i];
    ascii[i] = 0;
    int sign = 1, value = 0, any = 0;
    const char *p = ascii;
    while (*p == ' ') ++p;
    if (*p == '-') {
        sign = -1;
        ++p;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p++ - '0');
        any = 1;
    }
    return any ? sign * value : fallback;
}

static int read_axis(const wchar_t *key, int fallback) {
    wchar_t text[32];
    char ascii[32];
    DWORD n = GetPrivateProfileStringW(L"Wheel", key, L"", text, 32, ini_path);
    if (!n) {
        return fallback;
    }
    unsigned i = 0;
    for (; i < n && i < 31; ++i) ascii[i] = (char)text[i];
    ascii[i] = 0;
    for (int a = 0; a < 8; ++a) {
        if (ascii_eq_ci(ascii, axis_names[a])) return a;
    }
    if (ascii_eq_ci(ascii, "Slider") ) return 6;
    return -1; /* "-" / "keine" disables the axis */
}

static const char default_ini[] =
    "; FH2 wheel bridge (unofficial) - settings\r\n"
    "; The most important values are also in the game: Options > HUD > bottom.\r\n"
    "; Changes in this file apply on the next game start.\r\n"
    "[Wheel]\r\n"
    "; 1 = pass the wheel to the game, 0 = off (then keyboard/controller drive)\r\n"
    "Enabled=1\r\n"
    "; Part of the device name (empty = automatic, e.g. G920)\r\n"
    "Device=\r\n"
    "; Steering rotation in degrees (as set in G HUB)\r\n"
    "Rotation=900\r\n"
    "InvertSteering=0\r\n"
    "; Axes: X Y Z Rx Ry Rz Slider0 Slider1 or - (none). G920: steering X, throttle Y,\r\n"
    "; brake Rz, clutch Slider0.\r\n"
    "SteeringAxis=X\r\n"
    "ThrottleAxis=Y\r\n"
    "BrakeAxis=Rz\r\n"
    "ClutchAxis=Slider0\r\n"
    "; Clutch pedal as brake (the G920 brake pedal is stiff): 1 = the clutch pedal brakes and\r\n"
    "; the clutch is off, 0 = standard. Also in the game: Options > HUD > bottom.\r\n"
    "ClutchAsBrake=0\r\n"
    "; Button numbers (0 = first button). The log shows the numbers of pressed buttons.\r\n"
    "ButtonA=0\r\n"
    "ButtonB=1\r\n"
    "ButtonX=2\r\n"
    "ButtonY=3\r\n"
    "PaddleRight=4\r\n"
    "PaddleLeft=5\r\n"
    "ButtonMenu=6\r\n"
    "ButtonView=7\r\n"
    "; H-pattern shifter (Driving Force Shifter): FH2 does not use it, so it is off (0)\r\n"
    "Shifter=0\r\n"
    "Gear1=12\r\n"
    "Gear2=13\r\n"
    "Gear3=14\r\n"
    "Gear4=15\r\n"
    "Gear5=16\r\n"
    "Gear6=17\r\n"
    "Reverse=18\r\n"
    "ReverseValue=255\r\n"
    "; Force feedback: 1 = on, 0 = off\r\n"
    "ForceFeedback=1\r\n"
    "; The force feedback is the game's original one (its wheel programs, like on the Xbox).\r\n"
    "; Strength in percent (100 = like on the Xbox, up to 200), rumble in percent\r\n"
    "FFBStrength=100\r\n"
    "Rumble=100\r\n"
    "; Free wheel buttons (numbers see log, -1 = none): G920 RSB=8, LSB=9, Xbox button=10\r\n"
    "ExtraButton1=8\r\n"
    "ExtraButton2=9\r\n"
    "ExtraButton3=10\r\n"
    "; Function per button: 0 off, 1 camera, 2 look back, 3 look left, 4 look right,\r\n"
    "; 5 rewind, 6 telemetry, 7 photo mode, 8 radio previous, 9 radio next,\r\n"
    "; 10 handbrake, 11 clutch, 12 shift up, 13 shift down,\r\n"
    "; 14 nitro (needs the NitroSystem mod)\r\n"
    "ExtraFunction1=2\r\n"
    "ExtraFunction2=1\r\n"
    "ExtraFunction3=0\r\n"
    "; Detailed log in FH2_Wheel.log\r\n"
    "Log=1\r\n";

/* Default file text (also used to move an old FH2_Lenkrad.ini over, see ini_migrate.c). */
const char *wheel_bridge_default_ini(size_t *length) {
    if (length) *length = sizeof(default_ini) - 1;
    return default_ini;
}

static void config_load(void) {
    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE file = CreateFileW(ini_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(file, default_ini, sizeof(default_ini) - 1, &written, NULL);
            CloseHandle(file);
        }
    }
    cfg.enabled = read_int(L"Enabled", 1);
    cfg.rotation = read_int(L"Rotation", 900);
    if (cfg.rotation < 180 || cfg.rotation > 2520) cfg.rotation = 900;
    cfg.invert_steering = read_int(L"InvertSteering", 0);
    cfg.axis_steer = read_axis(L"SteeringAxis", 0);
    cfg.axis_throttle = read_axis(L"ThrottleAxis", 1);
    cfg.axis_brake = read_axis(L"BrakeAxis", 5);
    cfg.axis_clutch = read_axis(L"ClutchAxis", 6);
    cfg.btn_a = read_int(L"ButtonA", 0);
    cfg.btn_b = read_int(L"ButtonB", 1);
    cfg.btn_x = read_int(L"ButtonX", 2);
    cfg.btn_y = read_int(L"ButtonY", 3);
    cfg.btn_paddle_right = read_int(L"PaddleRight", 4);
    cfg.btn_paddle_left = read_int(L"PaddleLeft", 5);
    cfg.btn_menu = read_int(L"ButtonMenu", 6);
    cfg.btn_view = read_int(L"ButtonView", 7);
    cfg.shifter = read_int(L"Shifter", 0);
    static const wchar_t *const gear_keys[7] = {L"Gear1", L"Gear2", L"Gear3", L"Gear4",
                                                L"Gear5", L"Gear6", L"Reverse"};
    for (int g = 0; g < 7; ++g) cfg.gear[g] = read_int(gear_keys[g], 12 + g);
    cfg.reverse_value = read_int(L"ReverseValue", 255) & 0xff;
    cfg.ffb = read_int(L"ForceFeedback", 1);
    cfg.lock = read_int(L"Lock", cfg.rotation > 0 ? 81000 / cfg.rotation : 90);
    if (cfg.lock < 30 || cfg.lock > 450) cfg.lock = 450;
    cfg.centering = read_int(L"Centering", 20);
    cfg.damping = read_int(L"Damping", 10);
    cfg.ffb_csv = read_int(L"FFBLog", 1);
    cfg.strength = read_int(L"FFBStrength", 100);
    cfg.rumble = read_int(L"Rumble", 100);
    cfg.ffb_sign = read_int(L"FFBDirection", 0); /* 0 = automatisch, 1 / -1 = fest */
    cfg.curve10 = read_int(L"Curve", 10);        /* 10 = linear, 30 = Mitte dreimal so direkt */
    if (cfg.curve10 < 10 || cfg.curve10 > 60) cfg.curve10 = 10;
    cfg.outward = read_int(L"OutwardForce", 30); /* % der Kraft, die vom Mittelpunkt wegzieht */
    cfg.smoothing = read_int(L"Smoothing", 35);     /* ms Glaettung der Spielkraft (Schlaege gehen durch) */
    cfg.friction = read_int(L"Friction", 4);         /* % Lenkungsreibung */
    cfg.minforce = read_int(L"MinimumForce", 2);    /* % gegen die tote Zone des G920-Getriebes */
    cfg.ffb_mode = read_int(L"FFBMode", 0) == 1 ? 1 : 0;
    cfg.log = read_int(L"Log", 1);
    cfg.wheel_dll = read_int(L"WheelDll", 1);
    cfg.clutch_brake = read_int(L"ClutchAsBrake", 0) == 1;
    /* free wheel buttons: G920 RSB = 8, LSB = 9, Xbox button = 10 (DirectInput numbers) */
    static const wchar_t *const extra_keys[WHEEL_EXTRA_BUTTONS] = {L"ExtraButton1", L"ExtraButton2",
                                                                   L"ExtraButton3"};
    static const wchar_t *const function_keys[WHEEL_EXTRA_BUTTONS] = {
        L"ExtraFunction1", L"ExtraFunction2", L"ExtraFunction3"};
    static const int extra_defaults[WHEEL_EXTRA_BUTTONS] = {8, 9, 10};
    static const int function_defaults[WHEEL_EXTRA_BUTTONS] = {WHEEL_FUNCTION_LOOK_BACK,
                                                               WHEEL_FUNCTION_CAMERA,
                                                               WHEEL_FUNCTION_OFF};
    for (int k = 0; k < WHEEL_EXTRA_BUTTONS; ++k) {
        cfg.extra_button[k] = read_int(extra_keys[k], extra_defaults[k]);
        if (cfg.extra_button[k] < -1 || cfg.extra_button[k] > 31) cfg.extra_button[k] = -1;
        cfg.extra_function[k] = read_int(function_keys[k], function_defaults[k]);
        if (cfg.extra_function[k] < 0 || cfg.extra_function[k] >= WHEEL_FUNCTION_COUNT)
            cfg.extra_function[k] = function_defaults[k];
    }
    GetPrivateProfileStringW(L"Wheel", L"Device", L"", cfg.device, 128, ini_path);
}

/* ------------------------------------------------------------------ live settings (in-game menu) */

/* The menu thread writes the wanted values here; the poll thread copies them into cfg at the
   start of its next cycle, so cfg is only ever changed by the thread that uses it. */
static volatile LONG live_values[RIG_WHEEL_COUNT];
static volatile LONG live_generation;
static LONG applied_generation;
static int live_loaded;

static int *cfg_field(unsigned id) {
    switch (id) {
        case RIG_WHEEL_ENABLED: return &cfg.enabled;
        case RIG_WHEEL_FFB: return &cfg.ffb;
        case RIG_WHEEL_STRENGTH: return &cfg.strength;
        case RIG_WHEEL_RUMBLE: return &cfg.rumble;
        case RIG_WHEEL_DAMPING: return &cfg.damping;
        case RIG_WHEEL_FRICTION: return &cfg.friction;
        case RIG_WHEEL_SMOOTHING: return &cfg.smoothing;
        case RIG_WHEEL_MINFORCE: return &cfg.minforce;
        case RIG_WHEEL_OUTWARD: return &cfg.outward;
        case RIG_WHEEL_CENTERING: return &cfg.centering;
        case RIG_WHEEL_LOCK: return &cfg.lock;
        case RIG_WHEEL_CURVE: return &cfg.curve10;
        case RIG_WHEEL_ROTATION: return &cfg.rotation;
        case RIG_WHEEL_SHIFTER: return &cfg.shifter;
        case RIG_WHEEL_BUTTON1: return &cfg.extra_function[0];
        case RIG_WHEEL_BUTTON2: return &cfg.extra_function[1];
        case RIG_WHEEL_BUTTON3: return &cfg.extra_function[2];
        case RIG_WHEEL_FFB_MODE: return &cfg.ffb_mode;
        case RIG_WHEEL_CLUTCH_BRAKE: return &cfg.clutch_brake;
    }
    return NULL;
}

static void live_load(void) {
    for (unsigned id = 0; id < RIG_WHEEL_COUNT; ++id) {
        int *field = cfg_field(id);
        live_values[id] = field ? *field : 0;
    }
    applied_generation = live_generation;
    live_loaded = 1;
}

int wheel_bridge_setting(unsigned id) {
    return id < RIG_WHEEL_COUNT ? (int)live_values[id] : 0;
}

void wheel_bridge_set_setting(unsigned id, int value) {
    if (id >= RIG_WHEEL_COUNT || !live_loaded) {
        return;
    }
    InterlockedExchange(&live_values[id], value);
    InterlockedIncrement(&live_generation);
}

const wchar_t *wheel_bridge_ini_path(void) {
    return live_loaded ? ini_path : NULL;
}

/* ------------------------------------------------------------------ game imports */

static unsigned char *game;
static GetFactoryFn original_get_factory;
static CreateStringFn game_create_string;
static CoCreateFn original_cocreate;

static void *game_import(const char *dll, const char *name) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)game;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(game + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(game + dir.VirtualAddress); d->Name;
         ++d) {
        if (!ascii_eq_ci((const char *)(game + d->Name), dll) || !d->OriginalFirstThunk) continue;
        uint64_t *names = (uint64_t *)(game + d->OriginalFirstThunk);
        void **slots = (void **)(game + d->FirstThunk);
        for (unsigned k = 0; names[k]; ++k) {
            if (names[k] >> 63) continue;
            const char *n = (const char *)(game + (names[k] & 0x7fffffff) + 2);
            if (ascii_eq_ci(n, name)) return slots[k];
        }
    }
    return NULL;
}

static void *make_string(const wchar_t *text) {
    void *h = NULL;
    UINT32 n = 0;
    while (text[n]) ++n;
    if (game_create_string && game_create_string(text, n, &h) >= 0) return h;
    return NULL;
}

/* ------------------------------------------------------------------ fake GIP sender */

static volatile LONG send_count;
static float ffb_param[64];
static volatile uint64_t ffb_seen;
static volatile LONG ffb_updates;
static HRESULT WINAPI sender_qi(void *self, const GUID *iid, void **out) {
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_GipSender)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI static_addref(void *self) {
    (void)self;
    return 2;
}
static ULONG WINAPI static_release(void *self) {
    (void)self;
    return 1;
}
static HRESULT WINAPI sender_send(void *self, UINT64 id, UINT32 size, const unsigned char *buffer) {
    (void)self;
    (void)id;
    LONG n = InterlockedIncrement(&send_count);
    int cmd = buffer && size >= 0x14 ? buffer[8] : -1;
    if (cmd == 0x0b) {
        UINT32 len = *(const UINT32 *)(buffer + 0xc);
        if (len > size - 0x14) len = size - 0x14;
        for (UINT32 o = 0; o + 6 <= len; o += 6) {
            uint16_t pid = *(const uint16_t *)(buffer + 0x14 + o);
            if (pid == 0xffff) break;
            if (pid < 64) {
                float v;
                memory_copy(&v, buffer + 0x14 + o + 2, 4);
                ffb_param[pid] = v;
                ffb_seen |= 1ull << pid;
            }
        }
        InterlockedIncrement(&ffb_updates);
    }
    static volatile LONG other_logged;
    int log_it = cmd == 0x0b ? (n <= 30 || (n % 2000) == 0) : InterlockedIncrement(&other_logged) <= 80;
    if (buffer && size >= 0x14 && log_it) {
        char label[64] = "GIP-Befehl an Lenkrad #";
        unsigned p = 23;
        LONG v = n;
        char digits[12];
        int d = 0;
        do { digits[d++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (d) label[p++] = digits[--d];
        label[p] = 0;
        log_hex(label, buffer, size > 200 ? 200 : size);
    }
    return S_OK;
}
static HRESULT WINAPI sender_other(void *self) {
    (void)self;
    return S_OK;
}
static void *sender_vtbl[16] = {(void *)sender_qi, (void *)static_addref, (void *)static_release,
                                (void *)sender_send};
static struct { void **vtbl; } sender = {sender_vtbl};

static HRESULT WINAPI hook_cocreate(const GUID *clsid, void *outer, DWORD context, const GUID *iid,
                                   void **out) {
    if (guid_eq(clsid, &CLSID_GipSender)) {
        *out = &sender;
        wlog("Wheel-DLL: GIP-Dienst angefordert -> Bruecke liefert Ersatz.");
        return S_OK;
    }
    if (original_cocreate) return original_cocreate(clsid, outer, context, iid, out);
    *out = NULL;
    return REGDB_E_CLASSNOTREG;
}

/* ------------------------------------------------------------------ stubs for the wheel DLL */

static MallocFn crt_malloc;
static FreeFn crt_free;
static HMODULE crt_module;
static void *__cdecl stub_new(size_t n) { return crt_malloc ? crt_malloc(n ? n : 1) : NULL; }
static void __cdecl stub_delete(void *p) {
    if (p && crt_free) crt_free(p);
}
static int __cdecl stub_xcpt_filter(int code, void *info) {
    (void)code;
    (void)info;
    return 0;
}
static void __cdecl stub_nothing(void *p) { (void)p; }
static HRESULT WINAPI stub_embedded_null(void *s, BOOL *out) {
    (void)s;
    if (out) *out = FALSE;
    return S_OK;
}
static BOOL WINAPI stub_transform_error(HRESULT a, HRESULT b, void *msg) {
    (void)a;
    (void)b;
    (void)msg;
    return FALSE;
}

static void *resolve_import(const char *dll, const char *name) {
    void *f = NULL;
    if (ascii_eq_ci(dll, "combase.dll")) {
        if (ascii_eq_ci(name, "CoCreateInstance")) {
            original_cocreate = (CoCreateFn)game_import("combase.dll", name);
            if (!original_cocreate) {
                HMODULE m = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (m) original_cocreate = (CoCreateFn)(uintptr_t)GetProcAddress(m, name);
            }
            return (void *)hook_cocreate;
        }
        /* Azimuth's string functions reject HSTRINGs used outside the game image, so the
           wheel DLL gets the real Windows combase for everything else. */
        HMODULE m = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m) f = (void *)(uintptr_t)GetProcAddress(m, name);
        if (!f) f = game_import("combase.dll", name);
        if (!f && ascii_eq_ci(name, "WindowsStringHasEmbeddedNull")) f = (void *)stub_embedded_null;
        if (!f && ascii_eq_ci(name, "RoTransformError")) f = (void *)stub_transform_error;
        return f;
    }
    if (ascii_eq_ci(dll, "kernelx.dll")) {
        f = game_import("kernelx.dll", name);
        if (!f) f = (void *)(uintptr_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
        return f;
    }
    if (ascii_eq_ci(dll, "msvcr110.dll") || ascii_eq_ci(dll, "msvcp110.dll")) {
        /* Use the game's own CRT. If it is a real module, take every export from it so that
           new/delete/malloc/free always pair up; otherwise use the game's imports plus stubs
           that route new/delete to the game's malloc/free. */
        if (ascii_eq_ci(dll, "msvcr110.dll") && crt_module) f = (void *)(uintptr_t)GetProcAddress(crt_module, name);
        if (!f) f = game_import(dll, name);
        if (f) return f;
        if (ascii_eq_ci(name, "??2@YAPEAX_K@Z") || ascii_eq_ci(name, "_malloc_crt")) return (void *)stub_new;
        if (ascii_eq_ci(name, "??3@YAXPEAX@Z") || ascii_eq_ci(name, "??_V@YAXPEAX@Z")) return (void *)stub_delete;
        if (ascii_eq_ci(name, "__CppXcptFilter")) return (void *)stub_xcpt_filter;
        if (ascii_eq_ci(name, "__clean_type_info_names_internal")) return (void *)stub_nothing;
        return NULL;
    }
    return NULL;
}

/* ------------------------------------------------------------------ manual PE loader */

static unsigned char *wheel_image;
static DllGetFactoryFn wheel_get_factory;

static int load_wheel_dll(void) {
    HANDLE file = CreateFileW(dll_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        wlog("FEHLER: %S nicht gefunden.", dll_path);
        return 0;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 4096 || size.QuadPart > (16 << 20)) {
        CloseHandle(file);
        return 0;
    }
    unsigned char *raw = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart);
    DWORD got = 0;
    int ok = raw && ReadFile(file, raw, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(file);
    if (!ok) return 0;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)raw;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(raw + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        wlog("FEHLER: Wheel-DLL hat kein gueltiges Format.");
        return 0;
    }
    SIZE_T image_size = nt->OptionalHeader.SizeOfImage;
    unsigned char *base = VirtualAlloc((void *)(uintptr_t)nt->OptionalHeader.ImageBase, image_size,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) base = VirtualAlloc(NULL, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return 0;
    memory_copy(base, raw, nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        DWORD n = sec[i].SizeOfRawData < sec[i].Misc.VirtualSize || !sec[i].Misc.VirtualSize
                      ? sec[i].SizeOfRawData
                      : sec[i].Misc.VirtualSize;
        if (n && sec[i].PointerToRawData + n <= size.QuadPart) {
            memory_copy(base + sec[i].VirtualAddress, raw + sec[i].PointerToRawData, n);
        }
    }
    /* relocations */
    int64_t delta = (int64_t)((uintptr_t)base - nt->OptionalHeader.ImageBase);
    IMAGE_DATA_DIRECTORY rel = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && rel.VirtualAddress) {
        unsigned char *p = base + rel.VirtualAddress, *end = p + rel.Size;
        while (p + 8 <= end) {
            IMAGE_BASE_RELOCATION *block = (IMAGE_BASE_RELOCATION *)p;
            if (block->SizeOfBlock < 8) break;
            WORD *entries = (WORD *)(p + 8);
            unsigned count = (block->SizeOfBlock - 8) / 2;
            for (unsigned k = 0; k < count; ++k) {
                if ((entries[k] >> 12) == IMAGE_REL_BASED_DIR64) {
                    uint64_t *target = (uint64_t *)(base + block->VirtualAddress + (entries[k] & 0xfff));
                    *target += (uint64_t)delta;
                }
            }
            p += block->SizeOfBlock;
        }
    }
    /* imports */
    IMAGE_DATA_DIRECTORY imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + imp.VirtualAddress); d->Name; ++d) {
        const char *dll = (const char *)(base + d->Name);
        uint64_t *names = (uint64_t *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        void **slots = (void **)(base + d->FirstThunk);
        for (unsigned k = 0; names[k]; ++k) {
            const char *name = (names[k] >> 63) ? "#ordinal" : (const char *)(base + (names[k] & 0x7fffffff) + 2);
            void *f = (names[k] >> 63) ? NULL : resolve_import(dll, name);
            if (!f) {
                wlog("FEHLER: Import %s!%s nicht aufloesbar.", dll, name);
                return 0;
            }
            slots[k] = f;
        }
    }
    /* protections */
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        DWORD c = sec[i].Characteristics, prot = PAGE_READONLY, old;
        if (c & IMAGE_SCN_MEM_EXECUTE) prot = (c & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else if (c & IMAGE_SCN_MEM_WRITE) prot = PAGE_READWRITE;
        DWORD vs = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (vs) VirtualProtect(base + sec[i].VirtualAddress, vs, prot, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), base, image_size);
    /* unwind data so C++/SEH exceptions inside the DLL work */
    IMAGE_DATA_DIRECTORY pdata = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    AddTableFn add_table = (AddTableFn)(uintptr_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "RtlAddFunctionTable");
    if (pdata.VirtualAddress && add_table) {
        add_table(base + pdata.VirtualAddress, pdata.Size / 12, (DWORD64)(uintptr_t)base);
    }
    /* exports */
    IMAGE_DATA_DIRECTORY exp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exp.VirtualAddress) {
        IMAGE_EXPORT_DIRECTORY *e = (IMAGE_EXPORT_DIRECTORY *)(base + exp.VirtualAddress);
        DWORD *names = (DWORD *)(base + e->AddressOfNames);
        WORD *ords = (WORD *)(base + e->AddressOfNameOrdinals);
        DWORD *funcs = (DWORD *)(base + e->AddressOfFunctions);
        for (DWORD k = 0; k < e->NumberOfNames; ++k) {
            if (ascii_eq_ci((const char *)(base + names[k]), "DllGetActivationFactory")) {
                wheel_get_factory = (DllGetFactoryFn)(uintptr_t)(base + funcs[ords[k]]);
            }
        }
    }
    DllMainFn entry = (DllMainFn)(uintptr_t)(base + nt->OptionalHeader.AddressOfEntryPoint);
    HeapFree(GetProcessHeap(), 0, raw);
    if (!wheel_get_factory) {
        wlog("FEHLER: DllGetActivationFactory fehlt.");
        return 0;
    }
    if (!entry((HINSTANCE)base, DLL_PROCESS_ATTACH, NULL)) {
        wlog("FEHLER: DllMain der Wheel-DLL meldet Fehler.");
        return 0;
    }
    wheel_image = base;
    wlog("Wheel-DLL geladen bei %p (Relocation %s).", base, delta ? "ja" : "nein");
    return 1;
}

/* device info: VID/PID/firmware words the Wheel DLL asks the console for */
struct DevInfoObj {
    void **vtbl;
};
static HRESULT WINAPI devinfo_qi(void *self, const GUID *iid, void **out) {
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_DeviceInfo)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI devinfo_m3(void *self, void *out) {
    (void)self;
    if (out) zero(out, 16);
    return S_OK;
}
static HRESULT WINAPI devinfo_get(void *self, unsigned char *out) {
    (void)self;
    if (!out) return E_POINTER;
    zero(out, 16);
    *(uint16_t *)(out + 2) = 0x046d;
    *(uint16_t *)(out + 4) = 0xc261;
    *(uint16_t *)(out + 0xc) = 1;
    return S_OK;
}
static void *devinfo_vtbl[8] = {(void *)devinfo_qi, (void *)static_addref, (void *)static_release,
                                (void *)devinfo_m3, (void *)devinfo_get, (void *)sender_other,
                                (void *)sender_other, (void *)sender_other};
static struct DevInfoObj devinfo = {devinfo_vtbl};

/* ------------------------------------------------------------------ fake controller (outer) */

static void *inner;          /* non-delegating IInspectable of the aggregated Wheel */
static void *input_handler;  /* IInputHandler of the Wheel */
static void *real_statics;   /* Azimuth's IControllerStatics (kept for user lookup) */
static void *cached_user;
static void *user_statics;  /* Azimuth's IUserStatics (signed-in user without any gamepad) */
static volatile LONG wheel_ready;
static struct { void **vtbl; } outer;

static void refresh_user(void) {
    if (cached_user || !real_statics) return;
    void *vec = NULL;
    if (((Fn1)VT(real_statics)[6])(real_statics, &vec) < 0 || !vec) return;
    UINT32 n = 0;
    ((SizeFn)VT(vec)[7])(vec, &n);
    for (UINT32 i = 0; i < n && !cached_user; ++i) {
        void *ctl = NULL;
        if (((GetAtFn)VT(vec)[6])(vec, i, &ctl) >= 0 && ctl) {
            void *user = NULL;
            if (((Fn1)VT(ctl)[8])(ctl, &user) >= 0 && user) {
                cached_user = user; /* keep the reference */
                wlog("Benutzer vom Azimuth-Controller %u uebernommen.", i);
            }
            ((RefFn)VT(ctl)[2])(ctl);
        }
    }
    ((RefFn)VT(vec)[2])(vec);
    if (!cached_user && user_statics) {
        void *users = NULL;
        if (((Fn1)VT(user_statics)[9])(user_statics, &users) >= 0 && users) {
            void *u = NULL;
            if (((GetAtFn)VT(users)[6])(users, 0, &u) >= 0 && u) {
                cached_user = u;
                wlog("Benutzer aus der Azimuth-Benutzerliste uebernommen (kein Gamepad noetig).");
            }
            ((RefFn)VT(users)[2])(users);
        }
    }
}

static HRESULT WINAPI outer_qi(void *self, const GUID *iid, void **out) {
    if (!out) return E_POINTER;
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_Inspectable) || guid_eq(iid, &IID_Controller) ||
        guid_eq(iid, &IID_Agile)) {
        *out = self;
        return S_OK;
    }
    if (guid_eq(iid, &IID_DeviceInfo)) {
        *out = (void *)&devinfo;
        return S_OK;
    }
    if (inner) {
        HRESULT hr = ((QIFn)VT(inner)[0])(inner, iid, out);
        static int logged_wheel, logged_gamepad, logged_nav;
        if (guid_eq(iid, &IID_Wheel) && !logged_wheel++) wlog("Spiel fragt IWheel ab -> %X", (unsigned)hr);
        if (guid_eq(iid, &IID_Gamepad) && !logged_gamepad++) wlog("Spiel fragt IGamepad ab -> %X", (unsigned)hr);
        if (guid_eq(iid, &IID_NavController) && !logged_nav++) wlog("Spiel fragt INavigationController ab -> %X", (unsigned)hr);
        return hr;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static HRESULT WINAPI outer_iids(void *self, ULONG *count, GUID **iids) {
    (void)self;
    if (count) *count = 0;
    if (iids) *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI outer_name(void *self, void **name) {
    (void)self;
    *name = make_string(L"Windows.Xbox.Input.Controller");
    return S_OK;
}
static HRESULT WINAPI outer_trust(void *self, int *level) {
    (void)self;
    *level = 0;
    return S_OK;
}
static HRESULT WINAPI outer_id(void *self, UINT64 *id) {
    (void)self;
    if (!id) return E_POINTER;
    *id = WHEEL_ID;
    return S_OK;
}
static HRESULT WINAPI outer_type(void *self, void **type) {
    (void)self;
    if (!type) return E_POINTER;
    *type = make_string(L"Microsoft.Xbox.Input.Wheel");
    return S_OK;
}
static HRESULT WINAPI outer_user(void *self, void **user) {
    (void)self;
    if (!user) return E_POINTER;
    refresh_user();
    *user = cached_user;
    if (cached_user) ((RefFn)VT(cached_user)[1])(cached_user);
    static int logged;
    if (!logged++) wlog("Spiel fragt Benutzer des Lenkrads ab -> %s", cached_user ? "gefunden" : "keiner");
    return S_OK;
}
static void *outer_vtbl[9] = {(void *)outer_qi,   (void *)static_addref, (void *)static_release,
                              (void *)outer_iids, (void *)outer_name,   (void *)outer_trust,
                              (void *)outer_id,   (void *)outer_type,   (void *)outer_user};



/* ------------------------------------------------------------------ the game's wheel programs (real FFB)

   FH2 talks to the wheel like on the Xbox: it registers named parameters (IWheel slot 36),
   uploads small force feedback programs (slot 26) and then sets the parameters every frame
   (slot 38, followed by a commit). Microsoft's Wheel DLL turns that into GIP messages for the
   wheel's firmware, which runs the programs. These three methods are hooked here so the very
   same programs and values run in ffb_vm.c on the PC - the forces come from the game itself. */

#define WHEEL_VTABLE_RVA 0x15760u
#define WHEEL_RAWBUFFER_IAT 0x14198u
#define SLOT_SET_PROGRAM 26
#define SLOT_ADD_PARAMETER 36
#define SLOT_SET_PARAMETER 38
static const unsigned wheel_method_rva[3] = {0x8540u, 0x8ab0u, 0x8c10u};

typedef HRESULT(WINAPI *SetProgramFn)(void *, UINT8, void *, UINT32, void *);
typedef HRESULT(WINAPI *AddParameterFn)(void *, void *);
typedef HRESULT(WINAPI *SetParameterFn)(void *, void *, float);
typedef const wchar_t *(WINAPI *RawBufferFn)(void *, UINT32 *);

static FfbVm game_vm;
static SRWLOCK vm_lock = SRWLOCK_INIT;
static SetProgramFn original_set_program;
static AddParameterFn original_add_parameter;
static SetParameterFn original_set_parameter;
static RawBufferFn raw_buffer;
static volatile LONG game_ffb_hooked, programs_loaded, parameter_sets;

static const wchar_t *hstring_text(void *hstring, UINT32 *length) {
    *length = 0;
    if (!raw_buffer || !hstring) return NULL;
    const wchar_t *text = NULL;
    __try {
        text = raw_buffer(hstring, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        text = NULL;
        *length = 0;
    }
    return text;
}

static void log_wide(const char *label, const wchar_t *text, UINT32 length) {
    char line[700];
    unsigned n = 0;
    for (UINT32 i = 0; i < length && n < sizeof(line) - 1; ++i) line[n++] = text[i] < 128 ? (char)text[i] : '?';
    line[n] = 0;
    wlog("%s%s", label, line);
}

static HRESULT WINAPI hook_set_program(void *self, UINT8 slot, void *program, UINT32 flags, void *out) {
    UINT32 length;
    const wchar_t *text = hstring_text(program, &length);
    if (text && slot < FFB_VM_SLOTS) {
        size_t error_at = 0;
        AcquireSRWLockExclusive(&vm_lock);
        int ok = ffb_vm_load(&game_vm, slot, text, length, &error_at);
        ReleaseSRWLockExclusive(&vm_lock);
        LONG count = InterlockedIncrement(&programs_loaded);
        if (count <= 40) {
            char label[64] = "Spiel-FFB: Programm ";
            unsigned n = 20;
            if (slot >= 10) label[n++] = (char)('0' + slot / 10);
            label[n++] = (char)('0' + slot % 10);
            const char *tail = ok ? " geladen: " : " NICHT verstanden (bleibt aus): ";
            while (*tail && n < sizeof(label) - 1) label[n++] = *tail++;
            label[n] = 0;
            log_wide(label, text, length);
            if (!ok) wlog("Spiel-FFB: Fehler ab Zeichen %u.", (unsigned)error_at);
        }
    }
    return original_set_program(self, slot, program, flags, out);
}

static HRESULT WINAPI hook_add_parameter(void *self, void *name) {
    UINT32 length;
    const wchar_t *text = hstring_text(name, &length);
    if (text) {
        AcquireSRWLockExclusive(&vm_lock);
        int index = ffb_vm_param(&game_vm, text, length);
        ReleaseSRWLockExclusive(&vm_lock);
        char label[48] = "Spiel-FFB: Parameter ";
        unsigned n = 21;
        if (index >= 10) label[n++] = (char)('0' + index / 10);
        label[n++] = (char)('0' + (index < 0 ? 0 : index % 10));
        label[n++] = ' ';
        label[n++] = '=';
        label[n++] = ' ';
        label[n] = 0;
        log_wide(label, text, length);
    }
    return original_add_parameter(self, name);
}

static HRESULT WINAPI hook_set_parameter(void *self, void *name, float value) {
    UINT32 length;
    const wchar_t *text = hstring_text(name, &length);
    if (text) {
        AcquireSRWLockShared(&vm_lock);
        int index = -1;
        for (unsigned i = 0; i < game_vm.param_count && index < 0; ++i) {
            const wchar_t *n = game_vm.param[i].name;
            UINT32 k = 0;
            while (k < length && n[k] == text[k]) ++k;
            if (k == length && !n[k]) index = (int)i;
        }
        if (index >= 0) ffb_vm_set_param(&game_vm, index, value);
        ReleaseSRWLockShared(&vm_lock);
        if (index < 0) { /* not registered through slot 36 (should not happen): add it */
            AcquireSRWLockExclusive(&vm_lock);
            ffb_vm_set_param(&game_vm, ffb_vm_param(&game_vm, text, length), value);
            ReleaseSRWLockExclusive(&vm_lock);
        }
        InterlockedIncrement(&parameter_sets);
    }
    return original_set_parameter(self, name, value);
}

static int soft_wheel; /* 1 = our own wheel object (wheel_soft.c) instead of Microsoft's DLL */

/* Puts the three hooks into an IWheel method table; the previous entries are called on. */
static void patch_game_ffb(void **table) {
    DWORD old, ignored;
    if (!VirtualProtect(table + SLOT_SET_PROGRAM, (SLOT_SET_PARAMETER - SLOT_SET_PROGRAM + 1) * sizeof(void *),
                        PAGE_READWRITE, &old)) {
        wlog("Spiel-FFB: Methodentabelle nicht beschreibbar - eigenes FFB wird benutzt.");
        return;
    }
    original_set_program = (SetProgramFn)table[SLOT_SET_PROGRAM];
    original_add_parameter = (AddParameterFn)table[SLOT_ADD_PARAMETER];
    original_set_parameter = (SetParameterFn)table[SLOT_SET_PARAMETER];
    table[SLOT_SET_PROGRAM] = (void *)hook_set_program;
    table[SLOT_ADD_PARAMETER] = (void *)hook_add_parameter;
    table[SLOT_SET_PARAMETER] = (void *)hook_set_parameter;
    VirtualProtect(table + SLOT_SET_PROGRAM, (SLOT_SET_PARAMETER - SLOT_SET_PROGRAM + 1) * sizeof(void *), old,
                   &ignored);
    game_ffb_hooked = 1;
    wlog("Spiel-FFB: Lenkrad-Programme des Spiels werden mitgelesen.");
}

/* Patches the three methods in the IWheel method table: of our own wheel object, or of the
   Wheel DLL (our own mapping of it). */
static void hook_wheel_methods(void) {
    if (soft_wheel) {
        if (!raw_buffer) {
            HMODULE m = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
            raw_buffer = m ? (RawBufferFn)(uintptr_t)GetProcAddress(m, "WindowsGetStringRawBuffer") : NULL;
        }
        if (!raw_buffer) {
            wlog("Spiel-FFB: WindowsGetStringRawBuffer fehlt - eigenes FFB wird benutzt.");
            return;
        }
        patch_game_ffb(wheel_soft_wheel_table());
        return;
    }
    void *wheel = NULL;
    if (!wheel_image || ((QIFn)VT(inner)[0])(inner, &IID_Wheel, &wheel) < 0 || !wheel) {
        wlog("Spiel-FFB: IWheel nicht erreichbar - eigenes FFB wird benutzt.");
        return;
    }
    void **table = VT(wheel);
    ((RefFn)VT(wheel)[2])(wheel);
    static const unsigned slots[3] = {SLOT_SET_PROGRAM, SLOT_ADD_PARAMETER, SLOT_SET_PARAMETER};
    int expected = table == (void **)(wheel_image + WHEEL_VTABLE_RVA);
    for (unsigned i = 0; i < 3 && expected; ++i)
        expected = table[slots[i]] == (void *)(wheel_image + wheel_method_rva[i]);
    raw_buffer = *(RawBufferFn *)(wheel_image + WHEEL_RAWBUFFER_IAT);
    if (!expected || !raw_buffer) {
        wlog("Spiel-FFB: unbekannte Wheel-DLL - eigenes FFB wird benutzt.");
        return;
    }
    patch_game_ffb(table);
}

/* ------------------------------------------------------------------ vector wrapper */

typedef struct {
    void **vtbl;
    volatile LONG refs;
    void *real;
} VecWrap;

static HRESULT WINAPI vec_qi(void *self, const GUID *iid, void **out) {
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_Inspectable) || guid_eq(iid, &IID_Agile) ||
        guid_eq(iid, &IID_VectorViewController)) {
        InterlockedIncrement(&((VecWrap *)self)->refs);
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vec_addref(void *self) { return (ULONG)InterlockedIncrement(&((VecWrap *)self)->refs); }
static ULONG WINAPI vec_release(void *self) {
    VecWrap *v = self;
    LONG n = InterlockedDecrement(&v->refs);
    if (!n) {
        ((RefFn)VT(v->real)[2])(v->real);
        HeapFree(GetProcessHeap(), 0, v);
    }
    return (ULONG)n;
}
static HRESULT WINAPI vec_iids(void *self, void *a, void *b) {
    VecWrap *v = self;
    return ((Fn2)VT(v->real)[3])(v->real, a, b);
}
static HRESULT WINAPI vec_name(void *self, void *a) {
    VecWrap *v = self;
    return ((Fn1)VT(v->real)[4])(v->real, a);
}
static HRESULT WINAPI vec_trust(void *self, void *a) {
    VecWrap *v = self;
    return ((Fn1)VT(v->real)[5])(v->real, a);
}
static UINT32 vec_real_size(VecWrap *v) {
    UINT32 n = 0;
    if (((SizeFn)VT(v->real)[7])(v->real, &n) < 0) n = 0;
    return n;
}
static HRESULT WINAPI vec_get_at(void *self, UINT32 index, void **out) {
    VecWrap *v = self;
    UINT32 n = vec_real_size(v);
    if (index < n) return ((GetAtFn)VT(v->real)[6])(v->real, index, out);
    if (index == n) {
        *out = &outer;
        return S_OK;
    }
    *out = NULL;
    return E_BOUNDS;
}
static HRESULT WINAPI vec_size(void *self, UINT32 *out) {
    *out = vec_real_size(self) + 1;
    return S_OK;
}
static HRESULT WINAPI vec_index_of(void *self, void *value, UINT32 *index, BOOLEAN *found) {
    VecWrap *v = self;
    if (value == (void *)&outer) {
        *index = vec_real_size(v);
        *found = 1;
        return S_OK;
    }
    return ((IndexOfFn)VT(v->real)[8])(v->real, value, index, found);
}
static HRESULT WINAPI vec_get_many(void *self, UINT32 start, UINT32 capacity, void **items, UINT32 *actual) {
    VecWrap *v = self;
    UINT32 n = vec_real_size(v), got = 0;
    if (start < n) {
        HRESULT hr = ((GetManyFn)VT(v->real)[9])(v->real, start, capacity, items, &got);
        if (hr < 0) return hr;
    }
    UINT32 total = n + 1;
    if (start <= n && start + got == n && got < capacity && total > start) {
        items[got++] = &outer;
    }
    *actual = got;
    return S_OK;
}
static void *vec_vtbl[10] = {(void *)vec_qi,     (void *)vec_addref, (void *)vec_release,  (void *)vec_iids,
                             (void *)vec_name,   (void *)vec_trust,  (void *)vec_get_at,   (void *)vec_size,
                             (void *)vec_index_of, (void *)vec_get_many};

/* ------------------------------------------------------------------ statics wrapper */

typedef struct {
    void **vtbl;
    volatile LONG refs;
    void *real;
} StaticsWrap;

static HRESULT WINAPI st_qi(void *self, const GUID *iid, void **out) {
    StaticsWrap *s = self;
    if (guid_eq(iid, &IID_Unknown) || guid_eq(iid, &IID_Inspectable) || guid_eq(iid, &IID_Agile) ||
        guid_eq(iid, &IID_ControllerStatics)) {
        InterlockedIncrement(&s->refs);
        *out = self;
        return S_OK;
    }
    return ((QIFn)VT(s->real)[0])(s->real, iid, out);
}
static ULONG WINAPI st_addref(void *self) { return (ULONG)InterlockedIncrement(&((StaticsWrap *)self)->refs); }
static ULONG WINAPI st_release(void *self) {
    StaticsWrap *s = self;
    LONG n = InterlockedDecrement(&s->refs);
    if (!n) {
        ((RefFn)VT(s->real)[2])(s->real);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return (ULONG)n;
}
#define ST_FORWARD(index)                                                                  \
    static HRESULT WINAPI st_fwd##index(void *self, void *a, void *b, void *c) {           \
        StaticsWrap *s = self;                                                             \
        return ((Fn3)VT(s->real)[index])(s->real, a, b, c);                                \
    }
ST_FORWARD(3)
ST_FORWARD(4)
ST_FORWARD(5)
ST_FORWARD(7)
ST_FORWARD(8)
ST_FORWARD(9)
ST_FORWARD(10)
ST_FORWARD(11)
ST_FORWARD(12)

static HRESULT WINAPI st_get_controllers(void *self, void **out) {
    StaticsWrap *s = self;
    void *real = NULL;
    HRESULT hr = ((Fn1)VT(s->real)[6])(s->real, &real);
    if (hr < 0 || !real) return hr;
    VecWrap *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(VecWrap));
    if (!v) {
        *out = real;
        return hr;
    }
    v->vtbl = vec_vtbl;
    v->refs = 1;
    v->real = real;
    *out = v;
    static int logged;
    if (logged++ < 3) wlog("Spiel fragt Controller-Liste ab: %u von Azimuth + 1 Lenkrad.", vec_real_size(v));
    return hr;
}
static void *st_vtbl[13] = {(void *)st_qi,      (void *)st_addref,          (void *)st_release,
                            (void *)st_fwd3,    (void *)st_fwd4,            (void *)st_fwd5,
                            (void *)st_get_controllers, (void *)st_fwd7,   (void *)st_fwd8,
                            (void *)st_fwd9,    (void *)st_fwd10,           (void *)st_fwd11,
                            (void *)st_fwd12};

/* ------------------------------------------------------------------ DirectInput */

typedef struct {
    LONG axis[8];
    DWORD pov;
    BYTE buttons[32];
} JoyState;

static DIOBJECTDATAFORMAT joy_objects[41];
static DIDATAFORMAT joy_format;
static IDirectInput8W *dinput;
static IDirectInputDevice8W *device;
static GUID device_guid;
static wchar_t device_name[MAX_PATH];
static int device_found;

static void build_format(void) {
    const GUID *guids[8] = {&G_XAxis, &G_YAxis, &G_ZAxis, &G_RxAxis, &G_RyAxis, &G_RzAxis, &G_Slider, &G_Slider};
    for (int i = 0; i < 8; ++i) {
        joy_objects[i].pguid = guids[i];
        joy_objects[i].dwOfs = (DWORD)(i * 4);
        joy_objects[i].dwType = DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL;
        joy_objects[i].dwFlags = DIDOI_ASPECTPOSITION;
    }
    joy_objects[8].pguid = &G_POV;
    joy_objects[8].dwOfs = 32;
    joy_objects[8].dwType = DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL;
    joy_objects[8].dwFlags = 0;
    for (int i = 0; i < 32; ++i) {
        joy_objects[9 + i].pguid = NULL;
        joy_objects[9 + i].dwOfs = (DWORD)(36 + i);
        joy_objects[9 + i].dwType = DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL;
        joy_objects[9 + i].dwFlags = 0;
    }
    joy_format.dwSize = sizeof(DIDATAFORMAT);
    joy_format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
    joy_format.dwFlags = DIDF_ABSAXIS;
    joy_format.dwDataSize = sizeof(JoyState);
    joy_format.dwNumObjs = 41;
    joy_format.rgodf = joy_objects;
}

static BOOL CALLBACK enum_device(const DIDEVICEINSTANCEW *instance, void *context) {
    (void)context;
    BYTE type = (BYTE)(instance->dwDevType & 0xff);
    wlog("DirectInput-Geraet: \"%S\" Typ %u", instance->tszProductName, (unsigned)type);
    int match;
    if (cfg.device[0]) {
        match = wide_contains_ci(instance->tszProductName, cfg.device) ||
                wide_contains_ci(instance->tszInstanceName, cfg.device);
    } else {
        match = type == DI8DEVTYPE_DRIVING || wide_contains_ci(instance->tszProductName, L"G920") ||
                wide_contains_ci(instance->tszProductName, L"G29") ||
                wide_contains_ci(instance->tszProductName, L"G923") ||
                wide_contains_ci(instance->tszProductName, L"Wheel") ||
                wide_contains_ci(instance->tszProductName, L"Driving Force") ||
                wide_contains_ci(instance->tszProductName, L"Thrustmaster") ||
                wide_contains_ci(instance->tszProductName, L"Fanatec") ||
                wide_contains_ci(instance->tszProductName, L"MOZA") ||
                wide_contains_ci(instance->tszProductName, L"Simagic");
    }
    if (match && !device_found) {
        device_found = 1;
        device_guid = instance->guidInstance;
        unsigned i = 0;
        for (; instance->tszProductName[i] && i < MAX_PATH - 1; ++i) device_name[i] = instance->tszProductName[i];
        device_name[i] = 0;
    }
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK enum_ffb_device(const DIDEVICEINSTANCEW *instance, void *context) {
    (void)context;
    if ((BYTE)(instance->dwDevType & 0xff) == DI8DEVTYPE_GAMEPAD || device_found) return DIENUM_CONTINUE;
    wlog("Force-Feedback-Geraet ohne bekannten Namen genommen: \"%S\"", instance->tszProductName);
    device_found = 1;
    device_guid = instance->guidInstance;
    unsigned i = 0;
    for (; instance->tszProductName[i] && i < MAX_PATH - 1; ++i) device_name[i] = instance->tszProductName[i];
    device_name[i] = 0;
    return DIENUM_CONTINUE;
}

static int dinput_find(void) {
    HMODULE module = LoadLibraryExW(L"dinput8.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    DI8CreateFn create = module ? (DI8CreateFn)(uintptr_t)GetProcAddress(module, "DirectInput8Create") : NULL;
    if (!create) {
        wlog("FEHLER: dinput8.dll nicht verfuegbar.");
        return 0;
    }
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(uintptr_t)dinput_find, &self);
    HRESULT hr = create((HINSTANCE)self, DIRECTINPUT_VERSION, &IID_DI8W, (void **)&dinput, NULL);
    if (hr < 0 || !dinput) {
        wlog("FEHLER: DirectInput8Create %X", (unsigned)hr);
        return 0;
    }
    dinput->lpVtbl->EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, enum_device, NULL, DIEDFL_ATTACHEDONLY);
    if (!device_found && !cfg.device[0]) { /* a wheel with an unknown name: the first force-feedback device */
        dinput->lpVtbl->EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, enum_ffb_device, NULL,
                                    DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    }
    if (!device_found) {
        wlog("Kein Lenkrad gefunden - Bruecke bleibt aus.");
        return 0;
    }
    wlog("Lenkrad gefunden: \"%S\"", device_name);
    return 1;
}

static int dinput_open(void) {
    HRESULT hr = dinput->lpVtbl->CreateDevice(dinput, &device_guid, &device, NULL);
    if (hr < 0 || !device) {
        wlog("FEHLER: CreateDevice %X", (unsigned)hr);
        return 0;
    }
    build_format();
    hr = device->lpVtbl->SetDataFormat(device, &joy_format);
    if (hr < 0) {
        wlog("FEHLER: SetDataFormat %X", (unsigned)hr);
        return 0;
    }
    for (int i = 0; i < 8; ++i) {
        DIPROPRANGE range;
        zero(&range, sizeof(range));
        range.diph.dwSize = sizeof(DIPROPRANGE);
        range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        range.diph.dwObj = (DWORD)(i * 4);
        range.diph.dwHow = DIPH_BYOFFSET;
        range.lMin = 0;
        range.lMax = 65535;
        device->lpVtbl->SetProperty(device, DIPROP_RANGE, &range.diph);
        DIPROPDWORD dz;
        zero(&dz, sizeof(dz));
        dz.diph.dwSize = sizeof(DIPROPDWORD);
        dz.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dz.diph.dwObj = (DWORD)(i * 4);
        dz.diph.dwHow = DIPH_BYOFFSET;
        dz.dwData = 0;
        device->lpVtbl->SetProperty(device, DIPROP_DEADZONE, &dz.diph);
    }
    DIDEVCAPS caps;
    zero(&caps, sizeof(caps));
    caps.dwSize = sizeof(caps);
    if (device->lpVtbl->GetCapabilities(device, &caps) >= 0) {
        wlog("Geraet: %u Achsen, %u Tasten, %u POV, FFB=%s", (unsigned)caps.dwAxes, (unsigned)caps.dwButtons,
             (unsigned)caps.dwPOVs, (caps.dwFlags & DIDC_FORCEFEEDBACK) ? "ja" : "nein");
    }
    hr = device->lpVtbl->Acquire(device);
    wlog("Acquire -> %X", (unsigned)hr);
    return 1;
}

static void ffb_restart(void);
static HRESULT last_read_error;
static int dinput_read(JoyState *state) {
    HRESULT hr = device->lpVtbl->Poll(device);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        device->lpVtbl->Acquire(device);
        device->lpVtbl->Poll(device);
    }
    hr = device->lpVtbl->GetDeviceState(device, sizeof(JoyState), state);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        if (device->lpVtbl->Acquire(device) >= 0) ffb_restart();
        hr = device->lpVtbl->GetDeviceState(device, sizeof(JoyState), state);
    }
    if (hr < 0) last_read_error = hr;
    return hr >= 0;
}

/* ------------------------------------------------------------------ report synthesis */

static LONG pedal_rest[8];
static int rest_known;
static unsigned rest_decided;

static int pressed(const JoyState *s, int button) {
    return button >= 0 && button < 32 && (s->buttons[button] & 0x80);
}

static unsigned pedal(const JoyState *s, int axis) {
    if (axis < 0 || axis > 7) return 0;
    LONG v = s->axis[axis], r = pedal_rest[axis];
    LONG d = r > 32767 ? r - v : v - r;
    if (d < 0) d = 0;
    LONG span = r > 32767 ? r : 65535 - r;
    if (span < 1000) span = 65535;
    unsigned out = (unsigned)((d * 1023 + span / 2) / span);
    return out > 1023 ? 1023 : out;
}

static double m_ln(double x) {
    if (x <= 0) return -1e30;
    int k = 0;
    while (x < 0.5) { x *= 2.0; ++k; }
    while (x > 1.0) { x *= 0.5; --k; }
    double z = (x - 1.0) / (x + 1.0), z2 = z * z, term = z, sum = 0;
    for (int n = 1; n < 40; n += 2) { sum += term / n; term *= z2; }
    return 2.0 * sum - k * 0.69314718055994531;
}
static double m_exp(double y) {
    int n = 0;
    while (y > 0.35) { y -= 0.69314718055994531; ++n; }
    while (y < -0.35) { y += 0.69314718055994531; --n; }
    double term = 1, sum = 1;
    for (int i = 1; i < 16; ++i) { term *= y / i; sum += term; }
    while (n > 0) { sum *= 2; --n; }
    while (n < 0) { sum *= 0.5; ++n; }
    return sum;
}
/* progressive steering: physical x (0..1 of the wheel's half range) -> game value 1-(1-x)^p */
static uint16_t curve_table[4097];
static int curve_ready;
static void build_curve(void) {
    double p = cfg.curve10 / 10.0;
    for (int i = 0; i <= 4096; ++i) {
        double x = i / 4096.0, y = x;
        if (p > 1.0) y = x >= 1.0 ? 1.0 : 1.0 - m_exp(p * m_ln(1.0 - x));
        int v = (int)(y * 32767.0 + 0.5);
        curve_table[i] = (uint16_t)(v > 32767 ? 32767 : v);
    }
    curve_ready = 1;
}
static unsigned apply_curve(unsigned raw) {
    if (cfg.curve10 <= 10) return raw;
    if (!curve_ready) build_curve();
    long d = (long)raw - 32768;
    long mag = d < 0 ? -d : d;
    if (mag > 32767) mag = 32767;
    long idx = mag * 4096 / 32767;
    long out = curve_table[idx];
    long r = d < 0 ? 32768 - out : 32768 + out;
    if (r < 0) r = 0;
    if (r > 65535) r = 65535;
    return (unsigned)r;
}

/* pedal axes, with "clutch pedal as brake" applied */
static int brake_axis(void) {
    return cfg.clutch_brake && cfg.axis_clutch >= 0 ? cfg.axis_clutch : cfg.axis_brake;
}
static int clutch_axis(void) {
    return cfg.clutch_brake ? -1 : cfg.axis_clutch;
}

static void build_report(const JoyState *s, unsigned char *packet) {
    zero(packet, 0x14 + 0x11);
    *(uint64_t *)packet = WHEEL_ID;
    packet[8] = 0x20;
    *(uint32_t *)(packet + 0xc) = 0x11;
    unsigned char *p = packet + 0x14;
    p[0] = (unsigned char)((pressed(s, cfg.btn_menu) << 2) | (pressed(s, cfg.btn_view) << 3) |
                           (pressed(s, cfg.btn_a) << 4) | (pressed(s, cfg.btn_b) << 5) |
                           (pressed(s, cfg.btn_x) << 6) | (pressed(s, cfg.btn_y) << 7));
    DWORD pov = s->pov;
    int up = 0, down = 0, left = 0, right = 0;
    if ((pov & 0xffff) != 0xffff) {
        unsigned deg = (unsigned)pov / 100;
        up = deg >= 315 || deg <= 45;
        right = deg >= 45 && deg <= 135;
        down = deg >= 135 && deg <= 225;
        left = deg >= 225 && deg <= 315;
    }
    p[1] = (unsigned char)(up | (down << 1) | (left << 2) | (right << 3) |
                           (pressed(s, cfg.btn_paddle_left) << 4) | (pressed(s, cfg.btn_paddle_right) << 5));
    /* free buttons -> game functions (same button as in the layout, or the look hooks) */
    unsigned held = 0;
    for (int k = 0; k < WHEEL_EXTRA_BUTTONS; ++k) {
        int function = cfg.extra_function[k];
        if (function > 0 && function < WHEEL_FUNCTION_COUNT && pressed(s, cfg.extra_button[k]))
            held |= 1u << function;
    }
    wheel_buttons_update(held, &p[0], &p[1]);
    unsigned steer = cfg.axis_steer >= 0 ? (unsigned)s->axis[cfg.axis_steer] : 32768;
    if (steer > 65535) steer = 65535;
    if (cfg.invert_steering) steer = 65535 - steer;
    steer = apply_curve(steer);
    *(uint16_t *)(p + 2) = (uint16_t)steer;
    *(uint16_t *)(p + 4) = (uint16_t)pedal(s, cfg.axis_throttle);
    *(uint16_t *)(p + 6) = (uint16_t)pedal(s, brake_axis());
    *(uint16_t *)(p + 8) = (uint16_t)pedal(s, clutch_axis());
    p[0xa] = 0;
    p[0xb] = 0;
    unsigned char gear = 0;
    if (cfg.shifter) {
        for (int g = 0; g < 6; ++g) {
            if (pressed(s, cfg.gear[g])) gear = (unsigned char)(g + 1);
        }
        if (pressed(s, cfg.gear[6])) gear = (unsigned char)cfg.reverse_value;
    }
    p[0xc] = gear;
    *(uint16_t *)(p + 0xd) = (uint16_t)cfg.rotation;
    p[0xf] = 0;
    p[0x10] = (unsigned char)(0x80 | 0x40 | (clutch_axis() >= 0 ? 0x20 : 0));
}

static void send_static_config(void) {
    unsigned char packet[0x14 + 0x0b];
    zero(packet, sizeof(packet));
    *(uint64_t *)packet = WHEEL_ID;
    packet[8] = 0x21;
    *(uint32_t *)(packet + 0xc) = 0x0b;
    unsigned char *p = packet + 0x14;
    p[0] = 16;                              /* wheel bits */
    p[1] = 10;                              /* throttle bits */
    p[2] = 10;                              /* brake bits */
    p[3] = clutch_axis() >= 0 ? 10 : 0;     /* clutch bits (0 = none) */
    p[4] = 0;                               /* handbrake bits (none) */
    *(uint16_t *)(p + 5) = (uint16_t)cfg.rotation; /* min rotation */
    *(uint16_t *)(p + 7) = (uint16_t)cfg.rotation; /* max rotation */
    p[9] = 1;                               /* force feedback (switched off by setting 0 force) */
    p[0xa] = 0;                             /* flags: rotation not changeable */
    HRESULT hr = ((OnInputFn)VT(input_handler)[6])(input_handler, sizeof(packet), packet);
    wlog("Statische Lenkrad-Konfiguration gesendet -> %X", (unsigned)hr);
}

static void log_new_buttons(const JoyState *s) {
    static BYTE previous[32];
    for (int b = 0; b < 32; ++b) {
        int now = (s->buttons[b] & 0x80) != 0, before = (previous[b] & 0x80) != 0;
        if (now && !before) wlog("Taste %d gedrueckt.", b);
        previous[b] = s->buttons[b];
    }
    static DWORD last_pov = 0xffffffff;
    if (s->pov != last_pov) {
        if ((s->pov & 0xffff) != 0xffff) wlog("Steuerkreuz %u Grad.", (unsigned)s->pov / 100);
        last_pov = s->pov;
    }
}

/* ------------------------------------------------------------------ force feedback (DirectInput) */

__attribute__((unused)) static const GUID G_Spring = {0x13541c27, 0x8e33, 0x11d0, {0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35}};
__attribute__((unused)) static const GUID G_Damper = {0x13541c28, 0x8e33, 0x11d0, {0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35}};

typedef BOOL(WINAPI *EnumWindowsFn)(WNDENUMPROC, LPARAM);
typedef DWORD(WINAPI *WindowPidFn)(HWND, LPDWORD);
typedef BOOL(WINAPI *IsVisibleFn)(HWND);
typedef HWND(WINAPI *GetWindowFn)(HWND, UINT);
static EnumWindowsFn u_enum_windows;
static WindowPidFn u_window_pid;
static IsVisibleFn u_is_visible;
static GetWindowFn u_get_window;
static HWND game_window;
static IDirectInputEffect *fx_lock, *fx_center, *fx_damper, *fx_force, *fx_rumble_a, *fx_rumble_b;
static const GUID G_Constant = {0x13541c20, 0x8e33, 0x11d0, {0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35}};
static const GUID G_Sine = {0x13541c23, 0x8e33, 0x11d0, {0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35}};
static int force_sign = -1; /* Logitech G920: positive magnitude turns towards -X (measured) */
static int ffb_state; /* 0 = not yet, 1 = running, -1 = given up */
static int ffb_tries;
static int ffb_fresh; /* effects were (re)created: resend everything once */

static BOOL CALLBACK find_window(HWND hwnd, LPARAM lparam) {
    (void)lparam;
    DWORD pid = 0;
    u_window_pid(hwnd, &pid);
    if (pid == GetCurrentProcessId() && u_is_visible(hwnd) && !u_get_window(hwnd, 4 /* GW_OWNER */)) {
        game_window = hwnd;
        return FALSE;
    }
    return TRUE;
}

__attribute__((unused)) static IDirectInputEffect *make_condition(const GUID *type, LONG coefficient, LONG deadband, DWORD saturation) {
    DWORD axes[1] = {0}; /* offset of X in our data format */
    LONG direction[1] = {0};
    DICONDITION cond;
    zero(&cond, sizeof(cond));
    cond.lOffset = 0;
    cond.lPositiveCoefficient = coefficient;
    cond.lNegativeCoefficient = coefficient;
    cond.dwPositiveSaturation = saturation;
    cond.dwNegativeSaturation = saturation;
    cond.lDeadBand = deadband;
    DIEFFECT eff;
    zero(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = &cond;
    IDirectInputEffect *effect = NULL;
    HRESULT hr = device->lpVtbl->CreateEffect(device, type, &eff, &effect, NULL);
    if (hr < 0 || !effect) {
        wlog("FFB: Effekt nicht erstellt %X", (unsigned)hr);
        return NULL;
    }
    hr = effect->lpVtbl->Start(effect, 1, 0);
    if (hr < 0) wlog("FFB: Effekt-Start %X", (unsigned)hr);
    return effect;
}

static IDirectInputEffect *make_effect(const GUID *type, void *params, DWORD size) {
    DWORD axes[1] = {0};
    LONG direction[1] = {0};
    DIEFFECT eff;
    zero(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwGain = DI_FFNOMINALMAX;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.cbTypeSpecificParams = size;
    eff.lpvTypeSpecificParams = params;
    IDirectInputEffect *effect = NULL;
    HRESULT hr = device->lpVtbl->CreateEffect(device, type, &eff, &effect, NULL);
    if (hr < 0 || !effect) {
        wlog("FFB: Effekt nicht erstellt %X", (unsigned)hr);
        return NULL;
    }
    effect->lpVtbl->Start(effect, 1, 0);
    return effect;
}

static void set_constant(IDirectInputEffect *e, LONG magnitude) {
    if (!e) return;
    DICONSTANTFORCE cf;
    cf.lMagnitude = magnitude;
    DWORD axes[1] = {0};
    LONG direction[1] = {0};
    DIEFFECT eff;
    zero(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.cbTypeSpecificParams = sizeof(cf);
    eff.lpvTypeSpecificParams = &cf;
    e->lpVtbl->SetParameters(e, &eff, DIEP_TYPESPECIFICPARAMS);
}

static void set_periodic(IDirectInputEffect *e, DWORD magnitude, DWORD period_us) {
    if (!e) return;
    DIPERIODIC pe;
    zero(&pe, sizeof(pe));
    pe.dwMagnitude = magnitude;
    pe.dwPeriod = period_us;
    DWORD axes[1] = {0};
    LONG direction[1] = {0};
    DIEFFECT eff;
    zero(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = direction;
    eff.cbTypeSpecificParams = sizeof(pe);
    eff.lpvTypeSpecificParams = &pe;
    e->lpVtbl->SetParameters(e, &eff, DIEP_TYPESPECIFICPARAMS);
}

__attribute__((unused)) static void set_condition(IDirectInputEffect *e, LONG coefficient) {
    if (!e) return;
    if (coefficient < 0) coefficient = 0;
    if (coefficient > 10000) coefficient = 10000;
    DICONDITION c;
    zero(&c, sizeof(c));
    c.lPositiveCoefficient = coefficient;
    c.lNegativeCoefficient = coefficient;
    c.dwPositiveSaturation = 10000;
    c.dwNegativeSaturation = 10000;
    DIEFFECT eff;
    zero(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.cbTypeSpecificParams = sizeof(c);
    eff.lpvTypeSpecificParams = &c;
    e->lpVtbl->SetParameters(e, &eff, DIEP_TYPESPECIFICPARAMS);
}

/* Game FFB parameters (Xbox wheel "Bristol" program inputs), found by logging:
   p0 = 1 while driving, p1 = self-aligning torque (signed, ~+-0.2),
   p3 = steering weight (0 at rest .. 0.25 at speed, 0 when the tyres are unloaded),
   p7/p9 = rumble amplitudes with frequencies p8 (38 Hz) / p10 (21 Hz). */
static volatile LONG last_steer_raw = 32768;
static ULONGLONG last_vel_at;

/* program output Af (in the game's frame) -> DirectInput force towards +X (before force_sign).
   Only the game's force: no own centring, lock or minimum force. The one addition is a
   protection of the hardware: never push further into the end stop. */
static long game_force(float af, double x) {
    int sign = cfg.invert_steering ? -1 : 1;
    double total = (double)af * sign * 10000.0 * (double)cfg.strength / 100.0;
    if ((x > 0.97 && total > 0) || (x < -0.97 && total < 0)) total = 0;
    if (total > 10000.0) total = 10000.0;
    if (total < -10000.0) total = -10000.0;
    if (!cfg.ffb) total = 0.0;
    return (long)(total + (total >= 0.0 ? 0.5 : -0.5));
}

/* FFB-Art "Spiel": the game's own programs run every 10 ms, their output Af drives the motor
   (Af = 1 is the wheel's full force, like on the Xbox; FFB-Staerke scales it). */
static void ffb_update_game(ULONGLONG now) {
    static ULONGLONG last_tick;
    static int last_rumble = -1, logged;
    if (cfg.rumble != last_rumble) {
        last_rumble = cfg.rumble;
        float scale = (float)cfg.rumble / 100.0f;
        AcquireSRWLockExclusive(&vm_lock);
        ffb_vm_set_scale(&game_vm, L"LeftAmplitude", scale);
        ffb_vm_set_scale(&game_vm, L"RightAmplitude", scale);
        ReleaseSRWLockExclusive(&vm_lock);
    }
    if (!last_tick || now - last_tick > 200) last_tick = now - FFB_VM_TICK_MS;
    int sign = cfg.invert_steering ? -1 : 1;
    double x = ((double)last_steer_raw - 32768.0) / 32768.0; /* physical position, -1..1 */
    static float af;
    unsigned ticks = 0;
    AcquireSRWLockShared(&vm_lock);
    while (now - last_tick >= FFB_VM_TICK_MS && ticks < 5) {
        af = ffb_vm_tick(&game_vm, (float)(x * sign));
        last_tick += FFB_VM_TICK_MS;
        ++ticks;
    }
    unsigned loaded = ffb_vm_loaded(&game_vm);
    ReleaseSRWLockShared(&vm_lock);
    if (!ticks) return;
    if (!logged && loaded) {
        logged = 1;
        wlog("Spiel-FFB aktiv: %u Programme laufen alle 10 ms (Staerke %d%%, Vibration %d%%).", loaded,
             cfg.strength, cfg.rumble);
    }
    long force = game_force(af, x) * force_sign;
    static long last_force = 1;
    if (ffb_fresh) {
        ffb_fresh = 0;
        last_force = 1;
        set_periodic(fx_rumble_a, 0, 26000); /* rumble is part of the programs' output */
        set_periodic(fx_rumble_b, 0, 47000);
    }
    if (force > last_force + 20 || force < last_force - 20 || (force == 0 && last_force != 0)) {
        set_constant(fx_force, force);
        last_force = force;
    }
}

static void ffb_update(void) {
    static ULONGLONG last;
    ULONGLONG now = GetTickCount64();
    if (ffb_state != 1 || now - last < 4) return;
    last = now;
    static int mode_logged = -1;
    int game_mode = cfg.ffb_mode == 0 && game_ffb_hooked;
    if (game_mode != mode_logged) {
        mode_logged = game_mode;
        ffb_fresh = 1;
        wlog("FFB-Art: %s", game_mode ? "Spiel (originale Lenkrad-Programme von FH2)" : "Eigenes FFB der Mod");
    }
    if (game_mode) {
        ffb_update_game(now);
        return;
    }
    int driving = (ffb_seen & 1) && ffb_param[0] > 0.5f;
    float torque = (cfg.ffb && (ffb_seen & 2)) ? ffb_param[1] : 0.0f;
    float weight = (ffb_seen & 8) ? ffb_param[3] : 0.0f;
    float ra = (ffb_seen & (1ull << 7)) ? ffb_param[7] : 0.0f;
    float rb = (ffb_seen & (1ull << 9)) ? ffb_param[9] : 0.0f;
    float fa = (ffb_seen & (1ull << 8)) ? ffb_param[8] : 38.0f;
    float fb = (ffb_seen & (1ull << 10)) ? ffb_param[10] : 21.0f;
    /* game stopped sending (loading, closing, frozen): no stale forces */
    static LONG seen_updates;
    static ULONGLONG updates_at;
    if (ffb_updates != seen_updates) {
        seen_updates = ffb_updates;
        updates_at = now;
    }
    if (now - updates_at > 1500) driving = 0;
    if (!driving) torque = weight = ra = rb = 0.0f;
    if (!cfg.ffb) ra = rb = 0.0f; /* switched off in the menu: no game rumble either */
    /* All forces are computed here and sent as ONE constant force (the G920's condition effects
       misbehaved: one-sided pull that even survived the game). Units: +10000 = full push to +X. */
    double x = ((double)last_steer_raw - 32768.0) / 32768.0; /* -1..1 = -450..450 deg */
    static double last_x, vel;
    double dt = (double)(now - last_vel_at) / 1000.0;
    /* velocity from a 16-bit encoder sampled every few ms is noisy; heavy filter, else the damper rasps */
    if (dt > 0.0 && dt < 0.2) {
        double a = 1.0 - m_exp(-dt / 0.030);
        vel += (((x - last_x) / dt) - vel) * a;
    }
    last_x = x;
    last_vel_at = now;
    /* the game sends steps at ~60 Hz: smooth them, but let impacts (big jumps) through at once */
    static double smooth_torque;
    double raw_t = (double)torque;
    if (dt > 0.0 && dt < 0.2 && cfg.smoothing > 0) {
        double jump = raw_t - smooth_torque;
        if (jump > 0.12 || jump < -0.12) smooth_torque += jump * 0.7;
        else smooth_torque += jump * (1.0 - m_exp(-dt / ((double)cfg.smoothing / 1000.0)));
    } else {
        smooth_torque = raw_t;
    }
    double total = 0.0;
    double game = smooth_torque * 20000.0 * (double)cfg.strength / 100.0;
    if ((game > 0 && x > 0.03) || (game < 0 && x < -0.03)) game *= (double)cfg.outward / 100.0;
    total += game;
    if (!driving) total += -x * (double)cfg.centering * 400.0;           /* menu centring */
    double damp = (double)cfg.damping * 100.0 + (double)weight * 12000.0 * (double)cfg.strength / 100.0;
    total += -vel * damp * 0.5;                                          /* steering weight / damping */
    if (driving && cfg.friction > 0) {                                   /* soft friction, like a real rack */
        double v = vel * 25.0;
        double t = v > 3 ? 1 : v < -3 ? -1 : v * (27 + v * v) / (27 + 9 * v * v); /* tanh approx */
        total += -t * (double)cfg.friction * 100.0;
    }
    double lock = (double)cfg.lock / 450.0;
    if (lock < 1.0 && (x > lock || x < -lock)) {                         /* soft lock */
        double over = (x > 0 ? x - lock : x + lock) * 450.0;              /* degrees past the lock */
        double push = 2500.0 + (over < 0 ? -over : over) * 600.0;
        if (push > 10000.0) push = 10000.0;
        total += x > 0 ? -push : push;
    }
    if ((x > 0.97 && total > 0) || (x < -0.97 && total < 0)) total = 0;  /* never push into the end stop */
    /* runaway guard: we push towards the centre, but the wheel races outwards -> direction is wrong */
    static ULONGLONG runaway_since;
    int inward = (x > 0.15 && total < -2500) || (x < -0.15 && total > 2500);
    int outward_fast = (x > 0 && vel > 1.2) || (x < 0 && vel < -1.2);
    if (inward && outward_fast) {
        if (!runaway_since) runaway_since = now;
        else if (now - runaway_since > 120) {
            force_sign = -force_sign;
            runaway_since = 0;
            wlog("FFB: Lenkrad lief gegen die Kraft weg -> Richtung umgedreht (jetzt %d).", force_sign);
        }
    } else {
        runaway_since = 0;
    }
    /* soft clipping instead of a hard wall at full force */
    {
        double u = total / 10000.0;
        double t = u > 3 ? 1 : u < -3 ? -1 : u * (27 + u * u) / (27 + 9 * u * u);
        total = t * 10000.0;
    }
    /* the G920's gears swallow very small forces: lift them slightly */
    if (cfg.minforce > 0 && (total > 60 || total < -60)) {
        double mf = (double)cfg.minforce * 100.0;
        total = total > 0 ? mf + total * (1.0 - mf / 10000.0) : -mf + total * (1.0 - mf / 10000.0);
    }
    if (total > 10000.0) total = 10000.0;
    if (total < -10000.0) total = -10000.0;
    if (!cfg.ffb) total = 0.0; /* Force Feedback off: really no force at all on the wheel */
    long force = (long)total * force_sign;
    static long last_force = 1;
    static DWORD last_ra = 1, last_rb = 1;
    if (ffb_fresh) {
        ffb_fresh = 0;
        last_force = 1;
        last_ra = last_rb = 1;
    }
    if (force > last_force + 40 || force < last_force - 40 || (force == 0 && last_force != 0)) {
        set_constant(fx_force, force);
        last_force = force;
    }
    static float env_a, env_b;
    env_a += (ra - env_a) * 0.25f;
    env_b += (rb - env_b) * 0.25f;
    DWORD ma = (DWORD)(env_a * 40000.0f * (float)cfg.rumble / 100.0f), mb = (DWORD)(env_b * 40000.0f * (float)cfg.rumble / 100.0f);
    ma -= ma % 50;
    mb -= mb % 50;
    if (ma > 10000) ma = 10000;
    if (mb > 10000) mb = 10000;
    if (ma != last_ra) {
        set_periodic(fx_rumble_a, ma, fa > 1.0f ? (DWORD)(1000000.0f / fa) : 26000);
        last_ra = ma;
    }
    if (mb != last_rb) {
        set_periodic(fx_rumble_b, mb, fb > 1.0f ? (DWORD)(1000000.0f / fb) : 47000);
        last_rb = mb;
    }
}

/* Which way does a positive constant force turn this wheel? Short gentle pulse at start. */
static void calibrate_direction(void) {
    if (cfg.ffb_sign == 1 || cfg.ffb_sign == -1) {
        force_sign = cfg.ffb_sign;
        wlog("FFB-Richtung fest aus INI: %d", force_sign);
        return;
    }
    JoyState s;
    zero(&s, sizeof(s));
    if (!dinput_read(&s)) return;
    LONG start = s.axis[0];
    set_constant(fx_force, 3500);
    LONG moved = 0;
    for (int i = 0; i < 25; ++i) {
        Sleep(10);
        if (dinput_read(&s)) moved = s.axis[0] - start;
        if (moved > 600 || moved < -600) break;
    }
    set_constant(fx_force, 0);
    /* game: wheel left (-X) gives p1 > 0, i.e. p1 is the push towards +X */
    if (moved > 400) force_sign = 1;
    else if (moved < -150) force_sign = -1;
    wlog("FFB-Richtung: Testimpuls bewegte das Lenkrad um %d -> Vorzeichen %d%s", (int)moved, force_sign,
         (moved <= 400 && moved >= -150) ? " (unklar, Standard -1)" : "");
}

static void ffb_restart(void) {
    if (fx_lock) fx_lock->lpVtbl->Start(fx_lock, 1, 0);
    if (fx_center) fx_center->lpVtbl->Start(fx_center, 1, 0);
    if (fx_damper) fx_damper->lpVtbl->Start(fx_damper, 1, 0);
    if (fx_force) fx_force->lpVtbl->Start(fx_force, 1, 0);
    if (fx_rumble_a) fx_rumble_a->lpVtbl->Start(fx_rumble_a, 1, 0);
    if (fx_rumble_b) fx_rumble_b->lpVtbl->Start(fx_rumble_b, 1, 0);
}

static void ffb_setup(void) {
    if (ffb_state || ++ffb_tries > 60) {
        if (!ffb_state) {
            ffb_state = -1;
            wlog("FFB: aufgegeben (kein Spielfenster / kein exklusiver Zugriff).");
        }
        return;
    }
    if (!u_enum_windows) {
        HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!user32) return;
        u_enum_windows = (EnumWindowsFn)(uintptr_t)GetProcAddress(user32, "EnumWindows");
        u_window_pid = (WindowPidFn)(uintptr_t)GetProcAddress(user32, "GetWindowThreadProcessId");
        u_is_visible = (IsVisibleFn)(uintptr_t)GetProcAddress(user32, "IsWindowVisible");
        u_get_window = (GetWindowFn)(uintptr_t)GetProcAddress(user32, "GetWindow");
        if (!u_enum_windows || !u_window_pid || !u_is_visible || !u_get_window) {
            u_enum_windows = NULL;
            return;
        }
    }
    game_window = NULL;
    u_enum_windows(find_window, 0);
    if (!game_window) return;
    device->lpVtbl->Unacquire(device);
    HRESULT hr = device->lpVtbl->SetCooperativeLevel(device, game_window, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (hr < 0) {
        wlog("FFB: exklusiver Zugriff (Hintergrund) abgelehnt %X - versuche Vordergrund.", (unsigned)hr);
        hr = device->lpVtbl->SetCooperativeLevel(device, game_window, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    }
    if (hr < 0) {
        wlog("FFB: kein exklusiver Zugriff %X", (unsigned)hr);
        device->lpVtbl->SetCooperativeLevel(device, game_window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        device->lpVtbl->Acquire(device);
        ffb_state = -1;
        return;
    }
    hr = device->lpVtbl->Acquire(device);
    if (hr < 0) {
        wlog("FFB: Acquire exklusiv %X (spaeter erneut)", (unsigned)hr);
        return;
    }
    DIPROPDWORD ac;
    zero(&ac, sizeof(ac));
    ac.diph.dwSize = sizeof(DIPROPDWORD);
    ac.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    ac.diph.dwHow = DIPH_DEVICE;
    ac.dwData = DIPROPAUTOCENTER_OFF;
    device->lpVtbl->SetProperty(device, DIPROP_AUTOCENTER, &ac.diph);
    /* soft lock: spring that only acts beyond the car's full-lock angle */
    LONG deadband = (LONG)((long long)cfg.lock * 10000 / 450);
    if (deadband > 9900) deadband = 9900;
    {
        DICONSTANTFORCE cf;
        cf.lMagnitude = 0;
        fx_force = make_effect(&G_Constant, &cf, sizeof(cf));
        calibrate_direction();
    }
    if (cfg.ffb) {
        DIPERIODIC pe;
        zero(&pe, sizeof(pe));
        pe.dwPeriod = 26000;
        fx_rumble_a = make_effect(&G_Sine, &pe, sizeof(pe));
        pe.dwPeriod = 47000;
        fx_rumble_b = make_effect(&G_Sine, &pe, sizeof(pe));
    }
    (void)deadband;
    ffb_fresh = 1;
    ffb_state = 1;
    wlog("FFB aktiv: Anschlag %d Grad, Zentrierung %d%%, Daempfung %d%%, Spielkraefte %s (Staerke %d%%, Vibration %d%%).",
         cfg.lock, cfg.centering, cfg.damping, fx_force ? "an" : "aus", cfg.strength, cfg.rumble);
}

/* CSV with the game's force feedback parameters, for analysing the real game FFB */
static HANDLE csv_file;
static void csv_put(char **o, char *end, long long v) {
    if (v < 0) {
        fmt_put(o, end, '-');
        v = -v;
    }
    fmt_uint(o, end, (unsigned long long)v, 10, 1);
}
static void csv_write(const JoyState *s, ULONGLONG now) {
    static ULONGLONG start, last;
    static unsigned lines;
    if (!cfg.ffb_csv || lines > 40000 || now - last < 50) return;
    last = now;
    if (!csv_file) {
        static wchar_t path[PATH_CAPACITY];
        unsigned n = 0, slash = 0;
        for (; log_path[n]; ++n) {
            path[n] = log_path[n];
            if (log_path[n] == L'\\' || log_path[n] == L'/') slash = n + 1;
        }
        path[slash] = 0;
        if (!path_join(path, path, L"FH2_Wheel_ffb.csv")) return;
        csv_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (csv_file == INVALID_HANDLE_VALUE) {
            csv_file = NULL;
            cfg.ffb_csv = 0;
            return;
        }
        start = now;
        static const char header[] = "ms;lenk_grad;gas;bremse;updates;p0;p1;p2;p3;p4;p5;p6;p7;p8;p9;p10;p11;p12;p13;p14;p15;p16;p17;p18;p19;p20;p21;p22;p23;p24;p25;p26;p27;p28;p29;p30;p31\r\n";
        DWORD w;
        WriteFile(csv_file, header, sizeof(header) - 1, &w, NULL);
    }
    char line[700];
    char *o = line, *end = line + sizeof(line) - 4;
    csv_put(&o, end, (long long)(now - start));
    fmt_put(&o, end, ';');
    csv_put(&o, end, (long long)(((long long)s->axis[cfg.axis_steer >= 0 ? cfg.axis_steer : 0] - 32768) * 4500 / 32768)); /* 0.1 deg */
    fmt_put(&o, end, ';');
    csv_put(&o, end, (long long)pedal(s, cfg.axis_throttle));
    fmt_put(&o, end, ';');
    csv_put(&o, end, (long long)pedal(s, brake_axis()));
    fmt_put(&o, end, ';');
    csv_put(&o, end, ffb_updates);
    for (int i = 0; i < 32; ++i) {
        fmt_put(&o, end, ';');
        if (ffb_seen & (1ull << i)) csv_put(&o, end, (long long)(ffb_param[i] * 1000.0f));
    }
    *o++ = '\r';
    *o++ = '\n';
    DWORD w;
    WriteFile(csv_file, line, (DWORD)(o - line), &w, NULL);
    ++lines;
}

static int deliver(const unsigned char *packet, unsigned size) {
    __try {
        ((OnInputFn)VT(input_handler)[6])(input_handler, size, packet);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wlog("FEHLER: Ausnahme %X in der Wheel-DLL beim Melden der Eingabe.", (unsigned)GetExceptionCode());
        return 0;
    }
}

static ULONGLONG live_log_at;

/* Takes over values changed in the in-game menu (runs on the poll thread). */
static void live_apply(void) {
    LONG generation = live_generation;
    if (generation == applied_generation) return;
    applied_generation = generation;
    int old_ffb = cfg.ffb, old_rotation = cfg.rotation, old_curve = cfg.curve10, old_clutch = cfg.clutch_brake;
    for (unsigned id = 0; id < RIG_WHEEL_COUNT; ++id) {
        int *field = cfg_field(id);
        if (field && id != RIG_WHEEL_ENABLED) *field = (int)live_values[id];
    }
    if (cfg.curve10 != old_curve) curve_ready = 0; /* rebuilt by the next report */
    if (cfg.ffb && !old_ffb && ffb_state == 1 && !fx_rumble_a && !fx_rumble_b) {
        DIPERIODIC pe;
        zero(&pe, sizeof(pe));
        pe.dwPeriod = 26000;
        fx_rumble_a = make_effect(&G_Sine, &pe, sizeof(pe));
        pe.dwPeriod = 47000;
        fx_rumble_b = make_effect(&G_Sine, &pe, sizeof(pe));
    }
    /* the game learns the rotation and whether there is a clutch from the static configuration */
    if ((cfg.rotation != old_rotation || cfg.clutch_brake != old_clutch) && input_handler) {
        __try {
            send_static_config();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            wlog("FEHLER: Ausnahme %X beim erneuten Senden der Lenkrad-Konfiguration.",
                 (unsigned)GetExceptionCode());
        }
    }
    live_log_at = GetTickCount64() + 500;
}

static void live_log(ULONGLONG now) {
    if (!live_log_at || now < live_log_at) return;
    live_log_at = 0;
    wlog("Ingame-Menue: ForceFeedback=%d FFBMode=%d FFBStrength=%d Rumble=%d Damping=%d Friction=%d Smoothing=%d "
         "MinimumForce=%d OutwardForce=%d Centering=%d Lock=%d Curve=%d Rotation=%d Shifter=%d ClutchAsBrake=%d",
         cfg.ffb, cfg.ffb_mode, cfg.strength, cfg.rumble, cfg.damping, cfg.friction, cfg.smoothing,
         cfg.minforce, cfg.outward, cfg.centering, cfg.lock, cfg.curve10, cfg.rotation, cfg.shifter,
         cfg.clutch_brake);
}

/* ------------------------------------------------------------------ reconnect (cable pulled / USB hiccup) */

static void release_effect(IDirectInputEffect **effect) {
    if (*effect) {
        (*effect)->lpVtbl->Stop(*effect);
        (*effect)->lpVtbl->Release(*effect);
        *effect = NULL;
    }
}

/* The wheel is gone: drop the device and its effects, everything is set up again on reconnect. */
static void device_close(void) {
    int was_ffb = ffb_state == 1;
    ffb_state = 0; /* first, so nothing else touches the effects any more */
    ffb_tries = 0;
    release_effect(&fx_rumble_b);
    release_effect(&fx_rumble_a);
    release_effect(&fx_force);
    release_effect(&fx_damper);
    release_effect(&fx_center);
    release_effect(&fx_lock);
    if (device) {
        IDirectInputDevice8W *d = device;
        device = NULL;
        if (was_ffb) d->lpVtbl->SendForceFeedbackCommand(d, DISFFC_RESET);
        d->lpVtbl->Unacquire(d);
        d->lpVtbl->Release(d);
    }
    /* DirectInput may report all-zero axes right after reconnecting: skip those like at game
       start (the pedal rest positions found earlier stay valid) */
    rest_known = 0;
}

static int device_attached(void);

/* Looks for the wheel again (it may come back with a new instance GUID). */
static int device_reopen(void) {
    device_found = 0;
    if (!dinput || dinput->lpVtbl->EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, enum_device, NULL,
                                              DIEDFL_ATTACHEDONLY) < 0) {
        return 0;
    }
    if (!device_found && !cfg.device[0])
        dinput->lpVtbl->EnumDevices(dinput, DI8DEVCLASS_GAMECTRL, enum_ffb_device, NULL,
                                    DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    if (!device_found || !device_attached()) {
        return 0;
    }
    return dinput_open();
}

/* Asks Windows whether the wheel is still plugged in (G HUB can keep a silent device alive). */
static int status_usable = -1; /* decided once while the wheel is known to work */
static int device_attached(void) {
    if (!dinput || !status_usable) return 1;
    HRESULT hr = dinput->lpVtbl->GetDeviceStatus(dinput, &device_guid);
    if (status_usable < 0) {
        status_usable = hr != DI_NOTATTACHED;
        wlog("Anschluss-Pruefung per Windows: %s (%X).", status_usable ? "an" : "aus, liefert falsche Werte",
             (unsigned)hr);
        return 1;
    }
    return hr != DI_NOTATTACHED;
}

/* While the wheel is missing the game gets "hands off": wheel centred, pedals released, no buttons. */
static void neutral_report(unsigned char *packet) {
    JoyState neutral;
    zero(&neutral, sizeof(neutral));
    for (int i = 0; i < 8; ++i) neutral.axis[i] = pedal_rest[i];
    if (cfg.axis_steer >= 0) neutral.axis[cfg.axis_steer] = 32768;
    neutral.pov = 0xffffffff;
    build_report(&neutral, packet);
}

/* ------------------------------------------------------------------ setup assistant (any wheel) */

/* Started from the menu (Input: Profile > Set up wheel). A card asks for one control at a time;
   the axis that moves the most (or the button that goes down) is taken. At the end the result
   is used at once and saved to FH2_Wheel.ini. The game gets "hands off" meanwhile.
   Steering, throttle and brake are needed (20 s each, else nothing is changed); clutch and
   buttons can be skipped by waiting 10 s (the old value stays). */
enum {
    SETUP_STEER,
    SETUP_THROTTLE,
    SETUP_BRAKE,
    SETUP_CLUTCH,
    SETUP_PADDLE_RIGHT,
    SETUP_PADDLE_LEFT,
    SETUP_A,
    SETUP_B,
    SETUP_STEPS
};
#define SETUP_TAG 0x5e7u
#define SETUP_NEEDED_MS 20000u
#define SETUP_OPTIONAL_MS 10000u

static const char *const setup_texts[SETUP_STEPS] = {
    "Wheel setup 1/8\nTurn the wheel fully left and right",
    "Wheel setup 2/8\nPress the throttle fully, then let go",
    "Wheel setup 3/8\nPress the brake fully, then let go",
    "Wheel setup 4/8\nPress the clutch (none: wait 10 s)",
    "Wheel setup 5/8\nPress the right paddle (shift up)",
    "Wheel setup 6/8\nPress the left paddle (shift down)",
    "Wheel setup 7/8\nPress the button for A (confirm)",
    "Wheel setup 8/8\nPress the button for B (back)",
};

static volatile LONG setup_request;
static int setup_step = -1;
static ULONGLONG setup_step_at;
static LONG setup_lo[8], setup_hi[8], setup_start[8];
static BYTE setup_held[32];
static int setup_candidate;
static int setup_result[SETUP_STEPS];

int wheel_bridge_start_setup(void) {
    if (!wheel_ready) return 0;
    InterlockedExchange(&setup_request, 1);
    return 1;
}

static void setup_begin_step(int step, const JoyState *s) {
    setup_step = step;
    setup_step_at = GetTickCount64();
    setup_candidate = -1;
    for (int i = 0; i < 8; ++i) setup_lo[i] = setup_hi[i] = setup_start[i] = s->axis[i];
    for (int b = 0; b < 32; ++b) setup_held[b] = s->buttons[b] & 0x80;
    notify_show_step(setup_texts[step], SETUP_TAG, step <= SETUP_BRAKE ? SETUP_NEEDED_MS : SETUP_OPTIONAL_MS);
}

static int setup_axis_used(int axis, int step) {
    for (int k = 0; k < step && k <= SETUP_CLUTCH; ++k)
        if (setup_result[k] == axis) return 1;
    return 0;
}

static LONG setup_abs(LONG v) {
    return v < 0 ? -v : v;
}

/* 1 = this step is done (setup_result[step] set, -1 = skipped) */
static int setup_check(int step, const JoyState *s) {
    if (step == SETUP_STEER) { /* the axis with the widest swing, once it is back near the middle */
        int best = -1;
        LONG best_range = 0;
        for (int i = 0; i < 8; ++i) {
            if (s->axis[i] < setup_lo[i]) setup_lo[i] = s->axis[i];
            if (s->axis[i] > setup_hi[i]) setup_hi[i] = s->axis[i];
            if (setup_hi[i] - setup_lo[i] > best_range) best_range = setup_hi[i] - setup_lo[i], best = i;
        }
        if (best >= 0 && best_range >= 40000 && setup_abs(s->axis[best] - (setup_lo[best] + setup_hi[best]) / 2) < 10000) {
            setup_result[step] = best;
            return 1;
        }
        return 0;
    }
    if (step <= SETUP_CLUTCH) { /* a pedal: the axis that went furthest from where it was, once let go */
        if (setup_candidate < 0) {
            LONG best_move = 0;
            for (int i = 0; i < 8; ++i) {
                if (setup_axis_used(i, step)) continue;
                LONG move = setup_abs(s->axis[i] - setup_start[i]);
                if (move > best_move) best_move = move, setup_candidate = i;
            }
            if (best_move < 30000) setup_candidate = -1;
            return 0;
        }
        if (setup_abs(s->axis[setup_candidate] - setup_start[setup_candidate]) < 8000) {
            setup_result[step] = setup_candidate;
            return 1;
        }
        return 0;
    }
    /* a button: one that was not held when the step began, taken when it is let go */
    if (setup_candidate < 0) {
        for (int b = 0; b < 32; ++b) {
            if (!(s->buttons[b] & 0x80)) setup_held[b] = 0;
            else if (!setup_held[b]) {
                setup_candidate = b;
                break;
            }
        }
        return 0;
    }
    if (!(s->buttons[setup_candidate] & 0x80)) {
        setup_result[step] = setup_candidate;
        return 1;
    }
    return 0;
}

static char setup_file[16384], setup_edit[2][16384];

static void setup_save(void) {
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD length = 0;
    if (file != INVALID_HANDLE_VALUE) {
        if (!ReadFile(file, setup_file, sizeof(setup_file) - 1, &length, NULL)) length = 0;
        CloseHandle(file);
    }
    static const char *const keys[SETUP_STEPS] = {"SteeringAxis", "ThrottleAxis", "BrakeAxis", "ClutchAxis",
                                                  "PaddleRight", "PaddleLeft", "ButtonA", "ButtonB"};
    const char *source = setup_file;
    size_t source_length = length;
    unsigned slot = 0;
    for (int k = 0; k < SETUP_STEPS; ++k) {
        char number[12];
        const char *value;
        if (k <= SETUP_CLUTCH) {
            int axis = k == SETUP_CLUTCH ? cfg.axis_clutch : setup_result[k];
            value = axis >= 0 && axis < 8 ? axis_names[axis] : "-";
        } else {
            static int *const fields[4] = {&cfg.btn_paddle_right, &cfg.btn_paddle_left, &cfg.btn_a, &cfg.btn_b};
            value = ini_format_int(*fields[k - SETUP_PADDLE_RIGHT], number);
        }
        size_t written = ini_set_value(source, source_length, "Wheel", keys[k], value, setup_edit[slot],
                                       sizeof(setup_edit[slot]));
        if (!written) {
            wlog("Einrichtung: FH2_Wheel.ini zu gross, nicht gespeichert.");
            return;
        }
        source = setup_edit[slot];
        source_length = written;
        slot ^= 1;
    }
    char done[2] = {'1', 0};
    size_t written = ini_set_value(source, source_length, "Wheel", "SetupDone", done, setup_edit[slot],
                                   sizeof(setup_edit[slot]));
    if (written) source = setup_edit[slot], source_length = written;
    file = CreateFileW(ini_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        wlog("Einrichtung: FH2_Wheel.ini nicht schreibbar.");
        return;
    }
    DWORD out;
    WriteFile(file, source, (DWORD)source_length, &out, NULL);
    CloseHandle(file);
}

/* Runs in the poll thread with the raw reading. Returns 1 while the assistant has the wheel. */
static int known_wheel_name(const wchar_t *name);

static int setup_run(const JoyState *s) {
    static int hint_checked;
    if (!hint_checked && ini_path[0]) { /* a wheel Gripline has no layout for: point to the assistant once */
        hint_checked = 1;
        if (!cfg.device[0] && !known_wheel_name(device_name) && read_int(L"SetupDone", 0) != 1) {
            wlog("Unbekanntes Lenkrad \"%S\" - Hinweis auf den Einrichtungs-Assistenten.", device_name);
            notify_show_deferred("New wheel - set it up in GRIPLINE\nInput: Profile > Set up wheel");
        }
    }
    if (setup_step < 0) {
        if (!InterlockedExchange(&setup_request, 0)) return 0;
        wlog("Einrichtungs-Assistent gestartet.");
        setup_begin_step(SETUP_STEER, s);
        return 1;
    }
    int step = setup_step;
    if (setup_check(step, s)) {
        if (step <= SETUP_CLUTCH) wlog("Einrichtung: Schritt %d = Achse %s", step + 1, axis_names[setup_result[step]]);
        else wlog("Einrichtung: Schritt %d = Taste %d", step + 1, setup_result[step]);
    } else if (GetTickCount64() - setup_step_at >= (step <= SETUP_BRAKE ? SETUP_NEEDED_MS : SETUP_OPTIONAL_MS)) {
        if (step <= SETUP_BRAKE) {
            wlog("Einrichtung abgebrochen (Schritt %d: nichts erkannt).", step + 1);
            notify_show_step("Wheel setup cancelled\nNothing changed", SETUP_TAG, 5000);
            setup_step = -1;
            return 1;
        }
        setup_result[step] = -1;
        wlog("Einrichtung: Schritt %d uebersprungen.", step + 1);
    } else {
        return 1;
    }
    if (step + 1 < SETUP_STEPS) {
        setup_begin_step(step + 1, s);
        return 1;
    }
    /* done: use it now (this is the poll thread, the only one that reads cfg) */
    setup_step = -1;
    cfg.axis_steer = setup_result[SETUP_STEER];
    cfg.axis_throttle = setup_result[SETUP_THROTTLE];
    cfg.axis_brake = setup_result[SETUP_BRAKE];
    if (setup_result[SETUP_CLUTCH] >= 0) cfg.axis_clutch = setup_result[SETUP_CLUTCH];
    else cfg.axis_clutch = -1;
    int *buttons[4] = {&cfg.btn_paddle_right, &cfg.btn_paddle_left, &cfg.btn_a, &cfg.btn_b};
    for (int k = 0; k < 4; ++k)
        if (setup_result[SETUP_PADDLE_RIGHT + k] >= 0) *buttons[k] = setup_result[SETUP_PADDLE_RIGHT + k];
    for (int i = 0; i < 8; ++i) /* the pedals were let go: this is their rest position */
        if (i == cfg.axis_throttle || i == cfg.axis_brake || i == cfg.axis_clutch) {
            pedal_rest[i] = s->axis[i] > 32767 ? 65535 : 0;
            rest_decided |= 1u << i;
        }
    setup_save();
    wlog("Einrichtung fertig: Lenkung %s, Gas %s, Bremse %s, Kupplung %s, Wippen %d/%d, A %d, B %d",
         axis_names[cfg.axis_steer], axis_names[cfg.axis_throttle], axis_names[cfg.axis_brake],
         cfg.axis_clutch >= 0 ? axis_names[cfg.axis_clutch] : "-", cfg.btn_paddle_right, cfg.btn_paddle_left,
         cfg.btn_a, cfg.btn_b);
    notify_show_step(cfg.axis_clutch >= 0 ? "Wheel set up and saved\nSteering, pedals, clutch and buttons"
                                          : "Wheel set up and saved\nSteering, pedals and buttons (no clutch)",
                     SETUP_TAG, 6000);
    return 1;
}

static int known_wheel_name(const wchar_t *name) {
    return wide_contains_ci(name, L"G920") || wide_contains_ci(name, L"G29") || wide_contains_ci(name, L"G923") ||
           wide_contains_ci(name, L"Driving Force");
}

static DWORD WINAPI poll_thread(LPVOID unused) {
    (void)unused;
    unsigned char packet[0x14 + 0x11], last[0x14 + 0x11];
    zero(last, sizeof(last));
    JoyState state;
    LONG mins[8], maxs[8];
    for (int i = 0; i < 8; ++i) {
        mins[i] = 65535;
        maxs[i] = 0;
    }
    ULONGLONG last_send = 0, last_summary = GetTickCount64(), last_retry = 0, opened_at = 0, last_ffb_try = 0;
    int opened = 0, failures = 0, lost = 0, disconnects = 0, missing = 0;
    ULONGLONG last_status = 0;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        live_apply();
        live_log(now);
        if (!opened) {
            if (lost && now - last_send >= 100) { /* keep the game fed with "hands off" */
                neutral_report(packet);
                if (!deliver(packet, sizeof(packet))) {
                    wlog("Lenkrad-Thread beendet.");
                    return 0;
                }
                memory_copy(last, packet, sizeof(packet));
                last_send = now;
            }
            if (now - last_retry > 2000) {
                last_retry = now;
                opened = lost ? device_reopen() : dinput_open();
                opened_at = GetTickCount64();
                if (opened && lost) {
                    if (disconnects <= 20) wlog("Lenkrad wieder verbunden - Force Feedback wird neu eingerichtet.");
                    lost = 0;
                    failures = 0;
                    last_ffb_try = 0;
                }
            }
            Sleep(50);
            continue;
        }
        zero(&state, sizeof(state));
        if (!dinput_read(&state)) {
            ++failures;
            if (failures == 25) { /* ~0.5 s: let go of the pedals at once, no force left behind */
                if (ffb_state == 1 && fx_force) {
                    set_constant(fx_force, 0);
                    ffb_fresh = 1;
                }
                neutral_report(packet);
                if (deliver(packet, sizeof(packet))) {
                    memory_copy(last, packet, sizeof(packet));
                    last_send = now;
                }
            }
            if (failures == 50 && disconnects < 20)
                wlog("Lesen vom Lenkrad schlaegt fehl (%X, Verbindung?).", (unsigned)last_read_error);
            if (failures >= 100) { /* ~2 s: treat as unplugged and wait for it to come back */
                if (++disconnects <= 20)
                    wlog("Lenkrad getrennt - Spiel bekommt Neutralstellung, suche alle 2 s erneut.");
                device_close();
                opened = 0;
                lost = 1;
                last_retry = now;
            }
            Sleep(20);
            continue;
        }
        if (failures >= 50) wlog("Lenkrad liefert wieder Daten.");
        failures = 0;
        if (now - last_status >= 1000) { /* reads can keep "working" after unplugging (G HUB) */
            last_status = now;
            if (!device_attached()) {
                if (++missing >= 2) {
                    if (++disconnects <= 20)
                        wlog("Windows meldet: Lenkrad nicht mehr angeschlossen - Neutralstellung, suche alle 2 s erneut.");
                    if (ffb_state == 1 && fx_force) set_constant(fx_force, 0);
                    neutral_report(packet);
                    if (deliver(packet, sizeof(packet))) {
                        memory_copy(last, packet, sizeof(packet));
                        last_send = now;
                    }
                    device_close();
                    opened = 0;
                    lost = 1;
                    missing = 0;
                    last_retry = now;
                    continue;
                }
            } else {
                missing = 0;
            }
        }
        if (!rest_known) {
            /* DirectInput can return all-zero axes until the first device report arrives */
            int any = 0;
            for (int i = 0; i < 8; ++i) any |= state.axis[i] != 0;
            if (!any && now - opened_at < 1500) {
                Sleep(10);
                continue;
            }
            for (int i = 0; i < 8; ++i) /* Logitech: released = top of range */
                if (!(rest_decided & (1u << i))) pedal_rest[i] = 65535;
            rest_known = 1;
            wlog("Startwerte: X=%d Y=%d Z=%d Rx=%d Ry=%d Rz=%d S0=%d S1=%d", (int)state.axis[0], (int)state.axis[1],
                 (int)state.axis[2], (int)state.axis[3], (int)state.axis[4], (int)state.axis[5], (int)state.axis[6],
                 (int)state.axis[7]);
        }
        if (setup_run(&state)) { /* the setup assistant has the wheel: the game gets "hands off" */
            if (now - last_send >= 100) {
                neutral_report(packet);
                if (deliver(packet, sizeof(packet))) {
                    memory_copy(last, packet, sizeof(packet));
                    last_send = now;
                }
            }
            Sleep(5);
            continue;
        }
        /* until a pedal axis reports a real value (DirectInput starts at 32768), keep it released;
           then snap its rest position to the nearer end */
        for (int i = 0; i < 8; ++i) {
            if (i != cfg.axis_throttle && i != cfg.axis_brake && i != cfg.axis_clutch) continue;
            if (!(rest_decided & (1u << i)) && state.axis[i] != 32768 && state.axis[i] != 32767) {
                rest_decided |= 1u << i;
                pedal_rest[i] = state.axis[i] > 32767 ? 65535 : 0;
                if (i == cfg.axis_throttle || i == cfg.axis_brake || i == cfg.axis_clutch)
                    wlog("Pedal-Achse %s: Ruhestellung %d erkannt.", axis_names[i], (int)pedal_rest[i]);
            }
            if (!(rest_decided & (1u << i))) state.axis[i] = (LONG)pedal_rest[i];
        }
        for (int i = 0; i < 8; ++i) {
            if (state.axis[i] < mins[i]) mins[i] = state.axis[i];
            if (state.axis[i] > maxs[i]) maxs[i] = state.axis[i];
        }
        if (cfg.log) log_new_buttons(&state);
        if (!ffb_state && now - last_ffb_try >= 1000) {
            last_ffb_try = now;
            ffb_setup();
        }
        csv_write(&state, now);
        if (cfg.axis_steer >= 0) last_steer_raw = state.axis[cfg.axis_steer];
        ffb_update();
        build_report(&state, packet);
        if (cfg.log && packet[0x14 + 0xc] != last[0x14 + 0xc]) {
            wlog("Gang am Schaltknauf: %d", packet[0x14 + 0xc] == 255 ? -1 : (int)packet[0x14 + 0xc]);
        }
        if (!memory_equal(packet, last, sizeof(packet)) || now - last_send >= 100) {
            if (!deliver(packet, sizeof(packet))) {
                wlog("Lenkrad-Thread beendet.");
                return 0;
            }
            memory_copy(last, packet, sizeof(packet));
            last_send = now;
        }
        if (cfg.log && now - last_summary >= 30000) {
            last_summary = now;
            wlog("Achsen min/max: X %d..%d  Y %d..%d  Z %d..%d  Rz %d..%d  S0 %d..%d  S1 %d..%d", (int)mins[0],
                 (int)maxs[0], (int)mins[1], (int)maxs[1], (int)mins[2], (int)maxs[2], (int)mins[5], (int)maxs[5],
                 (int)mins[6], (int)maxs[6], (int)mins[7], (int)maxs[7]);
        }
        Sleep(2);
    }
    return 0;
}

/* ------------------------------------------------------------------ bridge setup */

static INIT_ONCE bridge_once = INIT_ONCE_STATIC_INIT;

static void bridge_setup_inner(void);
static BOOL CALLBACK bridge_setup(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    __try {
        bridge_setup_inner();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wheel_ready = 0;
        wlog("FEHLER: Ausnahme %X beim Einrichten - Lenkrad bleibt aus, Spiel laeuft normal weiter.",
             (unsigned)GetExceptionCode());
    }
    return TRUE;
}

/* The wheel object from Microsoft's Wheel DLL (aggregated by `outer`). 0 = not available. */
static int create_dll_wheel(void) {
    if (!load_wheel_dll()) return 0;
    void *class_name = NULL;
    {
        HMODULE m = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        CreateStringFn real_create = m ? (CreateStringFn)(uintptr_t)GetProcAddress(m, "WindowsCreateString") : NULL;
        if (!real_create || real_create(L"Microsoft.Xbox.Input.Wheel", 26, &class_name) < 0) {
            wlog("FEHLER: WindowsCreateString (System) nicht verfuegbar.");
            return 0;
        }
    }
    void *factory = NULL, *controller_factory = NULL;
    HRESULT hr = wheel_get_factory(class_name, &factory);
    if (hr < 0 || !factory) {
        wlog("FEHLER: DllGetActivationFactory %X", (unsigned)hr);
        return 0;
    }
    hr = ((QIFn)VT(factory)[0])(factory, &IID_ControllerFactory, &controller_factory);
    if (hr < 0 || !controller_factory) {
        wlog("FEHLER: IControllerFactory %X", (unsigned)hr);
        return 0;
    }
    outer.vtbl = outer_vtbl;
    hr = ((CreateInstanceFn)VT(controller_factory)[6])(controller_factory, WHEEL_ID, &outer, &inner);
    if (hr < 0 || !inner) {
        wlog("FEHLER: Wheel-Objekt nicht erstellt %X", (unsigned)hr);
        inner = NULL;
        return 0;
    }
    hr = ((QIFn)VT(inner)[0])(inner, &IID_InputHandler, &input_handler);
    if (hr < 0 || !input_handler) {
        wlog("FEHLER: IInputHandler %X", (unsigned)hr);
        inner = NULL;
        input_handler = NULL;
        return 0;
    }
    return 1;
}

static void bridge_setup_inner(void) {
    wlog("Spiel fordert Windows.Xbox.Input.Controller an - Bruecke startet.");
    if (!dinput_find()) return;
    crt_malloc = (MallocFn)game_import("msvcr110.dll", "malloc");
    crt_free = (FreeFn)game_import("msvcr110.dll", "free");
    if (!crt_malloc || !crt_free) {
        wlog("FEHLER: CRT des Spiels nicht gefunden.");
        return;
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(uintptr_t)crt_free, &crt_module)) {
        crt_module = NULL;
    }
    if (crt_module && !GetProcAddress(crt_module, "_calloc_crt")) crt_module = NULL;
    wlog("CRT des Spiels: %s", crt_module ? "geladenes Modul" : "Azimuth-Abbild (mit Ersatzfunktionen)");
    game_create_string = (CreateStringFn)game_import("combase.dll", "WindowsCreateString");
    soft_wheel = 0;
    if (!cfg.wheel_dll) {
        wlog("WheelDll=0 in FH2_Wheel.ini: die Mod nutzt ihre eigene Lenkrad-Umsetzung.");
    } else if (GetFileAttributesW(dll_path) == INVALID_FILE_ATTRIBUTES) {
        wlog("Hinweis: %S fehlt in diesem Spiel - kein Problem, die Mod nutzt ihre eigene Lenkrad-Umsetzung.",
             dll_path);
    } else if (!create_dll_wheel()) {
        wlog("Die Wheel-DLL des Spiels geht nicht - die Mod nutzt ihre eigene Lenkrad-Umsetzung.");
    }
    if (!inner) {
        outer.vtbl = outer_vtbl;
        WheelSoftEnv env = {make_string};
        inner = wheel_soft_create(&outer, &env);
        HRESULT hr = inner ? ((QIFn)VT(inner)[0])(inner, &IID_InputHandler, &input_handler) : E_FAIL;
        if (hr < 0 || !input_handler) {
            wlog("FEHLER: eigene Lenkrad-Umsetzung nicht erstellt %X", (unsigned)hr);
            inner = NULL;
            return;
        }
        soft_wheel = 1;
        wlog("Eigene Lenkrad-Umsetzung aktiv (ohne Microsoft.Xbox.Input.Wheel.dll).");
    }
    ffb_vm_reset(&game_vm);
    hook_wheel_methods();
    send_static_config();
    JoyState neutral;
    zero(&neutral, sizeof(neutral));
    if (cfg.axis_steer >= 0) neutral.axis[cfg.axis_steer] = 32768;
    neutral.pov = 0xffffffff;
    unsigned char packet[0x14 + 0x11];
    zero(pedal_rest, sizeof(pedal_rest));
    build_report(&neutral, packet);
    ((OnInputFn)VT(input_handler)[6])(input_handler, sizeof(packet), packet);
    HANDLE thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    wheel_ready = 1;
    wlog("Lenkrad-Bruecke bereit (ID %X%X).", (unsigned)(WHEEL_ID >> 32), (unsigned)WHEEL_ID);
    log_append(soft_wheel ? "WHEEL: Lenkrad-Bruecke aktiv (eigene Wheel-Umsetzung) - Details in FH2_Wheel.log.\r\n"
                          : "WHEEL: Lenkrad-Bruecke aktiv - Details in FH2_Wheel.log.\r\n");
    return;
}

static volatile LONG bridge_called;
static HRESULT WINAPI hook_get_factory(const wchar_t *name, const GUID *iid, void **out) {
    HRESULT hr = original_get_factory(name, iid, out);
    static volatile LONG seen;
    if (InterlockedIncrement(&seen) <= 30) wlog("Spiel holt Klasse \"%S\" -> %X", name, (unsigned)hr);
    if (hr >= 0 && out && *out && !user_statics && guid_eq(iid, &IID_UserStatics) &&
        wide_eq(name, L"Windows.Xbox.System.User")) {
        user_statics = *out;
        ((RefFn)VT(user_statics)[1])(user_statics);
    }
    if (hr < 0 || !out || !*out || !guid_eq(iid, &IID_ControllerStatics) ||
        !wide_eq(name, L"Windows.Xbox.Input.Controller")) {
        return hr;
    }
    bridge_called = 1;
    InitOnceExecuteOnce(&bridge_once, bridge_setup, NULL, NULL);
    if (!wheel_ready) return hr;
    StaticsWrap *wrap = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(StaticsWrap));
    if (!wrap) return hr;
    if (!real_statics) {
        real_statics = *out;
        ((RefFn)VT(real_statics)[1])(real_statics);
    }
    wrap->vtbl = st_vtbl;
    wrap->refs = 1;
    wrap->real = *out;
    *out = wrap;
    return hr;
}

static int install_hook(void) {
    void *volatile *slot = (void *volatile *)(game + IAT_GET_FACTORY);
    void *current = *slot;
    uintptr_t value = (uintptr_t)current;
    MEMORY_BASIC_INFORMATION info;
    if (value < 0x10000000u || (value >= (uintptr_t)game && value < (uintptr_t)game + GAME_IMAGE_SIZE) ||
        VirtualQuery(current, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        !(info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        return 0;
    }
    DWORD protection, ignored;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &protection)) return -1;
    original_get_factory = (GetFactoryFn)current;
    void *previous = InterlockedCompareExchangePointer(slot, (void *)hook_get_factory, current);
    VirtualProtect((void *)slot, sizeof(void *), protection, &ignored);
    return previous == current ? 1 : -1;
}

static DWORD WINAPI install_thread(LPVOID unused) {
    (void)unused;
    /* Azimuth may (re)write the game's import table after we patched it, so keep watching. */
    void *volatile *slot = (void *volatile *)(game + IAT_GET_FACTORY);
    int hooks = 0;
    for (unsigned i = 0;; ++i) {
        if (*slot != (void *)hook_get_factory) {
            void *before = *slot;
            int r = install_hook();
            if (r > 0) {
                ++hooks;
                if (hooks <= 20) wlog("Import-Hook gesetzt (#%d nach %u ms, Original %p).", hooks, i, before);
            } else if (r < 0 && hooks == 0) {
                wlog("FEHLER: Import-Hook nicht setzbar.");
                return 0;
            }
        }
        if (bridge_called) {
            wlog("Hook wurde vom Spiel benutzt - Waechter beendet.");
            return 0;
        }
        if (i == 120000) wlog("WARNUNG: Spiel hat den Controller-Import 2 Minuten lang nicht ueber den Hook angefragt.");
        Sleep(i < 30000 ? 1 : 50);
    }
}

void wheel_bridge_start(unsigned char *game_image, const wchar_t *executable_path,
                        const wchar_t *settings_directory) {
    if (!game_image || !settings_directory) return;
    game = game_image;
    if (!path_join(ini_path, settings_directory, WHEEL_INI) ||
        !path_join(log_path, settings_directory, WHEEL_LOG)) {
        return;
    }
    config_load();
    live_load();
    log_enabled = 1;
    {   /* keep the previous session's log as FH2_Wheel_previous.log */
        static wchar_t old_path[PATH_CAPACITY];
        if (path_join(old_path, settings_directory, L"FH2_Wheel_previous.log"))
            MoveFileExW(log_path, old_path, MOVEFILE_REPLACE_EXISTING);
    }
    HANDLE file = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    wlog("Gripline " GRIPLINE_VERSION " wheel bridge - Enabled=%d Rotation=%d Shifter=%d", cfg.enabled, cfg.rotation,
         cfg.shifter);
    if (!cfg.enabled) {
        wlog("Ausgeschaltet in FH2_Wheel.ini (Enabled=0).");
        return;
    }
    if (mod_scan_mapper_running()) {
        wlog("Azimuth Mapper is running - Gripline wheel off (the Mapper provides the controller).");
        mod_scan_announce_mapper();
        return;
    }
    wlog("Zusatztasten: Taste %d -> Funktion %d, Taste %d -> Funktion %d, Taste %d -> Funktion %d.",
         cfg.extra_button[0], cfg.extra_function[0], cfg.extra_button[1], cfg.extra_function[1],
         cfg.extra_button[2], cfg.extra_function[2]);
    wheel_buttons_install(game_image);
    /* game folder (Mount) holds the wheel DLL */
    unsigned n = 0, slash = 0;
    for (; executable_path[n] && n < PATH_CAPACITY - 64; ++n) {
        dll_path[n] = executable_path[n];
        if (executable_path[n] == L'\\' || executable_path[n] == L'/') slash = n + 1;
    }
    dll_path[slash] = 0;
    if (!path_join(dll_path, dll_path, WHEEL_DLL)) return;
    HANDLE thread = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

/* Called when the game process ends: Logitech wheels keep the last force effects running in
   the device if nobody stops them, so reset the motor explicitly. */
void wheel_bridge_shutdown(void) {
    if (!device || ffb_state != 1) return;
    ffb_state = -1;
    set_constant(fx_force, 0);
    device->lpVtbl->SendForceFeedbackCommand(device, DISFFC_STOPALL);
    device->lpVtbl->SendForceFeedbackCommand(device, DISFFC_RESET);
    device->lpVtbl->Unacquire(device);
}

#ifdef FH2_TWEAKS_TEST
/* game FFB: hook a fake Wheel DLL image (method table at +0x15760, raw-buffer import at +0x14198) */
__declspec(dllexport) int Fh2TestGameFfbHook(unsigned char *fake_image, void *fake_inner) {
    wheel_image = fake_image;
    inner = fake_inner;
    ffb_vm_reset(&game_vm);
    game_ffb_hooked = 0;
    hook_wheel_methods();
    return game_ffb_hooked;
}
/* own wheel object (no Wheel DLL): create it like the bridge does and hook the game FFB */
__declspec(dllexport) void *Fh2TestSoftWheelCreate(void *raw_buffer_fn, void *create_string_fn) {
    game_create_string = (CreateStringFn)create_string_fn;
    raw_buffer = (RawBufferFn)raw_buffer_fn;
    outer.vtbl = outer_vtbl;
    WheelSoftEnv env = {make_string};
    inner = wheel_soft_create(&outer, &env);
    input_handler = NULL;
    if (((QIFn)VT(inner)[0])(inner, &IID_InputHandler, &input_handler) < 0) return NULL;
    soft_wheel = 1;
    ffb_vm_reset(&game_vm);
    game_ffb_hooked = 0;
    hook_wheel_methods();
    return game_ffb_hooked ? &outer : NULL;
}
/* sends the static configuration and one report: DirectInput buttons (bit n = button n), axes */
__declspec(dllexport) void Fh2TestSoftWheelInput(unsigned buttons, unsigned steer, unsigned throttle) {
    send_static_config();
    JoyState s;
    zero(&s, sizeof(s));
    s.pov = 0xffffffff;
    for (int i = 0; i < 32; ++i)
        if (buttons & (1u << i)) s.buttons[i] = 0x80;
    if (cfg.axis_steer >= 0) s.axis[cfg.axis_steer] = (LONG)steer;
    if (cfg.axis_throttle >= 0) s.axis[cfg.axis_throttle] = (LONG)throttle;
    unsigned char packet[0x14 + 0x11];
    build_report(&s, packet);
    ((OnInputFn)VT(input_handler)[6])(input_handler, sizeof(packet), packet);
}
__declspec(dllexport) unsigned Fh2TestGameFfbTick(float position) {
    float af = ffb_vm_tick(&game_vm, position);
    unsigned bits;
    memory_copy(&bits, &af, 4);
    return bits;
}
__declspec(dllexport) unsigned Fh2TestGameFfbLoaded(void) {
    return ffb_vm_loaded(&game_vm);
}
__declspec(dllexport) long Fh2TestGameForce(float af, int x_permille) {
    return game_force(af, x_permille / 1000.0);
}
__declspec(dllexport) void Fh2TestWheelSet(unsigned id, int value) {
    int *field = cfg_field(id);
    if (field) *field = value;
}
/* wheel report bytes 0/1 for the given DirectInput buttons (bit n = button n) */
__declspec(dllexport) unsigned Fh2TestWheelReport(unsigned buttons) {
    JoyState s;
    zero(&s, sizeof(s));
    s.pov = 0xffffffff;
    for (int i = 0; i < 32; ++i)
        if (buttons & (1u << i)) s.buttons[i] = 0x80;
    if (cfg.axis_steer >= 0) s.axis[cfg.axis_steer] = 32768;
    unsigned char packet[0x14 + 0x11];
    build_report(&s, packet);
    return packet[0x14] | (unsigned)packet[0x15] << 8;
}
/* report pedals for raw brake / clutch axis values: brake << 16 | clutch, static config clutch bits << 32 */
__declspec(dllexport) unsigned long long Fh2TestWheelPedals(unsigned brake, unsigned clutch) {
    JoyState s;
    zero(&s, sizeof(s));
    s.pov = 0xffffffff;
    if (cfg.axis_brake >= 0) s.axis[cfg.axis_brake] = (LONG)brake;
    if (cfg.axis_clutch >= 0) s.axis[cfg.axis_clutch] = (LONG)clutch;
    unsigned char packet[0x14 + 0x11];
    build_report(&s, packet);
    unsigned b = packet[0x14 + 6] | (unsigned)packet[0x14 + 7] << 8, c = packet[0x14 + 8] | (unsigned)packet[0x14 + 9] << 8;
    return (unsigned long long)(clutch_axis() >= 0 ? 10 : 0) << 32 | b << 16 | c;
}
__declspec(dllexport) void Fh2TestWheelLiveApply(void) {
    live_apply();
}
__declspec(dllexport) int Fh2TestWheelConfig(unsigned id) {
    int *field = cfg_field(id);
    return field ? *field : -12345;
}
__declspec(dllexport) unsigned Fh2TestWheelCurve(unsigned raw) {
    return apply_curve(raw);
}
/* setup assistant: start it, feed one raw reading (8 axes, bit n = button n), read the result */
__declspec(dllexport) void Fh2TestWheelSetupStart(const wchar_t *ini) {
    unsigned i = 0;
    for (; ini[i] && i < PATH_CAPACITY - 1; ++i) ini_path[i] = ini[i];
    ini_path[i] = 0;
    InterlockedExchange(&setup_request, 1);
}
__declspec(dllexport) int Fh2TestWheelSetupFeed(const LONG *axes, unsigned buttons) {
    JoyState s;
    zero(&s, sizeof(s));
    s.pov = 0xffffffff;
    for (int i = 0; i < 8; ++i) s.axis[i] = axes[i];
    for (int i = 0; i < 32; ++i)
        if (buttons & (1u << i)) s.buttons[i] = 0x80;
    return setup_run(&s) | (setup_step + 1) << 4;
}
__declspec(dllexport) int Fh2TestWheelSetupResult(int k) {
    int values[8] = {cfg.axis_steer, cfg.axis_throttle, cfg.axis_brake, cfg.axis_clutch,
                     cfg.btn_paddle_right, cfg.btn_paddle_left, cfg.btn_a, cfg.btn_b};
    return k >= 0 && k < 8 ? values[k] : -100;
}
#endif
