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
#define CAR_DRIVER_HOLDER 0x4d10u
#define CAR_DRIVER_MODE 0x4d30u            /* 1 = the player drives, 0 = the AI */

#define HOLD_MS 2000u          /* the car at its place while the world there loads */
#define ROUTE_WAIT_MS 8000u    /* the GPS plans the route */
#define DRIVE_LIMIT_MS 300000u /* at most 5 minutes */
#define STUCK_SPEED 1.0f       /* m/s */
#define STUCK_MS 20000u
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

/* ------------------------------------------------------------------ the summon */

static void reset(void) {
    if (drive_car) release_car(drive_car);
    drive_car = drive_satnav = drive_tracker = NULL;
    routed = route_begun = driving_ai = 0;
    InterlockedExchange(&frozen, 0);
    AcquireSRWLockExclusive(&seen_lock);
    car_seen_ok = 0;
    ReleaseSRWLockExclusive(&seen_lock);
}

/* AI off, route off, GPS destination off; then the car brakes (still walking) or nothing */
static void stop(int brake) {
    if (driving_ai && drive_car) {
        if (!set_ai(drive_car, 0)) log_append("SUMMON: the AI driver could not be switched off again.\r\n");
    }
    if (route_begun && drive_tracker && has_vtable(drive_tracker, RVA_TRACKER_VTABLE)) ((ActionFn)(game + RVA_ROUTE_END))(drive_tracker);
    if (routed && drive_satnav && has_vtable(drive_satnav, RVA_SATNAV_VTABLE))
        ((ActionFn)(game + RVA_CLEAR_DESTINATION))(drive_satnav);
    if (driving_ai) clear_rewind(); /* the AI's drive is not the player's to rewind */
    reset();
    if (brake && walking_active()) {
        brake_from = GetTickCount64();
        InterlockedExchange(&phase, SUMMON_BRAKE);
    } else {
        InterlockedExchange(&phase, SUMMON_IDLE);
    }
}

static void begin(void *satnav, ULONGLONG now) {
    if (!walking_active()) return;
    int arrived;
    unsigned char active = 0;
    if (!memory_read((unsigned char *)satnav + 0x160, &active, 1) || !active) {
        log_append("SUMMON: the GPS is not active.\r\n");
        card("Auto rufen geht gerade nicht\nGPS nicht aktiv", "Can't summon the car now\nGPS not active");
        return;
    }
    if (!tracker_matches(tracker, satnav)) {
        log_append("SUMMON: no route tracker for this GPS.\r\n");
        card("Auto rufen geht gerade nicht\nKein Routen-Tracker (Gripline.log)", "Can't summon the car now\nNo route tracker (Gripline.log)");
        return;
    }
    void *car = get_car();
    if (!car || driver_mode(car) != 1) {
        log_append_value("SUMMON: the car is not driven by the player, driver mode: ", car ? driver_mode(car) : -2);
        release_car(car);
        card("Auto rufen geht gerade nicht\nAuto nicht bereit (Gripline.log)", "Can't summon the car now\nCar not ready (Gripline.log)");
        return;
    }
    if (!walking_walker_position(target)) {
        release_car(car);
        return;
    }
    (void)arrived;
    drive_car = car, drive_satnav = satnav, drive_tracker = tracker;
    hold_until = now + HOLD_MS;
    InterlockedExchange(&phase, SUMMON_HOLD);
    log_append("SUMMON: started - the car is held at its place while the world there loads.\r\n");
    card("Auto wird gerufen\nGPS plant die Route zu dir... (G/X = abbrechen)",
         "Summoning the car\nThe GPS plans the route to you... (G/X = cancel)");
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
        stop(1);
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
        if (gap >= 0.0f && gap < SUMMON_STOP_DISTANCE) { /* already here */
            stop(0);
            card("Auto ist schon da", "The car is already here");
            return;
        }
        if (now < hold_until) return;
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
                log_append(route ? "SUMMON: the GPS says the walker is already reached.\r\n"
                                 : "SUMMON: the GPS found no route to the walker.\r\n");
                stop(0);
                card("Auto rufen geht hier nicht\nKeine Route zu dir gefunden (Gripline.log)",
                     "Can't summon the car here\nNo route to you found (Gripline.log)");
            }
            return;
        }
        if (!begin_route(drive_tracker)) {
            log_append("SUMMON: the route tracker could not start (autodrive running?).\r\n");
            stop(0);
            card("Auto rufen geht gerade nicht\nRoute nicht startbar (Gripline.log)", "Can't summon the car now\nRoute could not start (Gripline.log)");
            return;
        }
        route_begun = 1;
        if (!set_ai(drive_car, 1)) {
            log_append("SUMMON: the AI driver could not take the car.\r\n");
            stop(0);
            card("Auto rufen geht gerade nicht\nKI-Fahrer nicht verf\xc3\xbc""gbar (Gripline.log)",
                 "Can't summon the car now\nAI driver not available (Gripline.log)");
            return;
        }
        driving_ai = 1;
        drive_from = slow_since = now;
        InterlockedExchange(&phase, SUMMON_DRIVE);
        log_append("SUMMON: the AI drives the car to the walker.\r\n");
        tinted("Dein Auto kommt\nBleib stehen, bis es nah ist (G/X = abbrechen)", "Your car is on its way\nStay put until it's close (G/X = cancel)",
               NOTIFY_COLOR_GREEN);
        return;
    }
    /* SUMMON_DRIVE */
    if (driver_mode(drive_car) != 0) {
        log_append("SUMMON: the game took the car back from the AI - summon stopped.\r\n");
        driving_ai = 0;
        stop(1);
        return;
    }
    int arrived = 0;
    void *route = route_of(satnav, &arrived);
    if (gap >= 0.0f && gap < SUMMON_STOP_DISTANCE) {
        log_append("SUMMON: the car is here.\r\n");
        stop(1);
        tinted("Dein Auto ist da\nF/Y = einsteigen", "Your car is here\nF/Y = get in", NOTIFY_COLOR_GREEN);
        return;
    }
    if (arrived || !route) {
        log_append_value(arrived ? "SUMMON: end of the route (nearest road), metres to the walker: "
                                 : "SUMMON: the route is gone, metres to the walker: ",
                         (int)gap);
        stop(1);
        if (arrived) card("Dein Auto ist da\nAn der n\xc3\xa4""chsten Stra\xc3\x9f""e - geh hin, F/Y = einsteigen",
                          "Your car is here\nAt the nearest road - walk over, F/Y = get in");
        else card("Auto rufen abgebrochen\nRoute weg (Gripline.log)", "Summon stopped\nRoute gone (Gripline.log)");
        return;
    }
    if (speed >= STUCK_SPEED) slow_since = now;
    if (now - slow_since > STUCK_MS || now - drive_from > DRIVE_LIMIT_MS) {
        log_append_value(now - drive_from > DRIVE_LIMIT_MS ? "SUMMON: took too long, metres to the walker: "
                                                           : "SUMMON: the car is stuck, metres to the walker: ",
                         (int)gap);
        stop(1);
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
    if (self == drive_satnav && (phase == SUMMON_HOLD || phase == SUMMON_DRIVE)) {
        log_append("SUMMON: the GPS was switched off - summon stopped.\r\n");
        stop(1);
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
