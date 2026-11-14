/* FH2 ANNA's interface for other platform mods: ANNA's menu.
 *
 *   const FH2AnnaApi* anna = (const FH2AnnaApi*)api->get_interface(FH2ANNA_INTERFACE, 4);
 *
 * Look it up after wait_for_mods (mods start in name order); NULL without FH2 ANNA (or an older
 * one, for version 2). Ask for version 5 for hold_input, falling back to 4 without it; 6 for
 * cinematic_button.
 *
 * Adding to the menu (version 2):
 *
 *   static void honk(void* user, const char* key) { ... }
 *   FH2AnnaEntry e = {sizeof e};
 *   e.key       = "honkhonk.horn";     // unique: start it with your mod's name
 *   e.parent    = "car";               // or NULL for the top list, or a list of yours
 *   e.label     = "Honk";
 *   e.on_choose = honk;
 *   anna->add_entry(&e);
 *
 * ANNA's own lists: "car" (Car), "car.lights" (Car > Lights), "car.doors" (Car > Doors),
 * "route" (Set Route To Nearest). An entry without on_choose is a
 * list: its entries are those whose parent is its key, and it only shows when some of them do.
 * on_choose runs on the game's thread (the one that updates the HUD and the cars) after ANNA has
 * shown the choice and closed (or at once, for stays_open). Keep it short.
 */
#ifndef FH2ANNA_H
#define FH2ANNA_H

#include <stdint.h>

#define FH2ANNA_INTERFACE "FH2ANNA"

typedef void (*FH2AnnaChooseFn)(void* user, const char* key);
typedef int (*FH2AnnaShownFn)(void* user, const char* key);
/* The label now ("Open Roof" or "Close Roof"); the text is copied straight away. */
typedef const char* (*FH2AnnaLabelFn)(void* user, const char* key);

typedef struct FH2AnnaEntry {
    uint32_t size;               /* sizeof(FH2AnnaEntry) */
    const char* key;             /* unique, letters, digits, '.', '_' */
    const char* parent;          /* NULL: the top list; else the key of a list */
    const char* label;           /* what it says, UTF-8 (ANNA shows ASCII best) */
    FH2AnnaChooseFn on_choose;   /* NULL: it's a list */
    FH2AnnaShownFn shown;        /* NULL: always; else asked each time its list opens */
    FH2AnnaLabelFn label_now;    /* NULL: label; else asked each time its list opens */
    void* user;                  /* passed back */
    /* ---- version 3 ---- */
    int stays_open;              /* 1: on_choose runs at once and ANNA stays open, the list shown again
                                    (its labels asked again): for things to flip back and forth */
    /* ---- version 4 ---- */
    FH2AnnaShownFn enabled;      /* NULL: always; else asked each time its list opens: 0 shows it
                                    greyed out: up and down pass over it, and choosing it does nothing
                                    (a list whose entries are all greyed out is too). A greyed-out entry shows
                                    `label` (not label_now), and only if it was added before the HUD
                                    loaded (add it from fh2mod_init); otherwise it shows as usual. */
} FH2AnnaEntry;

typedef struct FH2AnnaApi {
    uint32_t size;               /* sizeof(FH2AnnaApi) */
    /* ---- version 1 ---- */
    /* 1 while ANNA's menu is open on the HUD. */
    int (*is_open)(void);
    /* Always 0: ANNA takes no keys, only the pad (D-pad down opens it; the d-pad moves round it).
       A keyboard mod gives it keys by pressing those buttons (FH2Mouse does, while is_open says
       so for all but opening), and can show its keys in ANNA's two button pictures: the control's
       resources "FH2ANNA_Icon_Open" (D-pad down, under the minimap) and "FH2ANNA_Icon_Choose"
       (D-pad right, beside the highlighted entry), BitmapImages found from the HUDAnnaControl. */
    int (*uses_key)(uint32_t vk);
    /* ---- version 2 ---- */
    /* Adds an entry (copied). Returns 1, or 0 if the key is taken or the entry is incomplete. Add
       entries any time; they show from the next time their list opens. */
    int (*add_entry)(const FH2AnnaEntry* entry);
    /* ---- version 5 ---- */
    /* A mod has taken the player's driving input for itself on purpose (a camera of its own that the
       sticks fly, say, with the game's driving-input permission cleared so the car doesn't steer):
       1 while it has, 0 when it's given back. Meanwhile ANNA's autodrive doesn't take that for a
       menu or the pause and keeps driving (in events too). ANNA itself stays closed. Any thread. */
    void (*hold_input)(int held);
    /* ---- version 6 ---- */
    /* While ANNA's autodrive is on: the controller button that turns its cinematic camera on and off
       (FH2ANNA.ini's [Autodrive] Cinematic, LB unless it says another), as XInput's bit
       (XINPUT_GAMEPAD_LEFT_SHOULDER...); 0 while it isn't driving, or with no cinematic camera. ANNA
       reads it from the pad (the input's XInput buttons as read), so a keyboard mod gives it a key by
       holding that bit there, and can show its key in the prompt's picture of it: the control's
       resource "FH2ANNA_Icon_Cinematic". Any thread. */
    uint16_t (*cinematic_button)(void);
} FH2AnnaApi;

#endif
