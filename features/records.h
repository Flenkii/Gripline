#ifndef FH2_RECORDS_H
#define FH2_RECORDS_H

/* Records for ever, own sprints and telemetry as CSV (V7, community ideas).
   - Records: the best 0-100 (0-60 mph), 0-200 (0-100 mph), quarter mile, braking distance, top speed and
     own sprint time stay in Gripline_Records.txt next to Gripline.ini; a new one brings a gold card.
   - Own sprint: Gripline menu (F8) > Eigene Sprints: start and finish where the car stands; driving out
     of the start circle starts the clock, the finish circle (15 m) stops it.
   - CSV: Gripline menu > Live telemetry > "Record as CSV": 10 lines a second into
     Gripline_Telemetry\telemetry_<date>_<time>.csv (until switched off or the game ends). */

enum {
    RECORD_SPRINT,    /* 0-100 km/h, s */
    RECORD_SPRINT200, /* 0-200 km/h, s */
    RECORD_QUARTER,   /* s */
    RECORD_BRAKE,     /* 100-0 km/h, m */
    RECORD_TOP,       /* m/s */
    RECORD_OWN,       /* own sprint, s */
    RECORD_SPRINT_MPH, RECORD_SPRINT200_MPH, RECORD_BRAKE_MPH, /* 0-60, 0-100 mph, 60-0 */
    RECORD_COUNT
};

void records_start(unsigned char *game_image, const wchar_t *settings_directory);
/* a finished run (any thread); value in the unit above */
void records_offer(unsigned kind, float value);
/* all-time best, 0 = none */
float records_best(unsigned kind);
void records_clear(void);

/* game thread: own car position (world, m) and speed */
void records_sample(float dt, const float position[3], float speed);
/* watch thread */
void records_step(unsigned long long now);

/* own sprint, from the menu: 1 = done */
int records_set_start(void);
int records_set_finish(void);
void records_clear_sprint(void);
/* 0 none, 1 start only, 2 start and finish; metres between them */
int records_sprint_state(float *metres);

/* CSV */
void records_csv_set(int on);
int records_csv_on(void);
/* from the telemetry step: one line when it is time (10 a second) */
void records_csv_line(unsigned long long now, float speed, float long_g, float side_g, float slip, float yaw, float slope);

#endif
