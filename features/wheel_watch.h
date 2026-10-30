#ifndef FH2_WHEEL_WATCH_H
#define FH2_WHEEL_WATCH_H

/* Wheel plugged in / unplugged: a Gripline card names the wheel ("Lenkrad erkannt / Logitech G920").
   Looks at the raw input device list every two seconds (vendor and product id of known wheels);
   what is there at the game start gives no card. Reads nothing else and opens no device. */

/* From the watch thread, a few times a second. */
void wheel_watch_step(unsigned char *game_image, unsigned long long now);

#endif
