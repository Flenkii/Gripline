#ifndef GRIPLINE_SUPPORT_H
#define GRIPLINE_SUPPORT_H
#include "gripline.h"

/* memory and text helpers (the DLL has no C runtime) */
void memory_copy(void *destination, const void *source, size_t size);
int memory_equal(const void *left, const void *right, size_t size);
size_t text_length(const char *text);
const char *text_find(const char *text, size_t length, const char *needle);
int text_equal_ignore_case(const char *left, const char *right);
int path_join(wchar_t output[PATH_CAPACITY], const wchar_t *directory, const wchar_t *name);
int float_is_finite(float value);

/* reading and probing the game's memory without crashing */
int memory_read(const void *address, void *output, size_t size);
int memory_read_pointer(const void *address, void **value);
int memory_is_writable(const void *address, size_t size);
unsigned char *memory_alloc_near(unsigned char *base, size_t size);

/* One lock for every read-change-write of Gripline.ini and FH2_Keyboard.ini (F8 menu, Mods page,
   what's-new cards, load guard run on different threads). Never nested. */
void settings_file_lock(void);
void settings_file_unlock(void);

/* one line that names the state of the start-up (shown at the top of Gripline.log) */
void status_set(const char *message);
void status_append(const char *message);
const char *status_get(void);
#endif
