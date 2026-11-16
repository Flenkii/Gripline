/* The car's own door animation - see car_doors.h. Own code. */
#include "gripline.h"
#include "car_doors.h"
#include "logging.h"
#include "support.h"

#define RVA_CAR_EVENT 0xf5d300u    /* (car, name) */
#define RVA_GRAPH_OF 0x1703930u    /* lea rax, [rcx+0x1c8]; ret */
#define RVA_GRAPH_EVENT 0x16fb540u /* (graph, name): the look-up below, then the event */
#define RVA_EVENT_FIND 0x16fd4d0u  /* (map, &iterator, &name) */
#define RVA_EVENT_SPEED 0xf74e00u  /* (car, speed) */
#define RVA_GRAPH_LOD 0x16fbe90u   /* (graph, level): the graph input "GameInputs:AnimationLOD" */
#define CAR_ANIMATION 0x4d40u
#define CAR_ANIMATION_LOD 0x4d70u /* int: the level the game's per-frame code set last (game+0xf7bd84) */
#define ANIMATION_GRAPH 0x1c8u
#define GRAPH_EVENTS 0x268u

static const unsigned char car_event_code[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x89, 0x40, 0x4d, 0x00, 0x00, 0x48, 0x8b, 0xda,
    0x48, 0x85, 0xc9, 0x74, 0x15, 0xe8, 0x16, 0x66, 0x7a, 0x00, 0x48, 0x8b, 0xd3, 0x48, 0x8b, 0x08,
    0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0x16, 0xe2, 0x79, 0x00};
static const unsigned char graph_of_code[] = {0x48, 0x8d, 0x81, 0xc8, 0x01, 0x00, 0x00, 0xc3};
/* from "mov rbx, [r14+0x268]" to the call of the look-up */
static const unsigned char graph_find_code[] = {0x49, 0x8b, 0x9e, 0x68, 0x02, 0x00, 0x00, 0x4c, 0x8d, 0x44, 0x24, 0x28,
                                                0x48, 0x8d, 0x54, 0x24, 0x70, 0x49, 0x8d, 0x8e, 0x68, 0x02, 0x00, 0x00,
                                                0xe8, 0x1b, 0x1f, 0x00, 0x00, 0x48, 0x39, 0x5c};
static const unsigned char graph_lod_code[] = {0x8b, 0xc2, 0xc5, 0xe8, 0x57, 0xd2, 0x89, 0x51, 0x10, 0x48,
                                               0x8b, 0x15, 0xf8, 0x5c, 0x83, 0x01, 0x45, 0x33, 0xc9, 0xc4,
                                               0xe1, 0xea, 0x2a, 0xd0, 0xe9, 0x93, 0x00, 0x00, 0x00};
static const unsigned char speed_code[] = {0x48, 0x8b, 0x81, 0x40, 0x4d, 0x00, 0x00, 0x48, 0x85, 0xc0,
                                           0x74, 0x05, 0xc5, 0xfa, 0x11, 0x48, 0x20, 0xc3};

/* the game's std::string (MSVC x64): short text inline, its length, its room (15 = inline) */
typedef struct {
    char text[16];
    unsigned long long length, room;
} GameString;
_Static_assert(sizeof(GameString) == 32, "game string");

typedef void (*CarEventFn)(void *car, const char *name);
typedef void (*EventSpeedFn)(void *car, float speed);
typedef void *(*EventFindFn)(void *map, void **iterator, const GameString *name);
typedef void (*GraphLodFn)(void *graph, int level);

static int ready = -1;
static void *full_car; /* the car whose graph Gripline set to full detail (car thread only) */
static int full_logged;

static void log_bytes(const char *what, const unsigned char *at, unsigned size) {
    char line[160];
    unsigned n = 0;
    static const char hex[] = "0123456789abcdef";
    for (const char *t = what; *t && n < 60; ++t) line[n++] = *t;
    for (unsigned i = 0; i < size && i < 24 && n + 4 < sizeof(line); ++i) {
        unsigned char b = 0;
        if (!memory_read(at + i, &b, 1)) break;
        line[n++] = hex[b >> 4], line[n++] = hex[b & 15], line[n++] = ' ';
    }
    line[n++] = '\r', line[n++] = '\n', line[n] = 0;
    log_append(line);
}

/* The game's code, or the same with another mod's hook at the start (FH2MP hooks the car event to
   see door events: a jmp over the first instructions). Then the rest after the jmp is checked;
   calling the function goes through that mod's hook, as the game's own calls do. */
static int code_is(unsigned char *game, unsigned rva, const unsigned char *code, unsigned size, unsigned after_jmp,
                   unsigned after_far_jmp, const char *name) {
    const unsigned char *at = game + rva;
    if (memory_equal(at, code, size)) return 1;
    unsigned char first[2] = {0, 0};
    memory_read(at, first, 2);
    unsigned skip = first[0] == 0xe9 ? after_jmp : first[0] == 0xff && first[1] == 0x25 ? after_far_jmp : 0;
    if (skip && skip < size && memory_equal(at + skip, code + skip, size - skip)) {
        log_append("DOORS: ");
        log_append(name);
        log_append(" is hooked by another mod (FH2MP?) - Gripline calls it through that hook.\r\n");
        return 1;
    }
    log_append("DOORS: ");
    log_append(name);
    log_bytes(" differs: ", at, size);
    return 0;
}

int car_doors_ready(unsigned char *game) {
    if (ready < 0) {
        /* instruction ends after a 5-byte / 14-byte jmp: car event 6 / 16, speed 7 / 14 */
        ready = game && code_is(game, RVA_CAR_EVENT, car_event_code, sizeof(car_event_code), 6, 16, "car event") &&
                code_is(game, RVA_GRAPH_OF, graph_of_code, sizeof(graph_of_code), 0, 0, "graph") &&
                code_is(game, RVA_GRAPH_EVENT + 0x58, graph_find_code, sizeof(graph_find_code), 0, 0, "event look-up") &&
                code_is(game, RVA_EVENT_SPEED, speed_code, sizeof(speed_code), 7, 14, "event speed") &&
                code_is(game, RVA_GRAPH_LOD, graph_lod_code, sizeof(graph_lod_code), 0, 0, "animation detail");
        log_append(ready ? "DOORS: the game's door animation is there - getting out opens the door.\r\n"
                         : "DOORS: game code differs - getting out uses the fade.\r\n");
    }
    return ready;
}

static const char *door_name(int side, int open) {
    if (side == CAR_DOOR_RIGHT) return open ? "doorRF:open" : "doorRF:close";
    return open ? "doorLF:open" : "doorLF:close";
}

int car_door_exists(unsigned char *game, void *car, int side) {
    if (!car || !car_doors_ready(game)) return 0;
    void *animation = NULL, *graph = NULL, *end = NULL, *found = NULL;
    if (!memory_read_pointer((unsigned char *)car + CAR_ANIMATION, &animation) || !animation ||
        !memory_read_pointer((unsigned char *)animation + ANIMATION_GRAPH, &graph) || !graph ||
        !memory_read_pointer((unsigned char *)graph + GRAPH_EVENTS, &end))
        return 0;
    GameString name;
    memory_copy(&name, &(GameString){0}, sizeof(name));
    const char *text = door_name(side, 1);
    unsigned n = 0;
    while (text[n] && n < 15) name.text[n] = text[n], ++n;
    name.length = n, name.room = 15;
    __try {
        ((EventFindFn)(game + RVA_EVENT_FIND))((unsigned char *)graph + GRAPH_EVENTS, &found, &name);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        log_append("DOORS: looking up the door failed - fade instead.\r\n");
        return 0;
    }
    return found && found != end;
}

static void *car_graph(void *car) {
    void *animation = NULL, *graph = NULL;
    if (!memory_read_pointer((unsigned char *)car + CAR_ANIMATION, &animation) || !animation ||
        !memory_read_pointer((unsigned char *)animation + ANIMATION_GRAPH, &graph))
        return NULL;
    return graph;
}

int car_door_move(unsigned char *game, void *car, int side, int open) {
    if (!car || !car_doors_ready(game)) return 0;
    void *graph = car_graph(car);
    if (!graph) return 0;
    if (open) {
        /* the player's car normally runs its animation at a lower detail, where the doors stay shut:
           full detail, as the game's own per-frame code sets it for some cars */
        int level = -1;
        memory_read((unsigned char *)car + CAR_ANIMATION_LOD, &level, sizeof(level));
        if (!full_logged) {
            full_logged = 1;
            char line[96] = "DOORS: animation detail of the car was ";
            unsigned n = 0;
            while (line[n]) ++n;
            line[n++] = level >= 0 && level <= 9 ? (char)('0' + level) : '?';
            for (const char *t = " - full detail (0) for the door.\r\n"; *t; ++t) line[n++] = *t;
            line[n] = 0;
            log_append(line);
        }
        ((GraphLodFn)(game + RVA_GRAPH_LOD))(graph, 0);
        full_car = car;
    }
    ((EventSpeedFn)(game + RVA_EVENT_SPEED))(car, 1.0f); /* a "close at once" before must not stay */
    ((CarEventFn)(game + RVA_CAR_EVENT))(car, door_name(side, open));
    return 1;
}

void car_doors_settle(unsigned char *game, void *car) {
    if (!full_car) return;
    if (full_car == car && car_doors_ready(game)) {
        void *graph = car_graph(car);
        int level = 0;
        if (graph && memory_read((unsigned char *)car + CAR_ANIMATION_LOD, &level, sizeof(level)) && level >= 0 && level <= 3)
            ((GraphLodFn)(game + RVA_GRAPH_LOD))(graph, level); /* the game's own level again */
    }
    full_car = NULL; /* (another car: the game sets its level itself) */
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_doors.py: what 0 ready (checks the code again), 1 exists, 2 open, 3 close, 4 settle */
__declspec(dllexport) int Fh2TestDoors(unsigned char *game, void *car, int side, int what) {
    if (what == 0) ready = -1, full_car = NULL;
    if (what == 4) return car_doors_settle(game, car), 1;
    return what == 0 ? car_doors_ready(game) : what == 1 ? car_door_exists(game, car, side) : car_door_move(game, car, side, what == 2);
}
#endif
