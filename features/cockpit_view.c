/* Second cockpit view. FH2 places the driver camera at the steering wheel plus an offset that the
   camera keeps at +0x190 (x right, y up, z forward, metres; one vector per car, the default is the
   tunable Driver\DefaultOffsetFromSteeringWheel, the camera's set-up at game+0x103b690 writes it).
   The camera reads it every frame, so the second view only adds forward/up to it. Anything the game
   writes there itself (another car, the camera set up again) is taken as the new base.

   Camera change: the game reads every controller / wheel through 0x106c320 (user, device,
   XINPUT_STATE *), called from 0x106fae6 and 0x1075580. Both calls go through state_hook: while
   the driver camera is active, the first press of the camera button is taken away from the game
   and turns the second view on; the next press turns it off and goes on to the game. */
#include "cockpit_view.h"
#include "wheel_buttons.h"
#include "logging.h"
#include "support.h"
#include "noclip.h"

#define RVA_GET_STATE 0x106c320u
#define ACTION_SWITCH_CAMERA 17
#define DRIVER_ACTIVE_MS 300

#define OFFSET_FIELD 0x190

static volatile LONG second_view, forward_mm = 350, up_mm = -50;
static volatile LONG held_by[2];
static void *last_camera;
static float base[3], written[3];
static int have_written;
static volatile LONG64 driver_tick; /* GetTickCount64 of the last driver camera frame */
static unsigned char button_held[4], button_taken[4];

/* freecam: the camera button is pressed for the player until the driver camera is active */
#define DRIVER_SEARCH_MS 4000u
#define PRESS_EVERY_MS 400u
#define PRESS_MS 150u
static volatile LONG want_driver;
static volatile LONG64 want_since;
static volatile LONG press_device = -1; /* user * 2 + wheel of the controller that gets the presses */

static void hook_driver_camera(unsigned char *game_image);

void cockpit_view_set_offset(int forward_cm, int up_cm) {
    InterlockedExchange(&forward_mm, forward_cm * 10);
    InterlockedExchange(&up_mm, up_cm * 10);
}

int cockpit_view_second(void) {
    return second_view != 0;
}

static void set_view(LONG on) {
    if (InterlockedExchange(&second_view, on) != on)
        log_append(on ? "COCKPIT VIEW: second view (at the gauges)\r\n" : "COCKPIT VIEW: normal view\r\n");
}

void cockpit_view_input(unsigned source, int held) {
    if (source > 1) return;
    LONG before = InterlockedExchange(&held_by[source], held ? 1 : 0);
    if (held && !before) set_view(!second_view);
}

static int driver_active(void) {
    LONG64 seen = driver_tick;
    return seen && GetTickCount64() - (ULONGLONG)seen < DRIVER_ACTIVE_MS;
}

int cockpit_view_driver_active(void) {
    return driver_active();
}

void cockpit_view_want_driver(int on) {
    InterlockedExchange(&press_device, -1);
    InterlockedExchange64(&want_since, (LONG64)GetTickCount64());
    InterlockedExchange(&want_driver, on ? 1 : 0);
    if (on && !driver_active()) log_append("COCKPIT VIEW: freecam - changing to the driver camera.\r\n");
}

/* 1 = press the camera button in this reading (one controller only, so the camera changes once a press) */
static int press_for_driver(DWORD user, int wheel) {
    if (!want_driver || driver_active()) return 0;
    ULONGLONG t = GetTickCount64() - (ULONGLONG)want_since;
    if (t >= DRIVER_SEARCH_MS) return 0;
    LONG device = (LONG)(user * 2 + (wheel ? 1 : 0));
    InterlockedCompareExchange(&press_device, device, -1);
    return press_device == device && t % PRESS_EVERY_MS < PRESS_MS;
}

/* The camera button of one controller: 1 = hide this press from the game. */
static int camera_button(unsigned user, int held) {
    if (user >= 4) return 0;
    int press = held && !button_held[user];
    button_held[user] = (unsigned char)(held != 0);
    if (!held) {
        button_taken[user] = 0;
        return 0;
    }
    if (press) {
        if (second_view) {
            set_view(0); /* cockpit 2 -> the game's next camera */
        } else if (driver_active()) {
            set_view(1); /* cockpit -> cockpit 2, the game stays on the driver camera */
            button_taken[user] = 1;
        }
    }
    return button_taken[user];
}

typedef DWORD (*GetStateFn)(DWORD user, void *device, unsigned char *state);
static GetStateFn get_state;

static DWORD state_hook(DWORD user, void *device, unsigned char *state) {
    unsigned wheel_before = wheel_buttons_wheel_reads();
    DWORD result = get_state(user, device, state);
    if (result != 0 || !state) return result;
    static volatile LONG seen;
    if (!InterlockedExchange(&seen, 1)) log_append("COCKPIT VIEW: the game reads a controller through the camera hook.\r\n");
    int wheel = wheel_buttons_wheel_reads() != wheel_before;
    int input = wheel ? wheel_buttons_action_input(ACTION_SWITCH_CAMERA)
                      : controller_action_input(ACTION_SWITCH_CAMERA);
    if (input < 0 || input > 15 || input == 10 || input == 11) return result; /* 10/11 are triggers */
    unsigned short buttons, mask = (unsigned short)(1u << input);
    memory_copy(&buttons, state + 4, sizeof(buttons));
    if (press_for_driver(user, wheel)) {
        buttons |= mask;
        memory_copy(state + 4, &buttons, sizeof(buttons));
    }
    static volatile LONG pressed_seen;
    if ((buttons & mask) && !InterlockedExchange(&pressed_seen, 1)) log_append_value("COCKPIT VIEW: camera button seen, input ", input);
    if (camera_button(user, (buttons & mask) != 0)) {
        buttons &= (unsigned short)~mask;
        memory_copy(state + 4, &buttons, sizeof(buttons));
    }
    return result;
}

static const unsigned state_calls[2] = {0x106fae6u, 0x1075580u};

void cockpit_view_start(unsigned char *game_image) {
    if (game_image) hook_driver_camera(game_image);
    if (!game_image || get_state) return;
    unsigned char *target = game_image + RVA_GET_STATE;
    for (unsigned i = 0; i < ARRAY_COUNT(state_calls); ++i) {
        unsigned char *site = game_image + state_calls[i];
        int32_t expected = (int32_t)(target - (site + 5));
        if (site[0] != 0xe8 || !memory_equal(site + 1, &expected, 4)) {
            log_append("COCKPIT VIEW: game code differs - camera button step off (V key still works).\r\n");
            return;
        }
    }
    unsigned char *stub = memory_alloc_near(game_image, 0x1000);
    if (!stub) {
        log_append("COCKPIT VIEW: no memory near the game - camera button step off.\r\n");
        return;
    }
    static const unsigned char jmp[6] = {0xff, 0x25, 0, 0, 0, 0}; /* jmp [rip+0] */
    uint64_t function = (uint64_t)(uintptr_t)state_hook;
    memory_copy(stub, jmp, sizeof(jmp));
    memory_copy(stub + sizeof(jmp), &function, sizeof(function));
    DWORD old, ignored;
    VirtualProtect(stub, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), stub, 0x1000);
    get_state = (GetStateFn)target;
    for (unsigned i = 0; i < ARRAY_COUNT(state_calls); ++i) {
        unsigned char *site = game_image + state_calls[i];
        int32_t offset = (int32_t)(stub - (site + 5));
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
        memory_copy(site + 1, &offset, 4);
        VirtualProtect(site, 5, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    log_append("COCKPIT VIEW: camera button step ready (cockpit -> cockpit 2 -> next camera).\r\n");
}

/* The driver camera's vtable (game+0x2e78120) asks for its field of view every frame (slot 0x70);
   Extra Options may already have put its own FOV function there. Gripline chains in front of
   whatever is there, moves the camera for the second view and lets it answer. */
#define RVA_DRIVER_CAMERA_VTABLE 0x2e78120u
#define FOV_SLOT 0x70
typedef float (*GetFovFn)(void *);
static GetFovFn previous_fov;

static float driver_fov(void *camera) {
    cockpit_view_frame(camera);
    return previous_fov(camera);
}

static void hook_driver_camera(unsigned char *game_image) {
    void **slot = (void **)(game_image + RVA_DRIVER_CAMERA_VTABLE + FOV_SLOT);
    void *current = NULL;
    if (!memory_read_pointer(slot, &current) || !current) {
        log_append("COCKPIT VIEW: driver camera not found - cockpit view 2 cannot move the camera.\r\n");
        return;
    }
    if (current == (void *)(uintptr_t)driver_fov) return;
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
        log_append("COCKPIT VIEW: driver camera is write-protected - cockpit view 2 cannot move the camera.\r\n");
        return;
    }
    previous_fov = (GetFovFn)current;
    InterlockedExchangePointer(slot, (void *)(uintptr_t)driver_fov);
    VirtualProtect(slot, sizeof(void *), old, &ignored);
    log_append_module("COCKPIT VIEW: driver camera hooked in front of ", current);
}

void cockpit_view_frame(void *camera) {
    ULONGLONG now_tick = GetTickCount64();
    LONG64 seen = driver_tick;
    if (second_view && seen && now_tick - (ULONGLONG)seen > 1000) set_view(0); /* back from another camera */
    InterlockedExchange64(&driver_tick, (LONG64)now_tick);
    float *offset = (float *)((unsigned char *)camera + OFFSET_FIELD);
    float now[3];
    if (!memory_read(offset, now, sizeof(now))) return;
    if (camera != last_camera || !have_written || !memory_equal(now, written, sizeof(now))) {
        /* a new camera, or the game set the offset again: that is the car's own position */
        last_camera = camera;
        memory_copy(base, now, sizeof(base));
    }
    float want[3] = {base[0], base[1], base[2]};
    if (second_view) {
        want[1] += (float)up_mm / 1000.0f;
        want[2] += (float)forward_mm / 1000.0f;
    }
    if (noclip_freecam_active()) want[2] += FREECAM_AHEAD; /* out of the car, see noclip.h */
    memory_copy(written, want, sizeof(written));
    have_written = 1;
    if (!memory_equal(now, want, sizeof(want)) && memory_is_writable(offset, sizeof(want)))
        memory_copy(offset, want, sizeof(want));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestCockpitView(void *camera, int toggle, int forward_cm, int up_cm) {
    cockpit_view_set_offset(forward_cm, up_cm);
    if (toggle) {
        cockpit_view_input(COCKPIT_VIEW_KEYBOARD, 1);
        cockpit_view_input(COCKPIT_VIEW_KEYBOARD, 0);
    }
    if (camera) cockpit_view_frame(camera);
}
/* held: camera button state of user 0; returns 1 if the press is hidden, | second view << 1 */
__declspec(dllexport) int Fh2TestCockpitCameraButton(int held) {
    return camera_button(0, held) | (int)second_view << 1;
}
__declspec(dllexport) unsigned Fh2TestCockpitStateHook(unsigned char *game_image, unsigned char *state) {
    cockpit_view_start(game_image);
    return get_state ? state_hook(0, 0, state) : 0xffffffffu;
}
#endif
