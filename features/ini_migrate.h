#ifndef FH2_INI_MIGRATE_H
#define FH2_INI_MIGRATE_H
#include <stddef.h>
#include <wchar.h>

/* English settings files (fork4): the German files of fork1-fork3 are taken over once.

     FH2_Tastatur.ini [Tastatur]  ->  FH2_Keyboard.ini  [Keyboard]

   The new file is the mod's English default file with the user's values filled in (key names
   translated, German key names in keyboard bindings too). The old file is moved into the folder
   FH2_Settings_Backup next to the INIs. Nothing happens when a new file already exists.
   Safe to call more than once (only the first call per process does the work). */
void ini_migrate(const wchar_t *settings_directory);
/* Writes what ini_migrate did into Gripline.log (after the log was started). */
void ini_migrate_log(void);

/* default file texts of the modules */
size_t keyboard_default_ini(char *out, size_t capacity);
#endif
