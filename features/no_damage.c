#include <windows.h>
#include "gripline.h"
#include "game_addresses.h"
#include "logging.h"
#include "support.h"
#include "no_damage.h"

typedef void (*CarFn)(void *car);

static unsigned char *game;
static volatile LONG mode;
static volatile LONG broken; /* the game build does not match, or a reset faulted: stays off */
static int checked;

/* other cars: when each was reset last (a small table, pointers are reused when cars come and go) */
#define SLOTS 512
static void *slot_car[SLOTS];
static ULONGLONG slot_time[SLOTS];

void no_damage_bind(unsigned char *game_image) {
    game = game_image;
}

void no_damage_set_mode(int value) {
    if (value < 0 || value >= NO_DAMAGE_COUNT) value = NO_DAMAGE_OFF;
    if (InterlockedExchange(&mode, value) == value) return;
    log_append(value == NO_DAMAGE_ALL      ? "DAMAGE: no visible damage on all cars.\r\n"
               : value == NO_DAMAGE_OTHERS ? "DAMAGE: no visible damage on the other cars (Drivatars, traffic).\r\n"
                                           : "DAMAGE: visible damage as in the game.\r\n");
}

int no_damage_mode(void) {
    return (int)mode;
}

/* The game build must be the one the addresses come from: slot 0x120 of the car's vtable holds
   the reset routine and the helper starts with "xor eax, eax; mov [rcx+0x5ae8], rax". */
static int build_matches(void) {
    static const unsigned char helper[] = {0x33, 0xc0, 0x48, 0x89, 0x81, 0xe8, 0x5a, 0x00, 0x00};
    void *reset = NULL;
    unsigned char code[sizeof(helper)];
    if (!memory_read_pointer(game + GAME_RVA_PLAYER_CAR_VTABLE + NO_DAMAGE_RESET_SLOT, &reset) ||
        reset != game + NO_DAMAGE_RESET_RVA || !memory_read(game + NO_DAMAGE_HELPER_RVA, code, sizeof(code)))
        return 0;
    for (unsigned i = 0; i < sizeof(code); ++i)
        if (code[i] != helper[i]) return 0;
    return 1;
}

static int due(void *car) {
    ULONGLONG now = GetTickCount64();
    unsigned h = (unsigned)(((ULONG_PTR)car >> 4) * 2654435761u) % SLOTS, free_slot = SLOTS, oldest = h;
    for (unsigned k = 0; k < 16; ++k) {
        unsigned i = (h + k) % SLOTS;
        if (slot_car[i] == car) {
            if (now - slot_time[i] < NO_DAMAGE_OTHER_MS) return 0;
            slot_time[i] = now;
            return 1;
        }
        if (!slot_car[i] && free_slot == SLOTS) free_slot = i;
        if (slot_time[i] < slot_time[oldest]) oldest = i;
    }
    unsigned i = free_slot != SLOTS ? free_slot : oldest;
    slot_car[i] = car;
    slot_time[i] = now;
    return 1;
}

static int reset(void *car) {
    __try {
        ((CarFn)(game + NO_DAMAGE_RESET_RVA))(car);
        ((CarFn)(game + NO_DAMAGE_HELPER_RVA))(car);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void no_damage_after_update(void *car, int own, int driving) {
    int m = (int)mode;
    if (m == NO_DAMAGE_OFF || broken || !game || !car || !driving) return;
    if (own && m != NO_DAMAGE_ALL) return;
    if (!checked) {
        checked = 1;
        if (!build_matches()) {
            InterlockedExchange(&broken, 1);
            log_append("DAMAGE: the game's damage reset was not found (other game build?) - no damage option off.\r\n");
            return;
        }
    }
    void *vtable, *present;
    if (!memory_read_pointer(car, &vtable) || vtable != game + GAME_RVA_PLAYER_CAR_VTABLE) return;
    if (!memory_read_pointer((unsigned char *)car + NO_DAMAGE_PRESENT_OFFSET, &present) ||
        !memory_read_pointer(present, &vtable))
        return; /* not fully built (or being taken down): the reset would reach a missing object */
    if (!own && !due(car)) return;
    if (!reset(car)) {
        InterlockedExchange(&broken, 1);
        log_append("DAMAGE: the damage reset faulted - no damage option off until the next start.\r\n");
    }
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_no_damage.py: one car update with a mode; returns 1 when the game build matched */
__declspec(dllexport) int Fh2TestNoDamage(unsigned char *game_image, int value, void *car, int own, int driving) {
    game = game_image;
    mode = value;
    no_damage_after_update(car, own, driving);
    return !broken;
}
#endif
