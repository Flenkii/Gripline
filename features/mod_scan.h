#ifndef FH2_MOD_SCAN_H
#define FH2_MOD_SCAN_H

/* Which other tools run next to Gripline (conflict check, see mod_scan.c). */

/* 1 when the Azimuth Mapper (virtual Xbox controller built from keys, by Sam) is running. */
int mod_scan_mapper_running(void);

/* Tells the player once (log + card after the game's first picture) that the Mapper is
   running and Gripline's own keyboard stays off. */
void mod_scan_announce_mapper(void);

/* Game start: remembers where fh2-mods\ is (the platform's mod list, for the problem report). */
void mod_scan_start(const wchar_t *settings_directory);

/* The mods list text ("Gripline V6 - mods found\n<names>"); 0 = none found. */
int mod_scan_mods_list(char *out, unsigned size);

#endif
