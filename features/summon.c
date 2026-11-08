/* Summon - see summon.h. Own code; the driving hand-over follows what Extra Options' autodrive
   does (GPL-3.0, RokkuDayo), the GPS destination is new (NavigateToPosition, see summon.h). */
#include <xmmintrin.h>
#include "gripline.h"
#include "game_addresses.h"
#include "logging.h"
#include "support.h"
#include "summon.h"
#include "walking.h"
#include "notify.h"
#include "game_language.h"
#include "game_units.h"
#include "summon_sounds.h"

/* ------------------------------------------------------------------ game addresses */

#define RVA_SATNAV_VTABLE 0x2cc3188u
#define RVA_TRACKER_VTABLE 0x2cc3278u
#define RVA_SATNAV_UPDATE_SLOT 0x2cc3198u     /* vtable + 0x10 */
#define RVA_SATNAV_DEACTIVATE_SLOT 0x2cc31b0u /* vtable + 0x28 */
#define RVA_TRACKER_DESTROY_SLOT 0x2cc3278u   /* vtable + 0x00 */
#define RVA_TRACKER_UPDATE_SLOT 0x2cc3288u    /* vtable + 0x10 */
#define RVA_SATNAV_UPDATE 0x6b4d30u
#define RVA_SATNAV_DEACTIVATE 0x6c0c70u
#define RVA_TRACKER_DESTROY 0x6a26e0u
#define RVA_TRACKER_UPDATE 0x6b4e40u
#define RVA_NAVIGATE_TO_POSITION 0x6b58b0u /* SatNavComponent::NavigateToPosition (reflection name) */
#define RVA_CLEAR_DESTINATION 0x6b4400u    /* SatNavComponent::ClearSatNavDestination */
#define RVA_ROUTE_BEGIN 0x6bec10u          /* tracker: follow the route (autodrive) */
#define RVA_ROUTE_END 0x6a5fa0u
#define RVA_SET_AI_DRIVER 0xf79f10u        /* (car, on) */
#define RVA_REWIND_HISTORY 0x34d5710u
#define RVA_REWIND_RECORDER 0x34d5700u
#define RVA_REWIND_CLEAR 0x99c4c0u
#define RVA_SHARED_LOCK 0x3eb620u          /* weak reference -> shared (object, counter) */
#define CAR_ROAD_MODE 0x4f94u           /* the car's reset mode; 0 while asking for the nearest road */
#define CAR_ROAD_BINDINGS 0x4f70u
#define CAR_DRIVER_HOLDER 0x4d10u
#define CAR_DRIVER_MODE 0x4d30u            /* 1 = the player drives, 0 = the AI */

#define HOLD_MS 2000u          /* the car at its place while the world there loads (it was hidden far away) */
#define HOLD_NEAR_MS 300u      /* the car was in sight: the world around it is there already */
#define HOLD_FAR 40.0f         /* metres car -> walker from which the car may have been hidden */
#define LIVE_MS 250u           /* the live card (distance, speed) while the car comes */
#define LIVE_TAG 0x53a00001u
#define ARRIVED_SPEED 0.4f     /* m/s: the car stands */
#define ROUTE_WAIT_MS 8000u    /* the GPS plans the route */
#define DRIVE_LIMIT_MS 300000u /* at most 5 minutes */
#define STUCK_SPEED 1.0f       /* m/s */
#define STUCK_MS 20000u
#define START_STALL_MS 6000u   /* the AI has not got going at all (car off the road?) */
#define NEAR_END 30.0f         /* the GPS drops its route some 10-15 m before the end (Marvin's log 08.11.):
                                  a route gone this near the walker means the car has come */
#define SHORT_START 30.0f      /* nearer than this the GPS route is too short for the AI to set off */
#define CREEP_RANGE 120.0f     /* the car creeps (Gripline drives it) at most this far off the road */
#define CREEP_ROAD_REACHED 3.0f
#define TURN_AHEAD 14.0f       /* metres along the road towards the walker the car first drives (it turns round) */
#define CREEP_STUCK_SPEED 0.7f
#define CREEP_STUCK_MS 3000u
#define CREEP_TRIES 3          /* stuck while creeping: back up and drive round, at most this often */
#define CREEP_BACK_MS 1600u    /* backing up */
#define CREEP_DETOUR 10.0f     /* metres: the way round, 55 degrees to the side */
#define CREEP_DETOUR_MS 6000u
#define CREEP_LIMIT_MS 90000u
#define BRAKE_LIMIT_MS 5000u

typedef struct {
    void *object, *counter;
} Shared;
typedef void *(*UpdateFn)(void *, float);
typedef void (*ActionFn)(void *);
typedef void *(*DestroyFn)(void *, unsigned);
typedef void (*NavigateFn)(void *, const void *);
typedef void (*SetAiFn)(void *, unsigned char);
typedef void (*RewindClearFn)(void *, unsigned char, unsigned char);
typedef void (*SharedLockFn)(void *, Shared *);
typedef void *(*PlayerGetter)(void *, int);
typedef void *(*CarGetter)(void *, void **);
typedef unsigned char (*NearestRoadFn)(void *car, float *position, float *basis, unsigned char pop_history);

static unsigned char *game;
static int ready;
static void *previous_satnav_update, *previous_satnav_deactivate, *previous_tracker_update, *previous_tracker_destroy;

static volatile LONG request;      /* key: start / stop */
static volatile LONG phase;        /* SUMMON_* */
static volatile LONG frozen;
/* game thread only */
static void *tracker;              /* the route tracker seen last (its update) */
static void *drive_satnav, *drive_tracker, *drive_car;
static int routed, route_begun, driving_ai;
static ULONGLONG hold_until, route_until, drive_from, slow_since, brake_from;
static float target[3];            /* the walker's place the route goes to */
/* car update thread -> game thread */
static SRWLOCK seen_lock = SRWLOCK_INIT;
static float car_seen[3], car_speed;
static int car_seen_ok;
/* game thread -> car update thread: put the held car onto the road (once) */
static SRWLOCK place_lock = SRWLOCK_INIT;
static float place_at[3], place_forward[3];
static volatile LONG place_pending;
static int got_going; /* the car has moved since the AI took it */
static int road_checked;
static int road_ok; /* the nearest-road query is the game's code as found */
/* SUMMON_CREEP: game thread sets, car update thread reads */
static SRWLOCK creep_lock = SRWLOCK_INIT;
static float creep_at[3];
static int creep_to_walker;
static unsigned creep_serial;
static ULONGLONG creep_from, creep_slow_since, live_next;
/* stuck while creeping: back up, then a way round to one side, then on to the goal (game thread) */
static float creep_goal[3];
static int creep_goal_walker, creep_tries, creep_step, creep_side; /* step: 0 to the goal, 1 back, 2 round */
static ULONGLONG creep_step_until;
static volatile LONG creep_reverse;
static float eta_speed; /* smoothed speed for the arrival time on the live card */

static volatile LONG style = SUMMON_STYLE_NORMAL, sounds_on = 1;

void summon_set_style(int value) {
    InterlockedExchange(&style, value >= 0 && value < SUMMON_STYLE_COUNT ? value : SUMMON_STYLE_NORMAL);
}
int summon_style(void) {
    return (int)style;
}
void summon_set_sounds(int on) {
    InterlockedExchange(&sounds_on, on != 0);
}
int summon_sounds(void) {
    return sounds_on != 0;
}
float summon_creep_speed(void) {
    return style == SUMMON_STYLE_CALM ? 4.5f : style == SUMMON_STYLE_SPORTY ? 8.5f : 6.0f;
}
float summon_creep_accel(void) {
    return style == SUMMON_STYLE_CALM ? 1.8f : style == SUMMON_STYLE_SPORTY ? 3.5f : 2.5f;
}

static void sound(const void *wav) {
    if (sounds_on) walking_play_wav(wav);
}

int summon_ready(void) {
    return ready;
}

int summon_phase(void) {
    return (int)phase;
}

int summon_walker_frozen(void) {
    return frozen != 0;
}

void summon_request(void) {
    if (ready) InterlockedExchange(&request, 1);
}

/* ------------------------------------------------------------------ helpers */

static int in_game(const void *p) {
    return (const unsigned char *)p >= game && (const unsigned char *)p < game + GAME_IMAGE_SIZE;
}

static int has_vtable(void *object, unsigned rva) {
    void *vtable;
    return object && memory_read_pointer(object, &vtable) && vtable == game + rva;
}

static void *method(void *object, unsigned offset) {
    void *vtable, *fn;
    if (!object || !memory_read_pointer(object, &vtable) || !memory_read_pointer((unsigned char *)vtable + offset, &fn) ||
        !in_game(fn))
        return NULL;
    return fn;
}

static void card(const char *german, const char *english) {
    notify_show(game_language_is_german(game) ? german : english);
}

static void tinted(const char *german, const char *english, int color) {
    int de = game_language_is_german(game);
    notify_show_tinted(de ? german : english, color, de ? "SUMMON" : "SUMMON", 0);
}

static void release_shared(Shared *value) {
    unsigned char *counter = value->counter;
    if (counter && InterlockedDecrement((volatile LONG *)(counter + 8)) == 0) {
        ((ActionFn)((uintptr_t *)*(void **)counter)[0])(counter);
        if (InterlockedDecrement((volatile LONG *)(counter + 12)) == 0) ((ActionFn)((uintptr_t *)*(void **)counter)[1])(counter);
    }
    value->object = value->counter = NULL;
}

/* the own car, with a reference (release_car when done) */
static void *get_car(void) {
    void *manager, *car = NULL;
    if (!memory_read_pointer(game + GAME_RVA_PLAYER_MANAGER, &manager) || !manager) return NULL;
    void *player = ((PlayerGetter)(game + GAME_RVA_GET_LOCAL_PLAYER))(manager, 0);
    if (!player) return NULL;
    ((CarGetter)(game + GAME_RVA_GET_PLAYER_CAR))(player, &car);
    if (car && !has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE)) {
        ActionFn release = (ActionFn)method(car, 0x18);
        if (release) release(car);
        return NULL;
    }
    return car;
}

static void release_car(void *car) {
    ActionFn release = car ? (ActionFn)method(car, 0x18) : NULL;
    if (release) release(car);
}

/* 1 = the player drives, 0 = the AI, -1 = not known */
static int driver_mode(void *car) {
    void *holder, *driver;
    int mode = -1;
    if (!has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE) || !memory_read_pointer((unsigned char *)car + CAR_DRIVER_HOLDER, &holder) ||
        !holder || !memory_read_pointer(holder, &driver) || !method(driver, 0x48) ||
        !memory_read((unsigned char *)car + CAR_DRIVER_MODE, &mode, 4))
        return -1;
    return mode;
}

static int set_ai(void *car, int on) {
    if (driver_mode(car) != (on ? 1 : 0)) return 0;
    ((SetAiFn)(game + RVA_SET_AI_DRIVER))(car, (unsigned char)on);
    return driver_mode(car) == (on ? 0 : 1);
}

static void clear_rewind(void) {
    void *history, *recorder;
    if (!memory_read_pointer(game + RVA_REWIND_HISTORY, &history) || !memory_is_writable(history, 0x128) ||
        !memory_read_pointer(game + RVA_REWIND_RECORDER, &recorder) || !method(recorder, 0x30) || !method(recorder, 0x68))
        return;
    ((RewindClearFn)(game + RVA_REWIND_CLEAR))(history, 1, 1);
}

/* the planned route (NULL = none yet); arrived = the GPS says the destination is reached */
static void *route_of(void *satnav, int *arrived) {
    void *control, *route_control, *route;
    unsigned char active, reached, valid;
    *arrived = 0;
    if (!has_vtable(satnav, RVA_SATNAV_VTABLE) || !memory_read((unsigned char *)satnav + 0x160, &active, 1) || !active ||
        !memory_read_pointer((unsigned char *)satnav + 0x150, &control) || !control ||
        !memory_read_pointer(control, &route_control) || !route_control ||
        !memory_read((unsigned char *)route_control + 0x8a, &reached, 1) ||
        !memory_read_pointer((unsigned char *)route_control + 0x118, &route) || !route ||
        !memory_read((unsigned char *)route + 8, &valid, 1) || !valid)
        return NULL;
    *arrived = reached != 0;
    return route;
}

static int lock_reference(void *object, unsigned offset, Shared *out) {
    Shared weak;
    LONG owners;
    unsigned char *wrapper = (unsigned char *)object + offset;
    out->object = out->counter = NULL;
    if (!memory_read(wrapper + 0x38, &weak, sizeof(weak)) || !weak.object || !weak.counter ||
        !memory_read((unsigned char *)weak.counter + 8, &owners, sizeof(owners)) || owners <= 0)
        return 0;
    ((SharedLockFn)(game + RVA_SHARED_LOCK))(wrapper, out);
    return out->object != NULL;
}

/* the tracker belongs to this GPS (as autodrive checks it) */
static int tracker_matches(void *t, void *satnav) {
    if (!has_vtable(t, RVA_TRACKER_VTABLE)) return 0;
    Shared component = {0}, owner = {0};
    void *owner_routing, *route_property;
    int ok = lock_reference(t, 0x98, &component) && component.object == satnav &&
             lock_reference(t, 0xe0, &owner) && memory_read_pointer((unsigned char *)owner.object + 0x128, &owner_routing) &&
             memory_read_pointer((unsigned char *)t + 0x130, &route_property);
    release_shared(&owner);
    release_shared(&component);
    return ok;
}

static int begin_route(void *t) {
    void *task = NULL;
    if (!memory_read((unsigned char *)t + 0x128, &task, 8) || task) return 0; /* already followed (autodrive?) */
    ((ActionFn)(game + RVA_ROUTE_BEGIN))(t);
    if (memory_read_pointer((unsigned char *)t + 0x128, &task) && task) return 1;
    ((ActionFn)(game + RVA_ROUTE_END))(t);
    return 0;
}

/* the GPS to the walker's place (the game snaps it to the nearest road) */
static void navigate(void *satnav, const float at[3]) {
    _Alignas(16) __m128 point = _mm_setr_ps(at[0], at[1], at[2], 0.0f);
    struct {
        __m128 *first, *last, *end;
    } points = {&point, &point + 1, &point + 1}; /* std::vector<__m128> */
    ((NavigateFn)(game + RVA_NAVIGATE_TO_POSITION))(satnav, &points);
}

static float distance_xz(const float a[3], const float b[3]) {
    float x = a[0] - b[0], z = a[2] - b[2], d = x * x + z * z;
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(d > 0.0f ? d : 0.0f)));
}

/* The road the car stands on or next to, as the game's own "reset to the road" finds it (game+0xf5db40,
   used by Gripline before V6 for the garage swap): the car's road binding gives the nearest road node,
   which is checked first (the query follows its neighbours); the car's reset mode is borrowed as 0 only
   for this call. out = position, forward = the road's heading. */
static int node_ok(unsigned char *node, float position[3]) {
    void *edges;
    int edge_count;
    if (!memory_read(node + 0x14, position, 12) || !memory_read(node + 0x20, &edge_count, 4) || edge_count < 1 ||
        edge_count > 64 || !memory_read_pointer(node, &edges) || !edges)
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!float_is_finite(position[i])) return 0;
    int usable = 0;
    for (int i = 0; i < edge_count; ++i) {
        unsigned char *entry = (unsigned char *)edges + (size_t)i * 16, *edge;
        void *points;
        int index, count;
        if (!memory_read_pointer(entry, (void **)&edge) || !edge || !memory_read(entry + 8, &index, 4) ||
            !memory_read_pointer(edge, &points) || !points || !memory_read(edge + 0x10, &count, 4) || count < 1 ||
            count > 1000000 || index < 0 || index >= count)
            return 0;
        for (int side = -1; side <= 1; side += 2) {
            int next = index + side;
            unsigned char *neighbour;
            float np[3];
            if (next < 0 || next >= count) continue;
            if (!memory_read((unsigned char *)points + (size_t)next * 8, &neighbour, 8)) return 0;
            if (!neighbour) continue;
            if (!memory_read(neighbour + 0x14, np, 12)) return 0;
            for (unsigned j = 0; j < 3; ++j)
                if (!float_is_finite(np[j])) return 0;
            if (np[0] != position[0] || np[1] != position[1] || np[2] != position[2]) usable = 1;
        }
    }
    return usable;
}

static int nearest_road(unsigned char *car, float out[3], float forward[3]) {
    void *bindings, *outer, *inner;
    unsigned char *node;
    int mode;
    float node_position[3];
    if (!has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE) || !memory_read(car + CAR_ROAD_MODE, &mode, 4) ||
        !memory_is_writable(car + CAR_ROAD_MODE, 4) || !memory_read_pointer(car + CAR_ROAD_BINDINGS, &bindings) ||
        !has_vtable(bindings, GAME_RVA_ROAD_BINDINGS_VTABLE) || !memory_read_pointer((unsigned char *)bindings + 0xa8, &outer) ||
        !outer || !memory_read_pointer(outer, &inner) || !inner || !memory_read_pointer((unsigned char *)inner + 0x10, (void **)&node) ||
        !node || !node_ok(node, node_position))
        return 0;
    _Alignas(16) float position[4] = {0}, basis[12] = {0};
    *(volatile int *)(car + CAR_ROAD_MODE) = 0;
    unsigned char found = ((NearestRoadFn)(game + GAME_RVA_NEAREST_ROAD))(car, position, basis, 0);
    *(volatile int *)(car + CAR_ROAD_MODE) = mode;
    if (!found) return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!float_is_finite(position[i]) || !float_is_finite(basis[8 + i])) return 0;
    float f2 = basis[8] * basis[8] + basis[10] * basis[10];
    if (f2 < 0.25f || f2 > 1.1f) return 0;
    for (unsigned i = 0; i < 3; ++i) out[i] = position[i], forward[i] = basis[8 + i];
    return 1;
}

int summon_take_place(float position[3], float forward[3]) {
    if (!place_pending || !InterlockedExchange(&place_pending, 0)) return 0;
    AcquireSRWLockShared(&place_lock);
    for (unsigned i = 0; i < 3; ++i) position[i] = place_at[i], forward[i] = place_forward[i];
    ReleaseSRWLockShared(&place_lock);
    return 1;
}

unsigned summon_creep_target(float out[3], int *to_walker) {
    if (phase != SUMMON_CREEP) return 0;
    AcquireSRWLockShared(&creep_lock);
    out[0] = creep_at[0], out[1] = creep_at[1], out[2] = creep_at[2], *to_walker = creep_to_walker;
    unsigned serial = creep_serial;
    ReleaseSRWLockShared(&creep_lock);
    return serial;
}

static void creep_set(const float at[3], int to_walker, int fresh) {
    AcquireSRWLockExclusive(&creep_lock);
    creep_at[0] = at[0], creep_at[1] = at[1], creep_at[2] = at[2], creep_to_walker = to_walker;
    if (fresh && !++creep_serial) creep_serial = 1;
    ReleaseSRWLockExclusive(&creep_lock);
}

/* ------------------------------------------------------------------ the summon */

static void reset(void) {
    if (drive_car) release_car(drive_car);
    drive_car = drive_satnav = drive_tracker = NULL;
    routed = route_begun = driving_ai = got_going = road_checked = 0;
    InterlockedExchange(&place_pending, 0);
    InterlockedExchange(&frozen, 0);
    InterlockedExchange(&creep_reverse, 0);
    creep_step = 0;
    AcquireSRWLockExclusive(&seen_lock);
    car_seen_ok = 0;
    ReleaseSRWLockExclusive(&seen_lock);
}

/* AI off, route off, GPS destination off (the car stays where it is) */
static void release_ai(void) {
    if (driving_ai && drive_car) {
        if (!set_ai(drive_car, 0)) log_append("SUMMON: the AI driver could not be switched off again.\r\n");
    }
    if (route_begun && drive_tracker && has_vtable(drive_tracker, RVA_TRACKER_VTABLE)) ((ActionFn)(game + RVA_ROUTE_END))(drive_tracker);
    if (routed && drive_satnav && has_vtable(drive_satnav, RVA_SATNAV_VTABLE))
        ((ActionFn)(game + RVA_CLEAR_DESTINATION))(drive_satnav);
    if (driving_ai) clear_rewind(); /* the AI's drive is not the player's to rewind */
    driving_ai = route_begun = routed = 0;
}

/* Gripline drives the car itself to at (the road, or the walker) */
int summon_creep_reverse(void) {
    return creep_reverse != 0;
}

static void creep_begin(const float at[3], int to_walker, ULONGLONG now) {
    creep_set(at, to_walker, 1);
    creep_goal[0] = at[0], creep_goal[1] = at[1], creep_goal[2] = at[2], creep_goal_walker = to_walker;
    creep_tries = creep_step = 0;
    InterlockedExchange(&creep_reverse, 0);
    creep_from = creep_slow_since = now;
    InterlockedExchange(&phase, SUMMON_CREEP);
}

/* AI off, route off, GPS destination off; then the car brakes (still walking) or nothing */
static void stop_with(int brake, const void *wav) {
    sound(wav);
    release_ai();
    reset();
    if (brake && walking_active()) {
        brake_from = GetTickCount64();
        InterlockedExchange(&phase, SUMMON_BRAKE);
    } else {
        InterlockedExchange(&phase, SUMMON_IDLE);
    }
}

/* ends a summon that went well or was cancelled: "autopilot off" */
static void stop(int brake) {
    stop_with(brake, summon_end_wav);
}

/* ends a summon that went wrong (no route, stuck, ...): the warning tone (Marvin 08.11.2026) */
static void fail(int brake) {
    stop_with(brake, summon_warning_wav);
}

void summon_warning_sound(void) {
    sound(summon_warning_wav);
}

static void begin(void *satnav, ULONGLONG now) {
    if (!walking_active()) return;
    int arrived;
    unsigned char active = 0;
    if (!memory_read((unsigned char *)satnav + 0x160, &active, 1) || !active) {
        log_append("SUMMON: the GPS is not active.\r\n");
        summon_warning_sound();
        card("Auto rufen geht gerade nicht\nGPS nicht aktiv", "Can't summon the car now\nGPS not active");
        return;
    }
    if (!tracker_matches(tracker, satnav)) {
        log_append("SUMMON: no route tracker for this GPS.\r\n");
        summon_warning_sound();
        card("Auto rufen geht gerade nicht\nKein Routen-Tracker (Gripline.log)", "Can't summon the car now\nNo route tracker (Gripline.log)");
        return;
    }
    void *car = get_car();
    if (!car || driver_mode(car) != 1) {
        log_append_value("SUMMON: the car is not driven by the player, driver mode: ", car ? driver_mode(car) : -2);
        release_car(car);
        summon_warning_sound();
        card("Auto rufen geht gerade nicht\nAuto nicht bereit (Gripline.log)", "Can't summon the car now\nCar not ready (Gripline.log)");
        return;
    }
    if (!walking_walker_position(target)) {
        release_car(car);
        return;
    }
    (void)arrived;
    drive_car = car, drive_satnav = satnav, drive_tracker = tracker;
    float away = walking_car_distance();
    hold_until = now + (away < 0.0f || away > HOLD_FAR ? HOLD_MS : HOLD_NEAR_MS);
    live_next = now + 1500u; /* the start card first */
    InterlockedExchange(&phase, SUMMON_HOLD);
    sound(summon_start_wav); /* Marvin 08.11.2026: "autopilot on" when the summon starts */
    log_append_value("SUMMON: started - the car waits at its place a moment, metres to the walker: ", (int)(away + 0.5f));
    card("Auto wird gerufen\nGPS plant die Route zu dir... (G/X = abbrechen)",
         "Summoning the car\nThe GPS plans the route to you... (G/X = cancel)");
}

static void put_number(char *out, unsigned *n, int value) {
    char digits[12];
    unsigned d = 0, v = value < 0 ? 0u : (unsigned)value;
    do digits[d++] = (char)('0' + v % 10); while ((v /= 10) && d < sizeof(digits));
    while (d && *n < 120) out[(*n)++] = digits[--d];
    out[*n] = 0;
}

static void put_words(char *out, unsigned *n, const char *text) {
    while (*text && *n < 120) out[(*n)++] = *text++;
    out[*n] = 0;
}

/* while the car comes: how far it is and how fast, a live card updated in place */
static void live_card(float gap, float speed, int parking, ULONGLONG now) {
    if (gap < 0.0f || now < live_next || !notify_kind_on(NOTIFY_KIND_LIVE)) return;
    live_next = now + LIVE_MS;
    eta_speed += (speed - eta_speed) * 0.15f; /* about 2 s of smoothing at 4 updates a second */
    int de = game_language_is_german(game), imperial = game_units_imperial(game);
    char text[128];
    unsigned n = 0;
    text[0] = 0;
    put_words(text, &n, parking ? (de ? "Dein Auto parkt neben dir - " : "Your car is parking beside you - ")
                                : (de ? "Dein Auto kommt - " : "Your car is coming - "));
    put_number(text, &n, (int)(imperial ? gap * 3.28084f + 0.5f : gap + 0.5f));
    put_words(text, &n, imperial ? " ft\n" : " m\n");
    put_number(text, &n, (int)(speed * (imperial ? 2.23694f : 3.6f) + 0.5f));
    put_words(text, &n, imperial ? " mph" : " km/h");
    if (!parking && eta_speed > 2.0f && gap > 40.0f) { /* about when (the road is longer than the line) */
        int seconds = (int)(gap * 1.3f / eta_speed + 0.5f);
        put_words(text, &n, de ? " - ca. " : " - about ");
        if (seconds >= 60) {
            put_number(text, &n, seconds / 60);
            put_words(text, &n, ":");
            if (seconds % 60 < 10) put_words(text, &n, "0");
            put_number(text, &n, seconds % 60);
            put_words(text, &n, " min");
        } else {
            put_number(text, &n, seconds);
            put_words(text, &n, " s");
        }
    }
    put_words(text, &n, de ? "   G/X = abbrechen" : "   G/X = cancel");
    notify_show_live(text, LIVE_TAG, 1200);
}

/* The AI only drives along the route: off the road the car does not get going (Marvin 08.11.2026, car
   parked a few metres next to the road). The AI always tries first (the nearest-road point is a road
   node, often several metres away also for a car on the road - Marvin's log 08.11.: 8-10 m on the road);
   only when it does not get going Gripline drives the car to the road itself (not put there: "appearing
   out of nothing on the road is not nice"), then the AI tries again. Once per summon. */
static int creep_to_road(const float seen[3], int seen_ok, const float walker[3], int walker_ok, ULONGLONG now) {
    if (road_checked || !seen_ok || !walker_ok) return 0;
    road_checked = 1;
    /* Also for a car on the road facing away from the walker (Marvin 08.11.2026: the AI cannot reverse
       and turn, the car did not get away): Gripline drives it onto the road a little way in the walker's
       direction, so it ends there with its nose the right way; then the AI tries again. */
    float road[3], heading[3], target[3];
    float wx = walker[0] - seen[0], wz = walker[2] - seen[2], wd = distance_xz(seen, walker);
    if (!(wd > 1.0f)) return 0;
    wx /= wd, wz /= wd;
    if (road_ok && nearest_road(drive_car, road, heading) && distance_xz(seen, road) <= CREEP_RANGE) {
        float hl = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(heading[0] * heading[0] + heading[2] * heading[2])));
        float hx = heading[0] / hl, hz = heading[2] / hl;
        if (hx * (walker[0] - road[0]) + hz * (walker[2] - road[2]) < 0.0f) hx = -hx, hz = -hz; /* towards him */
        target[0] = road[0] + hx * TURN_AHEAD, target[1] = road[1], target[2] = road[2] + hz * TURN_AHEAD;
        log_append_value("SUMMON: the AI does not get going - the car drives onto the road towards the walker first, metres off: ",
                         (int)(distance_xz(seen, road) + 0.5f));
    } else {
        target[0] = seen[0] + wx * TURN_AHEAD, target[1] = seen[1], target[2] = seen[2] + wz * TURN_AHEAD;
        log_append("SUMMON: the AI does not get going - the car turns towards the walker first (no road found).\r\n");
    }
    release_ai();
    creep_begin(target, 0, now);
    tinted("Dein Auto wendet\nDann f\xc3\xa4hrt es zu dir (G/X = abbrechen)", "Your car is turning round\nThen it drives to you (G/X = cancel)",
           NOTIFY_COLOR_GREEN);
    return 1;
}

/* the GPS / the AI cannot bring the car: when it is near enough, Gripline drives it to the walker
   itself, straight across (Marvin 08.11.2026: "make it really good") */
static int creep_instead(float gap, int walker_ok, const float walker[3], ULONGLONG now, const char *why) {
    if (!(gap >= 0.0f && gap < CREEP_RANGE) || !walker_ok) return 0;
    log_append(why);
    log_append_value("SUMMON: Gripline drives the car to the walker itself, metres: ", (int)gap);
    release_ai();
    creep_begin(walker, 1, now);
    tinted("Dein Auto kommt\nEs f\xc3\xa4hrt querfeldein zu dir (G/X = abbrechen)",
           "Your car is on its way\nIt drives cross-country to you (G/X = cancel)", NOTIFY_COLOR_GREEN);
    return 1;
}

static void tick(void *satnav) {
    ULONGLONG now = GetTickCount64();
    int asked = request && InterlockedExchange(&request, 0);
    LONG p = phase;
    if (p == SUMMON_IDLE) {
        if (asked && walking_active()) begin(satnav, now);
        return;
    }
    if (p == SUMMON_BRAKE) {
        if (!walking_active() || now - brake_from > BRAKE_LIMIT_MS) InterlockedExchange(&phase, SUMMON_IDLE);
        return;
    }
    if (satnav != drive_satnav) return; /* another GPS component (there is one per player) */
    if (!walking_active()) {
        log_append("SUMMON: walking ended - summon stopped.\r\n");
        stop(0);
        return;
    }
    if (asked) {
        log_append("SUMMON: cancelled (key).\r\n");
        stop(1);
        card("Auto rufen abgebrochen", "Summon cancelled");
        return;
    }
    if (!drive_tracker) {
        log_append("SUMMON: the route tracker went away - summon stopped.\r\n");
        fail(1);
        card("Auto rufen abgebrochen\nRoute weg", "Summon stopped\nRoute gone");
        return;
    }
    float seen[3], speed = 0.0f;
    int seen_ok;
    AcquireSRWLockShared(&seen_lock);
    seen[0] = car_seen[0], seen[1] = car_seen[1], seen[2] = car_seen[2], speed = car_speed, seen_ok = car_seen_ok;
    ReleaseSRWLockShared(&seen_lock);
    float walker[3];
    int walker_ok = walking_walker_position(walker);
    float gap = seen_ok && walker_ok ? distance_xz(seen, walker) : -1.0f;
    InterlockedExchange(&frozen, gap > SUMMON_FREEZE);
    if (p == SUMMON_HOLD) {
        if (gap >= 0.0f && gap < SUMMON_STOP_DISTANCE) {
            stop(0);
            card("Dein Auto ist schon da", "Your car is already here");
            return;
        }
        if (now < hold_until) return;
        if (gap >= 0.0f && gap < SHORT_START && walker_ok) { /* too near for a GPS route: Gripline drives it */
            log_append_value("SUMMON: near - the car creeps to the walker itself, metres: ", (int)gap);
            creep_begin(walker, 1, now);
            tinted("Dein Auto kommt\nEs rangiert zu dir (G/X = abbrechen)", "Your car is on its way\nIt is manoeuvring to you (G/X = cancel)",
                   NOTIFY_COLOR_GREEN);
            return;
        }
        if (!routed) {
            routed = 1;
            route_until = now + ROUTE_WAIT_MS;
            navigate(satnav, target);
            log_append_value("SUMMON: GPS destination set to the walker, metres from the car: ", (int)gap);
            return;
        }
        int arrived = 0;
        void *route = route_of(satnav, &arrived);
        if (!route || arrived) {
            if (now >= route_until) {
                const char *why = route ? "SUMMON: the GPS says the walker is already reached.\r\n"
                                        : "SUMMON: the GPS found no route to the walker.\r\n";
                if (creep_instead(gap, walker_ok, walker, now, why)) return;
                log_append(why);
                fail(0);
                card("Auto rufen geht hier nicht\nKeine Route zu dir gefunden (Gripline.log)",
                     "Can't summon the car here\nNo route to you found (Gripline.log)");
            }
            return;
        }
        if (!begin_route(drive_tracker)) {
            if (creep_instead(gap, walker_ok, walker, now, "SUMMON: the route tracker could not start (autodrive running?).\r\n"))
                return;
            log_append("SUMMON: the route tracker could not start (autodrive running?).\r\n");
            fail(0);
            card("Auto rufen geht gerade nicht\nRoute nicht startbar (Gripline.log)", "Can't summon the car now\nRoute could not start (Gripline.log)");
            return;
        }
        route_begun = 1;
        if (!set_ai(drive_car, 1)) {
            if (creep_instead(gap, walker_ok, walker, now, "SUMMON: the AI driver could not take the car.\r\n")) return;
            log_append("SUMMON: the AI driver could not take the car.\r\n");
            fail(0);
            card("Auto rufen geht gerade nicht\nKI-Fahrer nicht verf\xc3\xbc""gbar (Gripline.log)",
                 "Can't summon the car now\nAI driver not available (Gripline.log)");
            return;
        }
        driving_ai = 1;
        got_going = 0;
        drive_from = slow_since = now;
        InterlockedExchange(&phase, SUMMON_DRIVE);
        log_append("SUMMON: the AI drives the car to the walker.\r\n");
        tinted("Dein Auto kommt\nBleib stehen, bis es nah ist (G/X = abbrechen)", "Your car is on its way\nStay put until it's close (G/X = cancel)",
               NOTIFY_COLOR_GREEN);
        return;
    }
    if (p == SUMMON_CREEP) {
        live_card(gap, speed, creep_to_walker && gap < SUMMON_HANDOVER + 10.0f, now);
        if (creep_step != 0) {
            /* backing up / driving round: no arrival checks */
        } else if (creep_to_walker) {
            if (walker_ok && creep_step == 0) creep_set(walker, 1, 0); /* follows the walker */
            /* here = it stands beside the walker (noclip.c stops it there), or it is very near anyway */
            if (gap >= 0.0f && ((gap < SUMMON_STOP_DISTANCE && speed < ARRIVED_SPEED && now - creep_from > 1000u) ||
                                gap < SUMMON_PARK_SIDE - 0.7f)) {
                log_append_value("SUMMON: the car is here (parked beside the walker), metres: ", (int)(gap + 0.5f));
                stop(1);
                tinted("Dein Auto ist da\nF/Y = einsteigen", "Your car is here\nF/Y = get in", NOTIFY_COLOR_GREEN);
                return;
            }
        } else {
            float at[3];
            int slow;
            summon_creep_target(at, &slow);
            float off = seen_ok ? distance_xz(seen, at) : 1e9f;
            if (off < 1.0f || (off < CREEP_ROAD_REACHED && speed < 1.5f)) { /* on the road and slow: no jolt */
                log_append("SUMMON: the car is on the road - the GPS and the AI take over.\r\n");
                InterlockedExchange(&phase, SUMMON_HOLD);
                hold_until = now; /* road_checked stays: straight to the route */
                return;
            }
        }
        if (creep_step == 1 && now >= creep_step_until) { /* backed up: now a way round, to one side */
            InterlockedExchange(&creep_reverse, 0);
            float dx = creep_goal[0] - seen[0], dz = creep_goal[2] - seen[2], d = dx * dx + dz * dz;
            if (seen_ok && d > 1.0f) {
                d = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(d)));
                dx /= d, dz /= d;
                float c = 0.574f, sn = 0.819f * (float)creep_side; /* 55 degrees */
                float round[3] = {seen[0] + (dx * c - dz * sn) * CREEP_DETOUR, seen[1], seen[2] + (dz * c + dx * sn) * CREEP_DETOUR};
                creep_set(round, 0, 0);
                creep_step = 2, creep_step_until = now + CREEP_DETOUR_MS;
            } else {
                creep_step = 0;
            }
            creep_slow_since = now;
        } else if (creep_step == 2) {
            float at[3];
            int w;
            summon_creep_target(at, &w);
            if (now >= creep_step_until || (seen_ok && distance_xz(seen, at) < 2.5f)) { /* round: on to the goal */
                if (creep_goal_walker && walker_ok) creep_goal[0] = walker[0], creep_goal[1] = walker[1], creep_goal[2] = walker[2];
                creep_set(creep_goal, creep_goal_walker, 0);
                creep_step = 0;
                creep_slow_since = now;
            }
        }
        if (speed >= CREEP_STUCK_SPEED || creep_step == 1) creep_slow_since = now;
        if (now - creep_from > 2000u && now - creep_slow_since > CREEP_STUCK_MS && creep_tries < CREEP_TRIES &&
            now - creep_from <= CREEP_LIMIT_MS) {
            /* something in the way: back up a little and drive round it, every other time the other side */
            ++creep_tries;
            creep_side = creep_tries & 1 ? 1 : -1;
            creep_step = 1, creep_step_until = now + CREEP_BACK_MS;
            InterlockedExchange(&creep_reverse, 1);
            log_append_value("SUMMON: the car is stuck - it backs up and drives round, try: ", creep_tries);
            if (creep_tries == 1) {
                summon_warning_sound();
                card("Dein Auto steckt kurz fest\nEs setzt zur\xc3\xbc""ck und f\xc3\xa4hrt drum herum", "Your car got stuck\nIt backs up and drives round");
            }
            return;
        }
        if ((now - creep_from > 2000u && now - creep_slow_since > CREEP_STUCK_MS) || now - creep_from > CREEP_LIMIT_MS) {
            log_append_value("SUMMON: the car does not get on while creeping (something in the way?), metres to the walker: ", (int)gap);
            fail(1);
            card("Auto rufen abgebrochen\nDas Auto kommt nicht weiter (Hindernis?)", "Summon stopped\nThe car can't get through (something in the way?)");
        }
        return;
    }
    /* SUMMON_DRIVE */
    if (driver_mode(drive_car) != 0) {
        log_append("SUMMON: the game took the car back from the AI - summon stopped.\r\n");
        driving_ai = 0;
        fail(1);
        return;
    }
    int arrived = 0;
    void *route = route_of(satnav, &arrived);
    live_card(gap, speed, 0, now);
    /* the hand-over comes earlier the faster the AI drives: Gripline brakes with at most 6 m/s^2 and
       must come to a stand beside the walker, not past him (25 m/s -> about 75 m) */
    float handover = 12.0f + speed * speed / (2.0f * 5.0f);
    if (handover < SUMMON_HANDOVER) handover = SUMMON_HANDOVER;
    if (gap >= 0.0f && gap < handover && walker_ok) {
        /* near: the AI would stop hard 9 m away; Gripline slows the car down softly and parks it
           beside the walker, near enough to get in (Marvin 08.11.2026: "make it cooler") */
        log_append_value("SUMMON: near the walker - the AI hands over, the car parks beside him, metres: ", (int)gap);
        log_append_value("SUMMON: speed at the hand-over, km/h: ", (int)(speed * 3.6f + 0.5f));
        release_ai();
        creep_begin(walker, 1, now);
        return;
    }
    if ((arrived || !route) && gap >= 0.0f && gap < CREEP_RANGE && walker_ok) {
        /* the GPS ends its route 10-15 m before the walker or at the road nearest to him: the last
           metres Gripline drives the car itself */
        log_append_value("SUMMON: end of the GPS route - the car creeps the last metres, metres: ", (int)gap);
        release_ai();
        creep_begin(walker, 1, now);
        return;
    }
    if (arrived || !route) {
        log_append_value(arrived ? "SUMMON: end of the route (nearest road), metres to the walker: "
                                 : "SUMMON: the route is gone, metres to the walker: ",
                         (int)gap);
        if (arrived) stop(1);
        else fail(1);
        if (arrived) card("Dein Auto ist da\nAn der n\xc3\xa4""chsten Stra\xc3\x9f""e - geh hin, F/Y = einsteigen",
                          "Your car is here\nAt the nearest road - walk over, F/Y = get in");
        else card("Auto rufen abgebrochen\nRoute weg (Gripline.log)", "Summon stopped\nRoute gone (Gripline.log)");
        return;
    }
    if (speed >= STUCK_SPEED) slow_since = now, got_going = 1;
    if (!got_going && now - drive_from > START_STALL_MS) {
        if (creep_instead(gap, walker_ok, walker, now, "SUMMON: the AI did not get the car going.\r\n")) return;
        if (creep_to_road(seen, seen_ok, walker, walker_ok, now)) return;
        log_append_value("SUMMON: the AI did not get the car going, metres to the walker: ", (int)gap);
        fail(1);
        card("Auto rufen abgebrochen\nDas Auto kommt nicht los - park es n\xc3\xa4her an einer Stra\xc3\x9f""e",
             "Summon stopped\nThe car can't get going - park it closer to a road");
        return;
    }
    if (now - slow_since > STUCK_MS && creep_instead(gap, walker_ok, walker, now, "SUMMON: the AI is stuck.\r\n")) return;
    if (now - slow_since > STUCK_MS || now - drive_from > DRIVE_LIMIT_MS) {
        log_append_value(now - drive_from > DRIVE_LIMIT_MS ? "SUMMON: took too long, metres to the walker: "
                                                           : "SUMMON: the car is stuck, metres to the walker: ",
                         (int)gap);
        fail(1);
        card("Auto rufen abgebrochen\nDas Auto steckt fest", "Summon stopped\nThe car is stuck");
    }
}

void summon_car_seen(const float position[3], float speed) {
    AcquireSRWLockExclusive(&seen_lock);
    car_seen[0] = position[0], car_seen[1] = position[1], car_seen[2] = position[2], car_speed = speed, car_seen_ok = 1;
    ReleaseSRWLockExclusive(&seen_lock);
}

void summon_braked(void) {
    if (phase == SUMMON_BRAKE) InterlockedExchange(&phase, SUMMON_IDLE);
}

/* ------------------------------------------------------------------ hooks (game thread) */

static void *satnav_update(void *self, float dt) {
    void *result = ((UpdateFn)previous_satnav_update)(self, dt);
    if (has_vtable(self, RVA_SATNAV_VTABLE)) tick(self);
    return result;
}

static void satnav_deactivate(void *self) {
    if (self == drive_satnav && (phase == SUMMON_HOLD || phase == SUMMON_DRIVE || phase == SUMMON_CREEP)) {
        log_append("SUMMON: the GPS was switched off - summon stopped.\r\n");
        fail(1);
    }
    ((ActionFn)previous_satnav_deactivate)(self);
}

static void *tracker_update(void *self, float dt) {
    tracker = self;
    return ((UpdateFn)previous_tracker_update)(self, dt);
}

static void *tracker_destroy(void *self, unsigned flags) {
    if (self == drive_tracker) {
        route_begun = 0;
        drive_tracker = NULL;
    }
    if (self == tracker) tracker = NULL;
    return ((DestroyFn)previous_tracker_destroy)(self, flags);
}

static int chain(unsigned slot_rva, unsigned expected, void *hook, void **previous) {
    void **slot = (void **)(game + slot_rva);
    void *current = NULL;
    if (!memory_read_pointer(slot, &current) || !current) return 0;
    if (current == hook) return 1;
    if (current != game + expected) log_append_module("SUMMON: a GPS slot was already hooked by ", current);
    DWORD old, ignored;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    *previous = current;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void *), old, &ignored);
    return 1;
}

int summon_start(unsigned char *game_image) {
    if (ready || !game_image) return ready;
    game = game_image;
    /* the game code summon relies on, as it was when it was found */
    static const unsigned char navigate_code[] = {0x80, 0xb9, 0x60, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0c, 0x48, 0x8b,
                                                  0x89, 0x50, 0x01, 0x00, 0x00, 0xe9, 0xcb, 0xce, 0x39, 0x00};
    static const unsigned char clear_code[] = {0x80, 0xb9, 0x60, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0c, 0x48, 0x8b,
                                               0x89, 0x50, 0x01, 0x00, 0x00, 0xe9, 0xab, 0x99, 0x39, 0x00};
    static const unsigned char set_ai_code[] = {0x48, 0x8b, 0xc1, 0x48, 0x8b, 0x89, 0x10, 0x4d, 0x00, 0x00, 0x48, 0x85, 0xc9};
    static const unsigned char begin_code[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xc9};
    static const unsigned char end_code[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x89, 0x28, 0x01};
    static const unsigned char rewind_code[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x56, 0x48, 0x83, 0xec, 0x20};
    if (!memory_equal(game + RVA_NAVIGATE_TO_POSITION, navigate_code, sizeof(navigate_code)) ||
        !memory_equal(game + RVA_CLEAR_DESTINATION, clear_code, sizeof(clear_code)) ||
        !memory_equal(game + RVA_SET_AI_DRIVER, set_ai_code, sizeof(set_ai_code)) ||
        !memory_equal(game + RVA_ROUTE_BEGIN, begin_code, sizeof(begin_code)) ||
        !memory_equal(game + RVA_ROUTE_END, end_code, sizeof(end_code)) ||
        !memory_equal(game + RVA_REWIND_CLEAR, rewind_code, sizeof(rewind_code))) {
        log_append("SUMMON: game code differs - summon off.\r\n");
        return 0;
    }
    static const unsigned char road_code[] = {0x48, 0x8b, 0xc4, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec};
    road_ok = memory_equal(game + GAME_RVA_NEAREST_ROAD, road_code, sizeof(road_code));
    if (!road_ok) log_append("SUMMON: nearest-road code differs - a car off the road is not put onto it.\r\n");
    if (!chain(RVA_TRACKER_UPDATE_SLOT, RVA_TRACKER_UPDATE, (void *)(uintptr_t)tracker_update, &previous_tracker_update) ||
        !chain(RVA_TRACKER_DESTROY_SLOT, RVA_TRACKER_DESTROY, (void *)(uintptr_t)tracker_destroy, &previous_tracker_destroy) ||
        !chain(RVA_SATNAV_DEACTIVATE_SLOT, RVA_SATNAV_DEACTIVATE, (void *)(uintptr_t)satnav_deactivate,
               &previous_satnav_deactivate) ||
        !chain(RVA_SATNAV_UPDATE_SLOT, RVA_SATNAV_UPDATE, (void *)(uintptr_t)satnav_update, &previous_satnav_update)) {
        log_append("SUMMON: the GPS hooks could not be set - summon off.\r\n");
        return 0;
    }
    ready = 1;
    log_append("SUMMON: ready (walking: key Summon / controller X).\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_summon.py */
__declspec(dllexport) int Fh2TestSummonStart(unsigned char *game_image) {
    return summon_start(game_image);
}
__declspec(dllexport) void Fh2TestSummonRequest(void) {
    summon_request();
}
__declspec(dllexport) void Fh2TestSummonCreepSet(float x, float y, float z, int to_walker) {
    float at[3] = {x, y, z};
    creep_set(at, to_walker, 1);
    InterlockedExchange(&phase, to_walker >= 0 ? SUMMON_CREEP : SUMMON_IDLE);
}
__declspec(dllexport) void Fh2TestSummonReverse(int on) {
    InterlockedExchange(&creep_reverse, on != 0);
}
__declspec(dllexport) void Fh2TestSummonStyle(int value) {
    summon_set_style(value);
}
__declspec(dllexport) int Fh2TestSummonPhase(void) {
    return (int)phase;
}
/* one GPS update as the game calls it */
__declspec(dllexport) void Fh2TestSummonTick(void *satnav) {
    tick(satnav);
}
__declspec(dllexport) void Fh2TestSummonTracker(void *t) {
    tracker = t;
}
#endif
