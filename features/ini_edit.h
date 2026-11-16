#ifndef FH2_INI_EDIT_H
#define FH2_INI_EDIT_H
#include <stddef.h>

/* Small, allocation-free INI helpers that behave like the launcher's IniDoc:
   - keys/sections are matched case-insensitively, comments (; #) are skipped,
   - setting a value keeps everything up to '=' and replaces the rest of the line,
   - a missing key is inserted at the end of its section (before trailing blank lines),
   - a missing section is appended at the end of the file,
   - line endings (CRLF or LF) and a UTF-8 byte-order mark are preserved. */

/* Leading integer of section/key ("70", " -1", "30 ; note"). Returns 1 when found. */
int ini_find_int(const char *text, size_t length, const char *section, const char *key,
                 int *value);

/* Copies text to out with section/key set to value. Returns the new length (out is
   zero-terminated) or 0 when it does not fit into capacity. */
size_t ini_set_value(const char *text, size_t length, const char *section, const char *key,
                     const char *value, char *out, size_t capacity);

/* Value text of section/key without surrounding blanks (zero-terminated, cut to capacity).
   Returns 1 when the key exists. */
int ini_find_text(const char *text, size_t length, const char *section, const char *key,
                  char *value, size_t capacity);

/* Decimal text of value; buffer needs 12 bytes. */
const char *ini_format_int(int value, char buffer[12]);
#endif
