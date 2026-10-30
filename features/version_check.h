#ifndef FH2_VERSION_CHECK_H
#define FH2_VERSION_CHECK_H

/* Start check: do Azimuth, Extra Options and FH2MP fit together? A Gripline card a few seconds
   after the start card when something does not:
   - Azimuth is newer than the versions Gripline was tested with,
   - Extra Options refused to start (its log: "REFUSED: unsupported Azimuth build" or another REFUSED),
   - FH2MP did not find its places in this azimuth.exe (platform.log), i.e. it needs an update.
   Only reads azimuth.exe's version resource and the two log files. */

/* game_image for the language; directory = the Azimuth folder (FH2_ExtraOptions.log, fh2-mods\platform.log). */
void version_check_start(unsigned char *game_image, const wchar_t *directory);

#endif
