/* Conflict check: looks for other programs that feed Azimuth a controller of their own.
   The Azimuth Mapper is a separate program (not a file in fh2-mods): it installs the ViGEmBus
   driver and shows Azimuth a virtual Xbox controller built from keys, pads and wheels. With
   Gripline's keyboard or wheel on top, every input would arrive twice.
   The Mapper opens one named event, "AzimuthMapper.ShowWindow" (it lets a second start bring
   the running window to the front), so its presence is one OpenEventW away - no process list. */
#include "extra_options.h"
#include "mod_scan.h"
#include "logging.h"
#include "notify.h"
#include "support.h"

int mod_scan_mapper_running(void) {
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, L"AzimuthMapper.ShowWindow");
    if (!event) return 0;
    CloseHandle(event);
    return 1;
}

void mod_scan_announce_mapper(void) {
    static volatile LONG announced;
    if (InterlockedCompareExchange(&announced, 1, 0) != 0) return;
    log_append("GRIPLINE: Azimuth Mapper is running; Gripline keyboard and wheel stay off.\r\n");
    notify_show_deferred("Azimuth Mapper detected\nGripline keyboard and wheel off - the Mapper's controller is used");
}

/* ------------------------------------------------------------------ mods found */

/* Mods are not looked up in the process list: the FH2 Mod Platform writes fh2-mods\platform-mods.txt
   (DLL, name, version, ... one tab separated line per mod) and a few known mods are asked for by
   module name. */
static wchar_t mods_file[PATH_CAPACITY];

typedef struct {
    const wchar_t *module;
    const char *name;
} KnownModule;
static const KnownModule known_modules[] = {{L"fh2wheels.dll", "fh2wheels"}, {L"fh2wheels-next.dll", "fh2wheels"}};

/* fh2wheels installs itself as data\bin\azimuth-tls.dll (Azimuth's own helper becomes
   fh2wheels-next.dll), so it is loaded under Azimuth's name. Its export table still carries the
   name it was built with. */
static int tls_helper_is_fh2wheels(void) {
    static const char want[] = "fh2wheels";
    unsigned char *base = (unsigned char *)GetModuleHandleW(L"azimuth-tls.dll");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    IMAGE_EXPORT_DIRECTORY exports;
    char name[sizeof(want)];
    if (!base || !memory_read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 64 || dos.e_lfanew > 4096 || !memory_read(base + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE)
        return 0;
    IMAGE_DATA_DIRECTORY dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !memory_read(base + dir.VirtualAddress, &exports, sizeof(exports)) ||
        !exports.Name || !memory_read(base + exports.Name, name, sizeof(name) - 1))
        return 0;
    for (unsigned i = 0; i < sizeof(want) - 1; ++i) {
        char c = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i];
        if (c != want[i]) return 0;
    }
    return 1;
}

typedef struct {
    char *out;
    unsigned size, used, names_at, count;
    int cut;
} NameList;

static void add_text(NameList *l, const char *text, unsigned length) {
    if (l->used + length + 1 > l->size) {
        l->cut = 1;
        return;
    }
    for (unsigned i = 0; i < length; ++i) l->out[l->used++] = text[i];
    l->out[l->used] = 0;
}

static int has_name(const NameList *l, const char *name, unsigned length) {
    for (unsigned at = l->names_at; at < l->used;) {
        unsigned end = at;
        while (end < l->used && l->out[end] != ',') ++end;
        if (end - at == length && memory_equal(l->out + at, name, length)) return 1;
        at = end + 2; /* ", " */
    }
    return 0;
}

static void add_name(NameList *l, const char *name, unsigned length) {
    if (!length || l->cut || has_name(l, name, length)) return;
    if (l->used + (l->count ? 2 : 0) + length + 3 + 1 > l->size) { /* room for "..." stays */
        l->cut = 1;
        return;
    }
    if (l->count) add_text(l, ", ", 2);
    add_text(l, name, length);
    if (!l->cut) ++l->count;
}

static int is_gripline_dll(const char *name, unsigned length) {
    static const char own[] = "gripline.dll";
    if (length != sizeof(own) - 1) return 0;
    for (unsigned i = 0; i < length; ++i) {
        char c = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + 32) : name[i];
        if (c != own[i]) return 0;
    }
    return 1;
}

static int mod_scan_mods_text(char *out, unsigned size) {
    static const char head[] = "Gripline " GRIPLINE_VERSION " - mods found\n";
    NameList list = {out, size, 0, 0, 0, 0};
    add_text(&list, head, sizeof(head) - 1);
    list.names_at = list.used;
    HANDLE file = CreateFileW(mods_file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        static char text[8192];
        DWORD got = 0;
        if (!ReadFile(file, text, sizeof(text) - 1, &got, NULL)) got = 0;
        CloseHandle(file);
        unsigned i = 0;
        if (got >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
            (unsigned char)text[2] == 0xbf)
            i = 3; /* UTF-8 byte order mark */
        while (i < got) {
            unsigned line = i, tab1 = 0, tab2 = 0;
            for (; i < got && text[i] != '\n'; ++i) {
                if (text[i] != '\t') continue;
                if (!tab1) tab1 = i;
                else if (!tab2) tab2 = i;
            }
            unsigned end = i;
            while (end > line && text[end - 1] == '\r') --end;
            ++i;
            if (text[line] == '#' || !tab1 || is_gripline_dll(text + line, tab1 - line)) continue;
            unsigned name_end = tab2 ? tab2 : end;
            if (name_end - (tab1 + 1) > 40) name_end = tab1 + 41;
            add_name(&list, text + tab1 + 1, name_end - (tab1 + 1));
        }
    }
    for (unsigned k = 0; k < ARRAY_COUNT(known_modules); ++k) {
        if (GetModuleHandleW(known_modules[k].module))
            add_name(&list, known_modules[k].name, (unsigned)text_length(known_modules[k].name));
    }
    if (tls_helper_is_fh2wheels()) add_name(&list, "fh2wheels", 9);
    if (list.cut) add_text(&list, "...", 3);
    return list.count > 0;
}

int mod_scan_mods_list(char *out, unsigned size) {
    return mod_scan_mods_text(out, size);
}

void mod_scan_start(const wchar_t *settings_directory) {
    if (settings_directory && path_join(mods_file, settings_directory, L"fh2-mods\\platform-mods.txt"))
        notify_show_deferred_call(mod_scan_mods_text);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestModsText(const wchar_t *directory, char *out, unsigned size) {
    mod_scan_start(directory);
    return mod_scan_mods_text(out, size);
}
#endif
