#ifndef FH2_WALK_GAUGE_H
#define FH2_WALK_GAUGE_H

/* V7.1 walking gauge (Marvin 08.11.2026): the game's own speedometer goes away when getting out of the
   car, so Gripline draws its own small gauge at the bottom right while walking. Three looks to choose
   from in the F8 menu (Gripline > Laufen: Tacho), saved as Gripline.ini [Gripline] WalkGauge:
   0 off, 1 Ring (round dial), 2 Racing bar (slanted banner with LED bar), 3 Step pulse (text only,
   a line that ticks at every footfall; the default).
   Each look shows the walking speed (in the unit of the game's speedometer), walking / running /
   crouching and how far the car is with an arrow pointing to it; near the car "Get in: F / Y".
   Drawn by notify.c's card window (CPU pixels + GDI text, no new imports), in the card colour. */

enum { WALK_GAUGE_OFF, WALK_GAUGE_RING, WALK_GAUGE_BAR, WALK_GAUGE_PULSE, WALK_GAUGE_COUNT };
#define WALK_GAUGE_DEFAULT WALK_GAUGE_PULSE

enum { WALK_MODE_STAND, WALK_MODE_WALK, WALK_MODE_RUN, WALK_MODE_CROUCH };

typedef struct {
    float speed;                /* m/s, smoothed */
    int mode;                   /* WALK_MODE_* */
    float distance;             /* metres to the parked car (horizontal), < 0 = not known */
    float car_ahead, car_right; /* direction to the car seen from the walker's heading, length 1 */
    float step;                 /* 0..1: how strong a footfall is right now (the pulse line) */
    int german, imperial;
} WalkGaugeData;

void walk_gauge_set_style(int style);
int walk_gauge_style(void);

/* Size in pixels of a look's picture for a screen this high. */
void walk_gauge_size(int style, int screen_h, int *w, int *h);
/* Space between the picture and the right / bottom edge of the screen. */
int walk_gauge_margin(int screen_h);
/* Draws one picture (premultiplied BGRA, w x h from walk_gauge_size, top-down) for the time now (ms).
   light / accent / second: the card colours (R, G, B). 1 = drawn. Only from one thread. */
int walk_gauge_draw(int style, const WalkGaugeData *data, unsigned long long now, int screen_h,
                    const unsigned char light[3], const unsigned char accent[3], const unsigned char second[3],
                    unsigned char *pixels);

#endif
