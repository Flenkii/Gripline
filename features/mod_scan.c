/* Conflict check: looks for other programs that feed Azimuth a controller of their own.
   The Azimuth Mapper is a separate program (not a file in fh2-mods): it installs the ViGEmBus
   driver and shows Azimuth a virtual Xbox controller built from keys (and, in beta, wheels).
   With Gripline's keyboard on top, every key would arrive twice. */
#include "extra_options.h"
#include <tlhelp32.h>
#include "mod_scan.h"

/* lower case parts of the program's file name */
static const wchar_t *const mapper_names[] = {L"azimuthmapper", L"azimuth-mapper", L"azimuth_mapper",
                                              L"azimuth mapper"};

static wchar_t lower_wide(wchar_t c) {
    return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c;
}

static int name_contains(const wchar_t *name, const wchar_t *part) {
    for (; *name; ++name) {
        unsigned k = 0;
        while (part[k] && lower_wide(name[k]) == part[k]) ++k;
        if (!part[k]) return 1;
    }
    return 0;
}

int mod_scan_mapper_running(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    int found = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry)) {
        for (unsigned i = 0; i < ARRAY_COUNT(mapper_names) && !found; ++i)
            found = name_contains(entry.szExeFile, mapper_names[i]);
    }
    CloseHandle(snapshot);
    return found;
}
