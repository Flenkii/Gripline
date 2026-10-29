#ifndef FH2_COCKPIT_H
#define FH2_COCKPIT_H

/* Full steering animation in the cockpit ([Gripline] SteeringAnimation=1, default off).

   Same result as the V1 script tools/fh2_lenkrad.py, now built into Gripline:
   - the driver models (media\drivers\{default,defaultRace,suited,unsuited,stig}.zip) get the
     hands hidden (their triangles collapsed),
   - every car's driver steering animation (scene\Drivers\...\leftRightTurn.gr2) gets a new
     steering-wheel curve that turns as far as the real wheel (Azimuth launcher rotation / 2 per
     side; FH2 itself turns it 90 degrees).
   The files are made once (again only when the rotation changes) into Mount\gripline_cockpit,
   which Gripline mounts over game:\media while the option is on. The game files are never
   changed; switching the option off simply leaves the folder unmounted. */

/* Game start: makes the files if needed (own thread). mount_dir = the game's Mount folder.
   The files are made in every case: they also hold the part of the animation without the wheel for the
   second cockpit view (cockpit_view.h). full = 0 (no full steering animation): the wheel turns as in
   the original and the hands stay. */
void cockpit_start(unsigned char *game_image, const wchar_t *mount_dir, int full, int rotation_degrees,
                   int curve_x10);

/* For the mount: 1 = mount the folder, 0 = not. Waits (up to 60 s) until the files are made. */
int cockpit_wait_ready(void);

/* After the folder is mounted: the game's cockpit steering angle goes up to rotation / 2 instead of
   90 degrees, so the animation turns 1:1 with the real wheel. 1 = patched. */
int cockpit_patch_game(void);

/* Shared with traffic_files.c: one file out of a game zip (NULL if missing). */
unsigned char *gripline_zip_extract(const wchar_t *zip_path, const char *entry, uint32_t *size);
void gripline_zip_free(void *data);

/* Plugin build: the files go to this folder (Extra Options mounts it over game:\media) instead
   of Mount\gripline_cockpit / Mount\gripline_traffic. NULL = not set. */
void gripline_set_output_root(const wchar_t *root);
const wchar_t *gripline_output_root(void);

#endif
