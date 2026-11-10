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
 *   Event icons off    EntityModel.zip Templates/templates_races.xml and templates_locations.xml: every
 *                      HUDStatusARActivationProviderComponent (the race / event symbols standing on the
 *                      road in free roam) draws on HudID "ar_hud"; Gripline points them at the game's own
 *                      "debug_hud", which is never shown. Checkpoints in races (ARCheckpoint) stay.
 *   Calm Drivatars     EntityModel.zip AI/Difficulty.xml <Global> holds how the open-world Drivatars cruise:
 *                      CruiseMinimum / CruiseMedium / CruiseHigh, each with a Chance (0.6 / 0.2 / 0.2) and
 *                      limits for throttle, braking and cornering, and CruiseReaction (a Drivatar reacts for
 *                      ReactionTime s when the player comes within ActivationDistance m). V7.1 (wish of
 *                      MRAlexis99: Drivatars keeping to the speed limit): every Drivatar gets the calmest
 *                      style (chances 1 / 0 / 0) and CruiseReaction is switched off. Only the <Global> block
 *                      changes; the race difficulty levels, road trips and hero drives keep their values.
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

static const char *const icon_files[] = {"Templates/templates_races.xml", "Templates/templates_locations.xml"};

static const char *find(const char *from, const char *end, const char *what) {
    size_t len = 0;
    while (what[len]) ++len;
    for (const char *p = from; p + len <= end; ++p)
        if (memory_equal(p, what, len)) return p;
    return NULL;
}

/* HUDStatusARActivationProviderComponent ... value="ar_hud" -> value="debug_hud"; NULL when nothing changed */
static char *icons_off(const char *in, uint32_t size, size_t *out_length, int *changed) {
    static const char component[] = "type=\"HUDStatusARActivationProviderComponent\"";
    static const char from[] = "value=\"ar_hud\"", to[] = "value=\"debug_hud\"";
    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 64 * 4);
    if (!out) return NULL;
    const char *end = in + size, *p = in;
    size_t n = 0;
    *changed = 0;
    for (;;) {
        const char *c = find(p, end, component);
        const char *close = c ? find(c, end, "</component>") : NULL;
        const char *v = close ? find(c, close, from) : NULL;
        if (!v || *changed >= 64) break;
        memory_copy(out + n, p, (size_t)(v - p));
        n += (size_t)(v - p);
        memory_copy(out + n, to, sizeof(to) - 1);
        n += sizeof(to) - 1;
        p = v + sizeof(from) - 1;
        ++*changed;
    }
    memory_copy(out + n, p, (size_t)(end - p));
    n += (size_t)(end - p);
    *out_length = n;
    if (!*changed) {
        HeapFree(GetProcessHeap(), 0, out);
        return NULL;
    }
    return out;
}

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

/* Difficulty.xml: inside <Global>, the <Chance value=".."/> of CruiseMinimum -> 1.0, CruiseMedium and
   CruiseHigh -> 0.0, and CruiseReaction's <Enabled value="true"/> -> false. Only these values change, the
   rest stays byte for byte. Copies in[*p .. value) to out and moves *p behind the old value. */
static int replace_value(char *out, size_t *n, const char **p, const char *end, const char *block_name,
                         const char *element, const char *value) {
    char open[48], close[48];
    size_t a = 0, b = 0;
    open[a++] = '<';
    close[b++] = '<';
    close[b++] = '/';
    for (const char *c = block_name; *c && a < 40; ++c) open[a++] = close[b++] = *c;
    open[a++] = '>';
    close[b++] = '>';
    open[a] = close[b] = 0;
    const char *block = find(*p, end, open);
    const char *block_end = block ? find(block, end, close) : NULL;
    const char *tag = block_end ? find(block, block_end, element) : NULL;
    const char *v = tag ? find(tag, block_end, "value=\"") : NULL;
    const char *v_end = v ? find(v + 7, block_end, "\"") : NULL;
    if (!v_end) return 0;
    v += 7;
    memory_copy(out + *n, *p, (size_t)(v - *p));
    *n += (size_t)(v - *p);
    for (const char *c = value; *c;) out[(*n)++] = *c++;
    *p = v_end;
    return 1;
}

/* returns a new buffer (caller frees) or NULL when the file is not laid out as expected */
static char *drivatars_calm(const char *in, uint32_t size, size_t *out_length, int *changed) {
    static const char *const blocks[] = {"CruiseMinimum", "CruiseMedium", "CruiseHigh", "CruiseReaction"};
    static const char *const elements[] = {"<Chance ", "<Chance ", "<Chance ", "<Enabled "};
    static const char *const values[] = {"1.0", "0.0", "0.0", "false"};
    const char *end = in + size;
    const char *global = find(in, end, "<Global>");
    const char *global_end = global ? find(global, end, "</Global>") : NULL;
    *changed = 0;
    if (!global_end) return NULL;
    char *out = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 64);
    if (!out) return NULL;
    const char *p = in;
    size_t n = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(blocks); ++i) /* in file order */
        if (replace_value(out, &n, &p, global_end, blocks[i], elements[i], values[i])) ++*changed;
    memory_copy(out + n, p, (size_t)(end - p));
    n += (size_t)(end - p);
    *out_length = n;
    if (*changed != (int)ARRAY_COUNT(blocks)) { /* unknown layout: the game keeps its own file */
        HeapFree(GetProcessHeap(), 0, out);
        return NULL;
    }
    return out;
}

/* Test hook (tests/test_world_files.py): patches a Difficulty.xml in memory. */
__declspec(dllexport) int Fh2TestCalmDrivatars(const char *in, uint32_t size, char *out, uint32_t capacity) {
    size_t length = 0;
    int changed = 0;
    char *result = drivatars_calm(in, size, &length, &changed);
    if (!result) return -1;
    int ok = length < capacity;
    if (ok) {
        memory_copy(out, result, length);
        out[length] = 0;
    }
    HeapFree(GetProcessHeap(), 0, result);
    return ok ? (int)length : -2;
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

void world_files_start(const wchar_t *mount_dir, int speed_cameras, int car_labels, int event_icons,
                       int calm_drivatars) {
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

        if (event_icons) {
            for (unsigned i = 0; i < ARRAY_COUNT(icon_files); ++i) place(root, "EntityModel/", icon_files[i], NULL, 0, 0);
        } else {
            static wchar_t zip[PATH_CAPACITY];
            int total = 0;
            /* like the traffic files: the old More Traffic script keeps the original zip next to it */
            if (mount_dir && path_join(zip, mount_dir, L"media\\EntityModel.zip.moretraffic-original") &&
                GetFileAttributesW(zip) == INVALID_FILE_ATTRIBUTES)
                path_join(zip, mount_dir, L"media\\EntityModel.zip");
            for (unsigned i = 0; mount_dir && i < ARRAY_COUNT(icon_files); ++i) {
                uint32_t size = 0;
                unsigned char *data = gripline_zip_extract(zip, icon_files[i], &size);
                size_t length = 0;
                int changed = 0;
                char *out = data ? icons_off((const char *)data, size, &length, &changed) : NULL;
                if (out && place(root, "EntityModel/", icon_files[i], out, length, 1)) total += changed;
                if (out) HeapFree(GetProcessHeap(), 0, out);
                if (data) gripline_zip_free(data);
            }
            log_append_value("WORLD: event icons on the road off (activation symbols moved to the debug HUD): ", total);
        }
        if (!calm_drivatars) {
            place(root, "EntityModel/", "AI/Difficulty.xml", NULL, 0, 0);
        } else {
            static wchar_t zip[PATH_CAPACITY];
            if (mount_dir && path_join(zip, mount_dir, L"media\\EntityModel.zip.moretraffic-original") &&
                GetFileAttributesW(zip) == INVALID_FILE_ATTRIBUTES)
                path_join(zip, mount_dir, L"media\\EntityModel.zip");
            uint32_t size = 0;
            unsigned char *data = mount_dir ? gripline_zip_extract(zip, "AI/Difficulty.xml", &size) : NULL;
            size_t length = 0;
            int changed = 0;
            char *out = data ? drivatars_calm((const char *)data, size, &length, &changed) : NULL;
            if (out && place(root, "EntityModel/", "AI/Difficulty.xml", out, length, 1))
                log_append_value("WORLD: calm Drivatars (Difficulty.xml: calmest cruise style for all, no reaction to the player): ",
                                 changed);
            else
                log_append("WORLD: calm Drivatars: AI/Difficulty.xml not found or not as expected - Drivatars stay original.\r\n");
            if (out) HeapFree(GetProcessHeap(), 0, out);
            if (data) gripline_zip_free(data);
        }
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
