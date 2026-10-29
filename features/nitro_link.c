/* Nitro key for NitroSystem - see nitro_link.h. Own code, no code from NitroSystem. */
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "nitro_link.h"
#include "notify.h"

typedef struct {
    DWORD packet;
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
} PadState;
_Static_assert(sizeof(PadState) == 16, "XINPUT_STATE");

typedef DWORD(WINAPI *GetStateFn)(DWORD, PadState *);

#define BUTTON_LT 0x8001u /* NitroSystem's own codes for the analog triggers */
#define BUTTON_RT 0x8002u

static volatile LONG held_sources;
static volatile LONG nitro_button = 0x0100; /* LB, NitroSystem's default */
static GetStateFn original_get_state;
static HMODULE linked_module;
static void *volatile *linked_slot;
static wchar_t ini_path[PATH_CAPACITY];
static ULONGLONG next_search, next_ini;
static unsigned char *game;
static volatile LONG online_state = -1; /* -1 unknown, 0 offline, 1 online session */
static ULONGLONG next_online_check;

/* FH2's network connection object (DAT_1436e24f8): its state +0x10 is 1 while a network
   session is up (the same test the Car Meet uses for "online meet", FUN_1411f73a0). */
#define NET_CONNECTION_RVA 0x36e24f8u

static int session_online(void) {
    void *connection = NULL;
    int state = 0;
    if (!game || !memory_read(game + NET_CONNECTION_RVA, &connection, sizeof(connection)) || !connection)
        return 0;
    if (!memory_read((unsigned char *)connection + 0x10, &state, sizeof(state))) return 0;
    return state == 1;
}

/* Online: nitro off (NitroSystem would give an unfair boost to the other players' races). */
static int nitro_blocked(void) {
    ULONGLONG now = GetTickCount64();
    if (now >= next_online_check) {
        next_online_check = now + 500;
        LONG online = session_online();
        LONG before = InterlockedExchange(&online_state, online);
        if (before != online && (before != -1 || online)) {
            if (online) {
                log_append("NITRO: online session - nitro off.\r\n");
                if (linked_module) notify_show("Nitro off\nOnline session (FH2MP)");
            } else {
                log_append("NITRO: offline again - nitro on.\r\n");
                if (linked_module) notify_show("Nitro on again\nOffline session");
            }
        }
    }
    return online_state == 1;
}

static const struct {
    const wchar_t *name;
    WORD mask;
} buttons[] = {
    {L"LB", 0x0100},     {L"RB", 0x0200},       {L"A", 0x1000},        {L"B", 0x2000},
    {L"X", 0x4000},      {L"Y", 0x8000},        {L"LS", 0x0040},       {L"RS", 0x0080},
    {L"DPadUp", 0x0001}, {L"DPadDown", 0x0002}, {L"DPadLeft", 0x0004}, {L"DPadRight", 0x0008},
    {L"Back", 0x0020},   {L"Start", 0x0010},    {L"LT", BUTTON_LT},    {L"RT", BUTTON_RT},
};

static int wide_equal_ignore_case(const wchar_t *a, const wchar_t *b) {
    for (;; ++a, ++b) {
        wchar_t x = *a, y = *b;
        if (x >= L'a' && x <= L'z') x = (wchar_t)(x - 32);
        if (y >= L'a' && y <= L'z') y = (wchar_t)(y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static int ascii_equal_ignore_case(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* NitroButton from NitroSystem's INI (it re-reads it too, so follow changes). */
static void read_button(void) {
    if (!ini_path[0]) return;
    wchar_t text[32];
    GetPrivateProfileStringW(L"Settings", L"NitroButton", L"LB", text, 32, ini_path);
    LONG mask = 0x0100; /* NitroSystem: unknown names fall back to LB */
    for (unsigned i = 0; i < ARRAY_COUNT(buttons); ++i) {
        if (wide_equal_ignore_case(text, buttons[i].name)) mask = buttons[i].mask;
    }
    InterlockedExchange(&nitro_button, mask);
}

static DWORD WINAPI linked_get_state(DWORD user, PadState *state) {
    GetStateFn original = original_get_state;
    DWORD result = original ? original(user, state) : 1167; /* ERROR_DEVICE_NOT_CONNECTED */
    if (!state) return result;
    if (nitro_blocked()) { /* also hide the real controller's NitroButton */
        if (result == 0) {
            unsigned blocked = (unsigned)nitro_button;
            if (blocked == BUTTON_LT) state->left_trigger = 0;
            else if (blocked == BUTTON_RT) state->right_trigger = 0;
            else state->buttons = (WORD)(state->buttons & ~blocked);
        }
        return result;
    }
    if (user != 0 || !held_sources) return result;
    if (result != 0) { /* no XInput pad (wheel / keyboard only): report an idle pad */
        PadState idle = {0};
        *state = idle;
        result = 0;
    }
    unsigned mask = (unsigned)nitro_button;
    if (mask == BUTTON_LT) {
        state->left_trigger = 255;
    } else if (mask == BUTTON_RT) {
        state->right_trigger = 255;
    } else {
        state->buttons = (WORD)(state->buttons | mask);
    }
    return result;
}

static int is_xinput(const char *name) {
    return ascii_equal_ignore_case(name, "XINPUT1_4.dll") || ascii_equal_ignore_case(name, "XINPUT1_3.dll") ||
           ascii_equal_ignore_case(name, "XINPUT9_1_0.dll") || ascii_equal_ignore_case(name, "XINPUT1_2.dll") ||
           ascii_equal_ignore_case(name, "XINPUT1_1.dll");
}

/* IAT slot of XInputGetState (by name, or ordinal 2 of xinput1_x) in a loaded module. */
static void *volatile *find_get_state_slot(unsigned char *module) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return NULL;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(module + dir.VirtualAddress); d->Name;
         ++d) {
        if (!is_xinput((const char *)(module + d->Name))) continue;
        IMAGE_THUNK_DATA64 *names =
            (IMAGE_THUNK_DATA64 *)(module + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        void **slots = (void **)(module + d->FirstThunk);
        for (unsigned i = 0; names[i].u1.AddressOfData; ++i) {
            ULONGLONG entry = names[i].u1.AddressOfData;
            int match;
            if (entry & IMAGE_ORDINAL_FLAG64) {
                match = (entry & 0xffff) == 2;
            } else {
                IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(module + (DWORD)entry);
                match = ascii_equal_ignore_case((const char *)by_name->Name, "XInputGetState");
            }
            if (match) return (void *volatile *)&slots[i];
        }
    }
    return NULL;
}

static int link_module(HMODULE module, const wchar_t *ini) {
    void *volatile *slot = find_get_state_slot((unsigned char *)module);
    if (!slot) {
        log_append("NITRO: FnFNitro.dll found, but no XInputGetState import - nitro key not linked.\r\n");
        return 0;
    }
    if (*slot == (void *)linked_get_state) return 1;
    DWORD old;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    original_get_state = (GetStateFn)*slot;
    InterlockedExchangePointer((void *volatile *)slot, (void *)linked_get_state);
    VirtualProtect((void *)slot, sizeof(void *), old, &old);
    linked_module = module;
    linked_slot = slot;
    memory_copy(ini_path, ini, sizeof(ini_path));
    read_button();
    log_append("NITRO: NitroSystem found - Gripline nitro key and wheel button press its "
               "NitroButton.\r\n");
    notify_show("NitroSystem found\nNitro: N / wheel button");
    return 1;
}

static void search(void) {
    HMODULE module = GetModuleHandleW(L"FnFNitro.dll");
    if (!module) return;
    static wchar_t path[PATH_CAPACITY];
    DWORD n = GetModuleFileNameW(module, path, PATH_CAPACITY);
    if (!n || n >= PATH_CAPACITY) return;
    while (n && path[n - 1] != L'\\' && path[n - 1] != L'/') --n;
    path[n] = 0;
    static wchar_t ini[PATH_CAPACITY];
    if (!path_join(ini, path, L"FnFNitro.ini")) return;
    link_module(module, ini);
}

void nitro_link_start(unsigned char *game_image) {
    game = game_image;
}

void nitro_link_set(unsigned source, int held) {
    if (held) {
        InterlockedOr(&held_sources, (LONG)source);
    } else if (held_sources & (LONG)source) {
        InterlockedAnd(&held_sources, ~(LONG)source);
    }
    static volatile LONG busy; /* keyboard (game thread) and wheel poll thread both call this */
    if (InterlockedCompareExchange(&busy, 1, 0) != 0) return;
    ULONGLONG now = GetTickCount64();
    if (!linked_module && now >= next_search) {
        next_search = now + 1000;
        search();
    } else if (linked_module && now >= next_ini) {
        next_ini = now + 2000;
        read_button();
    }
    InterlockedExchange(&busy, 0);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestNitroLink(void *module, const wchar_t *ini) {
    return link_module((HMODULE)module, ini);
}

__declspec(dllexport) void Fh2TestNitroSet(unsigned source, int held) {
    nitro_link_set(source, held);
}

__declspec(dllexport) void Fh2TestNitroGame(void *image) {
    game = (unsigned char *)image;
    next_online_check = 0;
}

__declspec(dllexport) void *Fh2TestNitroHook(void) {
    return (void *)linked_get_state;
}
#endif
