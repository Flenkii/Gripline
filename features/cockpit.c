/* Full steering animation in the cockpit - see cockpit.h.
   Port of tools/fh2_lenkrad.py (V1). Own code: Deflate (RFC 1951), Oodle1 decoding as
   documented by the public-domain liboodle notes, Granny GR2 v7 sections/relocations, quaternion
   curve formats as documented by LSLib (MIT; only the two 16-entry scale/offset tables). */
#include "extra_options.h"
#include "logging.h"
#include "support.h"
#include "cockpit.h"
#include "cockpit_view.h"

#define COCKPIT_VERSION 2
#define COCKPIT_FOLDER L"gripline_cockpit"
#define HALF_SAMPLES 121
#define SAMPLES (2 * HALF_SAMPLES)
/* The animation time holds two copies of the steering: [0, TURN_SPLIT] with the wheel shown, and
   [1 - TURN_SPLIT, 1] with the wheel shrunk to nothing (second cockpit view). The game's "Turn" value
   is moved into one of them at runtime (cockpit_patch_game). */
#define TURN_SPLIT 0.48
#define HIDDEN_SCALE 0.0001f

/* ------------------------------------------------------------------ memory, math */

static void *mem_alloc(size_t n) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
}

static void mem_free(void *p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

static double dsqrt(double x) {
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

#define PI 3.14159265358979323846

static double dsin(double x) {
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    double x2 = x * x, term = x, sum = x;
    for (int n = 1; n < 14; ++n) {
        term *= -x2 / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

static double dcos(double x) {
    return dsin(x + PI / 2);
}

static double datan(double x) {
    int negative = x < 0;
    if (negative) x = -x;
    int invert = x > 1.0;
    if (invert) x = 1.0 / x;
    double offset = 0.0;
    if (x > 0.41421356237309503) { /* atan(x) = pi/4 + atan((x - 1) / (x + 1)) */
        x = (x - 1.0) / (x + 1.0);
        offset = PI / 4;
    }
    double x2 = x * x, term = x, sum = x;
    for (int n = 1; n < 40; ++n) {
        term *= -x2;
        sum += term / (2 * n + 1);
    }
    sum += offset;
    if (invert) sum = PI / 2 - sum;
    return negative ? -sum : sum;
}

static double dacos(double w) {
    if (w >= 1.0) return 0.0;
    if (w <= -1.0) return PI;
    return 2.0 * datan(dsqrt((1.0 - w) / (1.0 + w)));
}

static double dexp(double x) {
    int k = 0;
    while (x > 0.5) { x -= 0.6931471805599453; ++k; }
    while (x < -0.5) { x += 0.6931471805599453; --k; }
    double term = 1.0, sum = 1.0;
    for (int n = 1; n < 25; ++n) {
        term *= x / n;
        sum += term;
    }
    while (k > 0) { sum *= 2.0; --k; }
    while (k < 0) { sum *= 0.5; ++k; }
    return sum;
}

static double dlog(double x) { /* x > 0 */
    int k = 0;
    while (x > 1.5) { x *= 0.5; ++k; }
    while (x < 0.75) { x *= 2.0; --k; }
    double y = (x - 1.0) / (x + 1.0), y2 = y * y, term = y, sum = y;
    for (int n = 1; n < 40; ++n) {
        term *= y2;
        sum += term / (2 * n + 1);
    }
    return 2.0 * sum + k * 0.6931471805599453;
}

static double dpow(double a, double b) {
    return a <= 0.0 ? 0.0 : dexp(dlog(a) * b);
}

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static void wr32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static float rdf(const unsigned char *p) {
    float f;
    memory_copy(&f, p, 4);
    return f;
}

/* growable byte buffer */
typedef struct {
    unsigned char *data;
    size_t length, capacity;
    int failed;
} Bytes;

static int bytes_reserve(Bytes *b, size_t more) {
    if (b->failed) return 0;
    if (b->length + more <= b->capacity) return 1;
    size_t capacity = b->capacity ? b->capacity * 2 : 4096;
    while (capacity < b->length + more) capacity *= 2;
    unsigned char *data = mem_alloc(capacity);
    if (!data) {
        b->failed = 1;
        return 0;
    }
    if (b->data) {
        memory_copy(data, b->data, b->length);
        mem_free(b->data);
    }
    b->data = data;
    b->capacity = capacity;
    return 1;
}

static void bytes_put(Bytes *b, const void *data, size_t n) {
    if (!bytes_reserve(b, n)) return;
    if (data) {
        memory_copy(b->data + b->length, data, n);
    } else {
        for (size_t i = 0; i < n; ++i) b->data[b->length + i] = 0;
    }
    b->length += n;
}

static void bytes_u32(Bytes *b, uint32_t v) {
    unsigned char t[4];
    wr32(t, v);
    bytes_put(b, t, 4);
}

static void bytes_align(Bytes *b, size_t a) {
    while (!b->failed && b->length % a) bytes_put(b, NULL, 1);
}

/* ------------------------------------------------------------------ CRC-32 */

static uint32_t crc32_of(const unsigned char *p, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ------------------------------------------------------------------ Deflate (RFC 1951) */

typedef struct {
    const unsigned char *in;
    size_t in_length, in_pos;
    uint32_t bits;
    int count;
    unsigned char *out;
    size_t out_length, out_pos;
} Inflate;

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[320];
} Huffman;

static int need_bits(Inflate *s, int n, uint32_t *value) {
    while (s->count < n) {
        if (s->in_pos >= s->in_length) return 0;
        s->bits |= (uint32_t)s->in[s->in_pos++] << s->count;
        s->count += 8;
    }
    *value = s->bits & ((1u << n) - 1u);
    s->bits >>= n;
    s->count -= n;
    return 1;
}

static int huffman_build(Huffman *h, const uint8_t *lengths, int n) {
    uint16_t offsets[16];
    for (int i = 0; i < 16; ++i) h->counts[i] = 0;
    for (int i = 0; i < n; ++i) h->counts[lengths[i]]++;
    h->counts[0] = 0;
    offsets[1] = 0;
    for (int i = 1; i < 15; ++i) offsets[i + 1] = (uint16_t)(offsets[i] + h->counts[i]);
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h->symbols[offsets[lengths[i]]++] = (uint16_t)i;
    return 1;
}

static int huffman_decode(Inflate *s, const Huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        uint32_t bit;
        if (!need_bits(s, 1, &bit)) return -1;
        code |= (int)bit;
        int count = h->counts[len];
        if (code - count < first) return h->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                           33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int inflate_codes(Inflate *s, const Huffman *lit, const Huffman *dist) {
    for (;;) {
        int symbol = huffman_decode(s, lit);
        if (symbol < 0) return 0;
        if (symbol < 256) {
            if (s->out_pos >= s->out_length) return 0;
            s->out[s->out_pos++] = (unsigned char)symbol;
        } else if (symbol == 256) {
            return 1;
        } else {
            symbol -= 257;
            if (symbol >= 29) return 0;
            uint32_t extra;
            if (!need_bits(s, length_extra[symbol], &extra)) return 0;
            size_t length = length_base[symbol] + extra;
            int d = huffman_decode(s, dist);
            if (d < 0 || d >= 30) return 0;
            if (!need_bits(s, distance_extra[d], &extra)) return 0;
            size_t distance = distance_base[d] + extra;
            if (distance > s->out_pos || length > s->out_length - s->out_pos) return 0;
            for (size_t i = 0; i < length; ++i, ++s->out_pos) s->out[s->out_pos] = s->out[s->out_pos - distance];
        }
    }
}

static int inflate_raw(const unsigned char *in, size_t in_length, unsigned char *out, size_t out_length) {
    static Huffman lit, dist;
    Inflate s = {in, in_length, 0, 0, 0, out, out_length, 0};
    uint32_t last, type;
    do {
        if (!need_bits(&s, 1, &last) || !need_bits(&s, 2, &type)) return 0;
        if (type == 0) {
            s.bits = 0;
            s.count = 0;
            if (s.in_pos + 4 > s.in_length) return 0;
            size_t n = rd16(s.in + s.in_pos);
            s.in_pos += 4;
            if (n > s.in_length - s.in_pos || n > s.out_length - s.out_pos) return 0;
            memory_copy(s.out + s.out_pos, s.in + s.in_pos, n);
            s.in_pos += n;
            s.out_pos += n;
        } else if (type == 1) {
            uint8_t lengths[320];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < 288; ++i) lengths[i] = 8;
            huffman_build(&lit, lengths, 288);
            for (i = 0; i < 30; ++i) lengths[i] = 5;
            huffman_build(&dist, lengths, 30);
            if (!inflate_codes(&s, &lit, &dist)) return 0;
        } else if (type == 2) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint32_t hlit, hdist, hclen, v;
            if (!need_bits(&s, 5, &hlit) || !need_bits(&s, 5, &hdist) || !need_bits(&s, 4, &hclen)) return 0;
            hlit += 257;
            hdist += 1;
            hclen += 4;
            uint8_t lengths[320];
            for (int i = 0; i < 19; ++i) lengths[i] = 0;
            for (uint32_t i = 0; i < hclen; ++i) {
                if (!need_bits(&s, 3, &v)) return 0;
                lengths[order[i]] = (uint8_t)v;
            }
            Huffman code;
            huffman_build(&code, lengths, 19);
            uint32_t n = 0;
            while (n < hlit + hdist) {
                int symbol = huffman_decode(&s, &code);
                if (symbol < 0) return 0;
                if (symbol < 16) {
                    lengths[n++] = (uint8_t)symbol;
                } else {
                    uint8_t repeat = 0;
                    uint32_t times;
                    if (symbol == 16) {
                        if (!n) return 0;
                        repeat = lengths[n - 1];
                        if (!need_bits(&s, 2, &times)) return 0;
                        times += 3;
                    } else if (symbol == 17) {
                        if (!need_bits(&s, 3, &times)) return 0;
                        times += 3;
                    } else {
                        if (!need_bits(&s, 7, &times)) return 0;
                        times += 11;
                    }
                    if (n + times > hlit + hdist) return 0;
                    while (times--) lengths[n++] = repeat;
                }
            }
            huffman_build(&lit, lengths, (int)hlit);
            huffman_build(&dist, lengths + hlit, (int)hdist);
            if (!inflate_codes(&s, &lit, &dist)) return 0;
        } else {
            return 0;
        }
    } while (!last);
    return s.out_pos == s.out_length;
}

/* ------------------------------------------------------------------ ZIP */

typedef struct {
    HANDLE file;
    unsigned char *directory;
    uint32_t directory_size, entries;
} Zip;

static int read_at(HANDLE file, uint64_t offset, void *buffer, uint32_t size) {
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)offset;
    DWORD got = 0;
    return SetFilePointerEx(file, at, NULL, FILE_BEGIN) && ReadFile(file, buffer, size, &got, NULL) && got == size;
}

static void zip_close(Zip *z) {
    if (z->file != INVALID_HANDLE_VALUE && z->file) CloseHandle(z->file);
    mem_free(z->directory);
    z->file = NULL;
    z->directory = NULL;
}

static int zip_open(Zip *z, const wchar_t *path) {
    static unsigned char tail[65536 + 22];
    z->directory = NULL;
    z->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (z->file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(z->file, &size) || size.QuadPart < 22) {
        zip_close(z);
        return 0;
    }
    uint32_t n = size.QuadPart < (LONGLONG)sizeof(tail) ? (uint32_t)size.QuadPart : (uint32_t)sizeof(tail);
    if (!read_at(z->file, (uint64_t)size.QuadPart - n, tail, n)) {
        zip_close(z);
        return 0;
    }
    for (int64_t i = (int64_t)n - 22; i >= 0; --i) {
        if (rd32(tail + i) == 0x06054b50u) {
            z->entries = rd16(tail + i + 10);
            z->directory_size = rd32(tail + i + 12);
            uint32_t offset = rd32(tail + i + 16);
            z->directory = mem_alloc(z->directory_size);
            if (z->directory && read_at(z->file, offset, z->directory, z->directory_size)) return 1;
            break;
        }
    }
    zip_close(z);
    return 0;
}

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c == '\\' ? '/' : c;
}

/* entry `index`'s name (not terminated) and its data; returns the data (caller frees) or NULL */
static unsigned char *zip_read(Zip *z, const unsigned char *entry, uint32_t *size) {
    uint16_t method = rd16(entry + 10);
    uint32_t packed = rd32(entry + 20), plain = rd32(entry + 24), local = rd32(entry + 42);
    unsigned char header[30];
    if (!read_at(z->file, local, header, 30) || rd32(header) != 0x04034b50u) return NULL;
    uint64_t data_at = (uint64_t)local + 30 + rd16(header + 26) + rd16(header + 28);
    unsigned char *in = mem_alloc(packed), *out = mem_alloc(plain);
    int ok = in && out && read_at(z->file, data_at, in, packed);
    if (ok) {
        if (method == 0 && packed == plain) {
            memory_copy(out, in, plain);
        } else {
            ok = method == 8 && inflate_raw(in, packed, out, plain);
        }
    }
    mem_free(in);
    if (!ok) {
        mem_free(out);
        return NULL;
    }
    *size = plain;
    return out;
}

/* ------------------------------------------------------------------ Oodle1 */

#define ONE 0x4000u
static const uint32_t replen[65] = {0,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  16,  17,
                                    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,  33,  34,
                                    35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,  50,  51,
                                    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 128, 192, 256, 512};

typedef struct {
    const unsigned char *d;
    size_t length, i;
    uint32_t sr, m, lsb;
} BitStream;

static void bs_init(BitStream *b, const unsigned char *d, size_t length, size_t i) {
    b->d = d;
    b->length = length;
    b->i = i + 1;
    b->sr = d[i] >> 1;
    b->lsb = d[i] & 1u;
    b->m = 0x80;
}

static void bs_ingest(BitStream *b) {
    while (b->m <= 0x800000u) {
        uint32_t byte = b->i < b->length ? b->d[b->i] : 0;
        b->sr = (((b->sr << 1) | b->lsb) << 7) | (byte >> 1);
        b->lsb = byte & 1u;
        b->m <<= 8;
        b->i++;
    }
}

static uint32_t bs_peek(BitStream *b, uint32_t one) {
    bs_ingest(b);
    uint32_t scale = b->m / one, v = b->sr / scale;
    return v < one - 1 ? v : one - 1;
}

static void bs_consume(BitStream *b, uint32_t minz, uint32_t span, uint32_t one) {
    uint32_t scale = b->m / one, sz = minz * scale;
    b->sr -= sz;
    if (minz < one - span) {
        b->m = span * scale;
    } else {
        b->m -= sz;
    }
}

static uint32_t bs_get(BitStream *b, uint32_t one) {
    bs_ingest(b);
    uint32_t scale = b->m / one, z = b->sr / scale;
    if (z > one - 1) z = one - 1;
    uint32_t sz = z * scale;
    b->sr -= sz;
    if (z < one - 1) {
        b->m = scale;
    } else {
        b->m -= sz;
    }
    return z;
}

typedef struct {
    uint32_t used, n, tot, hls, hns, nrw, dt, rri, ri;
    uint32_t *sym, *w, *occ;
    int bad; /* the data drove an index out of the model: stop (V1 script parity: such cars are skipped) */
} Dec;

static int dec_init(Dec *c, uint32_t alpha, uint32_t uniq) {
    uint32_t n = alpha + 2;
    c->n = n;
    c->used = uniq;
    c->sym = mem_alloc(n * 4);
    c->w = mem_alloc(n * 4);
    c->occ = mem_alloc(n * 4);
    if (!c->sym || !c->w || !c->occ) return 0;
    for (uint32_t i = 0; i < n; ++i) c->w[i] = ONE;
    c->w[0] = 0;
    c->occ[0] = 4;
    c->tot = 4;
    c->nrw = 8;
    uint32_t dt = (alpha - 1) * 32;
    c->dt = dt < 256 ? 256 : (dt > 15160 ? 15160 : dt);
    c->rri = 4;
    uint32_t ri = (alpha - 1) * 2, cap = c->dt / 2 - 32;
    ri = ri < cap ? ri : cap;
    c->ri = ri < 128 ? 128 : ri;
    c->hls = c->hns = 0;
    c->bad = 0;
    return 1;
}

static void dec_free(Dec *c) {
    mem_free(c->sym);
    mem_free(c->w);
    mem_free(c->occ);
}

static void dec_decay(Dec *c) {
    uint32_t *occ = c->occ, *sym = c->sym;
    occ[0] /= 2;
    c->tot = occ[0];
    uint32_t hw = 0, hi = 0, idx = 1;
    while (idx <= c->hls) {
        int brk = 0;
        while (occ[idx] <= 1) {
            if (idx >= c->hls) {
                occ[idx] = 0;
                c->hls--;
                brk = 1;
                break;
            }
            if (c->hls >= c->n) {
                c->bad = 1;
                return;
            }
            occ[idx] = occ[c->hls];
            occ[c->hls] = 0;
            sym[idx] = sym[c->hls];
            c->hls--;
        }
        if (brk || !occ[idx]) break;
        occ[idx] /= 2;
        c->tot += occ[idx];
        if (occ[idx] > hw) {
            hw = occ[idx];
            hi = idx;
        }
        idx++;
    }
    if (hw && hi != c->hls) {
        uint32_t t = occ[c->hls];
        occ[c->hls] = occ[hi];
        occ[hi] = t;
        t = sym[c->hls];
        sym[c->hls] = sym[hi];
        sym[hi] = t;
    }
    if (c->hls != c->used && !occ[0]) {
        occ[0] = 1;
        c->tot += 1;
    }
    for (uint32_t k = c->hls + 1; k < c->n; ++k) c->w[k] = ONE;
}

static void dec_renorm(Dec *c) {
    uint32_t q = 0x20000u / c->tot, *w = c->w, *occ = c->occ;
    w[0] = 0;
    uint32_t acc = (occ[0] * q) / 8;
    for (uint32_t idx = 1; idx <= c->hls; ++idx) {
        w[idx] = acc;
        acc += (occ[idx] * q) / 8;
    }
    if (c->rri * 2 < c->ri) {
        c->rri *= 2;
        c->nrw = c->tot + c->rri;
    } else {
        c->nrw = c->tot + c->ri;
    }
    c->hns = c->hls;
    for (uint32_t k = c->hls + 1; k < c->n; ++k) w[k] = ONE;
}

static uint32_t dec_decode(Dec *c, BitStream *bs, uint32_t alpha) {
    if (c->bad || c->hls >= c->n || c->hns >= c->n || c->tot == 0) {
        c->bad = 1;
        return 0;
    }
    if (c->tot >= c->nrw) {
        if (c->tot >= c->dt) dec_decay(c);
        if (c->bad || c->hls >= c->n || c->tot == 0) {
            c->bad = 1;
            return 0;
        }
        dec_renorm(c);
    }
    uint32_t z = bs_peek(bs, ONE), *w = c->w;
    /* bisect_right(w, z, 1, hns + 2) - 1 */
    uint32_t lo = 1, hi = c->hns + 2;
    if (hi > c->n) hi = c->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (z < w[mid]) hi = mid;
        else lo = mid + 1;
    }
    uint32_t idx = lo - 1;
    if (idx > c->hns) idx = c->hns;
    while (idx > 0 && !(idx + 1 < c->n && w[idx + 1] > z && w[idx] <= z)) idx--;
    bs_consume(bs, w[idx], w[idx + 1] - w[idx], ONE);
    c->occ[idx]++;
    c->tot++;
    if (idx) return c->sym[idx];
    if (c->hls != c->hns && bs_get(bs, 2)) {
        idx = bs_get(bs, c->hls - c->hns) + c->hns + 1;
        if (idx >= c->n) {
            c->bad = 1;
            return 0;
        }
        c->occ[idx] += 2;
        c->tot += 2;
        return c->sym[idx];
    }
    if (c->hls + 1 >= c->n) {
        c->bad = 1;
        return 0;
    }
    c->hls++;
    uint32_t s = bs_get(bs, alpha);
    c->sym[c->hls] = s;
    c->occ[c->hls] += 2;
    c->tot += 2;
    if (c->hls == c->used) {
        c->tot -= c->occ[0];
        c->occ[0] = 0;
    }
    return s;
}

typedef struct {
    uint32_t win, la, o1a, n, last;
    Dec lit[4], ln[65], o1, o4[256], o1k;
} Decomp;

static int decomp_init(Decomp *d, const uint32_t *h) {
    d->win = h[0] >> 9;
    d->la = h[0] & 0x1ff;
    uint32_t ul = h[1] & 0x1ff;
    for (int i = 0; i < 4; ++i)
        if (!dec_init(&d->lit[i], d->la, ul)) return 0;
    uint32_t rl = h[2];
    int k = 0;
    for (int g = 0; g < 4; ++g) {
        for (int i = 0; i < 16; ++i)
            if (!dec_init(&d->ln[k++], 65, (rl >> 24) & 0xff)) return 0;
        rl <<= 8;
    }
    if (!dec_init(&d->ln[k], 65, (rl >> 24) & 0xff)) return 0;
    d->o1a = d->win + 1 < 4 ? d->win + 1 : 4;
    uint32_t o4a = d->win / 4 + 1 < 256 ? d->win / 4 + 1 : 256;
    if (!dec_init(&d->o1, d->o1a, d->o1a)) return 0;
    for (int i = 0; i < 256; ++i)
        if (!dec_init(&d->o4[i], o4a, o4a)) return 0;
    if (!dec_init(&d->o1k, d->win / 1024 + 1, (h[1] >> 19) + 1)) return 0;
    d->n = d->last = 0;
    return 1;
}

static void decomp_free(Decomp *d) {
    for (int i = 0; i < 4; ++i) dec_free(&d->lit[i]);
    for (int i = 0; i < 65; ++i) dec_free(&d->ln[i]);
    dec_free(&d->o1);
    for (int i = 0; i < 256; ++i) dec_free(&d->o4[i]);
    dec_free(&d->o1k);
}

static int decomp_step(Decomp *d, BitStream *bs, unsigned char *out, size_t *pos, size_t cap) {
    uint32_t lc = dec_decode(&d->ln[d->last], bs, 65);
    if (lc > 64 || d->ln[d->last].bad) return 0;
    d->last = lc;
    if (!lc) {
        if (*pos >= cap) return 0;
        Dec *lit = &d->lit[d->n & 3];
        uint32_t v = dec_decode(lit, bs, d->la);
        if (lit->bad) return 0;
        out[(*pos)++] = (unsigned char)v;
        d->n++;
        return 1;
    }
    uint32_t len = replen[lc], ew = d->win < d->n ? d->win : d->n;
    uint32_t o1 = dec_decode(&d->o1, bs, d->o1a) + 1;
    uint32_t o1k = dec_decode(&d->o1k, bs, ew / 1024 + 1);
    if (o1k > 255 || d->o1.bad || d->o1k.bad) return 0;
    uint32_t o4a = ew / 4 + 1 < 256 ? ew / 4 + 1 : 256;
    uint32_t o4 = dec_decode(&d->o4[o1k], bs, o4a);
    if (d->o4[o1k].bad) return 0;
    size_t off = (size_t)o1k * 1024 + o4 * 4 + o1;
    d->n += len;
    if (off > *pos || len > cap - *pos) return 0;
    for (uint32_t k = 0; k < len; ++k, ++*pos) out[*pos] = out[*pos - off];
    return 1;
}

static unsigned char *oodle1(const unsigned char *data, size_t length, size_t off, const uint32_t stops[3]) {
    if (off + 37 > length) return NULL;
    uint32_t h[9];
    for (int i = 0; i < 9; ++i) h[i] = rd32(data + off + 4 * i);
    size_t total = stops[2], cap = stops[2] + 1024, pos = 0;
    unsigned char *out = mem_alloc(cap);
    Decomp *d = mem_alloc(sizeof(Decomp));
    int ok = out && d;
    BitStream bs;
    if (ok) bs_init(&bs, data, length, off + 36);
    for (int si = 0; ok && si < 3 && pos < total; ++si) {
        ok = decomp_init(d, h + si * 3);
        while (ok && pos < stops[si]) ok = decomp_step(d, &bs, out, &pos, cap);
        decomp_free(d);
    }
    mem_free(d);
    if (!ok || pos < total) {
        mem_free(out);
        return NULL;
    }
    return out;
}

/* ------------------------------------------------------------------ Granny GR2 */

#define MAX_SECTIONS 16

typedef struct {
    uint32_t src, ts, to;
} Reloc;

typedef struct {
    const unsigned char *d;
    size_t length;
    uint32_t P, MD, hsize, shoff, section_count;
    uint32_t secs[MAX_SECTIONS][11];
    uint32_t root_type[2], root_obj[2];
    unsigned char *datas[MAX_SECTIONS];
    uint32_t data_length[MAX_SECTIONS], base[MAX_SECTIONS];
    unsigned char *buf;
    uint32_t buf_length;
    Reloc *relocs[MAX_SECTIONS];
    uint32_t reloc_count[MAX_SECTIONS];
    uint64_t *pointer_keys; /* (absolute source << 32) | absolute target, sorted by source */
    uint32_t pointer_count;
} Gr2;

static void gr2_free(Gr2 *g) {
    for (uint32_t i = 0; i < MAX_SECTIONS; ++i) {
        mem_free(g->datas[i]);
        mem_free(g->relocs[i]);
    }
    mem_free(g->buf);
    mem_free(g->pointer_keys);
}

static void sort_keys(uint64_t *a, uint32_t n) { /* heap sort */
    for (uint32_t start = n / 2; start-- > 0;) {
        for (uint32_t root = start;;) {
            uint32_t child = root * 2 + 1;
            if (child >= n) break;
            if (child + 1 < n && a[child + 1] > a[child]) child++;
            if (a[root] >= a[child]) break;
            uint64_t t = a[root];
            a[root] = a[child];
            a[child] = t;
            root = child;
        }
    }
    for (uint32_t end = n; end-- > 1;) {
        uint64_t t = a[0];
        a[0] = a[end];
        a[end] = t;
        for (uint32_t root = 0;;) {
            uint32_t child = root * 2 + 1;
            if (child >= end) break;
            if (child + 1 < end && a[child + 1] > a[child]) child++;
            if (a[root] >= a[child]) break;
            t = a[root];
            a[root] = a[child];
            a[child] = t;
            root = child;
        }
    }
}

/* pointer stored at absolute buffer offset `at` (UINT32_MAX = none) */
static uint32_t gr2_p(const Gr2 *g, uint32_t at) {
    if (at == UINT32_MAX) return UINT32_MAX;
    uint32_t lo = 0, hi = g->pointer_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        uint32_t key = (uint32_t)(g->pointer_keys[mid] >> 32);
        if (key == at) return (uint32_t)g->pointer_keys[mid];
        if (key < at) lo = mid + 1;
        else hi = mid;
    }
    return UINT32_MAX;
}

static int gr2_ok(const Gr2 *g, uint32_t at, uint32_t size) {
    return at != UINT32_MAX && at <= g->buf_length && size <= g->buf_length - at;
}

static uint32_t gr2_u32(const Gr2 *g, uint32_t at) {
    return gr2_ok(g, at, 4) ? rd32(g->buf + at) : 0;
}

static const char *gr2_cstr(const Gr2 *g, uint32_t at) {
    if (!gr2_ok(g, at, 1)) return NULL;
    for (uint32_t i = at; i < g->buf_length; ++i)
        if (!g->buf[i]) return (const char *)g->buf + at;
    return NULL;
}

static int same_text(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

static int gr2_open(Gr2 *g, const unsigned char *d, size_t length) {
    static const unsigned char magic64[4] = {0xe5, 0x9b, 0x49, 0x5e};
    if (length < 0x68) return 0;
    g->d = d;
    g->length = length;
    g->P = memory_equal(d, magic64, 4) ? 8 : 4;
    g->MD = g->P == 8 ? 44 : 32;
    uint32_t fi = 0x20;
    if (rd32(d + fi) != 7) return 0;
    g->shoff = fi + rd32(d + fi + 12);
    g->section_count = rd32(d + fi + 16);
    g->hsize = rd32(d + 0x10);
    g->root_type[0] = rd32(d + fi + 20);
    g->root_type[1] = rd32(d + fi + 24);
    g->root_obj[0] = rd32(d + fi + 28);
    g->root_obj[1] = rd32(d + fi + 32);
    if (g->section_count > MAX_SECTIONS || g->shoff + g->section_count * 44 > length || g->hsize > length) return 0;
    for (uint32_t i = 0; i < g->section_count; ++i)
        for (int k = 0; k < 11; ++k) g->secs[i][k] = rd32(d + g->shoff + i * 44 + k * 4);
    uint32_t total = 0;
    for (uint32_t i = 0; i < g->section_count; ++i) {
        uint32_t *s = g->secs[i];
        g->data_length[i] = s[3];
        if (s[3] == 0) continue;
        if (s[0] == 0) {
            if ((uint64_t)s[1] + s[3] > length) return 0;
            g->datas[i] = mem_alloc(s[3]);
            if (!g->datas[i]) return 0;
            memory_copy(g->datas[i], d + s[1], s[3]);
        } else if (s[0] == 2) {
            uint32_t stops[3] = {s[5], s[6], s[3]};
            g->datas[i] = oodle1(d, length, s[1], stops);
            if (!g->datas[i]) return 0;
        } else {
            return 0;
        }
        total += s[3];
    }
    g->buf = mem_alloc(total);
    if (!g->buf) return 0;
    for (uint32_t i = 0; i < g->section_count; ++i) {
        g->base[i] = g->buf_length;
        if (g->data_length[i]) memory_copy(g->buf + g->buf_length, g->datas[i], g->data_length[i]);
        g->buf_length += g->data_length[i];
    }
    uint32_t pointers = 0;
    for (uint32_t i = 0; i < g->section_count; ++i) {
        uint32_t *s = g->secs[i];
        if ((uint64_t)s[7] + (uint64_t)s[8] * 12 > length) return 0;
        g->relocs[i] = mem_alloc((s[8] + 1) * sizeof(Reloc));
        if (!g->relocs[i]) return 0;
        for (uint32_t r = 0; r < s[8]; ++r) {
            Reloc *x = &g->relocs[i][r];
            x->src = rd32(d + s[7] + r * 12);
            x->ts = rd32(d + s[7] + r * 12 + 4);
            x->to = rd32(d + s[7] + r * 12 + 8);
            if (x->ts >= g->section_count) return 0;
        }
        g->reloc_count[i] = s[8];
        pointers += s[8];
    }
    g->pointer_keys = mem_alloc((pointers + 1) * 8);
    if (!g->pointer_keys) return 0;
    for (uint32_t i = 0; i < g->section_count; ++i)
        for (uint32_t r = 0; r < g->reloc_count[i]; ++r) {
            Reloc *x = &g->relocs[i][r];
            g->pointer_keys[g->pointer_count++] =
                ((uint64_t)(g->base[i] + x->src) << 32) | (uint32_t)(g->base[x->ts] + x->to);
        }
    sort_keys(g->pointer_keys, g->pointer_count);
    return 1;
}

typedef struct {
    uint32_t type;
    const char *name;
    uint32_t sub, count;
} Member;

/* member `index` of the type definition at `td`; 0 at the end */
static int gr2_member_at(const Gr2 *g, uint32_t td, uint32_t index, Member *m) {
    uint32_t at = td + index * g->MD;
    if (!gr2_ok(g, at, g->MD)) return 0;
    m->type = rd32(g->buf + at);
    if (!m->type) return 0;
    m->name = gr2_cstr(g, gr2_p(g, at + 4));
    m->sub = gr2_p(g, at + 4 + g->P);
    m->count = rd32(g->buf + at + 4 + 2 * g->P);
    return 1;
}

static uint32_t gr2_size_of(const Gr2 *g, uint32_t td, int depth);

static uint32_t gr2_msize(const Gr2 *g, const Member *m, int depth) {
    uint32_t P = g->P;
    switch (m->type) {
        case 1: return gr2_size_of(g, m->sub, depth + 1);
        case 2: case 8: case 22: return P;
        case 3: case 4: return 4 + P;
        case 5: return 2 * P;
        case 7: return 4 + 2 * P;
        case 9: return 68;
        case 10: case 19: case 20: return 4;
        case 11: case 12: case 13: case 14: return 1;
        case 15: case 16: case 17: case 18: case 21: return 2;
    }
    return 0;
}

static uint32_t gr2_size_of(const Gr2 *g, uint32_t td, int depth) {
    if (depth > 16 || td == UINT32_MAX) return 0;
    uint32_t size = 0;
    Member m;
    for (uint32_t i = 0; gr2_member_at(g, td, i, &m); ++i) size += gr2_msize(g, &m, depth) * (m.count ? m.count : 1);
    return size;
}

/* offset of member `name` in the type at td, UINT32_MAX if none */
static uint32_t gr2_member(const Gr2 *g, uint32_t td, const char *name, Member *found) {
    uint32_t off = 0;
    Member m;
    for (uint32_t i = 0; gr2_member_at(g, td, i, &m); ++i) {
        if (same_text(m.name, name)) {
            if (found) *found = m;
            return off;
        }
        off += gr2_msize(g, &m, 0) * (m.count ? m.count : 1);
    }
    return UINT32_MAX;
}

static int gr2_sec_of(const Gr2 *g, uint32_t abs, uint32_t *section, uint32_t *offset) {
    for (uint32_t i = 0; i < g->section_count; ++i) {
        if (g->data_length[i] && abs >= g->base[i] && abs < g->base[i] + g->data_length[i]) {
            *section = i;
            *offset = abs - g->base[i];
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ quaternions */

static void qmul(const double a[4], const double b[4], double out[4]) {
    double ax = a[0], ay = a[1], az = a[2], aw = a[3], bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

static const double ST[16] = {1.4142135,  0.70710677,  0.35355338,  0.35355338,  0.35355338,  0.17677669,
                              0.17677669, 0.17677669,  -1.4142135,  -0.70710677, -0.35355338, -0.35355338,
                              -0.35355338, -0.17677669, -0.17677669, -0.17677669};
static const double OT[16] = {-0.70710677, -0.35355338, -0.53033006, -0.17677669, 0.17677669, -0.17677669,
                              -0.088388346, 0.0,        0.70710677,  0.35355338,  0.53033006, 0.17677669,
                              -0.17677669, 0.17677669,  0.088388346, -0.0};

static void unpack_quat(uint32_t a, uint32_t b, uint32_t c, const double sc[4], const double of[4], int bits,
                        double f[4]) {
    uint32_t top = 1u << bits, mask = top - 1;
    uint32_t s1 = ((b & top) >> (bits - 1)) | (c >> bits);
    uint32_t s2 = (s1 + 1) & 3, s3 = (s2 + 1) & 3, s4 = (s3 + 1) & 3;
    double A = (a & mask) * sc[s2] + of[s2], B = (b & mask) * sc[s3] + of[s3], C = (c & mask) * sc[s4] + of[s4];
    double r = 1 - (A * A + B * B + C * C);
    double D = dsqrt(r > 0 ? r : 0);
    if (a & top) D = -D;
    f[s2] = A;
    f[s3] = B;
    f[s4] = C;
    f[s1] = D;
}

/* first and last orientation of a curve; 0 if the format is unknown or it has < 2 keys */
static int curve_ends(const Gr2 *g, uint32_t td, uint32_t obj, double first[4], double last[4]) {
    Member m0;
    if (!gr2_member_at(g, td, 0, &m0) || !m0.name || obj == UINT32_MAX) return 0;
    const char *kind = m0.name;
    const char *prefix = "CurveDataHeader_";
    for (const char *p = prefix; *p; ++p, ++kind)
        if (*kind != *p) return 0;
    if (same_text(kind, "D4nK16uC15u") || same_text(kind, "D4nK8uC7u")) {
        int wide = same_text(kind, "D4nK16uC15u");
        int bits = wide ? 15 : 7;
        double mul = wide ? 0.000030518509 : 0.0078740157;
        uint32_t so = gr2_member(g, td, "ScaleOffsetTableEntries", NULL);
        uint32_t ko = gr2_member(g, td, "KnotsControls", NULL);
        if (so == UINT32_MAX || ko == UINT32_MAX || !gr2_ok(g, obj + so, 2)) return 0;
        uint32_t sel = rd16(g->buf + obj + so);
        double sc[4], of[4];
        for (int k = 0; k < 4; ++k) {
            sc[k] = ST[(sel >> (4 * k)) & 15] * mul;
            of[k] = OT[(sel >> (4 * k)) & 15];
        }
        uint32_t count = gr2_u32(g, obj + ko), q = gr2_p(g, obj + ko + 4), es = wide ? 2 : 1;
        uint32_t n = count / 4;
        if (n < 2 || !gr2_ok(g, q, count * es)) return 0;
#define V(i) (wide ? rd16(g->buf + q + 2 * (i)) : g->buf[q + (i)])
        unpack_quat(V(n), V(n + 1), V(n + 2), sc, of, bits, first);
        unpack_quat(V(n + (n - 1) * 3), V(n + (n - 1) * 3 + 1), V(n + (n - 1) * 3 + 2), sc, of, bits, last);
#undef V
        return 1;
    }
    if (same_text(kind, "DaK32fC32f")) {
        uint32_t co = gr2_member(g, td, "Controls", NULL);
        if (co == UINT32_MAX) return 0;
        uint32_t count = gr2_u32(g, obj + co), q = gr2_p(g, obj + co + 4);
        uint32_t n = count >= 4 ? (count - 4) / 4 + 1 : 0; /* groups of 4 in range(0, count - 3, 4) */
        if (n < 2 || !gr2_ok(g, q, count * 4)) return 0;
        for (int k = 0; k < 4; ++k) {
            first[k] = rdf(g->buf + q + 4 * k);
            last[k] = rdf(g->buf + q + 16 * (n - 1) + 4 * k);
        }
        return 1;
    }
    return 0; /* D4Constant32f and others: the wheel does not turn */
}

/* ------------------------------------------------------------------ steering curve */

static void reloc_add(Reloc **list, uint32_t *count, uint32_t *capacity, uint32_t src, uint32_t ts, uint32_t to) {
    if (*count >= *capacity) {
        uint32_t cap = *capacity ? *capacity * 2 : 64;
        Reloc *n = mem_alloc(cap * sizeof(Reloc));
        if (!n) return;
        if (*list) memory_copy(n, *list, *count * sizeof(Reloc));
        mem_free(*list);
        *list = n;
        *capacity = cap;
    }
    (*list)[*count].src = src;
    (*list)[*count].ts = ts;
    (*list)[*count].to = to;
    ++*count;
}

static void gr2_put(Gr2 *g, uint32_t abs, const void *value, uint32_t size) {
    uint32_t section, offset;
    if (!gr2_ok(g, abs, size) || !gr2_sec_of(g, abs, &section, &offset)) return;
    memory_copy(g->buf + abs, value, size);
    memory_copy(g->datas[section] + offset, value, size);
}

/* Moves the knots of a curve (variant: type definition td, object obj) into [0, TURN_SPLIT] of the
   time. Every keyed Granny curve keeps its knots as Knots (floats) or quantized with
   OneOverKnotScale / OneOverKnotScaleTrunc; constant curves have none. */
static void squeeze_curve(Gr2 *g, uint32_t td, uint32_t obj, uint32_t *done, uint32_t *done_count) {
    if (td == UINT32_MAX || obj == UINT32_MAX) return;
    for (uint32_t i = 0; i < *done_count; ++i)
        if (done[i] == obj) return; /* a curve shared by two tracks: only once */
    if (*done_count < 1024) done[(*done_count)++] = obj;
    Member m;
    uint32_t off = gr2_member(g, td, "OneOverKnotScale", &m);
    if (off != UINT32_MAX && m.type == 10 && gr2_ok(g, obj + off, 4)) {
        float f = rdf(g->buf + obj + off) / (float)TURN_SPLIT;
        gr2_put(g, obj + off, &f, 4);
    }
    off = gr2_member(g, td, "OneOverKnotScaleTrunc", &m);
    if (off != UINT32_MAX && gr2_ok(g, obj + off, 2)) {
        uint16_t trunc;
        memory_copy(&trunc, g->buf + obj + off, 2);
        uint32_t bits = (uint32_t)trunc << 16;
        float f;
        memory_copy(&f, &bits, 4);
        f /= (float)TURN_SPLIT;
        memory_copy(&bits, &f, 4);
        trunc = (uint16_t)((bits + 0xffffu) >> 16); /* round the scale up: knots stay inside */
        gr2_put(g, obj + off, &trunc, 2);
    }
    off = gr2_member(g, td, "Knots", &m);
    if (off != UINT32_MAX && m.type == 3 && gr2_ok(g, obj + off, 4 + g->P)) {
        uint32_t count = gr2_u32(g, obj + off), knots = gr2_p(g, obj + off + 4);
        for (uint32_t k = 0; knots != UINT32_MAX && k < count && gr2_ok(g, knots + 4 * k, 4); ++k) {
            float f = rdf(g->buf + knots + 4 * k) * (float)TURN_SPLIT;
            gr2_put(g, knots + 4 * k, &f, 4);
        }
    }
}

/* points a curve variant (at abs, in the file) at type (dak_section, dak_offset) and the new object */
static int point_curve(Gr2 *g, uint32_t abs, uint32_t dak_section, uint32_t dak_offset, uint32_t object,
                       Reloc *relocs0, uint32_t relocs0_count) {
    uint32_t vs, vo;
    if (!gr2_sec_of(g, abs, &vs, &vo)) return 0;
    Reloc *list = vs == 0 ? relocs0 : g->relocs[vs];
    uint32_t list_count = vs == 0 ? relocs0_count : g->reloc_count[vs];
    int hit = 0;
    for (uint32_t r = 0; r < list_count; ++r) {
        if (list[r].src == vo) {
            list[r].ts = dak_section;
            list[r].to = dak_offset;
            ++hit;
        } else if (list[r].src == vo + g->P) {
            list[r].ts = 0;
            list[r].to = object;
            ++hit;
        }
    }
    return hit == 2;
}

/* GR2 with the new steering wheel curve, or NULL (why = reason). maxdeg per side. */
static unsigned char *steering_gr2(const unsigned char *src, size_t src_length, double maxdeg, double curve,
                                   size_t *out_length, const char **why) {
    static const char bone[] = "boneSteeringWheelSpindle";
    static const Gr2 zero_gr2;
    static Gr2 g;
    memory_copy(&g, &zero_gr2, sizeof(g));
    unsigned char *result = NULL;
    Bytes sec0 = {0}, out = {0};
    Reloc *relocs0 = NULL;
    uint32_t relocs0_count = 0, relocs0_capacity = 0;
    *why = "not a GR2 file";
    if (!gr2_open(&g, src, src_length)) goto done;
    uint32_t P = g.P;
    uint32_t rtd = g.base[g.root_type[0]] + g.root_type[1];
    uint32_t rob = g.base[g.root_obj[0]] + g.root_obj[1];
    Member tgm;
    uint32_t o = gr2_member(&g, rtd, "TrackGroups", &tgm);
    *why = "no track groups";
    if (o == UINT32_MAX) goto done;
    uint32_t tg_count = gr2_u32(&g, rob + o), tg_array = gr2_p(&g, rob + o + 4);
    uint32_t track = UINT32_MAX, track_oc = 0, track_ss = UINT32_MAX, dak_td = UINT32_MAX, hdr_td = UINT32_MAX;
    for (uint32_t i = 0; i < tg_count; ++i) {
        uint32_t tg = gr2_p(&g, tg_array + i * P);
        Member ttm;
        uint32_t o2 = gr2_member(&g, tgm.sub, "TransformTracks", &ttm);
        if (tg == UINT32_MAX || o2 == UINT32_MAX) continue;
        uint32_t count = gr2_u32(&g, tg + o2), array = gr2_p(&g, tg + o2 + 4);
        uint32_t es = gr2_size_of(&g, ttm.sub, 0), oc = gr2_member(&g, ttm.sub, "OrientationCurve", NULL);
        if (!es || oc == UINT32_MAX) continue;
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t e = array + k * es, vt = gr2_p(&g, e + oc);
            Member first;
            if (vt != UINT32_MAX && gr2_member_at(&g, vt, 0, &first)) {
                if (hdr_td == UINT32_MAX) hdr_td = first.sub;
                if (same_text(first.name, "CurveDataHeader_DaK32fC32f")) dak_td = vt;
            }
            if (same_text(gr2_cstr(&g, gr2_p(&g, e)), bone)) {
                track = e;
                track_oc = oc;
                track_ss = gr2_member(&g, ttm.sub, "ScaleShearCurve", NULL);
            }
        }
    }
    *why = "no steering wheel bone";
    if (track == UINT32_MAX) goto done;
    double q0[4], q1[4];
    uint32_t vt = gr2_p(&g, track + track_oc);
    *why = "the steering wheel does not turn in the original";
    if (vt == UINT32_MAX || !curve_ends(&g, vt, gr2_p(&g, track + track_oc + P), q0, q1)) goto done;
    double mid[4], qc[4], ln = 0;
    for (int k = 0; k < 4; ++k) {
        mid[k] = (q0[k] + q1[k]) / 2;
        ln += mid[k] * mid[k];
    }
    ln = dsqrt(ln);
    *why = "unexpected curve";
    if (ln < 1e-6) goto done;
    for (int k = 0; k < 4; ++k) qc[k] = mid[k] / ln;
    double inv[4] = {-qc[0], -qc[1], -qc[2], qc[3]}, rel[4];
    qmul(q0, inv, rel);
    if (rel[3] < 0)
        for (int k = 0; k < 4; ++k) rel[k] = -rel[k];
    double w = rel[3] > 1 ? 1 : (rel[3] < -1 ? -1 : rel[3]);
    double ang0 = 2 * dacos(w), s0 = 1 - w * w;
    s0 = dsqrt(s0 > 1e-12 ? s0 : 1e-12);
    double axis[3] = {rel[0] / s0, rel[1] / s0, rel[2] / s0};
    *why = "the steering wheel hardly turns in the original";
    if (ang0 * 180.0 / PI < 5) goto done;
    Member am;
    uint32_t ao = gr2_member(&g, rtd, "Animations", &am);
    *why = "no animation";
    if (ao == UINT32_MAX) goto done;
    uint32_t anim = gr2_p(&g, gr2_p(&g, rob + ao + 4));
    uint32_t dur_off = gr2_member(&g, am.sub, "Duration", NULL);
    if (anim == UINT32_MAX || dur_off == UINT32_MAX || !gr2_ok(&g, anim + dur_off, 4)) goto done;
    double dur = rdf(g.buf + anim + dur_off);
    *why = "no scale curve";
    if (track_ss == UINT32_MAX) goto done;
    /* every other track (the driver) plays in [0, TURN_SPLIT] of the time */
    static uint32_t squeezed[1024];
    uint32_t squeezed_count = 0;
    for (uint32_t i = 0; i < tg_count; ++i) {
        uint32_t tg = gr2_p(&g, tg_array + i * P);
        Member ttm;
        uint32_t o2 = gr2_member(&g, tgm.sub, "TransformTracks", &ttm);
        if (tg == UINT32_MAX || o2 == UINT32_MAX) continue;
        uint32_t count = gr2_u32(&g, tg + o2), array = gr2_p(&g, tg + o2 + 4);
        uint32_t es = gr2_size_of(&g, ttm.sub, 0);
        const char *curves[3] = {"OrientationCurve", "PositionCurve", "ScaleShearCurve"};
        for (uint32_t k = 0; es && k < count; ++k) {
            uint32_t e = array + k * es;
            if (e == track) continue;
            for (int c = 0; c < 3; ++c) {
                uint32_t co = gr2_member(&g, ttm.sub, curves[c], NULL);
                if (co != UINT32_MAX) squeeze_curve(&g, gr2_p(&g, e + co), gr2_p(&g, e + co + P), squeezed, &squeezed_count);
            }
        }
    }
    *why = "out of memory";
    bytes_put(&sec0, g.datas[0], g.data_length[0]);
    for (uint32_t r = 0; r < g.reloc_count[0]; ++r)
        reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, g.relocs[0][r].src, g.relocs[0][r].ts,
                  g.relocs[0][r].to);
    uint32_t dak_section, dak_offset;
    if (dak_td == UINT32_MAX) {
        /* define the type DaK32fC32f ourselves */
        uint32_t hs, ho;
        *why = "no curve type found";
        if (hdr_td == UINT32_MAX || !gr2_sec_of(&g, hdr_td, &hs, &ho)) goto done;
        const char *names[5] = {"Real32", "CurveDataHeader_DaK32fC32f", "Padding", "Knots", "Controls"};
        uint32_t name_at[5];
        for (int k = 0; k < 5; ++k) {
            bytes_align(&sec0, 8);
            name_at[k] = (uint32_t)sec0.length;
            bytes_put(&sec0, names[k], text_length(names[k]) + 1);
        }
        bytes_align(&sec0, 8);
        uint32_t real_td = (uint32_t)sec0.length;
#define MDEF(tp, name, has_target, tsec, toff)                                                          \
    do {                                                                                                \
        uint32_t a_ = (uint32_t)sec0.length;                                                            \
        bytes_u32(&sec0, tp);                                                                           \
        bytes_put(&sec0, NULL, 3 * P + 16);                                                             \
        reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, a_ + 4, 0, name);                        \
        if (has_target) reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, a_ + 4 + P, tsec, toff); \
    } while (0)
        MDEF(10, name_at[0], 0, 0, 0);
        bytes_put(&sec0, NULL, g.MD);
        bytes_align(&sec0, 8);
        uint32_t new_td = (uint32_t)sec0.length;
        MDEF(1, name_at[1], 1, hs, ho);
        MDEF(15, name_at[2], 0, 0, 0);
        MDEF(3, name_at[3], 1, 0, real_td);
        MDEF(3, name_at[4], 1, 0, real_td);
#undef MDEF
        bytes_put(&sec0, NULL, g.MD);
        dak_section = 0;
        dak_offset = new_td;
    } else if (!gr2_sec_of(&g, dak_td, &dak_section, &dak_offset)) {
        goto done;
    }
    bytes_align(&sec0, 8);
    uint32_t obj = (uint32_t)sec0.length;
    unsigned char head[4] = {1, 1, 0, 0};
    bytes_put(&sec0, head, 4);
    bytes_u32(&sec0, SAMPLES);
    bytes_put(&sec0, NULL, P);
    bytes_u32(&sec0, SAMPLES * 4);
    bytes_put(&sec0, NULL, P);
    bytes_align(&sec0, 8);
    uint32_t kn = (uint32_t)sec0.length;
    for (int i = 0; i < SAMPLES; ++i) {
        double part = (double)(i % HALF_SAMPLES) / (HALF_SAMPLES - 1);
        float k = (float)(dur * ((i < HALF_SAMPLES ? 0.0 : 1.0 - TURN_SPLIT) + TURN_SPLIT * part));
        bytes_put(&sec0, &k, 4);
    }
    bytes_align(&sec0, 8);
    uint32_t cn = (uint32_t)sec0.length;
    for (int i = 0; i < SAMPLES; ++i) {
        double u = 1 - 2 * (double)(i % HALF_SAMPLES) / (HALF_SAMPLES - 1);
        if (curve > 1.0) {
            double a = u < 0 ? -u : u;
            double m = 1 - dpow(1 - (a < 1.0 ? a : 1.0), 1.0 / curve);
            u = u < 0 ? -m : m;
        }
        double th = maxdeg * PI / 180.0 * u, s = dsin(th / 2);
        double r[4] = {axis[0] * s, axis[1] * s, axis[2] * s, dcos(th / 2)}, q[4];
        qmul(r, qc, q);
        for (int k = 0; k < 4; ++k) {
            float f = (float)q[k];
            bytes_put(&sec0, &f, 4);
        }
    }
    /* scale of the wheel: 1 in the first part, almost 0 in the second (3x3 matrix per knot) */
    bytes_align(&sec0, 8);
    uint32_t sobj = (uint32_t)sec0.length;
    bytes_put(&sec0, head, 4);
    bytes_u32(&sec0, 4);
    bytes_put(&sec0, NULL, P);
    bytes_u32(&sec0, 4 * 9);
    bytes_put(&sec0, NULL, P);
    bytes_align(&sec0, 8);
    uint32_t skn = (uint32_t)sec0.length;
    float scale_knots[4] = {0.0f, (float)(dur * TURN_SPLIT), (float)(dur * (1.0 - TURN_SPLIT)), (float)dur};
    bytes_put(&sec0, scale_knots, sizeof(scale_knots));
    bytes_align(&sec0, 8);
    uint32_t scn = (uint32_t)sec0.length;
    for (int k = 0; k < 4; ++k) {
        float v = k < 2 ? 1.0f : HIDDEN_SCALE, zero = 0.0f;
        for (int cell = 0; cell < 9; ++cell) bytes_put(&sec0, cell % 4 == 0 ? &v : &zero, 4);
    }
    *why = "pointer not found";
    if (sec0.failed || !point_curve(&g, track + track_oc, dak_section, dak_offset, obj, relocs0, relocs0_count) ||
        !point_curve(&g, track + track_ss, dak_section, dak_offset, sobj, relocs0, relocs0_count))
        goto done;
    reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, obj + 8, 0, kn);
    reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, obj + 8 + P + 4, 0, cn);
    reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, sobj + 8, 0, skn);
    reloc_add(&relocs0, &relocs0_count, &relocs0_capacity, sobj + 8 + P + 4, 0, scn);
    /* write: header, sections uncompressed, relocations, marshalling */
    *why = "out of memory";
    bytes_put(&out, g.d, g.hsize);
    uint32_t ss[MAX_SECTIONS][11];
    for (uint32_t i = 0; i < g.section_count; ++i) {
        for (int k = 0; k < 11; ++k) ss[i][k] = g.secs[i][k];
        bytes_align(&out, 4);
        const unsigned char *data = i == 0 ? sec0.data : g.datas[i];
        uint32_t n = i == 0 ? (uint32_t)sec0.length : g.data_length[i];
        ss[i][0] = 0;
        ss[i][1] = (uint32_t)out.length;
        ss[i][2] = ss[i][3] = n;
        if (n) bytes_put(&out, data, n);
    }
    for (uint32_t i = 0; i < g.section_count; ++i) {
        bytes_align(&out, 4);
        Reloc *rl = i == 0 ? relocs0 : g.relocs[i];
        uint32_t rc = i == 0 ? relocs0_count : g.reloc_count[i];
        ss[i][7] = (uint32_t)out.length;
        ss[i][8] = rc;
        for (uint32_t r = 0; r < rc; ++r) {
            bytes_u32(&out, rl[r].src);
            bytes_u32(&out, rl[r].ts);
            bytes_u32(&out, rl[r].to);
        }
        ss[i][9] = (uint32_t)out.length;
        uint64_t mar_end = (uint64_t)g.secs[i][9] + (uint64_t)g.secs[i][10] * 12;
        if (mar_end > g.length) goto done;
        bytes_put(&out, g.d + g.secs[i][9], g.secs[i][10] * 12);
    }
    if (out.failed) goto done;
    for (uint32_t i = 0; i < g.section_count; ++i)
        for (int k = 0; k < 11; ++k) wr32(out.data + g.shoff + i * 44 + k * 4, ss[i][k]);
    wr32(out.data + 0x24, (uint32_t)out.length);
    wr32(out.data + 0x28, crc32_of(out.data + 0x68, out.length - 0x68));
    result = out.data;
    out.data = NULL;
    *out_length = out.length;
    *why = NULL;
done:
    mem_free(out.data);
    mem_free(sec0.data);
    mem_free(relocs0);
    gr2_free(&g);
    return result;
}

/* ------------------------------------------------------------------ driver model: hide hands */

typedef struct {
    char type[5];
    uint32_t data_offset, meta_offset, meta_count;
} Blob;

static int blob_meta_is_all_ones(const unsigned char *d, size_t length, const Blob *b, const char *key) {
    for (uint32_t j = 0; j < b->meta_count; ++j) {
        size_t at = b->meta_offset + j * 8;
        if (at + 8 > length) return 0;
        const unsigned char *mt = d + at;
        if (mt[3] != (unsigned char)key[0] || mt[2] != (unsigned char)key[1] || mt[1] != (unsigned char)key[2] ||
            mt[0] != (unsigned char)key[3])
            continue;
        uint32_t vs = rd16(d + at + 4), off = rd16(d + at + 6);
        size_t v = at + off;
        return (vs >> 4) == 4 && v + 4 <= length && rd32(d + v) == 0xffffffffu;
    }
    return 0;
}

/* collapses the hand triangles of driver.modelbin in place; returns the count or -1 */
static int hide_hands(unsigned char *d, size_t length) {
    if (length < 16 || !memory_equal(d, "burG", 4)) return -1;
    uint32_t n = rd16(d + 6);
    if (16 + (size_t)n * 24 > length) return -1;
    int ib = -1, pv = -1;
    static Blob blobs[256];
    if (n > 256) return -1;
    for (uint32_t i = 0; i < n; ++i) {
        const unsigned char *b = d + 16 + i * 24;
        for (int k = 0; k < 4; ++k) blobs[i].type[k] = (char)b[3 - k];
        blobs[i].type[4] = 0;
        blobs[i].meta_count = rd16(b + 6);
        blobs[i].meta_offset = rd32(b + 8);
        blobs[i].data_offset = rd32(b + 12);
        if (ib < 0 && same_text(blobs[i].type, "IndB")) ib = (int)i;
        if (pv < 0 && same_text(blobs[i].type, "VerB") && blob_meta_is_all_ones(d, length, &blobs[i], "Id  "))
            pv = (int)i;
    }
    if (ib < 0 || pv < 0) return -1;
    uint32_t ibo = blobs[ib].data_offset, pvo = blobs[pv].data_offset;
    if (ibo + 16 > length || pvo + 16 > length || rd16(d + ibo + 8) != 2 || rd16(d + pvo + 8) != 12) return -1;
    size_t ioff = ibo + 16, poff = pvo + 16;
    uint32_t nv = rd32(d + pvo);
    if (poff + (size_t)nv * 12 > length) return -1;
    typedef struct {
        uint32_t lod, start, count;
    } Mesh;
    static Mesh meshes[256];
    uint32_t mc = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (!same_text(blobs[i].type, "Mesh")) continue;
        uint32_t o = blobs[i].data_offset;
        if (o + 37 > length) return -1;
        meshes[mc].lod = rd16(d + o + 4);
        meshes[mc].start = rd32(d + o + 25);
        meshes[mc].count = rd32(d + o + 33);
        ++mc;
    }
    int culled = 0;
    for (uint32_t a = 0; a < mc; ++a) {
        if (!(meshes[a].lod & 1)) continue;
        int seen = 0;
        for (uint32_t b = 0; b < a; ++b)
            if ((meshes[b].lod & 1) && meshes[b].start == meshes[a].start && meshes[b].count == meshes[a].count) seen = 1;
        if (seen) continue;
        uint32_t s = meshes[a].start, c = meshes[a].count;
        for (uint32_t b = 0; b < mc; ++b)
            if (!(meshes[b].lod & 1) && meshes[b].start < s + c && s < meshes[b].start + meshes[b].count) return -1;
        if (ioff + ((size_t)s + c) * 2 > length) return -1;
        for (uint32_t tri = s; tri < s + c; tri += 3) {
            unsigned char *t = d + ioff + (size_t)tri * 2;
            if (t + 6 > d + length) return -1;
            uint32_t idx[3] = {rd16(t), rd16(t + 2), rd16(t + 4)};
            if (idx[0] == idx[1] && idx[1] == idx[2]) continue;
            for (int k = 0; k < 3; ++k) {
                if (idx[k] >= nv) break;
                float x = rdf(d + poff + (size_t)idx[k] * 12), y = rdf(d + poff + (size_t)idx[k] * 12 + 4);
                if ((x > 0.22f || x < -0.22f) && y > 1.10f) {
                    t[2] = t[4] = t[0];
                    t[3] = t[5] = t[1];
                    ++culled;
                    break;
                }
            }
        }
    }
    return culled;
}

/* ------------------------------------------------------------------ files */

static wchar_t mount_path[PATH_CAPACITY];
static volatile LONG state; /* 0 off, 1 making, 2 ready, 3 failed */
static int want_rotation, want_curve;
static unsigned char *game;
static volatile LONG patched;

static size_t wlen(const wchar_t *s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

static int wjoin(wchar_t *out, const wchar_t *a, const wchar_t *b) {
    size_t na = wlen(a), nb = wlen(b);
    if (na + nb + 2 >= PATH_CAPACITY) return 0;
    memory_copy(out, a, na * 2);
    size_t n = na;
    if (n && out[n - 1] != L'\\') out[n++] = L'\\';
    memory_copy(out + n, b, (nb + 1) * 2);
    return 1;
}

static int wjoin_ascii(wchar_t *out, const wchar_t *a, const char *b) {
    static wchar_t tmp[512];
    size_t n = 0;
    for (; b[n] && n < 511; ++n) tmp[n] = b[n] == '/' ? L'\\' : (wchar_t)(unsigned char)b[n];
    tmp[n] = 0;
    return wjoin(out, a, tmp);
}

/* creates every folder of `path` (a file path) */
static void make_folders(wchar_t *path) {
    for (size_t i = 3; path[i]; ++i) {
        if (path[i] == L'\\') {
            path[i] = 0;
            CreateDirectoryW(path, NULL);
            path[i] = L'\\';
        }
    }
}

static int write_whole(wchar_t *path, const void *data, size_t length) {
    make_folders(path);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(f, data, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(f);
    return ok;
}

static int read_small(const wchar_t *path, char *out, size_t cap) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD got = 0;
    int ok = ReadFile(f, out, (DWORD)cap - 1, &got, NULL);
    CloseHandle(f);
    out[ok ? got : 0] = 0;
    return ok;
}

static void format_manifest(char *out, size_t cap, int rotation, int curve) {
    char n1[12], n2[12], n3[12];
    const char *parts[] = {"Gripline cockpit files - made by Gripline, safe to delete\r\nVersion=",
                           n1, "\r\nRotation=", n2, "\r\nCurve=", n3, "\r\n"};
    int values[3] = {COCKPIT_VERSION, rotation, curve};
    char *numbers[3] = {n1, n2, n3};
    for (int k = 0; k < 3; ++k) {
        int v = values[k], i = 0;
        char rev[12];
        do {
            rev[i++] = (char)('0' + v % 10);
            v /= 10;
        } while (v && i < 11);
        for (int j = 0; j < i; ++j) numbers[k][j] = rev[i - 1 - j];
        numbers[k][i] = 0;
    }
    size_t n = 0;
    for (unsigned p = 0; p < ARRAY_COUNT(parts); ++p)
        for (const char *s = parts[p]; *s && n + 1 < cap; ++s) out[n++] = *s;
    out[n] = 0;
}

static void log_number(const char *label, int value, const char *tail) {
    (void)tail;
    log_append_value(label, value);
}

static int make_files(void) {
    static wchar_t root[PATH_CAPACITY], media[PATH_CAPACITY], path[PATH_CAPACITY], pattern[PATH_CAPACITY];
    static char manifest[256], existing[256];
    if (!wjoin(root, mount_path, COCKPIT_FOLDER) || !wjoin(media, mount_path, L"media")) return 0;
    format_manifest(manifest, sizeof(manifest), want_rotation, want_curve);
    if (!wjoin(path, root, L"gripline_cockpit.txt")) return 0;
    if (read_small(path, existing, sizeof(existing)) && same_text(existing, manifest)) {
        log_append("COCKPIT: steering animation files are up to date.\r\n");
        return 1;
    }
    log_append("COCKPIT: making the steering animation files (once per rotation) ...\r\n");
    ULONGLONG started = GetTickCount64();
    DeleteFileW(path); /* half-made folders are never taken as ready */
    double maxdeg = want_rotation / 2.0, curve = want_curve / 10.0;
    int cars = 0, hands = 0, skipped = 0;
    /* driver models */
    static const char *const drivers[] = {"default", "defaultRace", "suited", "unsuited", "stig"};
    for (unsigned i = 0; i < ARRAY_COUNT(drivers); ++i) {
        static char rel[96];
        size_t n = 0;
        for (const char *s = "drivers\\"; *s; ++s) rel[n++] = *s;
        for (const char *s = drivers[i]; *s; ++s) rel[n++] = *s;
        for (const char *s = ".zip"; *s; ++s) rel[n++] = *s;
        rel[n] = 0;
        Zip z;
        if (!wjoin_ascii(path, media, rel) || !zip_open(&z, path)) continue;
        const unsigned char *e = z.directory;
        for (uint32_t k = 0; k < z.entries && e + 46 <= z.directory + z.directory_size; ++k) {
            uint16_t nl = rd16(e + 28);
            const char *name = (const char *)e + 46;
            static const char want[] = "driver.modelbin";
            int match = nl == sizeof(want) - 1;
            for (uint16_t c = 0; match && c < nl; ++c) match = lower(name[c]) == want[c];
            if (match) {
                uint32_t size;
                unsigned char *data = zip_read(&z, e, &size);
                int culled = data ? hide_hands(data, size) : -1;
                rel[n - 4] = 0; /* drivers\<name> */
                static char out_rel[128];
                size_t m = 0;
                for (const char *s = rel; *s; ++s) out_rel[m++] = *s;
                for (const char *s = "\\driver.modelbin"; *s; ++s) out_rel[m++] = *s;
                out_rel[m] = 0;
                if (culled > 0 && wjoin_ascii(path, root, out_rel) && write_whole(path, data, size)) ++hands;
                mem_free(data);
                break;
            }
            e += 46 + nl + rd16(e + 30) + rd16(e + 32);
        }
        zip_close(&z);
    }
    /* cars */
    if (!wjoin(pattern, media, L"cars\\*.zip")) return 0;
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            static wchar_t zip_path[PATH_CAPACITY], stem[MAX_PATH];
            size_t sl = wlen(found.cFileName);
            if (sl < 5 || sl - 4 >= MAX_PATH) continue;
            memory_copy(stem, found.cFileName, (sl - 4) * 2);
            stem[sl - 4] = 0;
            Zip z;
            if (!wjoin(zip_path, media, L"cars") || !wjoin(zip_path, zip_path, found.cFileName) ||
                !zip_open(&z, zip_path))
                continue;
            const unsigned char *e = z.directory;
            int any = 0;
            for (uint32_t k = 0; k < z.entries && e + 46 <= z.directory + z.directory_size; ++k) {
                uint16_t nl = rd16(e + 28);
                const char *name = (const char *)e + 46;
                static char low[300];
                if (nl < sizeof(low)) {
                    for (uint16_t c = 0; c < nl; ++c) low[c] = lower(name[c]);
                    low[nl] = 0;
                    static const char tail[] = "/leftrightturn.gr2";
                    int is_turn = nl >= sizeof(tail) - 1 && same_text(low + nl - (sizeof(tail) - 1), tail);
                    int in_drivers = 0;
                    for (uint16_t c = 0; c + 9 <= nl; ++c)
                        if (memory_equal(low + c, "/drivers/", 9)) in_drivers = 1;
                    if (is_turn && in_drivers) {
                        uint32_t size;
                        unsigned char *data = zip_read(&z, e, &size);
                        size_t out_length = 0;
                        const char *why = "could not read";
                        unsigned char *gr2 = data ? steering_gr2(data, size, maxdeg, curve, &out_length, &why) : NULL;
                        static wchar_t car_root[PATH_CAPACITY];
                        static char inner[300];
                        for (uint16_t c = 0; c <= nl; ++c) inner[c] = c < nl ? name[c] : 0;
                        if (gr2 && wjoin(car_root, root, L"cars") && wjoin(car_root, car_root, stem) &&
                            wjoin_ascii(path, car_root, inner) && write_whole(path, gr2, out_length)) {
                            any = 1;
                        } else {
                            ++skipped;
                        }
                        mem_free(gr2);
                        mem_free(data);
                    }
                }
                e += 46 + nl + rd16(e + 30) + rd16(e + 32);
            }
            cars += any;
            zip_close(&z);
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    if (!wjoin(path, root, L"gripline_cockpit.txt") || !write_whole(path, manifest, text_length(manifest))) return 0;
    log_number("COCKPIT: steering animation ready - cars: ", cars, "");
    log_number("COCKPIT: driver models with hidden hands: ", hands, "");
    log_number("COCKPIT: animations skipped: ", skipped, "");
    log_number("COCKPIT: seconds: ", (int)((GetTickCount64() - started) / 1000), "");
    return cars > 0 || hands > 0;
}

static DWORD WINAPI maker_thread(LPVOID unused) {
    (void)unused;
    int ok = 0;
    __try {
        ok = make_files();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    if (!ok) log_append("COCKPIT: could not make the steering animation files; original cockpit.\r\n");
    InterlockedExchange(&state, ok ? 2 : 3);
    return 0;
}

void cockpit_start(unsigned char *game_image, const wchar_t *mount_dir, int enabled, int rotation_degrees,
                   int curve_x10) {
    if (!enabled || !mount_dir || state != 0) return;
    game = game_image;
    if (wlen(mount_dir) + 32 >= PATH_CAPACITY) return;
    memory_copy(mount_path, mount_dir, (wlen(mount_dir) + 1) * 2);
    want_rotation = rotation_degrees < 180 ? 180 : (rotation_degrees > 2520 ? 2520 : rotation_degrees);
    want_curve = curve_x10 < 10 ? 10 : (curve_x10 > 60 ? 60 : curve_x10);
    InterlockedExchange(&state, 1);
    HANDLE thread = CreateThread(NULL, 0, maker_thread, NULL, 0, NULL);
    if (!thread) {
        InterlockedExchange(&state, 3);
        return;
    }
    CloseHandle(thread);
}

int cockpit_wait_ready(void) {
    /* the game may ask for its first file before cockpit_start ran: wait a little for it */
    for (int i = 0; i < 1200; ++i) {
        LONG s = state;
        if (s == 2) return 1;
        if (s == 3 || (s == 0 && i > 100)) return 0;
        Sleep(50);
    }
    return 0;
}

/* ------------------------------------------------------------------ game: steering range */

/* The game keeps the cockpit steering angle (radians) within +-90 degrees and gives the driver
   animation angle / 90 degrees. With a wheel that angle is the real one (steer * rotation / 2), so
   our curve (+-rotation/2) made the in-game wheel turn rotation/180 times too fast and stop at 90
   degrees of the real wheel. Both places load the same constant (pi/2); pointing them at our own
   rotation/2 lets the animation show exactly the game's angle - 1:1 with the real wheel. */
#define RVA_STEER_CLAMP 0x2a989du  /* vmovss xmm7, [pi/2]       presentation steering filter */
#define RVA_STEER_NORMAL 0x293ff0u /* vdivss xmm0, xmm1, [pi/2] driver animation value */
#define RVA_HALF_PI 0x2c5c58cu
/* car presentation update: Turn = (car->vtable[0x160]() + 0.999) * 0.5, then set as GameScalars:Turn */
#define RVA_TURN_CALL 0xf7c745u

/* the game's steering value -> the part of the animation to play (see TURN_SPLIT) */
static float turn_value(float value) {
    float turn = (value + 0.999f) * 0.5f;
    if (!(turn >= 0.0f)) turn = 0.0f;
    if (turn > 1.0f) turn = 1.0f;
    turn = cockpit_view_second() ? (float)(1.0 - TURN_SPLIT) + (float)TURN_SPLIT * turn : (float)TURN_SPLIT * turn;
    return turn * 2.0f - 0.999f;
}

/* call [rax+0x160] -> call stub: the original call, then turn_value */
static int hook_turn(unsigned char *page) {
    /* call [rax+0x160]; vaddss xmm1, xmm0, xmm9 */
    static const unsigned char original[10] = {0xff, 0x90, 0x60, 0x01, 0x00, 0x00, 0xc4, 0xc1, 0x7a, 0x58};
    unsigned char *site = game + RVA_TURN_CALL;
    if (!memory_equal(site, original, sizeof(original))) return 0;
    unsigned char stub[32] = {0x48, 0x83, 0xec, 0x28, 0xff, 0x90, 0x60, 0x01, 0x00, 0x00, 0x48, 0xb8};
    uint64_t function = (uint64_t)(uintptr_t)turn_value;
    memory_copy(stub + 12, &function, 8);
    static const unsigned char tail[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0xc3};
    memory_copy(stub + 20, tail, sizeof(tail));
    memory_copy(page, stub, sizeof(stub));
    unsigned char call[6] = {0xe8, 0, 0, 0, 0, 0x90};
    int32_t rel = (int32_t)(page - (site + 5));
    memory_copy(call + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect(site, 6, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memory_copy(site, call, 6);
    VirtualProtect(site, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 6);
    return 1;
}

int cockpit_patch_game(void) {
    static const struct {
        unsigned rva;
        unsigned char op[4];
    } sites[2] = {{RVA_STEER_CLAMP, {0xc5, 0xfa, 0x10, 0x3d}}, {RVA_STEER_NORMAL, {0xc5, 0xf2, 0x5e, 0x05}}};
    if (!game || InterlockedCompareExchange(&patched, 1, 0) != 0) return 0;
    for (int i = 0; i < 2; ++i) {
        const unsigned char *site = game + sites[i].rva;
        int32_t disp;
        memory_copy(&disp, site + 4, 4);
        if (!memory_equal(site, sites[i].op, 4) || site + 8 + disp != game + RVA_HALF_PI) {
            log_append("COCKPIT: game code differs - the wheel keeps the game's steering range.\r\n");
            return 0;
        }
    }
    unsigned char *page = memory_alloc_near(game, 0x1000);
    if (!page) {
        log_append("COCKPIT: no memory near the game - the wheel keeps the game's steering range.\r\n");
        return 0;
    }
    float limit = (float)(want_rotation / 2.0 * PI / 180.0);
    memory_copy(page, &limit, sizeof(limit));
    int turn_hooked = hook_turn(page + 0x40);
    DWORD old;
    VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
    log_append(turn_hooked ? "COCKPIT: second cockpit view hides the steering wheel.\r\n"
                           : "COCKPIT: game code differs - the second cockpit view keeps the steering wheel.\r\n");
    for (int i = 0; i < 2; ++i) {
        unsigned char *at = game + sites[i].rva + 4;
        int32_t disp = (int32_t)(page - (at + 4));
        if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &old)) return 0;
        memory_copy(at, &disp, 4);
        VirtualProtect(at, 4, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), game + RVA_STEER_NORMAL, RVA_STEER_CLAMP + 8 - RVA_STEER_NORMAL);
    log_number("COCKPIT: steering range set to +-", want_rotation / 2, " degrees (1:1 with the wheel).");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) float Fh2TestTurnValue(float value) {
    return turn_value(value);
}

__declspec(dllexport) int Fh2TestCockpitPatch(unsigned char *game_image, int rotation) {
    game = game_image;
    want_rotation = rotation;
    InterlockedExchange(&patched, 0);
    return cockpit_patch_game();
}

__declspec(dllexport) size_t Fh2TestCockpitGr2(const unsigned char *src, size_t length, int maxdeg, int curve_x10,
                                               unsigned char *out, size_t capacity) {
    size_t n = 0;
    const char *why;
    unsigned char *r = steering_gr2(src, length, maxdeg, curve_x10 / 10.0, &n, &why);
    if (!r) return 0;
    if (n <= capacity) memory_copy(out, r, n);
    mem_free(r);
    return n;
}

__declspec(dllexport) int Fh2TestCockpitHands(unsigned char *model, size_t length) {
    return hide_hands(model, length);
}

__declspec(dllexport) int Fh2TestInflate(const unsigned char *in, size_t n, unsigned char *out, size_t m) {
    return inflate_raw(in, n, out, m);
}

__declspec(dllexport) int Fh2TestCockpitMake(const wchar_t *mount, int rotation, int curve_x10) {
    memory_copy(mount_path, mount, (wlen(mount) + 1) * 2);
    want_rotation = rotation;
    want_curve = curve_x10;
    return make_files();
}

__declspec(dllexport) double Fh2TestMath(int which, double x) {
    switch (which) {
        case 0: return dsin(x);
        case 1: return dcos(x);
        case 2: return dacos(x);
        case 3: return dpow(x, 0.4);
        default: return dsqrt(x);
    }
}
#endif
