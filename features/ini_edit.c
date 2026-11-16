#include "ini_edit.h"

typedef struct {
    size_t start, end; /* line text without '\r' / '\n' */
    size_t next;       /* first byte of the following line */
} Line;

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static int lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int equal_ci(const char *a, size_t length, const char *b) {
    size_t i = 0;
    for (; i < length && b[i]; ++i) {
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) {
            return 0;
        }
    }
    return i == length && !b[i];
}

static size_t skip_bom(const char *text, size_t length) {
    return length >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
                   (unsigned char)text[2] == 0xbf
               ? 3
               : 0;
}

static int next_line(const char *text, size_t length, size_t at, Line *line) {
    if (at >= length) {
        return 0;
    }
    size_t end = at;
    while (end < length && text[end] != '\n') {
        ++end;
    }
    line->start = at;
    line->next = end < length ? end + 1 : end;
    while (end > at && text[end - 1] == '\r') {
        --end;
    }
    line->end = end;
    return 1;
}

static void trim(const char *text, size_t *start, size_t *end) {
    while (*start < *end && is_space(text[*start])) {
        ++*start;
    }
    while (*end > *start && is_space(text[*end - 1])) {
        --*end;
    }
}

/* [name] -> 1 and the trimmed name range */
static int section_of(const char *text, const Line *line, size_t *name, size_t *name_end) {
    size_t s = line->start, e = line->end;
    trim(text, &s, &e);
    if (e - s < 2 || text[s] != '[' || text[e - 1] != ']') {
        return 0;
    }
    *name = s + 1;
    *name_end = e - 1;
    trim(text, name, name_end);
    return 1;
}

/* key=value -> 1, key range and the position of '=' */
static int key_of(const char *text, const Line *line, size_t *key, size_t *key_end,
                  size_t *equals) {
    size_t s = line->start, e = line->end;
    trim(text, &s, &e);
    if (s == e || text[s] == ';' || text[s] == '#' || text[s] == '[') {
        return 0;
    }
    size_t eq = s;
    while (eq < e && text[eq] != '=') {
        ++eq;
    }
    if (eq == e || eq == s) {
        return 0;
    }
    *key = s;
    *key_end = eq;
    trim(text, key, key_end);
    *equals = eq;
    return 1;
}

static int is_blank(const char *text, const Line *line) {
    size_t s = line->start, e = line->end;
    trim(text, &s, &e);
    return s == e;
}

typedef struct {
    int found_key;
    size_t key_line_start, equals, key_line_end; /* key line */
    int found_section;
    size_t insert_at; /* where a missing key goes (start of a line or end of text) */
} Location;

static void locate(const char *text, size_t length, const char *section, const char *key,
                   Location *where) {
    Location result = {0, 0, 0, 0, 0, 0};
    Line line;
    int in_section = 0, in_first = 0, section_done = 0;
    size_t last_content_next = 0;
    for (size_t at = skip_bom(text, length); next_line(text, length, at, &line); at = line.next) {
        size_t a, b, c;
        if (section_of(text, &line, &a, &b)) {
            if (in_first && !section_done) {
                result.insert_at = last_content_next;
                section_done = 1;
            }
            /* keys are searched in every section of that name (like the launcher),
               new keys go into the first one */
            in_section = equal_ci(text + a, b - a, section);
            in_first = in_section && !result.found_section;
            if (in_first) {
                result.found_section = 1;
                last_content_next = line.next;
            }
            continue;
        }
        if (in_first && !section_done && !is_blank(text, &line)) {
            last_content_next = line.next;
        }
        if (!result.found_key && in_section && key_of(text, &line, &a, &b, &c) &&
            equal_ci(text + a, b - a, key)) {
            result.found_key = 1;
            result.key_line_start = line.start;
            result.equals = c;
            result.key_line_end = line.end;
        }
    }
    if (result.found_section && !section_done) {
        result.insert_at = last_content_next;
    }
    *where = result;
}

int ini_find_int(const char *text, size_t length, const char *section, const char *key,
                 int *value) {
    Location where;
    locate(text, length, section, key, &where);
    if (!where.found_key) {
        return 0;
    }
    size_t p = where.equals + 1, e = where.key_line_end;
    while (p < e && is_space(text[p])) {
        ++p;
    }
    int sign = 1, any = 0;
    long long number = 0;
    if (p < e && text[p] == '-') {
        sign = -1;
        ++p;
    }
    while (p < e && text[p] >= '0' && text[p] <= '9') {
        if (number < 100000000) {
            number = number * 10 + (text[p] - '0');
        }
        any = 1;
        ++p;
    }
    if (!any) {
        return 0;
    }
    *value = (int)(sign * number);
    return 1;
}

int ini_find_text(const char *text, size_t length, const char *section, const char *key,
                  char *value, size_t capacity) {
    Location where;
    locate(text, length, section, key, &where);
    if (!where.found_key || !capacity) {
        return 0;
    }
    size_t p = where.equals + 1, e = where.key_line_end;
    trim(text, &p, &e);
    size_t n = 0;
    while (p < e && n + 1 < capacity) {
        value[n++] = text[p++];
    }
    value[n] = 0;
    return 1;
}

typedef struct {
    char *out;
    size_t length, capacity;
    int ok;
} Output;

static void put(Output *o, const char *data, size_t size) {
    if (!o->ok || size >= o->capacity - o->length) {
        o->ok = 0;
        return;
    }
    for (size_t i = 0; i < size; ++i) {
        o->out[o->length + i] = data[i];
    }
    o->length += size;
}

static void put_text(Output *o, const char *text) {
    size_t n = 0;
    while (text[n]) {
        ++n;
    }
    put(o, text, n);
}

static const char *newline_of(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            return i && text[i - 1] == '\r' ? "\r\n" : "\n";
        }
    }
    return "\r\n";
}

size_t ini_set_value(const char *text, size_t length, const char *section, const char *key,
                     const char *value, char *out, size_t capacity) {
    if (!out || !capacity) {
        return 0;
    }
    Output o = {out, 0, capacity, 1};
    const char *nl = newline_of(text, length);
    Location where;
    locate(text, length, section, key, &where);
    if (where.found_key) {
        put(&o, text, where.equals + 1);
        put_text(&o, value);
        put(&o, text + where.key_line_end, length - where.key_line_end);
    } else if (where.found_section) {
        size_t at = where.insert_at;
        put(&o, text, at);
        if (at && text[at - 1] != '\n') {
            put_text(&o, nl); /* section was the last line without a line break */
        }
        put_text(&o, key);
        put_text(&o, "=");
        put_text(&o, value);
        put_text(&o, nl);
        put(&o, text + at, length - at);
    } else {
        put(&o, text, length);
        size_t content = skip_bom(text, length);
        if (length > content) {
            if (text[length - 1] != '\n') {
                put_text(&o, nl);
            }
            /* blank line before the new section unless the file already ends with one */
            Line line, last = {0, 0, 0};
            for (size_t at = content; next_line(text, length, at, &line); at = line.next) {
                last = line;
            }
            if (!is_blank(text, &last)) {
                put_text(&o, nl);
            }
        }
        put_text(&o, "[");
        put_text(&o, section);
        put_text(&o, "]");
        put_text(&o, nl);
        put_text(&o, key);
        put_text(&o, "=");
        put_text(&o, value);
        put_text(&o, nl);
    }
    if (!o.ok) {
        return 0;
    }
    out[o.length] = 0;
    return o.length;
}

const char *ini_format_int(int value, char buffer[12]) {
    char *cursor = buffer + 11;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    *cursor = 0;
    do {
        *--cursor = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) {
        *--cursor = '-';
    }
    return cursor;
}
