/* Creator mode. Gripline's cards (notify.c) are simply not drawn while it is on; the game's HUD
   is hidden in the UI draw hooks (see hud_set_hidden). */
#include "creator_mode.h"
#include "notify.h"
#include "autodrive_ui.h"
#include "logging.h"
#include "support.h"

static volatile LONG active;
static volatile LONG held_by[2];

int creator_mode_active(void) {
    return active != 0;
}

/* The game's whole UI (HUD, minimap, prompts) is not drawn while driving, through the same
   draw hooks as the Autodrive cinematic camera; menus and loading screens stay. */
static void hud_set_hidden(int hidden) {
    autodrive_ui_set_creator(hidden);
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

#ifdef FH2_TWEAKS_TEST
#include "autodrive_game.h"
/* Binds a fake game image (player manager -> simulation -> paused) and gives one input. */
__declspec(dllexport) int Fh2TestCreatorMode(unsigned char *game_image, unsigned source, int held) {
    autodrive_game_bind(game_image);
    creator_mode_input(source, held);
    return creator_mode_active();
}
#endif
