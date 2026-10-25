#ifndef FH2_TRAFFIC_FILES_H
#define FH2_TRAFFIC_FILES_H

/* More Traffic: raised copies of the game's traffic data (AI/CarLoadingSettings.xml, AI/Densities*.xml
   from media\EntityModel.zip) in Mount\gripline_traffic, mounted over game:\media - see traffic_files.c. */

/* Game start: makes the files for TrafficLevel 1..5 (0 = original, folder not mounted). */
void traffic_files_start(const wchar_t *mount_dir, int traffic_level);

/* For the mount: 1 = mount the folder, 0 = not. */
int traffic_files_wait_ready(void);

#endif
