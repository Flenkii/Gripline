#ifndef FH2_TRAFFIC_LIVE_H
#define FH2_TRAFFIC_LIVE_H

/* V7.3 live traffic: the amount from the menu is written into the density tables the game holds in memory
   (see traffic_live.c). */
#define TRAFFIC_LIVE_PARSE_RVA 0x18d3f90u /* Densities*.xml parser (table, xml reader, road tags) */

/* the parser's address when its code is the known one, else NULL (logged) */
void *traffic_live_target(unsigned char *game);

/* the detour to give the platform */
void *traffic_live_parse_detour(void);

/* after the hook is in: original = the parser to call */
void traffic_live_start(void *original);

#endif
