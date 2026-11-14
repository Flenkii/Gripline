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

#define TAB_ROW_PART 0.27f /* the wheel switches tabs while the pointer is in this top part of the picture */
enum { IN_DPAD_UP = 0, IN_DPAD_DOWN = 1, IN_LB = 8, IN_RB = 9, IN_A = 12, IN_B = 13 }; /* keyboard_menu_pulse */
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

/* ---- test build diagnostics (menumouse2): which fields of the mouse device change while the pointer
   moves over the tiles, and what the keyboard device (the pad's focus) holds - logged as class names */
#define INPUT_KEYBOARD_DEVICE 0x98u
#define INPUT_MOUSE_DEVICE 0xa0u
#define DUMP_FIELDS 64 /* 8-byte fields 0x00..0x1f8 */
static int dump_lines;
static void *dump_last[2][DUMP_FIELDS];

static const char *class_name(const void *object) {
    if (!object || (uintptr_t)object & 7) return NULL;
    unsigned char *vt = read_pointer(object);
    unsigned image = 0, header = 0;
    memory_read(game + 0x3c, &header, 4);
    memory_read(game + header + 0x50, &image, 4);
    if (!vt || vt < game || vt >= game + image) return NULL;
    unsigned char *col = read_pointer(vt - 8);
    unsigned type_rva = 0;
    if (!col || col < game || col >= game + image || !memory_read(col + 12, &type_rva, 4) || type_rva >= image) return NULL;
    const char *name = (const char *)(game + type_rva + 0x10);
    char start[4];
    if (!memory_read(name, start, 4) || start[0] != '.' || start[1] != '?') return NULL;
    return name;
}

static void put_text(char *out, unsigned *n, unsigned cap, const char *text) {
    while (*text && *n + 1 < cap) out[(*n)++] = *text++;
    out[*n] = 0;
}

static void put_hex(char *out, unsigned *n, unsigned cap, unsigned value) {
    char digits[9];
    int d = 0;
    do digits[d++] = "0123456789abcdef"[value & 15]; while ((value >>= 4) && d < 8);
    while (d && *n + 1 < cap) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

static void dump_fields(const char *label, unsigned char *device, int which) {
    if (!device || dump_lines >= 600) return;
    for (unsigned f = 1; f < DUMP_FIELDS && dump_lines < 600; ++f) {
        void *value = NULL;
        memory_read(device + f * 8, &value, sizeof(value));
        if (value == dump_last[which][f]) continue;
        dump_last[which][f] = value;
        const char *name = class_name(value);
        if (!name) continue;
        char line[220];
        unsigned n = 0;
        line[0] = 0;
        put_text(line, &n, sizeof(line), "MENUMOUSE: ");
        put_text(line, &n, sizeof(line), label);
        put_text(line, &n, sizeof(line), "+0x");
        put_hex(line, &n, sizeof(line), f * 8);
        put_text(line, &n, sizeof(line), " = ");
        put_text(line, &n, sizeof(line), name);
        /* one level deeper for the element: its own object fields (parent, template...) */
        put_text(line, &n, sizeof(line), "\r\n");
        log_append(line);
        ++dump_lines;
        if (which == 0 || which == 1) {
            for (unsigned g = 1; g < 32 && dump_lines < 600; ++g) {
                void *inner = NULL;
                memory_read((unsigned char *)value + g * 8, &inner, sizeof(inner));
                const char *inner_name = class_name(inner);
                if (!inner_name) continue;
                n = 0;
                put_text(line, &n, sizeof(line), "MENUMOUSE:     .+0x");
                put_hex(line, &n, sizeof(line), g * 8);
                put_text(line, &n, sizeof(line), " = ");
                put_text(line, &n, sizeof(line), inner_name);
                put_text(line, &n, sizeof(line), "\r\n");
                log_append(line);
                ++dump_lines;
            }
        }
    }
}

/* menumouse3 diagnostics: on each change of the pad focus, the focused element and its parents (+0x70)
   with their plausible float fields (layout offsets / sizes) - to find where a tile sits */
#define ELEMENT_PARENT 0x70u
static void *focus_before;
static int focus_logs;

static void put_float(char *out, unsigned *n, unsigned cap, float v) {
    if (v < 0) { put_text(out, n, cap, "-"); v = -v; }
    unsigned whole = (unsigned)v, tenth = (unsigned)((v - (float)whole) * 10.0f);
    char digits[12];
    int d = 0;
    do digits[d++] = (char)('0' + whole % 10); while ((whole /= 10) && d < 10);
    while (d && *n + 1 < cap) out[(*n)++] = digits[--d];
    out[*n] = 0;
    char t[3] = {'.', (char)('0' + tenth), 0};
    put_text(out, n, cap, t);
}

static void log_focus_chain(unsigned char *keyboard) {
    if (!keyboard || focus_logs >= 40) return;
    unsigned char *element = read_pointer(keyboard + 0x40);
    if (element == focus_before) return;
    focus_before = element;
    ++focus_logs;
    log_append("MENUMOUSE: --- pad focus changed ---\r\n");
    for (int depth = 0; element && depth < 14; ++depth) {
        const char *name = class_name(element);
        if (!name) break;
        static char line[1400];
        unsigned n = 0;
        line[0] = 0;
        put_text(line, &n, sizeof(line), "MENUMOUSE: ^");
        put_hex(line, &n, sizeof(line), (unsigned)depth);
        put_text(line, &n, sizeof(line), " ");
        put_text(line, &n, sizeof(line), name);
        for (unsigned off = 0x78; off < 0x240; off += 4) {
            float v = 0.0f;
            if (!memory_read(element + off, &v, 4)) break;
            unsigned bits;
            memory_copy(&bits, &v, 4);
            if (!bits || (bits & 0x7f800000) == 0x7f800000) continue;
            if (!(v > 0.5f && v < 4000.0f) && !(v < -0.5f && v > -4000.0f)) continue;
            put_text(line, &n, sizeof(line), " ");
            put_hex(line, &n, sizeof(line), off);
            put_text(line, &n, sizeof(line), "=");
            put_float(line, &n, sizeof(line), v);
        }
        put_text(line, &n, sizeof(line), "\r\n");
        log_append(line);
        element = read_pointer(element + ELEMENT_PARENT);
    }
}


/* ---- hover + click (menumouse4): tiles are children of a Canvas (Canvas.Left/Top at +0x90/+0x94,
   RenderSize at +0x134/+0x138). AVUI's own hit test misses them (3D planes), so Gripline maps the pointer
   to a tile itself and gives it the keyboard focus, like the pad does. Left click then = A. */
#define ELEMENT_LEFT 0x90u
#define ELEMENT_SIZE 0x134u
#define KEYBOARD_FOCUS 0x40u
#define ORIGIN_X 100.0f /* the tile Canvas' place in the 1920x1080 menu (screenshot + StackPanel margin) */
#define ORIGIN_Y 288.0f
#define MAX_TILES 32
static unsigned char *tile_canvas, *tiles[MAX_TILES];
static int tile_count, hover_logs;
static unsigned char *hovered;

static int is_tile(const void *object) {
    const char *name = class_name(object);
    if (!name) return 0;
    for (const char *c = name; *c; ++c)
        if ((c[0] == 'T' && c[1] == 'i' && c[2] == 'l' && c[3] == 'e' && c[4] == '@') ||
            (c[0] == 'B' && c[1] == 'u' && c[2] == 't' && c[3] == 't' && c[4] == 'o' && c[5] == 'n' && c[6] == '@'))
            return 1;
    return 0;
}

static int tile_rect(unsigned char *tile, float r[4]) {
    float pos[2] = {0, 0}, size[2] = {0, 0};
    if (!memory_read(tile + ELEMENT_LEFT, pos, 8) || !memory_read(tile + ELEMENT_SIZE, size, 8)) return 0;
    for (int i = 0; i < 2; ++i) if (!(pos[i] > -20000.0f && pos[i] < 20000.0f)) pos[i] = 0.0f; /* NaN = not set */
    if (!(size[0] > 20.0f && size[0] < 3000.0f && size[1] > 20.0f && size[1] < 3000.0f)) return 0;
    r[0] = ORIGIN_X + pos[0], r[1] = ORIGIN_Y + pos[1], r[2] = r[0] + size[0], r[3] = r[1] + size[1];
    return 1;
}

static void add_tile(unsigned char *tile) {
    for (int i = 0; i < tile_count; ++i) if (tiles[i] == tile) return;
    if (tile_count < MAX_TILES) tiles[tile_count++] = tile;
}

/* the Canvas keeps its children in an array somewhere behind up to three pointers (exact place not known,
   menumouse5: not directly behind one): search for the array that holds the focused tile (or its input
   interface at +0xb8) and take every tile in it. Once per new Canvas. */
#define LIST_ITEMS 64
static int scan_list(unsigned char *list, unsigned char *canvas, unsigned char *focused, unsigned *adjust) {
    if (!list || (uintptr_t)list & 7 || (uintptr_t)list < 0x10000 || (uintptr_t)list >> 47) return 0;
    int at = -1;
    for (int i = 0; i < LIST_ITEMS && at < 0; ++i) {
        unsigned char *item = NULL;
        if (!memory_read(list + 8 * i, &item, 8)) break;
        if (item == focused) at = i, *adjust = 0;
        else if (item == focused + 0xb8) at = i, *adjust = 0xb8;
    }
    if (at < 0) return 0;
    int found = 0;
    for (int i = 0; i < LIST_ITEMS; ++i) {
        unsigned char *item = NULL;
        if (!memory_read(list + 8 * i, &item, 8)) break;
        if (!item || (uintptr_t)item & 7) continue;
        item -= *adjust;
        if (read_pointer(item + ELEMENT_PARENT) == canvas && is_tile(item)) add_tile(item), ++found;
    }
    return found;
}

static void collect_siblings(unsigned char *canvas, unsigned char *focused) {
    unsigned adjust = 0;
    for (unsigned o1 = 0x78; o1 < 0x300; o1 += 8) {
        unsigned char *p1 = read_pointer(canvas + o1);
        if (!p1 || p1 == focused || (uintptr_t)p1 & 7) continue;
        if (scan_list(p1, canvas, focused, &adjust)) goto done;
        for (unsigned o2 = 0; o2 < 0x80; o2 += 8) {
            unsigned char *p2 = read_pointer(p1 + o2);
            if (!p2 || (uintptr_t)p2 & 7 || p2 == canvas) continue;
            if (scan_list(p2, canvas, focused, &adjust)) { log_append_value("MENUMOUSE: children at canvas+", (int)o1); log_append_value("MENUMOUSE:   then +", (int)o2); goto done; }
            for (unsigned o3 = 0; o3 < 0x40; o3 += 8) {
                unsigned char *p3 = read_pointer(p2 + o3);
                if (scan_list(p3, canvas, focused, &adjust)) {
                    log_append_value("MENUMOUSE: children at canvas+", (int)o1);
                    log_append_value("MENUMOUSE:   then +", (int)o2);
                    log_append_value("MENUMOUSE:   then +", (int)o3);
                    goto done;
                }
            }
        }
    }
    log_append("MENUMOUSE: children list of the tile canvas not found.\r\n");
    return;
done:
    if (adjust) log_append("MENUMOUSE:   (list holds input interfaces)\r\n");
}

enum { IN_DPAD_LEFT = 2, IN_DPAD_RIGHT = 3 };
#define STEP_WAIT_MS 260u
#define MAX_STEPS 10
static unsigned char *target;
static int steps, click_waiting;
static ULONGLONG step_at;
static unsigned char *step_from;
static void log_rect(const char *label, unsigned char *tile);

static void track_focus(unsigned char *keyboard) {
    unsigned char *focused = keyboard ? read_pointer(keyboard + KEYBOARD_FOCUS) : NULL;
    if (!focused || !is_tile(focused)) return;
    unsigned char *canvas = read_pointer(focused + ELEMENT_PARENT);
    if (!canvas) return;
    if (canvas != tile_canvas) { tile_canvas = canvas, tile_count = 0; hovered = NULL; }
    int before = tile_count;
    add_tile(focused);
    if (tile_count != before) {
        collect_siblings(canvas, focused);
        log_append_value("MENUMOUSE: tiles known on this page ", tile_count);
        for (int i = 0; i < tile_count && i < 12; ++i) log_rect("MENUMOUSE:   tile ", tiles[i]);
    }
}

static void log_rect(const char *label, unsigned char *tile) {
    float r[4];
    if (!tile || !tile_rect(tile, r)) return;
    static char line[200];
    unsigned n = 0;
    line[0] = 0;
    put_text(line, &n, sizeof(line), label);
    put_float(line, &n, sizeof(line), r[0]);
    put_text(line, &n, sizeof(line), ",");
    put_float(line, &n, sizeof(line), r[1]);
    put_text(line, &n, sizeof(line), " - ");
    put_float(line, &n, sizeof(line), r[2]);
    put_text(line, &n, sizeof(line), ",");
    put_float(line, &n, sizeof(line), r[3]);
    put_text(line, &n, sizeof(line), " ");
    const char *name = class_name(tile);
    put_text(line, &n, sizeof(line), name ? name : "?");
    put_text(line, &n, sizeof(line), "\r\n");
    log_append(line);
}

static void navigate(unsigned char *keyboard) {
    unsigned char *focused = keyboard ? read_pointer(keyboard + KEYBOARD_FOCUS) : NULL;
    ULONGLONG now = GetTickCount64();
    if (!target || focused == target || steps >= MAX_STEPS || !focused) {
        target = NULL;
        if (click_waiting && now - step_at >= STEP_WAIT_MS) { click_waiting = 0; keyboard_menu_pulse(IN_A); }
        return;
    }
    if (step_from && focused == step_from && now - step_at < STEP_WAIT_MS) return; /* step still on its way */
    float a[4], b[4];
    if (!tile_rect(focused, a) || !tile_rect(target, b)) { target = NULL; return; }
    float cx = (b[0] + b[2]) * 0.5f, cy = (b[1] + b[3]) * 0.5f;
    unsigned input;
    if (cx < a[0]) input = IN_DPAD_LEFT;
    else if (cx >= a[2]) input = IN_DPAD_RIGHT;
    else if (cy < a[1]) input = IN_DPAD_UP;
    else input = IN_DPAD_DOWN;
    keyboard_menu_pulse(input);
    ++steps;
    step_from = focused;
    step_at = now;
}

static void hover(unsigned char *keyboard, float ux, float uy, int moved) {
    track_focus(keyboard);
    navigate(keyboard);
    if (!moved || !tile_count) return;
    unsigned char *under = NULL;
    float r[4];
    for (int i = 0; i < tile_count; ++i)
        if (read_pointer(tiles[i] + ELEMENT_PARENT) == tile_canvas && tile_rect(tiles[i], r) &&
            ux >= r[0] && ux < r[2] && uy >= r[1] && uy < r[3]) under = tiles[i];
    if (!under || under == hovered) return;
    hovered = under;
    target = under, steps = 0, step_from = NULL;
    if (hover_logs < 12) {
        ++hover_logs;
        log_append_value("MENUMOUSE: hover tile at UI x ", (int)ux);
        log_append_value("MENUMOUSE:   UI y ", (int)uy);
        log_rect("MENUMOUSE:   tile ", under);
    }
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
    hover(read_pointer((unsigned char *)context + CONTEXT_INPUT_MANAGER + 0) ?
              read_pointer((unsigned char *)read_pointer((unsigned char *)context + CONTEXT_INPUT_MANAGER) + INPUT_KEYBOARD_DEVICE) : NULL,
          point[0], point[1], moved);
    if (provider != active_provider) moved = 1; /* the first report activates the mouse in that window */
    active_provider = provider;
    if (moved) {
        report(provider, RAW_MOVE, point, 0);
        unsigned char *input = read_pointer((unsigned char *)context + CONTEXT_INPUT_MANAGER);
        (void)input;
    }
    int changed = buttons ^ buttons_before;
    unsigned actions = 0;
    if (changed & 1) actions |= (buttons & 1) ? RAW_LEFT_DOWN : RAW_LEFT_UP;
    if (changed & 2) actions |= (buttons & 2) ? RAW_RIGHT_DOWN : RAW_RIGHT_UP;
    if (changed & 4) actions |= (buttons & 4) ? RAW_MIDDLE_DOWN : RAW_MIDDLE_UP;
    if (actions) {
        unsigned char handled = report(provider, actions, point, 0);
        if (buttons & 1) {
            unsigned char *input = read_pointer((unsigned char *)context + CONTEXT_INPUT_MANAGER);
            dump_fields("keyboard", read_pointer(input + INPUT_KEYBOARD_DEVICE), 1);
        }
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
    int x, y, buttons, wheel, w, h, system_shown;
    { /* diagnostics: the pad focus, on the UI thread */
        unsigned char *context = ((ContextFn)(game + RVA_UI_CONTEXT))();
        unsigned char *input = context ? read_pointer(context + CONTEXT_INPUT_MANAGER) : NULL;
        if (input && has_vtable(read_pointer(game + RVA_WINDOW_MANAGER), RVA_WINDOW_MANAGER_VT))
            log_focus_chain(read_pointer(input + INPUT_KEYBOARD_DEVICE));
    }
    if (!keyboard_menu_mouse(&x, &y, &buttons, &wheel, &w, &h, &system_shown)) {
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
        last_w = 0;
        return;
    }
    /* a menu just opened counts as a move (the pointer shows), except in "only once moved" mode (V7.1) */
    int moved = last_w ? (float)x != px || (float)y != py : keyboard_menu_mouse_mode() != MENU_MOUSE_MOVED;
    px = (float)x, py = (float)y;
    last_w = w, last_h = h;
    ULONGLONG now = GetTickCount64();
    if (moved || wheel || buttons != buttons_before || buttons) moved_at = now;
    if (!moved_at || now - moved_at >= HIDE_MS) {
        if (shown) notify_cursor(0, 0, 0);
        shown = 0;
        deactivate();
        buttons_before = buttons;
        return;
    }
    /* Gripline's arrow only where Windows hides its own cursor over the game */
    shown = !system_shown;
    notify_cursor(shown, x, y);
    /* wheel: over the tab row at the top -> previous / next tab (LB / RB), elsewhere up / down in lists */
    if (wheel) {
        int notches = (wheel < 0 ? -wheel : wheel) / 120;
        if (notches < 1) notches = 1;
        if (notches > 4) notches = 4;
        int top = py < (float)h * TAB_ROW_PART;
        for (int i = 0; i < notches; ++i) keyboard_menu_pulse(wheel > 0 ? (top ? IN_LB : IN_DPAD_UP) : (top ? IN_RB : IN_DPAD_DOWN));
    }
    if ((buttons & 2) && !(buttons_before & 2)) keyboard_menu_pulse(IN_B); /* right click: back */
    if ((buttons & 1) && !(buttons_before & 1)) { /* left click: open the tile (after the walk to it) */
        if (target) click_waiting = 1;
        else keyboard_menu_pulse(IN_A);
    }
    feed(moved, buttons, 0);
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
