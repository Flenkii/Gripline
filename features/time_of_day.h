#ifndef FH2_TIME_OF_DAY_H
#define FH2_TIME_OF_DAY_H

/* Fixed time of day chosen in the Gripline menu ([Traffic] TimeOfDay in
   FH2_ExtraOptions.ini). Uses the game's own time-of-day clock (the one the race scripts set
   with SetTimeOfDay / StopTimeOfDay); no game files are touched. Free roam offline only. */
enum {
    TIME_OF_DAY_ORIGINAL, /* the game's normal day and night */
    TIME_OF_DAY_MORNING,
    TIME_OF_DAY_MIDDAY,
    TIME_OF_DAY_SUNSET,
    TIME_OF_DAY_NIGHT,
    TIME_OF_DAY_SLOW, /* the clock keeps running, four times slower */
    TIME_OF_DAY_COUNT
};

int time_of_day_setting(void);
void time_of_day_set_setting(int value);
/* Called once per frame on the game thread (from the local car's update). */
void time_of_day_tick(void);
#endif
