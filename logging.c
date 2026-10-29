#include "logging.h"
#include "features.h"
#include "settings.h"
#include "support.h"

static wchar_t runtime_log_path[PATH_CAPACITY];
static wchar_t startup_log_path[PATH_CAPACITY];

void log_reset(void) {
    runtime_log_path[0] = 0;
}

void log_set_directory(const wchar_t *directory) {
    if (!path_join(runtime_log_path, directory, MOD_LOG_NAME)) {
        runtime_log_path[0] = 0;
    }
}

void log_append(const char *message) {
    if (!runtime_log_path[0]) {
        return;
    }
    HANDLE file =
        CreateFileW(runtime_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD bytes_written;
    WriteFile(file, message, (DWORD)text_length(message), &bytes_written, NULL);
    CloseHandle(file);
}

static void write_text(HANDLE file, const char *text) {
    DWORD bytes_written;
    WriteFile(file, text, (DWORD)text_length(text), &bytes_written, NULL);
}

static void write_number(HANDLE file, int value) {
    char number[12];
    char *cursor = number + sizeof(number) - 1;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    *cursor = 0;
    do {
        *--cursor = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) {
        *--cursor = '-';
    }
    write_text(file, cursor);
}

void log_append_value(const char *label, int value) {
    if (!runtime_log_path[0]) {
        return;
    }
    HANDLE file =
        CreateFileW(runtime_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        write_text(file, label);
        write_number(file, value);
        write_text(file, "\r\n");
        CloseHandle(file);
    }
}

void log_start(const wchar_t *directory, const ModOptions *options) {
    if (!path_join(startup_log_path, directory, MOD_LOG_NAME)) {
        return;
    }
    HANDLE file = CreateFileW(startup_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    write_text(file, "Gripline " GRIPLINE_VERSION " - FH2 Mod Platform mod next to the official Extra Options\r\n");
    write_text(file, status_get());
    for (size_t index = 0; index < setting_count; ++index) {
        const SettingDefinition *definition = &setting_definitions[index];
        int value = settings_value(options, definition);
        write_text(file, "\r\n");
        write_text(file, definition->log_name);
        write_text(file, "=");
        write_number(file, value);
    }
    write_text(file, "\r\n");
    CloseHandle(file);
}

/* diagnostics: "<label><module>+0x<offset>" for a code or data address */
void log_append_module(const char *label, const void *address) {
    char line[400];
    size_t n = 0;
    for (const char *p = label; *p && n < 200; ++p) line[n++] = *p;
    HMODULE module = NULL;
    wchar_t path[MAX_PATH];
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      (LPCWSTR)address, &module) &&
        GetModuleFileNameW(module, path, MAX_PATH)) {
        const wchar_t *name = path;
        for (const wchar_t *p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/') name = p + 1;
        for (; *name && n < 300; ++name) line[n++] = *name < 128 ? (char)*name : '?';
    } else {
        for (const char *p = "(no module)"; *p; ++p) line[n++] = *p;
    }
    uintptr_t offset = (uintptr_t)address - (uintptr_t)module;
    line[n++] = '+';
    line[n++] = '0';
    line[n++] = 'x';
    int started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned digit = (unsigned)(offset >> shift) & 15;
        if (!digit && !started && shift) continue;
        started = 1;
        line[n++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    log_append(line);
}
