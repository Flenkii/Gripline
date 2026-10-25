#include "autodrive_ui.h"
#include "autodrive_game.h"
#include "support.h"

typedef void (*DrawScreenFn)(void *, void *, void *);
typedef void (*PresentFn)(void *, void *);
typedef void (*ReleaseFn)(void *);

static unsigned char *game_image;
static volatile LONG rendering_disabled, creator;

void autodrive_ui_prepare(unsigned char *image) {
    game_image = image;
}

void autodrive_ui_set_hidden(int hidden) {
    InterlockedExchange(&rendering_disabled, hidden != 0);
}

void autodrive_ui_set_creator(int on) {
    InterlockedExchange(&creator, on != 0);
}

/* Creator mode looks at the simulation at most every 50 ms, so the three draw hooks of one
   frame nearly always agree (the same as a change of the Autodrive flag). */
static int ui_hidden(void) {
    static volatile LONG driving;
    static volatile LONG64 checked;
    if (InterlockedCompareExchange(&rendering_disabled, 0, 0)) return 1;
    if (!creator) return 0;
    ULONGLONG now = GetTickCount64();
    if (now - (ULONGLONG)checked >= 50) {
        InterlockedExchange64(&checked, (LONG64)now);
        InterlockedExchange(&driving, autodrive_game_simulation_running());
    }
    return driving != 0;
}

void autodrive_ui_present(void *application, void *transform) {
    if (!ui_hidden()) {
        ((PresentFn)(game_image + 0x15b5110))(application, transform);
    }
}

void autodrive_ui_draw(void *application, void *screen, void *transform) {
    if (!ui_hidden()) {
        ((DrawScreenFn)(game_image + 0x15b51a0))(application, screen, transform);
        return;
    }
    // Application::Render consumes the passed screen even when no draw is
    // submitted.
    unsigned char *root = *(unsigned char **)screen;
    if (root && InterlockedDecrement((volatile LONG *)(root + 0x10)) == 0) {
        ((ReleaseFn)(game_image + 0x257730))(root);
    }
}

void autodrive_ui_wait_render(void *renderer, void *surface, void *parameters) {
    if (!ui_hidden()) {
        ((DrawScreenFn)(game_image + 0x1140ed0))(renderer, surface, parameters);
        return;
    }
    // The graphics wrapper uses Release at +0x18, rather than the COM +0x10 slot.
    void *owned = *(void **)surface;
    if (owned) {
        ((ReleaseFn)(*(void ***)owned)[0x18 / 8])(owned);
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestUiHidden(void) {
    return ui_hidden();
}
#endif
