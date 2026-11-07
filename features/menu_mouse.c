/* Mouse pointer in the game's menus (V7.1) - see menu_mouse.h. Own code. */
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "menu_mouse.h"
#include "keyboard_pad.h"
#include "notify.h"

/* game addresses (forza_durango_release_final.exe as of 07.11.2026, checked at start) */
#define RVA_APP_FRAME_SLOT 0x2ecfaa0u  /* GameApplication vtable 0x2ecfa58, slot 9 (per UI frame) */
#define RVA_APP_FRAME 0x15b5110u       /* the game's function in that slot */
#define RVA_WINDOW_MANAGER 0x3684b88u  /* global: the game's WindowLayerManager (AVUI::WindowManager) */
#define RVA_WINDOW_MANAGER_VT 0x2ecf920u
#define RVA_PRESENTATION_SOURCE_VT 0x2edfd98u
#define RVA_MOUSE_PROVIDER_VT 0x2edfcb0u
#define RVA_REPORT_INPUT 0x149a2f0u    /* MouseInputProvider::ReportInput */
#define RVA_UI_CONTEXT 0x1608900u      /* this thread's AVUI context (TLS), NULL off the UI thread */
#define MANAGER_ACTIVE_WINDOW 0x68u         /* WindowManager -> active Window (the keyboard goes there too) */
#define MANAGER_UI_SIZE 0xb8u                  /* WindowManager: UI size, 2 floats */
#define WINDOW_SOURCE 0x260u           /* Window -> PresentationSource */
#define SOURCE_MOUSE 0x90u             /* PresentationSource -> MouseInputProvider */
#define CONTEXT_INPUT_MANAGER 0xb8u

/* WPF RawMouseActions (AVUI keeps the values: Deactivate 4 and CancelCapture 0x80000 are used by the game) */
enum {
    RAW_DEACTIVATE = 0x4, RAW_MOVE = 0x10, RAW_LEFT_DOWN = 0x40, RAW_LEFT_UP = 0x80, RAW_RIGHT_DOWN = 0x100,
    RAW_RIGHT_UP = 0x200, RAW_MIDDLE_DOWN = 0x400, RAW_MIDDLE_UP = 0x800, RAW_WHEEL = 0x10000
};

#define HIDE_MS 6000u /* the pointer goes after this long without the mouse (pad players never see it) */

typedef unsigned char (*ReportInputFn)(void *provider, int mode, unsigned timestamp, unsigned actions,
                                      const float *point, int wheel);
typedef void *(*ContextFn)(void);
typedef void (*FrameFn)(void *application, void *transform);

static unsigned char *game;
static FrameFn previous_frame;
static int broken;

/* UI thread only */
static float px, py;               /* pointer, client pixels */
static int last_w, last_h, buttons_before, shown;
static ULONGLONG moved_at;
static void *active_provider;      /* the provider that got our last report (activated) */
static int logged_source, logged_context, logged_click, logged_missing;

static void *read_pointer(const void *address) {
    void *value = NULL;
    return memory_read_pointer(address, &value) ? value : NULL;
}

static int has_vtable(const void *object, unsigned rva) {
    return object && read_pointer(object) == (void *)(game + rva);
}

/* the MouseInputProvider of the active menu window, NULL when the chain is not as expected */
static void *find_provider(float size[2]) {
    unsigned char *manager = read_pointer(game + RVA_WINDOW_MANAGER);
    if (!has_vtable(manager, RVA_WINDOW_MANAGER_VT)) return NULL;
    unsigned char *window = read_pointer(manager + MANAGER_ACTIVE_WINDOW);
    unsigned char *source = window ? read_pointer(window + WINDOW_SOURCE) : NULL;
    if (!has_vtable(source, RVA_PRESENTATION_SOURCE_VT)) return NULL;
    void *provider = read_pointer(source + SOURCE_MOUSE);
    if (!has_vtable(provider, RVA_MOUSE_PROVIDER_VT)) return NULL;
    memory_copy(size, manager + MANAGER_UI_SIZE, sizeof(float) * 2);
    if (!logged_source) {
        logged_source = 1;
        log_append_module("MENUMOUSE: menu window found, vtable ", read_pointer(window));
        log_append_value("MENUMOUSE: UI width ", (int)size[0]);
        log_append_value("MENUMOUSE: UI height ", (int)size[1]);
    }
    return provider;
}

static unsigned char report(void *provider, unsigned actions, const float point[2], int wheel) {
    return ((ReportInputFn)(game + RVA_REPORT_INPUT))(provider, 0, (unsigned)GetTickCount64(), actions, point, wheel);
}

static void deactivate(void) {
    if (!active_provider) return;
    float size[2], point[2] = {0.0f, 0.0f};
    if (find_provider(size) == active_provider) report(active_provider, RAW_DEACTIVATE, point, 0);
    active_provider = NULL;
}

static void feed(int moved, int buttons, int wheel) {
    void *context = ((ContextFn)(game + RVA_UI_CONTEXT))();
    if (!context || !read_pointer((unsigned char *)context + CONTEXT_INPUT_MANAGER)) {
        if (!logged_context) {
            logged_context = 1;
            log_append("MENUMOUSE: no menu input on this thread - pointer only, no hover/click.\r\n");
        }
        return;
    }
    float size[2];
    void *provider = find_provider(size);
    if (!provider) {
        if (!logged_missing) {
            logged_missing = 1;
            log_append("MENUMOUSE: no menu window (yet) - pointer only.\r\n");
        }
        return;
    }
    float point[2] = {px, py};
    if (size[0] > 16.0f && size[0] < 16384.0f && size[1] > 16.0f && size[1] < 16384.0f && last_w > 0 && last_h > 0) {
        point[0] = px * size[0] / (float)last_w;
        point[1] = py * size[1] / (float)last_h;
    }
    if (provider != active_provider) moved = 1; /* the first report activates the mouse in that window */
    active_provider = provider;
    if (moved) report(provider, RAW_MOVE, point, 0);
    int changed = buttons ^ buttons_before;
    unsigned actions = 0;
    if (changed & 1) actions |= (buttons & 1) ? RAW_LEFT_DOWN : RAW_LEFT_UP;
    if (changed & 2) actions |= (buttons & 2) ? RAW_RIGHT_DOWN : RAW_RIGHT_UP;
    if (changed & 4) actions |= (buttons & 4) ? RAW_MIDDLE_DOWN : RAW_MIDDLE_UP;
    if (actions) {
        unsigned char handled = report(provider, actions, point, 0);
        if (logged_click < 6) { /* first clicks: where and whether the menu took it */
            ++logged_click;
            log_append_value((actions & (RAW_LEFT_DOWN | RAW_RIGHT_DOWN | RAW_MIDDLE_DOWN)) ? "MENUMOUSE: button down, handled "
                                                                                               : "MENUMOUSE: button up, handled ",
                             handled);
            log_append_value("MENUMOUSE:   at UI x ", (int)point[0]);
            log_append_value("MENUMOUSE:   at UI y ", (int)point[1]);
        }
    }
    if (wheel) report(provider, RAW_WHEEL, point, wheel);
}

static void frame(void) {
    int dx, dy, buttons, wheel, w, h;
    if (!keyboard_menu_mouse(&dx, &dy, &buttons, &wheel, &w, &h)) {
        if (shown) notify_cursor(0, 0, 0);
        shown = 0;
        if (active_provider) {
            if (buttons_before) { /* a button held while the menu closed: let go */
                float size[2], point[2] = {px, py};
                if (find_provider(size) == active_provider)
                    report(active_provider, (buttons_before & 1 ? RAW_LEFT_UP : 0) | (buttons_before & 2 ? RAW_RIGHT_UP : 0) |
                                                (buttons_before & 4 ? RAW_MIDDLE_UP : 0), point, 0);
            }
            deactivate();
        }
        buttons_before = 0;
        moved_at = 0;
        return;
    }
    if (w != last_w || h != last_h) {
        if (last_w > 0 && last_h > 0) {
            px = px * (float)w / (float)last_w;
            py = py * (float)h / (float)last_h;
        } else {
            px = (float)w * 0.5f;
            py = (float)h * 0.5f;
        }
        last_w = w, last_h = h;
    }
    int moved = dx || dy;
    px += (float)dx;
    py += (float)dy;
    if (px < 0.0f) px = 0.0f;
    if (py < 0.0f) py = 0.0f;
    if (px > (float)(w - 1)) px = (float)(w - 1);
    if (py > (float)(h - 1)) py = (float)(h - 1);
    ULONGLONG now = GetTickCount64();
    if (moved || wheel || buttons != buttons_before || buttons) moved_at = now;
    if (!moved_at || now - moved_at >= HIDE_MS) {
        if (shown) notify_cursor(0, 0, 0);
        shown = 0;
        deactivate();
        buttons_before = buttons;
        return;
    }
    shown = 1;
    notify_cursor(1, (int)px, (int)py);
    feed(moved, buttons, wheel);
    buttons_before = buttons;
}

static void hook_frame(void *application, void *transform) {
    if (!broken) {
        __try {
            frame();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            broken = 1;
            notify_cursor(0, 0, 0);
            log_append("MENUMOUSE: an exception while feeding the menu - menu mouse off until restart.\r\n");
        }
    }
    previous_frame(application, transform);
}

int menu_mouse_start(unsigned char *game_image) {
    if (game || !game_image) return game != NULL;
    static const unsigned char report_start[] = {0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0x68, 0xe8,
                                                 0x48, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00};
    static const unsigned char context_start[] = {0x33, 0xc9, 0xe9, 0x69, 0x39, 0xe8, 0xff};
    if (!memory_equal(game_image + RVA_REPORT_INPUT, report_start, sizeof(report_start)) ||
        !memory_equal(game_image + RVA_UI_CONTEXT, context_start, sizeof(context_start))) {
        log_append("MENUMOUSE: game code differs - menu mouse off.\r\n");
        return 0;
    }
    void **slot = (void **)(game_image + RVA_APP_FRAME_SLOT);
    void *current = NULL;
    if (!memory_read_pointer(slot, &current) || !current) {
        log_append("MENUMOUSE: UI frame slot empty - menu mouse off.\r\n");
        return 0;
    }
    if (current != game_image + RVA_APP_FRAME) log_append_module("MENUMOUSE: UI frame already hooked by ", current);
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    previous_frame = (FrameFn)current;
    game = game_image;
    InterlockedExchangePointer(slot, (void *)(uintptr_t)hook_frame);
    VirtualProtect(slot, sizeof(void *), old, &ignored);
    log_append("MENUMOUSE: ready - mouse pointer in the menus (FH2_Keyboard.ini MenuMouse).\r\n");
    return 1;
}
