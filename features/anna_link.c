/* V7.2: Gripline in FH2 ANNA's in-car menu - see anna_link.h. Own code; fh2anna.h is FH2 ANNA's MIT header. */
#include "gripline.h"
#include <stddef.h>
#include "anna_link.h"
#include "../third_party/fh2anna/fh2anna.h"
#include "logging.h"
#include "support.h"
#include "game_language.h"
#include "cockpit_view.h"
#include "cruise.h"
#include "telemetry.h"
#include "noclip.h"
#include "walking.h"
#include "nitro_link.h"
#include "key_menu.h"
#include "rig_settings.h"

static unsigned char *game;
static int linked;
static unsigned added_count;
static int anna_state; /* 0 not installed, 1 list added, 2 list refused */

/* Gripline.log starts after fh2mod_init (log_reset at game start): the result is written then */
void anna_link_set_game(unsigned char *game_image) {
    game = game_image;
    if (anna_state == 1)
        log_append_value("ANNA: Gripline list in FH2 ANNA's menu (D-pad down > Gripline), entries ", (int)added_count);
    else if (anna_state == 2)
        log_append("ANNA: FH2 ANNA did not take the Gripline list.\r\n");
    else
        log_append("ANNA: FH2 ANNA not installed (or older than its interface 4) - Gripline stays in F8 and the Mods page.\r\n");
}

static int german(void) {
    return game && game_language_is_german(game);
}

/* label_now: the text is copied by ANNA straight away, one buffer is enough */
static const char *pick(const char *de, const char *en) {
    return german() ? de : en;
}

/* ---------------------------------------------------------------- entries */

static const char *dashcam_label(void *user, const char *key) {
    (void)user, (void)key;
    if (cockpit_view_second()) return pick("Dashboard Cam ausschalten", "Disable Dashboard Cam");
    return pick("Dashboard Cam einschalten", "Enable Dashboard Cam");
}

/* the dashboard cam lives on the driver camera: from another camera the camera button does the rest */
static int dashcam_enabled(void *user, const char *key) {
    (void)user, (void)key;
    return cockpit_view_driver_active();
}

static void dashcam_choose(void *user, const char *key) {
    (void)user, (void)key;
    cockpit_view_input(COCKPIT_VIEW_ANNA, 1);
    cockpit_view_input(COCKPIT_VIEW_ANNA, 0);
    log_append("ANNA: dashboard cam switched.\r\n");
}

static const char *walk_label(void *user, const char *key) {
    (void)user, (void)key;
    return pick("Aussteigen und laufen", "Get Out And Walk");
}

static int offline_ready(void *user, const char *key) {
    (void)user, (void)key;
    return !game_session_online();
}

static void walk_choose(void *user, const char *key) {
    (void)user, (void)key;
    noclip_request(MODE_WALK); /* the same checks and fade as the walking key */
    log_append("ANNA: get out and walk.\r\n");
}

static const char *cruise_label(void *user, const char *key) {
    (void)user, (void)key;
    if (cruise_active()) return pick("Tempomat ausschalten", "Disable Cruise Control");
    return pick("Tempomat einschalten", "Enable Cruise Control");
}

static void cruise_choose(void *user, const char *key) {
    (void)user, (void)key;
    cruise_request_toggle(); /* too slow: cruise.c shows its card */
    log_append("ANNA: cruise control switched.\r\n");
}

static const char *telemetry_label(void *user, const char *key) {
    (void)user, (void)key;
    if (telemetry_enabled()) return pick("Telemetrie ausblenden", "Hide Telemetry");
    return pick("Telemetrie einblenden", "Show Telemetry");
}

static void telemetry_choose(void *user, const char *key) {
    (void)user, (void)key;
    int on = !telemetry_enabled();
    if (!rig_setting_set(RIG_GRIP_TELEMETRY, on)) telemetry_set_enabled(on); /* rig_settings saves it, as F8 does */
    log_append(on ? "ANNA: telemetry on.\r\n" : "ANNA: telemetry off.\r\n");
}

static const char *freecam_label(void *user, const char *key) {
    (void)user, (void)key;
    return pick("Freie Kamera", "Free Camera");
}

static void freecam_choose(void *user, const char *key) {
    (void)user, (void)key;
    noclip_request(MODE_FREECAM);
    log_append("ANNA: freecam.\r\n");
}

static const char *menu_label(void *user, const char *key) {
    (void)user, (void)key;
    return pick("Gripline-Menü", "Gripline Menu");
}

static void menu_choose(void *user, const char *key) {
    (void)user, (void)key;
    key_menu_request_toggle(); /* D-pad and A / B work it, as with LS + D-pad right */
    log_append("ANNA: Gripline menu.\r\n");
}

typedef struct {
    const char *key, *label;
    FH2AnnaChooseFn choose;
    FH2AnnaLabelFn label_now;
    FH2AnnaShownFn enabled;
    int stays_open;
} Entry;

/* `label` = what a greyed-out entry shows (ANNA shows label, not label_now, then) */
static const Entry entries[] = {
    {"gripline.dashcam", "Dashboard Cam (cockpit camera)", dashcam_choose, dashcam_label, dashcam_enabled, 1},
    {"gripline.walk", "Get Out And Walk (offline)", walk_choose, walk_label, offline_ready, 0},
    {"gripline.cruise", "Cruise Control", cruise_choose, cruise_label, NULL, 0},
    {"gripline.telemetry", "Telemetry", telemetry_choose, telemetry_label, NULL, 1},
    {"gripline.freecam", "Free Camera (offline)", freecam_choose, freecam_label, offline_ready, 0},
    {"gripline.menu", "Gripline Menu", menu_choose, menu_label, NULL, 0},
};

int anna_link_add(const void *interface) {
    const FH2AnnaApi *anna = (const FH2AnnaApi *)interface;
    if (!anna || anna->size < offsetof(FH2AnnaApi, add_entry) + sizeof(anna->add_entry) || !anna->add_entry) {
        anna_state = 0;
        return 0;
    }
    FH2AnnaEntry list;
    memory_copy(&list, &(FH2AnnaEntry){0}, sizeof(list));
    list.size = sizeof(list);
    list.key = "gripline";
    list.label = "Gripline";
    if (!anna->add_entry(&list)) {
        anna_state = 2;
        return 0;
    }
    unsigned added = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(entries); ++i) {
        FH2AnnaEntry e;
        memory_copy(&e, &(FH2AnnaEntry){0}, sizeof(e));
        e.size = sizeof(e);
        e.key = entries[i].key;
        e.parent = "gripline";
        e.label = entries[i].label;
        e.on_choose = entries[i].choose;
        e.label_now = entries[i].label_now;
        e.stays_open = entries[i].stays_open;
        e.enabled = entries[i].enabled;
        added += anna->add_entry(&e) ? 1u : 0u;
    }
    linked = added > 0;
    added_count = added;
    anna_state = linked ? 1 : 2;
    return linked;
}

int anna_link_active(void) {
    return linked;
}

#ifdef FH2_TWEAKS_TEST
/* runs an entry's callbacks: 0 label_now (returns the text), 1 enabled, 2 choose */
__declspec(dllexport) const char *Fh2TestAnnaEntry(unsigned index, int what, int *result) {
    if (index >= ARRAY_COUNT(entries)) return NULL;
    const Entry *e = &entries[index];
    if (what == 0) return e->label_now(NULL, e->key);
    if (what == 1) *result = e->enabled ? e->enabled(NULL, e->key) : 1;
    if (what == 2) e->choose(NULL, e->key);
    return e->key;
}

__declspec(dllexport) void Fh2TestAnnaGame(unsigned char *image) {
    game = image;
}
#endif
