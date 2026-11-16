#include "logging.h"
#include "features.h"
#include "settings.h"
#include "support.h"

static wchar_t log_path[PATH_CAPACITY];

void log_reset(void) {
    log_path[0] = 0;
}

void log_set_directory(const wchar_t *directory) {
    if (!path_join(log_path, directory, MOD_LOG_NAME)) log_path[0] = 0;
}

/* ---------------------------------------------------------------- small text builder */

typedef struct {
    char data[448];
    size_t used;
} Line;

static void line_text(Line *line, const char *text) {
    for (; *text && line->used + 1 < sizeof(line->data); ++text) line->data[line->used++] = *text;
}

static void line_number(Line *line, int value) {
    char digits[12];
    unsigned count = 0, rest = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    do {
        digits[count++] = (char)('0' + rest % 10);
        rest /= 10;
    } while (rest);
    if (value < 0) line_text(line, "-");
    while (count && line->used + 1 < sizeof(line->data)) line->data[line->used++] = digits[--count];
}

static void line_hex(Line *line, uintptr_t value) {
    int digits = 1;
    while (digits < 16 && (value >> (4 * digits))) ++digits;
    while (digits-- && line->used + 1 < sizeof(line->data)) {
        unsigned nibble = (unsigned)(value >> (4 * digits)) & 15u;
        line->data[line->used++] = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
    }
}

/* ---------------------------------------------------------------- writing */

static void write_to(HANDLE file, const char *text, size_t length) {
    DWORD written;
    WriteFile(file, text, (DWORD)length, &written, NULL);
}

static HANDLE open_for_append(void) {
    if (!log_path[0]) return INVALID_HANDLE_VALUE;
    return CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

static void flush_line(const Line *line) {
    HANDLE file = open_for_append();
    if (file == INVALID_HANDLE_VALUE) return;
    write_to(file, line->data, line->used);
    CloseHandle(file);
}

void log_append(const char *message) {
    HANDLE file = open_for_append();
    if (file == INVALID_HANDLE_VALUE) return;
    write_to(file, message, text_length(message));
    CloseHandle(file);
}

void log_append_value(const char *label, int value) {
    if (!log_path[0]) return;
    Line line = {{0}, 0};
    line_text(&line, label);
    line_number(&line, value);
    line_text(&line, "\r\n");
    flush_line(&line);
}

/* "<label><module file>+0x<offset>" for a code or data address; for finding which DLL hooked what */
void log_append_module(const char *label, const void *address) {
    Line line = {{0}, 0};
    line_text(&line, label);

    HMODULE module = NULL;
    wchar_t file_name[MAX_PATH];
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (address && GetModuleHandleExW(flags, (LPCWSTR)address, &module) &&
        GetModuleFileNameW(module, file_name, MAX_PATH)) {
        const wchar_t *base = file_name;
        for (const wchar_t *p = file_name; *p; ++p)
            if (*p == L'\\' || *p == L'/') base = p + 1;
        for (; *base && line.used < 300; ++base) line.data[line.used++] = *base < 128 ? (char)*base : '?';
    } else {
        line_text(&line, "(no module)");
    }
    line_text(&line, "+0x");
    line_hex(&line, (uintptr_t)address - (uintptr_t)module);
    line_text(&line, "\r\n");
    flush_line(&line);
}

/* ---------------------------------------------------------------- start of a new log */

void log_start(const wchar_t *directory, const ModOptions *options) {
    static wchar_t path[PATH_CAPACITY]; /* static: 64 KB would need the stack probe the DLL does not link */
    if (!path_join(path, directory, MOD_LOG_NAME)) return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    Line head = {{0}, 0};
    line_text(&head, "Gripline " GRIPLINE_VERSION " - FH2 Mod Platform mod next to the official Extra Options\r\n");
    write_to(file, head.data, head.used);
    write_to(file, status_get(), text_length(status_get())); /* up to 511 characters: not through Line */

    for (size_t i = 0; i < setting_count; ++i) {
        Line row = {{0}, 0};
        line_text(&row, "\r\n");
        line_text(&row, setting_definitions[i].log_name);
        line_text(&row, "=");
        line_number(&row, settings_value(options, &setting_definitions[i]));
        write_to(file, row.data, row.used);
    }
    write_to(file, "\r\n", 2);
    CloseHandle(file);
}
