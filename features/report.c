/* Problem report (menu: "Gripline: Create Problem Report"). Packs the files a bug report needs
   into Gripline_Report.zip next to the INIs: a plain ZIP without compression (stored), so no
   compressor is needed and every unzip tool opens it. Missing files are skipped; files the game
   still writes to (logs) are opened with full sharing and read as they are at that moment. */
#include "gripline.h"
#include "report.h"
#include "logging.h"
#include "mod_scan.h"
#include "notify.h"
#include "support.h"

#define REPORT_NAME L"Gripline_Report.zip"
#define MAX_FILE (2u * 1024u * 1024u) /* a file is cut after 2 MB */
#define MAX_ENTRIES 16

static wchar_t directory[PATH_CAPACITY];
static wchar_t path[PATH_CAPACITY];
static unsigned char data[MAX_FILE];

static const struct {
    const wchar_t *file; /* relative to the settings folder */
    const char *name;    /* name inside the ZIP */
} files[] = {
    {L"FH2_ExtraOptions.log", "FH2_ExtraOptions.log"},
    {L"FH2_ExtraOptions.ini", "FH2_ExtraOptions.ini"},
    {L"Gripline.log", "Gripline.log"}, /* plugin build */
    {L"Gripline.ini", "Gripline.ini"},
    {L"FH2_Keyboard.ini", "FH2_Keyboard.ini"},
    {L"fh2-mods\\platform.log", "fh2-mods/platform.log"},
    {L"fh2-mods\\platform-mods.txt", "fh2-mods/platform-mods.txt"},
    {L"data\\logs\\azimuth.log", "azimuth.log"}, /* Azimuth 1.0.3: also its wheel and force feedback */
    {L"mods\\loadorder.txt", "mods/loadorder.txt"},
};

typedef struct {
    const char *name;
    unsigned crc, size, offset;
} Entry;

void report_init(const wchar_t *settings_directory) {
    unsigned i = 0;
    for (; settings_directory && settings_directory[i] && i < PATH_CAPACITY - 1; ++i) directory[i] = settings_directory[i];
    directory[i] = 0;
}

static unsigned crc32(const unsigned char *p, unsigned n) {
    unsigned c = 0xffffffffu;
    for (unsigned i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static void put32(unsigned char *p, unsigned v) {
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

/* local time as MS-DOS date << 16 | time */
static unsigned dos_now(void) {
    FILETIME utc, local;
    GetSystemTimeAsFileTime(&utc);
    if (!FileTimeToLocalFileTime(&utc, &local)) local = utc;
    unsigned long long t = ((unsigned long long)local.dwHighDateTime << 32 | local.dwLowDateTime) / 10000000ull;
    unsigned secs = (unsigned)(t % 86400), days = (unsigned)(t / 86400); /* days since 1601-01-01 */
    long long z = (long long)days - 134774 + 719468;                     /* days since 0000-03-01 */
    long long era = z / 146097, doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    unsigned day = (unsigned)(doy - (153 * mp + 2) / 5 + 1), month = (unsigned)(mp < 10 ? mp + 3 : mp - 9);
    unsigned year = (unsigned)(yoe + era * 400 + (month <= 2));
    if (year < 1980) year = 1980;
    return ((year - 1980) << 25 | month << 21 | day << 16) | (secs / 3600) << 11 | (secs / 60 % 60) << 5 | (secs % 60) / 2;
}

static int write_all(HANDLE file, const void *p, unsigned n) {
    DWORD done = 0;
    return WriteFile(file, p, n, &done, NULL) && done == n;
}

static int add_entry(HANDLE zip, Entry *e, unsigned *offset, unsigned when, const unsigned char *bytes, unsigned size) {
    unsigned name_length = (unsigned)text_length(e->name);
    unsigned char h[30];
    e->crc = crc32(bytes, size);
    e->size = size;
    e->offset = *offset;
    put32(h, 0x04034b50);
    put16(h + 4, 10);
    put16(h + 6, 0);
    put16(h + 8, 0); /* stored */
    put32(h + 10, when);
    put32(h + 14, e->crc);
    put32(h + 18, size);
    put32(h + 22, size);
    put16(h + 26, name_length);
    put16(h + 28, 0);
    if (!write_all(zip, h, 30) || !write_all(zip, e->name, name_length) || !write_all(zip, bytes, size)) return 0;
    *offset += 30 + name_length + size;
    return 1;
}

int report_create(void) {
    if (!directory[0] || !path_join(path, directory, REPORT_NAME)) return 0;
    HANDLE zip = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (zip == INVALID_HANDLE_VALUE) {
        log_append("REPORT: could not write Gripline_Report.zip\r\n");
        notify_show("Problem report failed\nCould not write Gripline_Report.zip");
        return 0;
    }
    Entry entries[MAX_ENTRIES];
    unsigned count = 0, offset = 0, when = dos_now();
    int ok = 1;
    /* first a short summary: version and the mods found */
    {
        static const char head[] = "Gripline " GRIPLINE_VERSION " problem report\r\nMods: ";
        unsigned n = (unsigned)sizeof(head) - 1;
        memory_copy(data, head, n);
        char mods[512];
        if (mod_scan_mods_list(mods, sizeof(mods))) {
            unsigned i = 0;
            while (mods[i] && mods[i] != '\n') ++i; /* skip the card's title line */
            for (i += mods[i] ? 1 : 0; mods[i] && n < 1000; ++i) data[n++] = (unsigned char)mods[i];
        } else {
            memory_copy(data + n, "(none)", 6);
            n += 6;
        }
        const char *mapper = mod_scan_mapper_running() ? "\r\nAzimuth Mapper: running\r\n" : "\r\nAzimuth Mapper: not running\r\n";
        unsigned m = (unsigned)text_length(mapper);
        memory_copy(data + n, mapper, m);
        n += m;
        entries[count].name = "Gripline_Report.txt";
        ok = add_entry(zip, &entries[count++], &offset, when, data, n);
    }
    for (unsigned f = 0; ok && f < sizeof(files) / sizeof(files[0]) && count < MAX_ENTRIES; ++f) {
        if (!path_join(path, directory, files[f].file)) continue;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) continue;
        DWORD size = 0;
        int read_ok = ReadFile(file, data, MAX_FILE, &size, NULL);
        CloseHandle(file);
        if (!read_ok) continue;
        entries[count].name = files[f].name;
        ok = add_entry(zip, &entries[count++], &offset, when, data, size);
    }
    /* central directory */
    unsigned directory_offset = offset, directory_size = 0;
    for (unsigned i = 0; ok && i < count; ++i) {
        unsigned name_length = (unsigned)text_length(entries[i].name);
        unsigned char c[46];
        put32(c, 0x02014b50);
        put16(c + 4, 20);
        put16(c + 6, 10);
        put16(c + 8, 0);
        put16(c + 10, 0);
        put32(c + 12, when);
        put32(c + 16, entries[i].crc);
        put32(c + 20, entries[i].size);
        put32(c + 24, entries[i].size);
        put16(c + 28, name_length);
        put16(c + 30, 0);
        put16(c + 32, 0);
        put16(c + 34, 0);
        put16(c + 36, 0);
        put32(c + 38, 0);
        put32(c + 42, entries[i].offset);
        ok = write_all(zip, c, 46) && write_all(zip, entries[i].name, name_length);
        directory_size += 46 + name_length;
    }
    unsigned char end[22];
    put32(end, 0x06054b50);
    put16(end + 4, 0);
    put16(end + 6, 0);
    put16(end + 8, count);
    put16(end + 10, count);
    put32(end + 12, directory_size);
    put32(end + 16, directory_offset);
    put16(end + 20, 0);
    ok = ok && write_all(zip, end, 22);
    CloseHandle(zip);
    if (!ok) {
        log_append("REPORT: writing Gripline_Report.zip failed\r\n");
        notify_show("Problem report failed\nCould not write Gripline_Report.zip");
        return 0;
    }
    log_append("REPORT: Gripline_Report.zip written next to the INIs.\r\n");
    notify_show("Problem report saved\nGripline_Report.zip next to azimuth.exe");
    return (int)count;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestReport(const wchar_t *settings_directory) {
    report_init(settings_directory);
    return report_create();
}
#endif
