#ifndef FH2_ANNA_LINK_H
#define FH2_ANNA_LINK_H

/* V7.2: Gripline in FH2 ANNA's in-car menu (qiangqiang101/FH2_ANNA, its interface "FH2ANNA").
   With FH2 ANNA installed, D-pad down > Gripline lists: Dashboard Cam on/off (in the cockpit camera),
   Get Out And Walk, Cruise Control, Telemetry, Free Camera and the Gripline menu. ANNA reads the labels
   as voice commands too ("ANNA, Enable Dashboard Cam"). Each entry does what the Gripline key does,
   through the same request (same checks: walking and freecam stay offline only).
   Without FH2 ANNA nothing changes: F8 and the Mods page as before. */

/* From fh2mod_init (FH2ANNA.dll starts before Gripline.dll): interface = the platform's
   get_interface("FH2ANNA", 4), may be NULL. Returns 1 when the list was added. */
int anna_link_add(const void *interface);

/* Game start: the game image, for the menu language. */
void anna_link_set_game(unsigned char *game_image);

/* 1 when Gripline's list is in ANNA's menu (problem report, start card). */
int anna_link_active(void);

#endif
