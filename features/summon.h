#ifndef FH2_SUMMON_H
#define FH2_SUMMON_H

/* V7.1 test: summon the car while walking (Marvin 08.11.2026, "like Tesla's summon").
   Key Summon (FH2_Keyboard.ini, default G; controller X while walking): the game's own AI driver
   drives the parked car along a GPS route to the walker and stops next to him. Again = stop.
   Offline only, only while walking.

   How (own code, found in forza_durango_release_final.exe, 08.11.2026):
   - GPS: the player's SatNavComponent (vtable game+0x2cc3188, the same object Extra Options'
     autodrive uses) has the reflected method "NavigateToPosition" (game+0x6b58b0 (component,
     const std::vector<__m128> *points)): the game plans its own route to the points (each one
     snapped to the road network). "ClearSatNavDestination" (game+0x6b4400 (component)) removes it.
     Gripline calls both on the game's thread, from the component's own update (slot 0x10, chained).
   - Driving: as Extra Options' autodrive (F2): the route tracker (vtable game+0x2cc3278) starts
     following the route (game+0x6bec10), then the car's driver is switched to the AI
     (game+0xf79f10 (car, 1)). At the end both are undone and the rewind history is cleared.
   - The car: noclip.c holds it at its place while walking. For the summon it holds it there 2 s
     (the world around it loads, it may have been under the walker), then lets it go and follows it;
     at the end it brakes it softly and takes the place where it stopped as its new place.
   - The walker stands still while the car is farther than SUMMON_FREEZE m (the game has floor
     collision only around the car), looking around works. */

#define SUMMON_STOP_DISTANCE 9.0f /* metres car -> walker: nearer and standing = the car is here */
#define SUMMON_HANDOVER 30.0f     /* metres car -> walker: the AI hands over, Gripline parks the car beside him */
#define SUMMON_PARK_SIDE 3.2f     /* metres beside the walker the car's centre stops (getting in: 4 m) */
#define SUMMON_FREEZE 50.0f       /* walker stands still while the car is farther */

/* Game start (plugin): checks the game code and chains the GPS hooks. 1 = summon can work. */
int summon_start(unsigned char *game_image);
int summon_ready(void);

/* Key / controller X while walking (watch thread / input thread): start, or stop a running summon. */
void summon_request(void);

/* What noclip.c does with the car while walking (car update thread). */
enum {
    SUMMON_IDLE,  /* normal walking: the car is held at its place */
    SUMMON_HOLD,  /* held at its place, the route is being planned */
    SUMMON_DRIVE, /* the AI drives: the car is let go, its place follows it */
    SUMMON_BRAKE, /* the AI is off: noclip.c brakes the car softly, then summon_braked() */
    SUMMON_CREEP  /* Gripline itself drives the car slowly to a point (summon_creep_target): off the
                     road to the road, and the last metres to the walker (Marvin 08.11.2026) */
};
int summon_phase(void);
/* the summon warning tone (when summoning cannot work), if the sounds are on */
void summon_warning_sound(void);

/* F8 > Einstellungen (V7.1): how the car drives where Gripline drives it, and the autopilot sounds */
enum { SUMMON_STYLE_CALM, SUMMON_STYLE_NORMAL, SUMMON_STYLE_SPORTY, SUMMON_STYLE_COUNT };
void summon_set_style(int style);
int summon_style(void);
void summon_set_sounds(int on);
int summon_sounds(void);
/* noclip.c: top speed (m/s) and speeding up (m/s^2) for the style */
float summon_creep_speed(void);
float summon_creep_accel(void);
/* noclip.c, car update thread, while summoning: where the car is and how fast (m/s) */
void summon_car_seen(const float position[3], float speed);
/* noclip.c, car update thread, while held: 1 once when the car is to be put onto the road first
   (position a little above it, the road's heading towards the walker) */
int summon_take_place(float position[3], float forward[3]);
/* noclip.c, car update thread, SUMMON_CREEP: where to go; to_walker = 1: to the walker (the car parks
   beside him). Returns a number that changes with each new creep (0 = not creeping). */
unsigned summon_creep_target(float out[3], int *to_walker);
/* noclip.c, SUMMON_CREEP: 1 = back up straight (the car got stuck, summon.c drives round next) */
int summon_creep_reverse(void);
/* noclip.c, SUMMON_DRIVE: 1 while the AI has not got the car going yet (the car gets a slow nudge) */
int summon_ai_waiting(void);
/* noclip.c: the car stands (brake phase over) */
void summon_braked(void);
/* walking.c: 1 = the walker stands still (car far away and moving). */
int summon_walker_frozen(void);

#endif
