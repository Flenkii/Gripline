#ifndef FH2_WORLD_FILES_H
#define FH2_WORLD_FILES_H

/* V7: world options as data files over game:\media (like traffic_files.c), next game start:
   speed_cameras = 0 removes speed traps and speed zones, car_labels = 0 hides the names above cars. */
void world_files_start(const wchar_t *mount_dir, int speed_cameras, int car_labels);

#endif
