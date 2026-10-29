#ifndef FH2_WEATHER_H
#define FH2_WEATHER_H

/* Fixed weather chosen in the "Lenkrad & Verkehr" menu ([Traffic] Wetter in
   FH2_ExtraOptions.ini). Uses the game's own SetWeatherControl call (the one its car meets and
   island scripts use); the weather files are not touched. */
enum {
    WEATHER_ORIGINAL, /* the game's normal weather changes */
    WEATHER_SUNNY,
    WEATHER_CLOUDY,
    WEATHER_THREATENING,
    WEATHER_RAIN,
    WEATHER_FOG, /* Storm Island only; elsewhere the same as cloudy */
    WEATHER_COUNT
};

int weather_setting(void);
void weather_set_setting(int value);
/* 1 while another mod (Extra Options' Weather Control) fixes the weather: Gripline hands it back. */
void weather_set_yield(int yield);
/* Called once per frame on the game thread (from the local car's update). */
void weather_tick(void);
#endif
