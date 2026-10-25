/* compat6: free wheel buttons -> game functions (unofficial add-on). See wheel_buttons.h.
 *
 * Game addresses (forza_durango_release_final.exe, TimeDateStamp 0x565501ed):
 *   0x106c320  builds the XInput-like state of a controller; for a wheel (no IGamepad,
 *              rbx == NULL) the right-stick button and the left stick Y are hard zero:
 *              0x106c65b  test rbx,rbx / je 0x106c67f   (RS button, continues at 0x106c681)
 *              0x106ca23  test rbx,rbx / je 0x106ca4b   (left stick Y, continues at 0x106ca4f)
 *   0x2dc300   loads a controller layout from gamedb (kind 1 Controller, 2 Wheel, 3 Bristol)
 *              into a binding map; called from 0x2f8f1f, 0x2f8f3b, 0x2f8f57 and 0x2fd673.
 *   0x1073f00  adds a binding (map, context, action, input, 0x25, 0x25, [0x2e69db0], [0x2e69db4]).
 * A binding is 28 bytes {context, action, input, ...}; map->contexts[0] is a std::vector of them.
 * Inputs: 0-3 D-pad, 4 Menu, 5 View, 6/7 stick buttons, 8/9 LB/RB, 12-15 A/B/X/Y,
 *         0x13/0x14 left stick up/down. Actions: see function_actions[]. */
#include "wheel_buttons.h"
#include "logging.h"
#include "support.h"
#include "video_options_text.h"
#include "nitro_link.h"
#include "cockpit_view.h"

void wheel_log_text(const char *text);

#define RVA_RS_SITE 0x106c65bu
#define RVA_RS_WHEEL 0x106c681u
#define RVA_LY_SITE 0x106ca23u
#define RVA_LY_WHEEL 0x106ca4fu
#define RVA_LOAD_LAYOUT 0x2dc300u
#define RVA_BIND 0x1073f00u
#define RVA_BIND_PARAM_A 0x2e69db0u
#define RVA_BIND_PARAM_B 0x2e69db4u
#define KIND_WHEEL 2
#define INPUT_RS_BUTTON 7
#define INPUT_LY_UP 0x13
#define INPUT_LY_DOWN 0x14
#define ACTION_COUNT 32

enum {
    ACTION_HANDBRAKE = 3,
    ACTION_SHIFT_UP = 4,
    ACTION_SHIFT_DOWN = 5,
    ACTION_CLUTCH = 6,
    ACTION_LOOK_LEFT = 11,
    ACTION_LOOK_RIGHT = 12,
    ACTION_LOOK_STRAIGHT_BACK = 13,
    ACTION_TELEMETRY = 14,
    ACTION_PHOTO_MODE = 15,
    ACTION_SWITCH_CAMERA = 17,
    ACTION_REWIND = 20,
    ACTION_RADIO_LEFT = 23,
    ACTION_RADIO_RIGHT = 24,
};

static const signed char function_actions[WHEEL_FUNCTION_COUNT] = {
    [WHEEL_FUNCTION_OFF] = -1,
    [WHEEL_FUNCTION_CAMERA] = ACTION_SWITCH_CAMERA,
    [WHEEL_FUNCTION_LOOK_BACK] = ACTION_LOOK_STRAIGHT_BACK,
    [WHEEL_FUNCTION_LOOK_LEFT] = ACTION_LOOK_LEFT,
    [WHEEL_FUNCTION_LOOK_RIGHT] = ACTION_LOOK_RIGHT,
    [WHEEL_FUNCTION_REWIND] = ACTION_REWIND,
    [WHEEL_FUNCTION_TELEMETRY] = ACTION_TELEMETRY,
    [WHEEL_FUNCTION_PHOTO] = ACTION_PHOTO_MODE,
    [WHEEL_FUNCTION_RADIO_PREVIOUS] = ACTION_RADIO_LEFT,
    [WHEEL_FUNCTION_RADIO_NEXT] = ACTION_RADIO_RIGHT,
    [WHEEL_FUNCTION_HANDBRAKE] = ACTION_HANDBRAKE,
    [WHEEL_FUNCTION_CLUTCH] = ACTION_CLUTCH,
    [WHEEL_FUNCTION_SHIFT_UP] = ACTION_SHIFT_UP,
    [WHEEL_FUNCTION_SHIFT_DOWN] = ACTION_SHIFT_DOWN,
    [WHEEL_FUNCTION_NITRO] = -1,
    [WHEEL_FUNCTION_COCKPIT_VIEW] = -1,
};

static const wchar_t *const names_german[WHEEL_FUNCTION_COUNT] = {
    L"Aus",          L"Kamera wechseln", L"Blick zurück", L"Blick links",     L"Blick rechts",
    L"Zurückspulen", L"Telemetrie",      L"Fotomodus",    L"Radio: zurück",   L"Radio: weiter",
    L"Handbremse",   L"Kupplung",        L"Hochschalten", L"Runterschalten", L"Nitro (NitroSystem)",
    L"Cockpit-Ansicht 2"};
static const wchar_t *const names_english[WHEEL_FUNCTION_COUNT] = {
    L"Off",    L"Change Camera", L"Look Back",     L"Look Left",      L"Look Right",
    L"Rewind", L"Telemetry",     L"Photo Mode",    L"Radio: Previous", L"Radio: Next",
    L"Handbrake", L"Clutch",     L"Shift Up",      L"Shift Down",    L"Nitro (NitroSystem)",
    L"Cockpit View 2"};

const wchar_t *wheel_function_name(unsigned char *game_image, unsigned function) {
    if (function >= WHEEL_FUNCTION_COUNT) {
        return L"";
    }
    return video_options_text_is_german(game_image) ? names_german[function]
                                                     : names_english[function];
}

/* ------------------------------------------------------------------ input of the wheel layout */

typedef struct {
    int32_t context, action, input, mode_a, mode_b, param_a, param_b;
} Binding;
_Static_assert(sizeof(Binding) == 28, "game binding record");

typedef uint64_t (*LoadLayoutFn)(void *map, uint64_t id, uint64_t kind, void *options,
                                 uint64_t extra);
typedef void (*BindFn)(void *map, uint64_t context, uint64_t action, uint64_t input,
                       uint64_t mode_a, uint64_t mode_b, uint64_t param_a, uint64_t param_b);

static unsigned char *image;
static LoadLayoutFn load_layout;
static BindFn bind;
/* action -> input of the active wheel layout (-1 = none we can trigger) */
static volatile LONG action_input[ACTION_COUNT];
static volatile LONG layouts_seen;
static volatile LONG extra_rs, extra_ly;
static int installed;

/* report bit of an input: byte index in the wheel report and mask, 0 mask = not a report bit */
static void report_bit(int input, unsigned *byte, unsigned char *mask) {
    static const signed char bytes[16] = {1, 1, 1, 1, 0, 0, -1, -1, 1, 1, -1, -1, 0, 0, 0, 0};
    static const unsigned char masks[16] = {0x01, 0x02, 0x04, 0x08, 0x04, 0x08, 0,    0,
                                            0x10, 0x20, 0,    0,    0x10, 0x20, 0x40, 0x80};
    *mask = 0;
    if (input >= 0 && input < 16 && bytes[input] >= 0) {
        *byte = (unsigned)bytes[input];
        *mask = masks[input];
    }
}

static int virtual_input(int input) {
    return input == INPUT_RS_BUTTON || input == INPUT_LY_UP || input == INPUT_LY_DOWN;
}

static const char *input_name(int input) {
    static const char *const names[16] = {"Steuerkreuz hoch", "Steuerkreuz runter", "Steuerkreuz links",
                                          "Steuerkreuz rechts", "Menu", "View", "LS", "RS",
                                          "Wippe links", "Wippe rechts", "LT", "RT", "A", "B", "X", "Y"};
    if (input >= 0 && input < 16) return names[input];
    if (input == INPUT_LY_UP) return "linker Stick hoch";
    if (input == INPUT_LY_DOWN) return "linker Stick runter";
    return "-";
}

static void append(char *line, size_t capacity, size_t *length, const char *text) {
    while (*text && *length + 1 < capacity) line[(*length)++] = *text++;
    line[*length] = 0;
}

static void log_table(uint64_t id) {
    static const char *const labels[WHEEL_FUNCTION_COUNT] = {
        "", "Kamera", "Blick zurueck", "Blick links", "Blick rechts", "Zurueckspulen", "Telemetrie",
        "Fotomodus", "Radio zurueck", "Radio weiter", "Handbremse", "Kupplung", "Hochschalten",
        "Runterschalten", "Nitro", "Cockpit-Ansicht"};
    char line[700];
    size_t length = 0;
    char number[4] = {(char)('0' + (id % 100) / 10), (char)('0' + id % 10), 0, 0};
    append(line, sizeof(line), &length, "Spiel laedt Lenkrad-Layout ");
    append(line, sizeof(line), &length, id >= 10 ? number : number + 1);
    append(line, sizeof(line), &length, " - Zusatztasten loesen aus:");
    for (unsigned f = 1; f < WHEEL_FUNCTION_NITRO; ++f) {
        append(line, sizeof(line), &length, " ");
        append(line, sizeof(line), &length, labels[f]);
        append(line, sizeof(line), &length, "=");
        append(line, sizeof(line), &length, input_name((int)action_input[function_actions[f]]));
        append(line, sizeof(line), &length, ";");
    }
    wheel_log_text(line);
}

/* Wheel layout loaded: RS -> look back, left stick Y -> look left/right, then remember which
   input triggers which action. Runs on the game's thread right after the layout was read. */
static void adapt_wheel_map(void *map, uint64_t id) {
    unsigned char *contexts = *(unsigned char **)map;
    unsigned char *contexts_end = *(unsigned char **)((unsigned char *)map + 8);
    if (!contexts || contexts_end - contexts < 24) {
        return;
    }
    Binding *first = *(Binding **)contexts, *last = *(Binding **)(contexts + 8);
    if (!first || last < first || last - first > 256) {
        wheel_log_text("Zusatztasten: Lenkrad-Layout hat ein unerwartetes Format - nichts geaendert.");
        return;
    }
    int has_rs = 0;
    for (Binding *b = first; b < last; ++b) {
        if (b->context != 0 || b->action < 0 || b->action >= ACTION_COUNT || b->input < 0 ||
            b->input > 0x40) {
            wheel_log_text("Zusatztasten: unerwarteter Eintrag im Lenkrad-Layout - nichts geaendert.");
            return;
        }
    }
    for (Binding *b = first; b < last; ++b) {
        /* the left stick never moves on a wheel: its handbrake binding is free */
        if (b->input == INPUT_LY_UP) b->action = ACTION_LOOK_LEFT;
        if (b->input == INPUT_LY_DOWN) b->action = ACTION_LOOK_RIGHT;
        if (b->input == INPUT_RS_BUTTON) {
            b->action = ACTION_LOOK_STRAIGHT_BACK;
            has_rs = 1;
        }
    }
    if (!has_rs) {
        bind(map, 0, ACTION_LOOK_STRAIGHT_BACK, INPUT_RS_BUTTON, 0x25, 0x25,
             *(uint32_t *)(image + RVA_BIND_PARAM_A), *(uint32_t *)(image + RVA_BIND_PARAM_B));
        contexts = *(unsigned char **)map;
        first = *(Binding **)contexts;
        last = *(Binding **)(contexts + 8);
    }
    LONG table[ACTION_COUNT];
    for (unsigned a = 0; a < ACTION_COUNT; ++a) table[a] = -1;
    for (int pass = 0; pass < 2; ++pass) { /* real buttons first, then the stick inputs */
        for (Binding *b = first; b < last; ++b) {
            unsigned byte;
            unsigned char mask;
            report_bit(b->input, &byte, &mask);
            int usable = pass == 0 ? mask != 0 : virtual_input(b->input);
            if (usable && table[b->action] < 0) table[b->action] = b->input;
        }
    }
    for (unsigned a = 0; a < ACTION_COUNT; ++a) InterlockedExchange(&action_input[a], table[a]);
    InterlockedIncrement(&layouts_seen);
    log_table(id);
}

/* controller (gamepad) layouts: remember which input triggers which action, for the keyboard */
#define KIND_CONTROLLER 1
static volatile LONG controller_input[ACTION_COUNT];
static volatile LONG controller_ready;

static void controller_defaults(void) {
    /* ControllerLayouts layout 0 of gamedb (the game's default) */
    static const signed char layout0[ACTION_COUNT] = {
        [0] = 11,  [1] = 10,  [2] = -1,  [3] = 12,  [4] = 13,  [5] = 14,  [6] = 8,   [7] = -1,
        [8] = -1,  [9] = 0x1a, [10] = 0x1b, [11] = 0x1c, [12] = 0x1d, [13] = -1, [14] = 1, [15] = 0,
        [16] = -1, [17] = 9,  [18] = -1, [19] = -1, [20] = 15, [21] = -1, [22] = -1, [23] = 2,
        [24] = 3,  [25] = -1, [26] = -1, [27] = -1, [28] = -1, [29] = -1, [30] = -1, [31] = -1};
    for (unsigned a = 0; a < ACTION_COUNT; ++a) controller_input[a] = layout0[a];
}

int controller_action_input(unsigned action) {
    if (!InterlockedCompareExchange(&controller_ready, 1, 0)) controller_defaults();
    return action < ACTION_COUNT ? (int)controller_input[action] : -1;
}

static void record_controller_map(void *map, uint64_t id) {
    unsigned char *contexts = *(unsigned char **)map;
    unsigned char *contexts_end = *(unsigned char **)((unsigned char *)map + 8);
    if (!contexts || contexts_end - contexts < 24) return;
    Binding *first = *(Binding **)contexts, *last = *(Binding **)(contexts + 8);
    if (!first || last < first || last - first > 256) return;
    LONG table[ACTION_COUNT];
    for (unsigned a = 0; a < ACTION_COUNT; ++a) table[a] = -1;
    for (Binding *b = first; b < last; ++b) {
        if (b->context != 0 || b->action < 0 || b->action >= ACTION_COUNT || b->input < 0 || b->input > 0x40)
            return; /* unexpected: keep the previous table */
        if (table[b->action] < 0) table[b->action] = b->input;
    }
    InterlockedExchange(&controller_ready, 1);
    for (unsigned a = 0; a < ACTION_COUNT; ++a) InterlockedExchange(&controller_input[a], table[a]);
    char line[64] = "KEYBOARD: game loads controller layout ";
    size_t length = text_length(line);
    if (id >= 10) line[length++] = (char)('0' + (id % 100) / 10);
    line[length++] = (char)('0' + id % 10);
    line[length++] = '\r';
    line[length++] = '\n';
    line[length] = 0;
    log_append(line);
}

static uint64_t layout_hook(void *map, uint64_t id, uint64_t kind, void *options, uint64_t extra) {
    uint64_t result = load_layout(map, id, kind, options, extra);
    if ((kind & 0xff) == KIND_CONTROLLER && map) {
        __try {
            record_controller_map(map, id & 0xffffffff);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log_append("KEYBOARD: unexpected controller layout - keyboard keeps the default layout.\r\n");
        }
    }
    if ((kind & 0xff) == KIND_WHEEL && map) {
        __try {
            adapt_wheel_map(map, id & 0xffffffff);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            wheel_log_text("FEHLER: Ausnahme beim Anpassen des Lenkrad-Layouts - Zusatztasten fuer Blicke aus.");
        }
    }
    return result;
}

/* called by the state stubs on the game's input thread (wheel path only) */
static volatile LONG wheel_reads;

static int rs_pressed(void) {
    InterlockedIncrement(&wheel_reads);
    return extra_rs != 0;
}

static float stick_y(void) {
    LONG value = extra_ly;
    return value > 0 ? 1.0f : value < 0 ? -1.0f : 0.0f;
}

unsigned wheel_buttons_wheel_reads(void) {
    return (unsigned)wheel_reads;
}

int wheel_buttons_action_input(unsigned action) {
    return installed && action < ACTION_COUNT ? (int)action_input[action] : -1;
}

void wheel_buttons_update(unsigned held, unsigned char *byte0, unsigned char *byte1) {
    LONG rs = 0, ly = 0;
    nitro_link_set(NITRO_SOURCE_WHEEL, (held >> WHEEL_FUNCTION_NITRO) & 1);
    cockpit_view_input(COCKPIT_VIEW_WHEEL, (held >> WHEEL_FUNCTION_COCKPIT_VIEW) & 1);
    for (unsigned f = 1; f < WHEEL_FUNCTION_NITRO; ++f) {
        if (!(held & (1u << f))) continue;
        int input = (int)action_input[function_actions[f]];
        unsigned byte;
        unsigned char mask;
        report_bit(input, &byte, &mask);
        if (mask) {
            (byte ? byte1 : byte0)[0] |= mask;
        } else if (installed && input == INPUT_RS_BUTTON) {
            rs = 1;
        } else if (installed && input == INPUT_LY_UP) {
            ly = 1;
        } else if (installed && input == INPUT_LY_DOWN) {
            ly = -1;
        }
    }
    if (extra_rs != rs) InterlockedExchange(&extra_rs, rs);
    if (extra_ly != ly) InterlockedExchange(&extra_ly, ly);
}

/* ------------------------------------------------------------------ installation */

static int rel32(const unsigned char *from_next, const unsigned char *to, int32_t *out) {
    int64_t delta = (int64_t)((intptr_t)to - (intptr_t)from_next);
    if (delta < INT32_MIN || delta > INT32_MAX) {
        return 0;
    }
    *out = (int32_t)delta;
    return 1;
}

static size_t put(unsigned char *at, const void *bytes, size_t size) {
    memory_copy(at, bytes, size);
    return size;
}

static size_t put64(unsigned char *at, uint64_t value) {
    return put(at, &value, 8);
}

/* test rbx,rbx; jz wheel; ret (gamepad path continues behind the call);
   wheel: drop the return address, call the C function, jump to the continuation */
static size_t state_stub(unsigned char *at, void *function, unsigned char *continuation) {
    static const unsigned char head[] = {0x48, 0x85, 0xdb, 0x74, 0x01, 0xc3,
                                         0x48, 0x83, 0xc4, 0x08, 0x48, 0x83, 0xec, 0x20, 0x48, 0xb8};
    static const unsigned char call_rax[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20,
                                             0xff, 0x25, 0, 0, 0, 0};
    size_t n = put(at, head, sizeof(head));
    n += put64(at + n, (uint64_t)(uintptr_t)function);
    n += put(at + n, call_rax, sizeof(call_rax));
    n += put64(at + n, (uint64_t)(uintptr_t)continuation);
    return n;
}

static size_t jump_stub(unsigned char *at, void *function) {
    static const unsigned char jmp[] = {0xff, 0x25, 0, 0, 0, 0};
    size_t n = put(at, jmp, sizeof(jmp));
    return n + put64(at + n, (uint64_t)(uintptr_t)function);
}

static int write_code(unsigned char *site, const unsigned char *bytes, size_t size) {
    DWORD old, ignored;
    if (!VirtualProtect(site, size, PAGE_EXECUTE_READWRITE, &old)) {
        return 0;
    }
    memory_copy(site, bytes, size);
    VirtualProtect(site, size, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), site, size);
    return 1;
}

static const unsigned load_calls[4] = {0x2f8f1f, 0x2f8f3b, 0x2f8f57, 0x2fd673};

int wheel_buttons_install(unsigned char *game_image) {
    if (installed) return installed;
    /* until the game loads the player's layout: WheelLayouts layout 0 (gamedb) */
    for (unsigned a = 0; a < ACTION_COUNT; ++a) action_input[a] = -1;
    action_input[ACTION_SWITCH_CAMERA] = 15;
    action_input[ACTION_REWIND] = 14;
    action_input[ACTION_TELEMETRY] = 1;
    action_input[ACTION_PHOTO_MODE] = 0;
    action_input[ACTION_RADIO_LEFT] = 2;
    action_input[ACTION_RADIO_RIGHT] = 3;
    action_input[ACTION_HANDBRAKE] = 13;
    action_input[ACTION_CLUTCH] = 12;
    action_input[ACTION_SHIFT_DOWN] = 8;
    action_input[ACTION_SHIFT_UP] = 9;
    if (!game_image || installed) {
        return installed;
    }
    image = game_image;
    load_layout = (LoadLayoutFn)(image + RVA_LOAD_LAYOUT);
    bind = (BindFn)(image + RVA_BIND);
    /* every site must hold exactly the bytes of the supported game version */
    static const unsigned char rs_original[5] = {0x48, 0x85, 0xdb, 0x74, 0x1f};
    static const unsigned char ly_original[5] = {0x48, 0x85, 0xdb, 0x74, 0x23};
    int ok = memory_equal(image + RVA_RS_SITE, rs_original, 5) &&
             memory_equal(image + RVA_LY_SITE, ly_original, 5);
    for (unsigned i = 0; ok && i < ARRAY_COUNT(load_calls); ++i) {
        unsigned char *site = image + load_calls[i];
        int32_t expected;
        ok = site[0] == 0xe8 && rel32(site + 5, image + RVA_LOAD_LAYOUT, &expected) &&
             memory_equal(site + 1, &expected, 4);
    }
    if (!ok) {
        wheel_log_text("Zusatztasten: Spielcode weicht ab - Blick zurueck/links/rechts nicht verfuegbar, "
                       "andere Funktionen gehen trotzdem.");
        return 0;
    }
    unsigned char *page = memory_alloc_near(image, 0x1000);
    if (!page) {
        wheel_log_text("Zusatztasten: kein Speicher nahe am Spiel - Blicke nicht verfuegbar.");
        return 0;
    }
    unsigned char *rs_stub = page, *ly_stub = page + 0x40, *layout_stub = page + 0x80;
    state_stub(rs_stub, (void *)rs_pressed, image + RVA_RS_WHEEL);
    state_stub(ly_stub, (void *)stick_y, image + RVA_LY_WHEEL);
    jump_stub(layout_stub, (void *)layout_hook);
    DWORD old;
    VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);

    unsigned char call[5] = {0xe8, 0, 0, 0, 0};
    int32_t offset;
    struct {
        unsigned rva;
        unsigned char *target;
    } sites[6] = {{RVA_RS_SITE, rs_stub},        {RVA_LY_SITE, ly_stub},
                  {load_calls[0], layout_stub}, {load_calls[1], layout_stub},
                  {load_calls[2], layout_stub}, {load_calls[3], layout_stub}};
    for (unsigned i = 0; i < ARRAY_COUNT(sites); ++i) {
        if (!rel32(image + sites[i].rva + 5, sites[i].target, &offset)) {
            wheel_log_text("Zusatztasten: Sprungweite zu gross - Blicke nicht verfuegbar.");
            return 0;
        }
    }
    for (unsigned i = 0; i < ARRAY_COUNT(sites); ++i) {
        rel32(image + sites[i].rva + 5, sites[i].target, &offset);
        memory_copy(call + 1, &offset, 4);
        if (!write_code(image + sites[i].rva, call, 5)) {
            wheel_log_text("FEHLER: Spielcode fuer Zusatztasten nicht beschreibbar.");
            return 0; /* earlier sites stay hooked; the hooks are safe on their own */
        }
    }
    installed = 1;
    wheel_log_text("Zusatztasten: Spiel-Hooks gesetzt (RS = Blick zurueck, linker Stick = Blick links/rechts).");
    log_append("WHEEL: free wheel buttons ready (look back/left/right hooks installed).\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestControllerActionInput(unsigned action) {
    return controller_action_input(action);
}
__declspec(dllexport) int Fh2TestWheelActionInput(unsigned action) {
    return action < ACTION_COUNT ? (int)action_input[action] : -2;
}
__declspec(dllexport) int Fh2TestWheelExtraState(void) {
    return (int)(extra_rs ? 1 : 0) | (int)((extra_ly + 1) << 4);
}
__declspec(dllexport) unsigned Fh2TestWheelButtonsUpdate(unsigned held) {
    unsigned char b0 = 0, b1 = 0;
    wheel_buttons_update(held, &b0, &b1);
    return b0 | (unsigned)b1 << 8;
}
#endif
