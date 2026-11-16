#ifndef FH2_DASHCAM_CARS_H
#define FH2_DASHCAM_CARS_H
#include <wchar.h>

/* V7.2 Dashboard Cam per car (Marvin 14.11.2026): forward / height of the Dashboard Cam are kept for
   every car on its own, in Gripline.ini [DashCamCars] <car file stem> = <forward cm> <height cm>
   (e.g. NIS_GTR_12 = 40 -10). A car with no line of its own uses [Gripline] ViewForward / ViewUp.
   The car comes from the logbook (logbook_current_stem). */

/* fh2mod_init: the folder with Gripline.ini */
void dashcam_cars_start(const wchar_t *settings_directory);
/* watch thread: when the car changes, its own values (or the general ones) go live */
void dashcam_cars_step(unsigned long long now);
/* a forward / height change from a menu: 1 = kept for the car now (saved in [DashCamCars]),
   0 = no car known (the caller keeps it as the general value) */
int dashcam_cars_take(int forward_cm, int up_cm);
/* 1 while the values on show belong to the car now */
int dashcam_cars_own(void);

#endif
