/* More Traffic, part 2: the game's own traffic data, not only its caps.
 *
 * Raising the caps (traffic_tunables.c) alone hardly shows: the open world still asks for few cars.
 * media\EntityModel.zip holds what it asks for:
 *   AI/CarLoadingSettings.xml  <TrafficSettings MaxActiveTraffic="18" MaxActiveTypes="8">, and per
 *                              model <CarType MaxActive> (bus/truck: 1)
 *   AI/Densities*.xml          per road type and car type (traffic / festival) a density min/max
 *                              and spawnIn/OutDistance; road type "open_world" has no traffic at all
 * At game start Gripline reads these files out of the original zip, raises the values for
 * TrafficLevel (same rules as tools/fh2_more_traffic.py, presets light..maximum) and writes them to
 * Mount\gripline_traffic, which media_overrides.c mounts over game:\media. The game files are never
 * changed; TrafficLevel=0 leaves the folder unmounted. Read once by the game: applies after a restart. */
#include "extra_options.h"
#include "logging.h"
#include "support.h"
#include "cockpit.h"
#include "traffic_files.h"

#define TRAFFIC_FOLDER L"gripline_traffic"

enum { ROADS_NORMAL, ROADS_EMPTY, ROADS_DRIVATARS };

typedef struct {
    int max_active, types, type_mult, fill, roads, max_festival;
    double traffic, festival, cap, distance;
} Level;

/* TrafficLevel 0..5; MaxActiveTraffic follows the menu ("24 cars" .. "150 cars"). spawnIn/OutDistance only
   exists for town roads (200/250 m), so the distance factor stays small: a larger circle with the same
   number of cars only thins them out. 6 = no traffic, 7 = Drivatars only (the festival cars, which the game
   caps at 11 in CarLoadingSettings, instead of civilian traffic). */
static const Level levels[8] = {
    {18, 8, 1, 0, ROADS_NORMAL, 11, 1.0, 1.0, 1.0, 1.0},     /* original */
    {24, 8, 1, 0, ROADS_NORMAL, 14, 1.5, 1.5, 1.0, 1.0},     /* light */
    {36, 10, 1, 1, ROADS_NORMAL, 18, 2.0, 2.0, 1.0, 1.0},    /* busy */
    {48, 10, 2, 1, ROADS_NORMAL, 24, 3.0, 2.5, 1.5, 1.25},   /* strong */
    {64, 12, 2, 1, ROADS_NORMAL, 32, 4.0, 3.0, 2.0, 1.25},   /* rush hour */
    {150, 16, 4, 1, ROADS_NORMAL, 60, 10.0, 5.0, 4.0, 1.5},  /* chaos */
    {18, 8, 1, 0, ROADS_EMPTY, 11, 1.0, 1.0, 1.0, 1.0},      /* no traffic: density 0 for traffic and festival cars */
    {18, 8, 1, 0, ROADS_DRIVATARS, 80, 1.0, 8.0, 3.0, 1.0},  /* Drivatars only, at the road's traffic speed */
};

static const char *const density_files[] = {
    "AI/Densities.xml",
    "AI/DensitiesRoadTrip.xml",
    "AI/DensitiesIEDriveToFestival.xml",
};

static volatile LONG state; /* 0 not started, 2 ready, 3 off / failed */
static int changes;

/* ------------------------------------------------------------------ text */

typedef struct {
    char *data;
    size_t length, capacity;
    int failed;
} Text;

static void put(Text *t, const char *s, size_t n) {
    if (t->failed) return;
    if (t->length + n > t->capacity) {
        size_t capacity = t->capacity ? t->capacity * 2 : 65536;
        while (capacity < t->length + n) capacity *= 2;
        char *data = HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!data) {
            t->failed = 1;
            return;
        }
        if (t->data) {
            memory_copy(data, t->data, t->length);
            HeapFree(GetProcessHeap(), 0, t->data);
        }
        t->data = data;
        t->capacity = capacity;
    }
    memory_copy(t->data + t->length, s, n);
    t->length += n;
}

static void text_free(Text *t) {
    if (t->data) HeapFree(GetProcessHeap(), 0, t->data);
    t->data = NULL;
}

static char lower_char(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

/* first `needle` (no case) in [from, end) or NULL */
static const char *find(const char *from, const char *end, const char *needle) {
    size_t n = text_length(needle);
    for (const char *p = from; p + n <= end; ++p) {
        size_t i = 0;
        while (i < n && lower_char(p[i]) == lower_char(needle[i])) ++i;
        if (i == n) return p;
    }
    return NULL;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* the value of attribute `name` inside the tag [tag, end): its first character and length */
static const char *attribute(const char *tag, const char *end, const char *name, size_t *length) {
    size_t n = text_length(name);
    for (const char *p = tag + 1; p + n < end; ++p) {
        if (!is_space(p[-1]) || !memory_equal(p, name, n)) continue;
        const char *q = p + n;
        while (q < end && is_space(*q)) ++q;
        if (q >= end || *q != '=') continue;
        ++q;
        while (q < end && is_space(*q)) ++q;
        if (q >= end || *q != '"') continue;
        const char *value = ++q;
        while (q < end && *q != '"') ++q;
        if (q >= end) return NULL;
        *length = (size_t)(q - value);
        return value;
    }
    return NULL;
}

static int attribute_is(const char *tag, const char *end, const char *name, const char *want) {
    size_t length;
    const char *value = attribute(tag, end, name, &length);
    if (!value || length != text_length(want)) return 0;
    for (size_t i = 0; i < length; ++i)
        if (lower_char(value[i]) != lower_char(want[i])) return 0;
    return 1;
}

static int parse_number(const char *s, size_t n, double *out) {
    double v = 0, scale = 0;
    size_t i = 0;
    int negative = n && s[0] == '-', digits = 0;
    if (negative) ++i;
    for (; i < n; ++i) {
        if (s[i] == '.' && !scale) {
            scale = 1;
        } else if (s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            if (scale) scale *= 10;
            ++digits;
        } else {
            return 0;
        }
    }
    if (!digits) return 0;
    *out = (negative ? -v : v) / (scale ? scale : 1);
    return 1;
}

/* like the XML: 0.7 / 3 / 1.25 (at most 4 decimals) */
static size_t format_number(double v, char *out) {
    size_t n = 0;
    if (v < 0) {
        out[n++] = '-';
        v = -v;
    }
    unsigned long long scaled = (unsigned long long)(v * 10000.0 + 0.5);
    unsigned long long whole = scaled / 10000, fraction = scaled % 10000;
    char reverse[24];
    int r = 0;
    do {
        reverse[r++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole && r < 20);
    while (r) out[n++] = reverse[--r];
    if (fraction) {
        out[n++] = '.';
        for (unsigned long long d = 1000; d && fraction; d /= 10) {
            out[n++] = (char)('0' + fraction / d);
            fraction %= d;
        }
    }
    return n;
}

/* A tag is edited in a copy: replaces the value of `name` by rule(value). */
typedef struct {
    char data[4096];
    size_t length;
} Tag;

typedef double (*Rule)(double value, const Level *level);

static void tag_set(Tag *tag, const char *name, Rule rule, const Level *level) {
    size_t length;
    const char *value = attribute(tag->data, tag->data + tag->length, name, &length);
    double old;
    if (!value || !parse_number(value, length, &old)) return;
    double now = rule(old, level);
    if (now == old) return;
    char number[32];
    size_t n = format_number(now, number);
    size_t at = (size_t)(value - tag->data);
    if (tag->length - length + n >= sizeof(tag->data)) return;
    char *tail = tag->data + at + length;
    size_t tail_length = tag->length - at - length;
    if (n != length) {
        /* move the rest of the tag (overlapping) */
        if (n > length) {
            for (size_t i = tail_length; i-- > 0;) tail[i + n - length] = tail[i];
        } else {
            for (size_t i = 0; i < tail_length; ++i) tail[i - (length - n)] = tail[i];
        }
    }
    memory_copy(tag->data + at, number, n);
    tag->length = tag->length - length + n;
    ++changes;
}

static void tag_text(Tag *tag, const char *name, const char *text, size_t n) {
    size_t length;
    const char *value = attribute(tag->data, tag->data + tag->length, name, &length);
    if (!value || tag->length - length + n >= sizeof(tag->data)) return;
    size_t at = (size_t)(value - tag->data);
    char *tail = tag->data + at + length;
    size_t tail_length = tag->length - at - length;
    if (n > length) {
        for (size_t i = tail_length; i-- > 0;) tail[i + n - length] = tail[i];
    } else {
        for (size_t i = 0; i < tail_length; ++i) tail[i - (length - n)] = tail[i];
    }
    memory_copy(tag->data + at, text, n);
    tag->length = tag->length - length + n;
}

static int tag_load(Tag *tag, const char *from, const char *end) {
    if ((size_t)(end - from) >= sizeof(tag->data)) return 0;
    tag->length = (size_t)(end - from);
    memory_copy(tag->data, from, tag->length);
    return 1;
}

/* ------------------------------------------------------------------ rules */

static double clamp_to(double v, double cap) {
    return v < cap ? v : cap;
}

static double rule_traffic(double v, const Level *l) {
    return v <= l->cap ? clamp_to(v * l->traffic, l->cap) : v;
}

static double rule_festival(double v, const Level *l) {
    return v <= l->cap ? clamp_to(v * l->festival, l->cap) : v;
}

static double rule_fill(double v, const Level *l) {
    return clamp_to(v * (l->traffic > 1.0 ? l->traffic : 1.0), l->cap);
}

static double rule_zero(double v, const Level *l) {
    (void)v;
    (void)l;
    return 0;
}

static double rule_distance(double v, const Level *l) {
    return (double)(long long)(v * l->distance + 0.5);
}

static double rule_max_active(double v, const Level *l) {
    return v > l->max_active ? v : l->max_active;
}

static double rule_max_festival(double v, const Level *l) {
    return v > l->max_festival ? v : l->max_festival;
}

static double rule_types(double v, const Level *l) {
    return v > l->types ? v : l->types;
}

static double rule_type_mult(double v, const Level *l) {
    double now = (double)(long long)(v * l->type_mult + 0.5);
    return now < 1 ? 1 : now;
}

/* ------------------------------------------------------------------ files */

/* Drivatars only: the festival entries get the speed of the traffic entry of the same road type (the
   festival list has none), so the Drivatars may drive like traffic instead of racing - if the game uses it. */
typedef struct {
    char name[32], speed[16];
} RoadSpeed;
static RoadSpeed speeds[32];
static unsigned speed_count;

static void remember_speed(const char *tag, const char *end) {
    size_t name_length, speed_length;
    const char *name = attribute(tag, end, "densityNameTagValue", &name_length);
    const char *speed = attribute(tag, end, "speed", &speed_length);
    if (!name || !speed || name_length >= 32 || speed_length >= 16 || speed_count >= ARRAY_COUNT(speeds)) return;
    RoadSpeed *r = &speeds[speed_count++];
    memory_copy(r->name, name, name_length);
    r->name[name_length] = 0;
    memory_copy(r->speed, speed, speed_length);
    r->speed[speed_length] = 0;
}

static void give_speed(Tag *tag) {
    size_t length;
    const char *end = tag->data + tag->length;
    if (attribute(tag->data, end, "speed", &length)) return;
    for (unsigned i = 0; i < speed_count; ++i) {
        if (!attribute_is(tag->data, end, "densityNameTagValue", speeds[i].name)) continue;
        /* insert ' speed="x"' before the closing "/>" */
        size_t n = text_length(speeds[i].speed), insert = 9 + n;
        if (tag->length < 2 || tag->length + insert >= sizeof(tag->data)) return;
        char *close = tag->data + tag->length - 2;
        while (close > tag->data && is_space(close[-1])) --close;
        size_t tail = (size_t)(tag->data + tag->length - close);
        for (size_t k = tail; k-- > 0;) close[k + insert] = close[k];
        memory_copy(close, " speed=\"", 8);
        memory_copy(close + 8, speeds[i].speed, n);
        close[8 + n] = '"';
        tag->length += insert;
        ++changes;
        return;
    }
}

/* <DensityList cartype="traffic|festival"> ... <Density densityNameTagValue=".." min max spawnInDistance ../> */
static void patch_densities(const char *in, size_t size, Text *out, const Level *l) {
    const char *end = in + size, *p = in;
    speed_count = 0;
    while (p < end) {
        const char *list = find(p, end, "<DensityList");
        const char *open_end = list ? find(list, end, ">") : NULL;
        const char *close = open_end ? find(open_end, end, "</DensityList>") : NULL;
        if (!close) break;
        put(out, p, (size_t)(open_end + 1 - p));
        int traffic = attribute_is(list, open_end, "cartype", "traffic");
        int festival = attribute_is(list, open_end, "cartype", "festival");
        /* road type "b" is the pattern for "open_world" (no traffic there in the game) */
        const char *pattern = NULL, *pattern_end = NULL;
        for (const char *d = open_end; traffic && l->fill && (d = find(d, close, "<Density")) != NULL; ++d) {
            const char *e = find(d, close, "/>"), *gt = find(d, close, ">");
            if (e && gt == e + 1 && is_space(d[8]) && attribute_is(d, e, "densityNameTagValue", "b")) {
                pattern = d;
                pattern_end = e + 2;
                break;
            }
        }
        const char *q = open_end + 1;
        for (;;) {
            const char *d = find(q, close, "<Density");
            if (!d) break;
            const char *e = find(d, close, "/>"), *gt = find(d, close, ">");
            if (!e || !is_space(d[8]) || gt != e + 1) { /* not a <Density .../> tag */
                put(out, q, (size_t)(d + 1 - q));
                q = d + 1;
                continue;
            }
            put(out, q, (size_t)(d - q));
            static Tag tag;
            if (!tag_load(&tag, d, e + 2)) {
                put(out, d, (size_t)(e + 2 - d));
                q = e + 2;
                continue;
            }
            size_t max_length;
            const char *max = attribute(d, e, "max", &max_length);
            double max_value = 1;
            if (pattern && attribute_is(d, e, "densityNameTagValue", "open_world") && max &&
                parse_number(max, max_length, &max_value) && max_value == 0) {
                static Tag copy;
                size_t id_length;
                const char *id = attribute(d, e, "id", &id_length);
                if (tag_load(&copy, pattern, pattern_end)) {
                    if (id) tag_text(&copy, "id", id, id_length);
                    tag_text(&copy, "densityNameTagValue", "open_world", 10);
                    tag_set(&copy, "min", rule_fill, l);
                    tag_set(&copy, "max", rule_fill, l);
                    ++changes;
                    tag = copy;
                }
            } else if (l->roads == ROADS_EMPTY) {
                if (traffic || festival) {
                    tag_set(&tag, "min", rule_zero, l);
                    tag_set(&tag, "max", rule_zero, l);
                }
            } else if (l->roads == ROADS_DRIVATARS) {
                if (traffic) {
                    remember_speed(d, e);
                    tag_set(&tag, "min", rule_zero, l);
                    tag_set(&tag, "max", rule_zero, l);
                } else if (festival) {
                    tag_set(&tag, "min", rule_festival, l);
                    tag_set(&tag, "max", rule_festival, l);
                    give_speed(&tag);
                }
            } else {
                if (traffic && l->traffic != 1.0) {
                    tag_set(&tag, "min", rule_traffic, l);
                    tag_set(&tag, "max", rule_traffic, l);
                } else if (festival && l->festival != 1.0) {
                    tag_set(&tag, "min", rule_festival, l);
                    tag_set(&tag, "max", rule_festival, l);
                }
                if (traffic && l->distance != 1.0) {
                    tag_set(&tag, "spawnInDistance", rule_distance, l);
                    tag_set(&tag, "spawnOutDistance", rule_distance, l);
                }
            }
            put(out, tag.data, tag.length);
            q = e + 2;
        }
        put(out, q, (size_t)(close - q));
        p = close;
    }
    put(out, p, (size_t)(end - p));
}

/* <TrafficSettings MaxActiveTraffic MaxActiveTypes>, <CarType MaxActive> */
static int patch_car_loading(const char *in, size_t size, Text *out, const Level *l) {
    const char *end = in + size, *p = in;
    const char *settings = find(in, end, "<TrafficSettings");
    const char *settings_end = settings ? find(settings, end, ">") : NULL;
    if (!settings_end) return 0;
    while (p < end) {
        const char *a = find(p, end, "<TrafficSettings");
        const char *b = l->type_mult > 1 ? find(p, end, "<CarType") : NULL;
        const char *c = find(p, end, "<FestivalSettings");
        const char *t = a && (!b || a < b) ? a : b;
        if (c && (!t || c < t)) t = c;
        const char *t_end = t ? find(t, end, ">") : NULL;
        if (!t_end) break;
        put(out, p, (size_t)(t - p));
        static Tag tag;
        if (!tag_load(&tag, t, t_end + 1)) {
            put(out, t, (size_t)(t_end + 1 - t));
        } else {
            if (t == c) {
                tag_set(&tag, "MaxActiveFestival", rule_max_festival, l);
            } else if (t == a) {
                tag_set(&tag, "MaxActiveTraffic", rule_max_active, l);
                tag_set(&tag, "MaxActiveTypes", rule_types, l);
            } else if (is_space(t[8])) {
                tag_set(&tag, "MaxActive", rule_type_mult, l);
            }
            put(out, tag.data, tag.length);
        }
        p = t_end + 1;
    }
    put(out, p, (size_t)(end - p));
    return 1;
}

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

/* each file goes to EntityModel\AI\.. (the zip's own folder, like gripline_cockpit\cars\<zip>\..);
   AI\.. as well in case the game looks it up without the zip name */
static int write_both(const wchar_t *root, const char *name, const Text *text) {
    static wchar_t path[PATH_CAPACITY];
    int ok = join_ascii(path, root, "EntityModel/", name) && write_file(path, text->data, text->length);
    if (join_ascii(path, root, "", name)) write_file(path, text->data, text->length);
    return ok;
}

static int make_files(const wchar_t *mount_dir, const Level *l) {
    static wchar_t root[PATH_CAPACITY], zip[PATH_CAPACITY], backup[PATH_CAPACITY];
    const wchar_t *plugin_root = gripline_output_root();
    if (plugin_root) lstrcpynW(root, plugin_root, PATH_CAPACITY);
    else if (!path_join(root, mount_dir, TRAFFIC_FOLDER)) return 0;
    if (!path_join(zip, mount_dir, L"media\\EntityModel.zip")) return 0;
    /* the old script tools/fh2_more_traffic.py changes the zip itself and keeps the original next to it */
    if (path_join(backup, mount_dir, L"media\\EntityModel.zip.moretraffic-original") &&
        GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES) {
        memory_copy(zip, backup, sizeof(zip));
        log_append("TRAFFIC: EntityModel.zip.moretraffic-original found (old More Traffic script) - read from it.\r\n");
    }
    int written = 0;
    changes = 0;
    uint32_t size;
    char *data = (char *)gripline_zip_extract(zip, "AI/CarLoadingSettings.xml", &size);
    if (data) {
        Text out = {0};
        if (patch_car_loading(data, size, &out, l) && !out.failed && write_both(root, "AI/CarLoadingSettings.xml", &out))
            ++written;
        text_free(&out);
        gripline_zip_free(data);
    }
    int car_loading_changes = changes;
    for (unsigned i = 0; i < ARRAY_COUNT(density_files); ++i) {
        data = (char *)gripline_zip_extract(zip, density_files[i], &size);
        if (!data) continue;
        /* UTF-16 files are left as they are */
        if (!(size >= 2 && (unsigned char)data[0] == 0xff && (unsigned char)data[1] == 0xfe)) {
            Text out = {0};
            patch_densities(data, size, &out, l);
            if (!out.failed && write_both(root, density_files[i], &out)) ++written;
            text_free(&out);
        }
        gripline_zip_free(data);
    }
    log_append_value("TRAFFIC: traffic files written to Mount\\gripline_traffic: ", written);
    log_append_value("TRAFFIC: car loading values changed: ", car_loading_changes);
    log_append_value("TRAFFIC: density values changed: ", changes - car_loading_changes);
    return written > 0 && (car_loading_changes > 0 || l->roads == ROADS_EMPTY);
}

void traffic_files_start(const wchar_t *mount_dir, int traffic_level) {
    if (state != 0) return;
    if (!mount_dir || traffic_level <= 0 || traffic_level > 7) {
        InterlockedExchange(&state, 3);
        return;
    }
    int ok = 0;
    __try {
        ok = make_files(mount_dir, &levels[traffic_level]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    if (ok && traffic_level == 7) {
        log_append("TRAFFIC: Drivatars only - no civilian traffic, more Drivatars at traffic speed.\r\n");
    } else if (ok && traffic_level == 6) {
        log_append("TRAFFIC: no traffic - traffic and festival density 0.\r\n");
    } else if (ok) {
        log_append_value("TRAFFIC: game traffic data raised (more cars on the roads), TrafficLevel=", traffic_level);
    } else {
        log_append("TRAFFIC: could not make the traffic files; only the density caps are raised.\r\n");
    }
    InterlockedExchange(&state, ok ? 2 : 3);
}

int traffic_files_wait_ready(void) {
    /* the game may ask for its first file before traffic_files_start ran */
    for (int i = 0; i < 100; ++i) {
        LONG s = state;
        if (s == 2) return 1;
        if (s == 3) return 0;
        Sleep(50);
    }
    return 0;
}

#ifdef FH2_TWEAKS_TEST
/* tests: runs the rules on one file in memory (kind 0 = CarLoadingSettings, 1 = Densities) */
__declspec(dllexport) size_t Fh2TestTrafficPatch(int kind, int level, const char *in, size_t size, char *out,
                                                size_t capacity) {
    if (level < 0 || level > 7) return 0;
    Text text = {0};
    changes = 0;
    if (kind == 0) {
        patch_car_loading(in, size, &text, &levels[level]);
    } else {
        patch_densities(in, size, &text, &levels[level]);
    }
    size_t n = text.failed || text.length > capacity ? 0 : text.length;
    if (n) memory_copy(out, text.data, n);
    text_free(&text);
    return n;
}
#endif

#ifdef FH2_TWEAKS_TEST
/* tests: the whole start again (state reset), returns 2 = ready, 3 = off / failed */
__declspec(dllexport) int Fh2TestTrafficFilesStart(const wchar_t *mount_dir, int traffic_level) {
    InterlockedExchange(&state, 0);
    traffic_files_start(mount_dir, traffic_level);
    return (int)state;
}
#endif
