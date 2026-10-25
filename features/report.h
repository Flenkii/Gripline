#ifndef FH2_REPORT_H
#define FH2_REPORT_H

/* Problem report: one click in the menu packs Gripline's logs and settings into
   Gripline_Report.zip next to the INIs (to attach to a GitHub issue). */

/* Game start: the folder with the INIs and logs (next to azimuth.exe). */
void report_init(const wchar_t *settings_directory);

/* Writes Gripline_Report.zip. Returns the number of files packed (0 = failed). Shows a card. */
int report_create(void);

#endif
