/* V7: world options as files, mounted over game:\media like the More Traffic files.
 *
 *   Speed cameras off  media\EntityModel.zip speed_cameras.xml / speed_cameras_island.xml hold every
 *                      speed trap and speed zone (entity groups speed_traps, speed_zones; loaded by
 *                      entities_offline_freeroam*.xml and the bucket list routes). Gripline writes both
 *                      files with the two groups empty: no cameras, no flash, no HUD popup. The camera
 *                      poles stay in the world (they are part of the map), they just do nothing.
 *   Car labels off     media\GameTunableSettings.zip GameTunableSettings.ini HUD\CarLabelMaxDistance 110
 *                      (and CarLabelMaxFadeDistance) is how near a car must be for its name label;
 *                      0 = never shown.
 * The game files are never changed. Option back on = Gripline deletes its copy. Read once by the game:
 * applies after a restart. */
#include "gripline.h"
#include "logging.h"
#include "support.h"
#include "cockpit.h"
#include "world_files.h"

static const char empty_cameras[] =
    "<entity_group id=\"speed_cameras\">\r\n"
    "    <!-- Gripline: speed cameras off -->\r\n"
    "    <entity_group id=\"speed_traps\">\r\n"
    "    </entity_group>\r\n"
    "\r\n"
    "    <entity_group id=\"speed_zones\">\r\n"
    "    </entity_group>\r\n"
    "</entity_group>\r\n";

static const char *const camera_files[] = {"speed_cameras.xml", "speed_cameras_island.xml"};

static int write_file(wchar_t *path, const char *data, size_t length) {
    for (size_t i = 3; path[i]; ++i) {
        if (path[i] == L'\\') {
            path[i] = 0;
            CreateDirectoryW(path, NULL);
            path[i] = L'\\';
        }
    }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(f, data, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(f);
    return ok;
}

static int join_ascii(wchar_t *out, const wchar_t *folder, const char *prefix, const char *name) {
    static wchar_t tail[260];
    size_t n = 0;
    for (const char *parts[2] = {prefix, name}, **s = parts; s < parts + 2; ++s)
        for (const char *c = *s; *c && n + 1 < 260; ++c) tail[n++] = *c == '/' ? L'\\' : (wchar_t)(unsigned char)*c;
    tail[n] = 0;
    return path_join(out, folder, tail);
}

/* <zip>\name and name (same rule as the traffic files); write = 0 deletes both */
static int place(const wchar_t *root, const char *zip, const char *name, const char *data, size_t length, int write) {
    static wchar_t path[PATH_CAPACITY];
    int ok = 0;
    if (join_ascii(path, root, zip, name)) ok = write ? write_file(path, data, length) : (DeleteFileW(path), 1);
    if (join_ascii(path, root, "", name)) {
        if (write) write_file(path, data, length);
        else DeleteFileW(path);
    }
    return ok;
}

/* "HUD\CarLabelMaxDistance 110" -> "... 0"; returns a new buffer (caller frees) or NULL */
static char *labels_off(const char *in, uint32_t size, size_t *out_length, int *changed) {
    static const char *const keys[] = {"HUD\\CarLabelMaxDistance ", "HUD\\CarLabelMaxFadeDistance "};
    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 16);
    if (!out) return NULL;
    size_t n = 0;
    *changed = 0;
    for (uint32_t i = 0; i < size;) {
        uint32_t end = i;
        while (end < size && in[end] != '\n') ++end;
        int done = 0;
        for (unsigned k = 0; k < ARRAY_COUNT(keys) && !done; ++k) {
            size_t len = 0;
            while (keys[k][len]) ++len;
            if (end - i >= len && memory_equal(in + i, keys[k], len)) {
                memory_copy(out + n, keys[k], len);
                n += len;
                out[n++] = '0';
                if (end > i && in[end - 1] == '\r') out[n++] = '\r';
                ++*changed;
                done = 1;
            }
        }
        if (!done) {
            memory_copy(out + n, in + i, end - i);
            n += end - i;
        }
        if (end < size) out[n++] = '\n';
        i = end + 1;
    }
    *out_length = n;
    return out;
}

void world_files_start(const wchar_t *mount_dir, int speed_cameras, int car_labels) {
    const wchar_t *root = gripline_output_root();
    if (!root || !root[0]) {
        if (!speed_cameras || !car_labels) log_append("WORLD: no media folder - speed cameras / car labels stay original.\r\n");
        return;
    }
    __try {
        unsigned placed = 0;
        for (unsigned i = 0; i < ARRAY_COUNT(camera_files); ++i)
            placed += (unsigned)place(root, "EntityModel/", camera_files[i], empty_cameras, sizeof(empty_cameras) - 1,
                                      !speed_cameras);
        if (!speed_cameras)
            log_append_value("WORLD: speed cameras off (empty speed camera files written): ", (int)placed);

        if (car_labels) {
            place(root, "GameTunableSettings/", "GameTunableSettings.ini", NULL, 0, 0);
        } else {
            static wchar_t zip[PATH_CAPACITY];
            uint32_t size = 0;
            unsigned char *data =
                mount_dir && path_join(zip, mount_dir, L"media\\GameTunableSettings.zip")
                    ? gripline_zip_extract(zip, "GameTunableSettings.ini", &size)
                    : NULL;
            size_t length = 0;
            int changed = 0;
            char *out = data ? labels_off((const char *)data, size, &length, &changed) : NULL;
            if (out && changed && place(root, "GameTunableSettings/", "GameTunableSettings.ini", out, length, 1))
                log_append_value("WORLD: car labels off (HUD\\CarLabel values set to 0): ", changed);
            else
                log_append("WORLD: car labels: GameTunableSettings.ini not found or not changed - labels stay.\r\n");
            if (out) HeapFree(GetProcessHeap(), 0, out);
            if (data) gripline_zip_free(data);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log_append("WORLD: error while writing the world files - game files are untouched.\r\n");
    }
}
