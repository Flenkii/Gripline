#ifndef GRIPLINE_LOGGING_H
#define GRIPLINE_LOGGING_H
#include "gripline.h"

void log_reset(void);                                   /* stop logging (no file known) */
void log_set_directory(const wchar_t *directory);       /* where Gripline.log lives */
void log_start(const wchar_t *directory, const ModOptions *options); /* new file: header + settings */
void log_append(const char *message);
void log_append_value(const char *label, int value);    /* "<label><number>" */
void log_append_module(const char *label, const void *address); /* "<label><module>+0x<offset>" */
#endif
