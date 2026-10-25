/* Creator mode. Gripline's cards (notify.c) are simply not drawn while it is on; the game's HUD
   is hidden through the game's own HUD switch (see hud_set_hidden). */
#include "creator_mode.h"
#include "notify.h"
#include "logging.h"
#include "support.h"

static unsigned char *image;
static volatile LONG active;
static volatile LONG held_by[2];

int creator_mode_active(void) {
    return active != 0;
}

static void hud_set_hidden(int hidden) {
    (void)hidden;
    (void)image;
}

static void set_active(LONG on) {
    if (InterlockedExchange(&active, on) == on) return;
    notify_set_hidden(on);
    hud_set_hidden(on);
    log_append(on ? "CREATOR MODE: on (HUD and cards hidden)\r\n" : "CREATOR MODE: off\r\n");
}

void creator_mode_input(unsigned source, int held) {
    if (source > 1) return;
    LONG before = InterlockedExchange(&held_by[source], held ? 1 : 0);
    if (held && !before) set_active(!active);
}

void creator_mode_start(unsigned char *game_image) {
    image = game_image;
}
