#include "support.h"

/* ---------------------------------------------------------------- small C library pieces
   The DLL is built without a C runtime, so copying, comparing and measuring are done here.
   The copies go through volatile so the compiler cannot turn them back into memcpy calls. */

void memory_copy(void *destination, const void *source, size_t size) {
    volatile unsigned char *to = (volatile unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    for (size_t i = 0; i < size; ++i) to[i] = from[i];
}

int memory_equal(const void *left, const void *right, size_t size) {
    const unsigned char *a = (const unsigned char *)left, *b = (const unsigned char *)right;
    for (size_t i = 0; i < size; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

size_t text_length(const char *text) {
    const char *end = text;
    while (*end) ++end;
    return (size_t)(end - text);
}

static char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

int text_equal_ignore_case(const char *left, const char *right) {
    for (;; ++left, ++right) {
        if (lower_ascii(*left) != lower_ascii(*right)) return 0;
        if (!*left) return 1;
    }
}

const char *text_find(const char *text, size_t length, const char *needle) {
    size_t need = text_length(needle);
    if (!need) return text;
    if (need > length) return NULL;
    for (size_t at = 0; at + need <= length; ++at) {
        if (text[at] == needle[0] && memory_equal(text + at, needle, need)) return text + at;
    }
    return NULL;
}

/* "dir" + "name" into output, with a backslash between when the directory has none */
int path_join(wchar_t output[PATH_CAPACITY], const wchar_t *directory, const wchar_t *name) {
    const size_t limit = PATH_CAPACITY - 1;
    size_t used = 0;
    for (; directory[used]; ++used) {
        if (used >= limit - 7) return 0;
        output[used] = directory[used];
    }
    if (used && output[used - 1] != L'\\' && output[used - 1] != L'/') output[used++] = L'\\';
    for (size_t i = 0;; ++i) {
        if (used >= limit) return 0;
        output[used++] = name[i];
        if (!name[i]) return 1;
    }
}

int float_is_finite(float value) {
    uint32_t bits;
    memory_copy(&bits, &value, sizeof(bits));
    return ((bits >> 23) & 0xffu) != 0xffu; /* exponent all ones = infinity or NaN */
}

/* the compiler may call these for struct copies and zeroing */
void *memset(void *destination, int value, size_t size);
void *memcpy(void *destination, const void *source, size_t size);
__attribute__((used)) void *memset(void *destination, int value, size_t size) {
    volatile unsigned char *to = (volatile unsigned char *)destination;
    for (size_t i = 0; i < size; ++i) to[i] = (unsigned char)value;
    return destination;
}
__attribute__((used)) void *memcpy(void *destination, const void *source, size_t size) {
    memory_copy(destination, source, size);
    return destination;
}

/* ---------------------------------------------------------------- settings file lock */

static SRWLOCK settings_lock = SRWLOCK_INIT;

void settings_file_lock(void) {
    AcquireSRWLockExclusive(&settings_lock);
}

void settings_file_unlock(void) {
    ReleaseSRWLockExclusive(&settings_lock);
}

/* ---------------------------------------------------------------- start-up status line */

static char status_line[512] = "Not initialized";

void status_set(const char *message) {
    status_line[0] = 0;
    status_append(message);
}

void status_append(const char *message) {
    size_t have = text_length(status_line);
    size_t room = sizeof(status_line) - 1 - have;
    size_t add = text_length(message);
    if (add > room) add = room;
    memory_copy(status_line + have, message, add);
    status_line[have + add] = 0;
}

const char *status_get(void) {
    return status_line;
}

/* ---------------------------------------------------------------- reading game memory safely */

/* Exception filter for memory_read: a bad address, a paged-out page or a guard page ends the read.
   A guard page is armed again so the game's own stack growth still works. */
static int read_filter(EXCEPTION_POINTERS *info) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    switch (record->ExceptionCode) {
    case EXCEPTION_GUARD_PAGE:
        if (record->NumberParameters >= 2) {
            void *page = (void *)record->ExceptionInformation[1];
            MEMORY_BASIC_INFORMATION region;
            if (VirtualQuery(page, &region, sizeof(region)) == sizeof(region)) {
                DWORD old;
                VirtualProtect(page, 1, region.Protect | PAGE_GUARD, &old);
            }
        }
        return EXCEPTION_EXECUTE_HANDLER;
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        return EXCEPTION_EXECUTE_HANDLER;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

int memory_read(const void *address, void *output, size_t size) {
    if (!address || !output || size > UINTPTR_MAX - (uintptr_t)address) return 0;
    __try {
        memory_copy(output, address, size);
    } __except (read_filter(GetExceptionInformation())) {
        return 0;
    }
    return 1;
}

int memory_read_pointer(const void *address, void **value) {
    return memory_read(address, value, sizeof(*value)) && *value;
}

/* true when every page of [address, address + size) is committed and writable */
int memory_is_writable(const void *address, size_t size) {
    uintptr_t at = (uintptr_t)address, end = at + size;
    if (!at || end < at) return 0;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    while (at < end) {
        MEMORY_BASIC_INFORMATION page;
        if (!VirtualQuery((void *)at, &page, sizeof(page))) return 0;
        if (page.State != MEM_COMMIT || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        if (!(page.Protect & writable)) return 0;
        uintptr_t next = (uintptr_t)page.BaseAddress + page.RegionSize;
        if (next <= at) return 0;
        at = next;
    }
    return 1;
}

/* Free memory close enough to the game image for rip-relative jumps (about 1.5 GB each way):
   looks downwards from the image, then upwards from its end, one allocation unit at a time. */
unsigned char *memory_alloc_near(unsigned char *base, size_t size) {
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    const uintptr_t unit = system.dwAllocationGranularity ? system.dwAllocationGranularity : 0x10000;
    const uintptr_t reach = 0x60000000;
    const uintptr_t image_start = (uintptr_t)base, image_end = image_start + GAME_IMAGE_SIZE;

    for (int upwards = 0; upwards < 2; ++upwards) {
        uintptr_t at = upwards ? (image_end + unit - 1) & ~(unit - 1) : (image_start - unit) & ~(unit - 1);
        for (unsigned step = 0; step < 0x8000; ++step) {
            int too_far = upwards ? at - image_end > reach : (at < 0x10000 || image_start - at > reach);
            if (too_far) break;
            MEMORY_BASIC_INFORMATION region;
            if (VirtualQuery((void *)at, &region, sizeof(region)) == sizeof(region) && region.State == MEM_FREE) {
                void *block = VirtualAlloc((void *)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (block) return (unsigned char *)block;
            }
            at = upwards ? at + unit : at - unit;
        }
    }
    return NULL;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestMemoryRead(const void *address, void *output, size_t size) {
    return memory_read(address, output, size);
}

__declspec(dllexport) uint64_t Fh2TestMemoryBenchmark(const void *address, unsigned count, int process_api) {
    uint64_t sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint64_t value = 0;
        SIZE_T copied;
        int ok = process_api ? ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &copied)
                             : memory_read(address, &value, sizeof(value));
        if (ok) sum += value;
    }
    return sum;
}
#endif
