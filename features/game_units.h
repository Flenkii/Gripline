#ifndef FH2_GAME_UNITS_H
#define FH2_GAME_UNITS_H

/* The speed unit the game shows (Options > HUD > Units: Default / English / Metric).
   1 = miles (mph, mi, ft), 0 = metric (km/h, km, m). Metric when it cannot be read. */
int game_units_imperial(unsigned char *game_image);

/* the raw values for the log: the option (-1 = Default) and the unit system of the language */
int game_units_raw(unsigned char *game_image, int *option, int *language_units);
#endif
