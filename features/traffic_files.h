#ifndef FH2_TRAFFIC_FILES_H
#define FH2_TRAFFIC_FILES_H

/* More Traffic: raised copies of the game's traffic data (AI/CarLoadingSettings.xml, AI/Densities*.xml
   from media\EntityModel.zip) in Mount\gripline_traffic, mounted over game:\media - see traffic_files.c. */

/* Game start: makes the files for TrafficLevel 1..5 (6 = no traffic, 0 = original, folder not mounted). */
void traffic_files_start(const wchar_t *mount_dir, int traffic_level);


/* Game start, before the traffic starts: 1 = the last start with raised traffic never reached the road
   (load hung) - Gripline.ini is set to Original traffic and the caller must use TrafficLevel 0. */
int traffic_load_guard(const wchar_t *directory, int traffic_level);

/* The own car's update (dt in s): after 5 s of it the start was fine (cheap, call from every update). */
void traffic_load_reached(float dt);

#endif
