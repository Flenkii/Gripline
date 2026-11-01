#ifndef FH2_LOGBOOK_H
#define FH2_LOGBOOK_H
#include <stddef.h>

/* Logbook (V7, Marvin): kilometres, driving time and top speed of every car, kept for ever in
   Gripline_Logbook.txt next to Gripline.ini. Shown in the Gripline menu (F8) > Logbook.
   A milestone card comes at 100, 500, 1000, 2500, 5000 and 10000 km (mi) with one car.
   Which car is driven: the game keeps no name Gripline knows, so the own car object is searched
   once for the name of one of the car files in media\cars (e.g. NIS_GTR_12); found, the place is
   kept for the next cars (log "LOGBOOK:"). Noclip and freecam do not count.

   "What's new" card (V7): once after an update, a few cards name the main new things; Gripline.ini
   [Gripline] NewsSeen keeps the version that was shown (70 = V7.0). */

void logbook_start(unsigned char *game_image, const wchar_t *settings_directory, const wchar_t *mount_dir,
                   int news_seen);
/* game thread, own car after its update: dt s, velocity m/s */
void logbook_sample(void *car, float dt, const float velocity[3]);
/* watch thread, a few times a second */
void logbook_step(unsigned long long now);

/* for the menu: totals; metres, seconds, top speed m/s */
void logbook_total(double *metres, double *seconds, float *top);
/* the car now (name empty when not known) */
void logbook_current(wchar_t *name, size_t capacity, double *metres);
/* the cars with the most kilometres: rank 0.. ; returns 0 past the end */
int logbook_top(unsigned rank, wchar_t *name, size_t capacity, double *metres);

#endif
