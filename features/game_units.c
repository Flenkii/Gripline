/* The game's unit setting - see game_units.h. Own code.
   The game keeps it in its locale object (pointer at game+0x36629a0): +0x2c = the Units option
   (-1 Default, 0 English, 2 Metric; written by game+0x2ffd00 from "Options/Units"), and for Default the
   unit system of the current language: +0x28 = language index, entry size 0xe0, unit system at +0x98.
   The game's own readers (e.g. game+0x1239674) do the same. */
#include "game_units.h"
#include "support.h"

#define GAME_RVA_LOCALE 0x36629a0u
#define UNITS_METRIC 2

int game_units_raw(unsigned char *game_image, int *option, int *language_units) {
    unsigned char *locale = NULL;
    int index = -1;
    *option = -1, *language_units = -1;
    if (!game_image || !memory_read(game_image + GAME_RVA_LOCALE, &locale, sizeof(locale)) || !locale ||
        !memory_read(locale + 0x2c, option, sizeof(*option)) || !memory_read(locale + 0x28, &index, sizeof(index)))
        return 0;
    if (index >= 0 && index < 64) memory_read(locale + (size_t)index * 0xe0 + 0x98, language_units, sizeof(*language_units));
    return 1;
}

int game_units_imperial(unsigned char *game_image) {
    int option, language;
    if (!game_units_raw(game_image, &option, &language)) return 0;
    int units = option != -1 ? option : language;
    return units >= 0 && units < UNITS_METRIC; /* 0 English, 1 (UK) also uses miles */
}
