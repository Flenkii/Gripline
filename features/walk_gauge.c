/* V7.1 walking gauge - see walk_gauge.h.

   The shapes (dial, arcs, slanted segments, arrow, pulse line) are drawn here pixel by pixel from
   distance functions (smooth edges without supersampling); the text is drawn by GDI in white into a
   mask and then laid on in its colour, so it works on a see-through picture (look 3 has no box). */
#include <immintrin.h>
#include "gripline.h"
#include "walk_gauge.h"

static volatile LONG style = WALK_GAUGE_DEFAULT;

void walk_gauge_set_style(int value) {
    if (value < 0 || value >= WALK_GAUGE_COUNT) value = WALK_GAUGE_DEFAULT;
    InterlockedExchange(&style, value);
}

int walk_gauge_style(void) {
    return (int)style;
}

static float scale_for(int screen_h) {
    float s = (float)screen_h / 1080.0f;
    return s < 0.6f ? 0.6f : s > 2.5f ? 2.5f : s;
}

/* picture size in 1080p units */
static const int size_1080[WALK_GAUGE_COUNT][2] = {{0, 0}, {330, 380}, {630, 152}, {440, 250}};

void walk_gauge_size(int which, int screen_h, int *w, int *h) {
    float s = scale_for(screen_h);
    if (which <= 0 || which >= WALK_GAUGE_COUNT) which = WALK_GAUGE_DEFAULT;
    *w = (int)(size_1080[which][0] * s), *h = (int)(size_1080[which][1] * s);
}

int walk_gauge_margin(int screen_h) {
    return (int)(30 * scale_for(screen_h));
}

/* ------------------------------------------------------------------ maths */

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x > 0.0f ? x : 0.0f)));
}
static float absf(float x) {
    return x < 0.0f ? -x : x;
}
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
#define PI_F 3.14159265f

static float atan2f_(float y, float x) {
    float ax = absf(x), ay = absf(y), mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx == 0.0f) return 0.0f;
    float a = mn / mx, q = a * a;
    float r = ((-0.0464964749f * q + 0.15931422f) * q - 0.327622764f) * q * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = PI_F - r;
    return y < 0.0f ? -r : r;
}

static void sin_cos(float a, float *s, float *c) {
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    float sign = 1.0f; /* fold into -pi/2..pi/2: sin stays, cos flips */
    if (a > PI_F * 0.5f) a = PI_F - a, sign = -1.0f;
    else if (a < -PI_F * 0.5f) a = -PI_F - a, sign = -1.0f;
    float a2 = a * a;
    *s = a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
    *c = sign * (1.0f - a2 / 2.0f * (1.0f - a2 / 12.0f * (1.0f - a2 / 30.0f * (1.0f - a2 / 56.0f))));
}

/* ------------------------------------------------------------------ painting */

typedef struct {
    unsigned char *px; /* premultiplied BGRA */
    int w, h;
} Canvas;

/* colour of a shape: one colour, or a fade from c0 to c1 (linear along x0,y0 -> x1,y1, or round
   from x0,y0 out to the radius x1) */
typedef struct {
    float c0[3], c1[3], a0, a1;
    int kind; /* 0 one colour, 1 linear, 2 round */
    float x0, y0, x1, y1;
} Paint;

static Paint solid(const unsigned char rgb[3], float a) {
    Paint p;
    for (int k = 0; k < 3; ++k) p.c0[k] = p.c1[k] = rgb[k];
    p.a0 = p.a1 = a, p.kind = 0, p.x0 = p.y0 = p.x1 = p.y1 = 0.0f;
    return p;
}
static Paint white(float a) {
    static const unsigned char w[3] = {255, 255, 255};
    return solid(w, a);
}
static Paint black(float a) {
    static const unsigned char b[3] = {0, 0, 0};
    return solid(b, a);
}
static Paint fade(const unsigned char from[3], float a0, const unsigned char to[3], float a1, float x0, float y0,
                  float x1, float y1) {
    Paint p = solid(from, a0);
    for (int k = 0; k < 3; ++k) p.c1[k] = to[k];
    p.a1 = a1, p.kind = 1, p.x0 = x0, p.y0 = y0, p.x1 = x1, p.y1 = y1;
    return p;
}

static void over(Canvas *c, int x, int y, const float rgb[3], float a) {
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    unsigned char *p = c->px + ((size_t)y * c->w + x) * 4;
    float k = 1.0f - a;
    p[0] = (unsigned char)(rgb[2] * a + p[0] * k + 0.5f);
    p[1] = (unsigned char)(rgb[1] * a + p[1] * k + 0.5f);
    p[2] = (unsigned char)(rgb[0] * a + p[2] * k + 0.5f);
    p[3] = (unsigned char)(255.0f * a + p[3] * k + 0.5f);
}

static void paint_at(const Paint *p, float x, float y, float rgb[3], float *a) {
    float t = 0.0f;
    if (p->kind == 1) {
        float dx = p->x1 - p->x0, dy = p->y1 - p->y0, l2 = dx * dx + dy * dy;
        t = l2 > 0.0f ? ((x - p->x0) * dx + (y - p->y0) * dy) / l2 : 0.0f;
    } else if (p->kind == 2) {
        float dx = x - p->x0, dy = y - p->y0;
        t = p->x1 > 0.0f ? root(dx * dx + dy * dy) / p->x1 : 0.0f;
    }
    t = clampf(t, 0.0f, 1.0f);
    for (int k = 0; k < 3; ++k) rgb[k] = p->c0[k] + (p->c1[k] - p->c0[k]) * t;
    *a = p->a0 + (p->a1 - p->a0) * t;
}

/* signed distance of a shape at a pixel centre (< 0 inside) */
typedef float (*DistanceFn)(const void *shape, float x, float y);

/* fills a shape. soft = 1: a crisp edge; larger: a soft glow or shadow that fades out over soft px */
static void fill(Canvas *c, float bx0, float by0, float bx1, float by1, DistanceFn distance, const void *shape,
                 const Paint *p, float soft) {
    int x0 = (int)(bx0 - soft - 1.0f), y0 = (int)(by0 - soft - 1.0f), x1 = (int)(bx1 + soft + 2.0f),
        y1 = (int)(by1 + soft + 2.0f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float d = distance(shape, fx, fy);
            float cover = soft <= 1.0f ? clampf(0.5f - d, 0.0f, 1.0f) : clampf(0.5f - d / soft, 0.0f, 1.0f);
            if (soft > 1.0f) cover *= cover;
            if (cover <= 0.0f) continue;
            float rgb[3], a;
            paint_at(p, fx, fy, rgb, &a);
            over(c, x, y, rgb, a * cover);
        }
}

typedef struct {
    float x, y, r;
} Circle;
static float circle_distance(const void *shape, float x, float y) {
    const Circle *s = (const Circle *)shape;
    float dx = x - s->x, dy = y - s->y;
    return root(dx * dx + dy * dy) - s->r;
}
static void disc(Canvas *c, float x, float y, float r, const Paint *p, float soft) {
    Circle s = {x, y, r};
    fill(c, x - r, y - r, x + r, y + r, circle_distance, &s, p, soft);
}

/* a ring stroke r +- width/2 from angle a0 over span (radians, clockwise on screen), round ends */
typedef struct {
    float x, y, r, half, a0, span;
    float ex0, ey0, ex1, ey1; /* end points */
} ArcShape;
static float arc_distance(const void *shape, float x, float y) {
    const ArcShape *s = (const ArcShape *)shape;
    float dx = x - s->x, dy = y - s->y;
    if (s->span >= 2.0f * PI_F - 1e-3f) return absf(root(dx * dx + dy * dy) - s->r) - s->half;
    float t = atan2f_(dy, dx) - s->a0;
    while (t < 0.0f) t += 2.0f * PI_F;
    while (t >= 2.0f * PI_F) t -= 2.0f * PI_F;
    if (t <= s->span) return absf(root(dx * dx + dy * dy) - s->r) - s->half;
    float d0x = x - s->ex0, d0y = y - s->ey0, d1x = x - s->ex1, d1y = y - s->ey1;
    float d0 = root(d0x * d0x + d0y * d0y), d1 = root(d1x * d1x + d1y * d1y);
    return (d0 < d1 ? d0 : d1) - s->half;
}
static void arc(Canvas *c, float x, float y, float r, float width, float a0, float span, const Paint *p, float soft) {
    if (span <= 0.0f) return;
    ArcShape s = {x, y, r, width * 0.5f, a0, span, 0, 0, 0, 0};
    float sn, cs;
    sin_cos(a0, &sn, &cs);
    s.ex0 = x + cs * r, s.ey0 = y + sn * r;
    sin_cos(a0 + span, &sn, &cs);
    s.ex1 = x + cs * r, s.ey1 = y + sn * r;
    float o = r + width;
    fill(c, x - o, y - o, x + o, y + o, arc_distance, &s, p, soft);
}

/* a line with round ends */
typedef struct {
    float ax, ay, bx, by, half;
} Line;
static float line_distance(const void *shape, float x, float y) {
    const Line *s = (const Line *)shape;
    float ex = s->bx - s->ax, ey = s->by - s->ay, wx = x - s->ax, wy = y - s->ay;
    float l2 = ex * ex + ey * ey, t = l2 > 0.0f ? clampf((wx * ex + wy * ey) / l2, 0.0f, 1.0f) : 0.0f;
    float dx = wx - ex * t, dy = wy - ey * t;
    return root(dx * dx + dy * dy) - s->half;
}
static void line(Canvas *c, float ax, float ay, float bx, float by, float width, const Paint *p, float soft) {
    Line s = {ax, ay, bx, by, width * 0.5f};
    float o = width;
    fill(c, (ax < bx ? ax : bx) - o, (ay < by ? ay : by) - o, (ax > bx ? ax : bx) + o, (ay > by ? ay : by) + o,
         line_distance, &s, p, soft);
}

/* convex polygons (up to 6 corners); several of them make one shape (the arrow) */
typedef struct {
    float x[6], y[6];
    int n;
} Poly;
typedef struct {
    Poly part[2];
    int parts;
    float outline; /* > 0: only the rim, this wide */
} Shape;
static float poly_distance(const Poly *p, float x, float y) {
    float area = 0.0f;
    for (int i = 0, j = p->n - 1; i < p->n; j = i++) area += (p->x[j] - p->x[i]) * (p->y[j] + p->y[i]);
    float side = area > 0.0f ? 1.0f : -1.0f, d = -1e9f;
    for (int i = 0, j = p->n - 1; i < p->n; j = i++) {
        float ex = p->x[i] - p->x[j], ey = p->y[i] - p->y[j], l = root(ex * ex + ey * ey);
        if (l <= 0.0f) continue;
        float e = side * ((x - p->x[j]) * ey - (y - p->y[j]) * ex) / l; /* > 0 outside this edge */
        if (e > d) d = e;
    }
    return d;
}
static float shape_distance(const void *shape, float x, float y) {
    const Shape *s = (const Shape *)shape;
    float d = 1e9f;
    for (int i = 0; i < s->parts; ++i) {
        float e = poly_distance(&s->part[i], x, y);
        if (e < d) d = e;
    }
    return s->outline > 0.0f ? absf(d) - s->outline * 0.5f : d;
}
static void shape(Canvas *c, const Shape *s, const Paint *p, float soft) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < s->parts; ++i)
        for (int k = 0; k < s->part[i].n; ++k) {
            float x = s->part[i].x[k], y = s->part[i].y[k];
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    fill(c, x0, y0, x1, y1, shape_distance, s, p, soft);
}
/* a slanted box: the top edge lean px to the right of the bottom edge */
static Shape slanted(float x, float y, float w, float h, float lean) {
    Shape s;
    s.parts = 1, s.outline = 0.0f, s.part[0].n = 4;
    s.part[0].x[0] = x + lean, s.part[0].y[0] = y;
    s.part[0].x[1] = x + w, s.part[0].y[1] = y;
    s.part[0].x[2] = x + w - lean, s.part[0].y[2] = y + h;
    s.part[0].x[3] = x, s.part[0].y[3] = y + h;
    return s;
}

/* rounded box (the mode pill) */
typedef struct {
    float x, y, w, h, r;
} RoundBox;
static float round_box_distance(const void *shape, float x, float y) {
    const RoundBox *s = (const RoundBox *)shape;
    float qx = absf(x - (s->x + s->w * 0.5f)) - (s->w * 0.5f - s->r);
    float qy = absf(y - (s->y + s->h * 0.5f)) - (s->h * 0.5f - s->r);
    float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;
    return root(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - s->r;
}

/* arrow to the car: ahead = up on the screen, right = to the right */
static void arrow(Canvas *c, float x, float y, float size, float ahead, float right, const Paint *p) {
    static const float ux[4] = {0.0f, 0.72f, 0.0f, -0.72f}, uy[4] = {-1.0f, 0.8f, 0.42f, 0.8f};
    float px[4], py[4];
    for (int i = 0; i < 4; ++i) { /* turned: up -> (right, -ahead) */
        float ax = ux[i] * size, ay = uy[i] * size;
        px[i] = x + ax * ahead - ay * right;
        py[i] = y + ax * right + ay * ahead;
    }
    Shape s;
    s.parts = 2, s.outline = 0.0f;
    s.part[0].n = s.part[1].n = 3;
    int a[3] = {0, 1, 2}, b[3] = {0, 2, 3};
    for (int i = 0; i < 3; ++i) {
        s.part[0].x[i] = px[a[i]], s.part[0].y[i] = py[a[i]];
        s.part[1].x[i] = px[b[i]], s.part[1].y[i] = py[b[i]];
    }
    shape(c, &s, p, 1.0f);
}

/* ------------------------------------------------------------------ text (GDI into a mask) */

typedef HDC(WINAPI *CreateCompatibleDCFn)(HDC);
typedef HBITMAP(WINAPI *CreateDIBSectionFn)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
typedef HGDIOBJ(WINAPI *SelectObjectFn)(HDC, HGDIOBJ);
typedef BOOL(WINAPI *DeleteObjectFn)(HGDIOBJ);
typedef HFONT(WINAPI *CreateFontWFn)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                     DWORD, LPCWSTR);
typedef COLORREF(WINAPI *SetTextColorFn)(HDC, COLORREF);
typedef int(WINAPI *SetBkModeFn)(HDC, int);
typedef BOOL(WINAPI *ExtTextOutWFn)(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *);
typedef BOOL(WINAPI *GdiFlushFn)(void);
typedef BOOL(WINAPI *GetTextExtentPoint32WFn)(HDC, LPCWSTR, int, SIZE *);
typedef BOOL(WINAPI *GetTextMetricsWFn)(HDC, TEXTMETRICW *);

static struct {
    CreateCompatibleDCFn CreateCompatibleDC;
    CreateDIBSectionFn CreateDIBSection;
    SelectObjectFn SelectObject;
    DeleteObjectFn DeleteObject;
    CreateFontWFn CreateFontW;
    SetTextColorFn SetTextColor;
    SetBkModeFn SetBkMode;
    ExtTextOutWFn ExtTextOutW;
    GdiFlushFn GdiFlush;
    GetTextExtentPoint32WFn GetTextExtentPoint32W;
    GetTextMetricsWFn GetTextMetricsW;
} gdi;

static int gdi_ready(void) {
    if (gdi.GdiFlush) return 1;
    HMODULE m = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) return 0;
#define LOAD(name) gdi.name = (name##Fn)(uintptr_t)GetProcAddress(m, #name)
    LOAD(CreateCompatibleDC);
    LOAD(CreateDIBSection);
    LOAD(SelectObject);
    LOAD(DeleteObject);
    LOAD(CreateFontW);
    LOAD(SetTextColor);
    LOAD(SetBkMode);
    LOAD(ExtTextOutW);
    LOAD(GetTextExtentPoint32W);
    LOAD(GetTextMetricsW);
    LOAD(GdiFlush); /* last: it marks the table as complete */
#undef LOAD
    if (gdi.CreateCompatibleDC && gdi.CreateDIBSection && gdi.SelectObject && gdi.DeleteObject && gdi.CreateFontW &&
        gdi.SetTextColor && gdi.SetBkMode && gdi.ExtTextOutW && gdi.GetTextExtentPoint32W && gdi.GetTextMetricsW &&
        gdi.GdiFlush)
        return 1;
    gdi.GdiFlush = NULL;
    return 0;
}

/* the mask: one DIB as big as the biggest picture, kept */
static HDC mask_dc;
static HBITMAP mask_bitmap;
static unsigned char *mask; /* BGRA, the text is white on black */
static int mask_w, mask_h;
static float *blur_a; /* the shadow blur's row sums, as big as the mask */

static int mask_ready(int w, int h) {
    if (mask && mask_w >= w && mask_h >= h) return 1;
    if (w < mask_w) w = mask_w;
    if (h < mask_h) h = mask_h;
    if (!mask_dc) mask_dc = gdi.CreateCompatibleDC(NULL);
    if (!mask_dc) return 0;
    if (mask_bitmap) gdi.DeleteObject(mask_bitmap);
    mask = NULL, mask_bitmap = NULL;
    BITMAPINFO info;
    for (size_t i = 0; i < sizeof(info); ++i) ((unsigned char *)&info)[i] = 0;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    mask_bitmap = gdi.CreateDIBSection(mask_dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!mask_bitmap || !bits) return 0;
    gdi.SelectObject(mask_dc, mask_bitmap);
    mask = (unsigned char *)bits, mask_w = w, mask_h = h;
    if (blur_a) HeapFree(GetProcessHeap(), 0, blur_a);
    blur_a = (float *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * sizeof(float)); /* none: no shadows */
    gdi.SetBkMode(mask_dc, TRANSPARENT);
    gdi.SetTextColor(mask_dc, RGB(255, 255, 255));
    return 1;
}

enum { FONT_UI, FONT_DIGITS };
static HFONT make_font(int face, float px, int weight, int italic) {
    return gdi.CreateFontW(-(int)(px + 0.5f), 0, 0, 0, weight, (DWORD)italic, 0, 0, DEFAULT_CHARSET, 0, 0,
                           ANTIALIASED_QUALITY, 0, face == FONT_DIGITS ? L"Bahnschrift" : L"Segoe UI");
}

static unsigned wlen(const wchar_t *t) {
    unsigned n = 0;
    while (t[n]) ++n;
    return n;
}

static float text_width(HFONT font, const wchar_t *t) {
    SIZE size = {0, 0};
    gdi.SelectObject(mask_dc, font);
    if (!gdi.GetTextExtentPoint32W(mask_dc, t, (int)wlen(t), &size)) return (float)wlen(t) * 8.0f;
    return (float)size.cx;
}

enum { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT };
enum { AT_BASELINE, AT_MIDDLE }; /* y is the baseline, or the middle of the capital letters */

/* writes t at x, y; shadow > 0: a soft dark shadow this many px below */
static void text(Canvas *c, HFONT font, const wchar_t *t, float x, float y, int align, int at, const Paint *p,
                 float shadow) {
    unsigned n = wlen(t);
    if (!n) return;
    gdi.SelectObject(mask_dc, font);
    TEXTMETRICW tm;
    SIZE size = {0, 0};
    if (!gdi.GetTextMetricsW(mask_dc, &tm) || !gdi.GetTextExtentPoint32W(mask_dc, t, (int)n, &size)) return;
    float left = align == ALIGN_LEFT ? x : align == ALIGN_CENTRE ? x - size.cx * 0.5f : x - (float)size.cx;
    float cap = (float)(tm.tmAscent - tm.tmInternalLeading);
    float top = at == AT_BASELINE ? y - (float)tm.tmAscent : y - (float)tm.tmAscent + cap * 0.5f;
    int pad = (int)(shadow * 3.0f) + 2 + (int)(tm.tmOverhang + tm.tmHeight / 4);
    int x0 = (int)left - pad, y0 = (int)top - pad, x1 = (int)left + size.cx + pad, y1 = (int)top + tm.tmHeight + pad;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    if (x1 <= x0 || y1 <= y0) return;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) ((unsigned *)mask)[(size_t)yy * mask_w + xx] = 0;
    RECT clip = {x0, y0, x1, y1};
    gdi.ExtTextOutW(mask_dc, (int)(left + 0.5f), (int)(top + 0.5f), ETO_CLIPPED, &clip, t, n, NULL);
    gdi.GdiFlush();
#define COVER(xx, yy) ((float)mask[((size_t)(yy) * mask_w + (xx)) * 4 + 1] / 255.0f)
    if (shadow > 0.0f && blur_a) { /* blurred (box, two passes) and moved down a little */
        int r = (int)shadow, off = (int)(shadow * 0.6f + 0.5f), rw = x1 - x0, rh = y1 - y0;
        float k = 1.0f / (float)((2 * r + 1) * (2 * r + 1));
        for (int yy = 0; yy < rh; ++yy) { /* rows: the sum over 2r+1 pixels */
            float sum = 0.0f;
            for (int xx = -r; xx < rw + r; ++xx) {
                int in = xx + r, outx = xx - r - 1;
                int sx = x0 + in - off / 2, sy = y0 + yy - off;
                if (in < rw + r && sx >= x0 && sx < x1 && sy >= y0 && sy < y1) sum += COVER(sx, sy);
                sx = x0 + outx - off / 2;
                if (outx >= -r && sx >= x0 && sx < x1 && sy >= y0 && sy < y1) sum -= COVER(sx, sy);
                if (xx >= 0 && xx < rw) blur_a[(size_t)yy * rw + xx] = sum;
            }
        }
        float rgb[3] = {0, 0, 0};
        for (int xx = 0; xx < rw; ++xx) { /* columns */
            float sum = 0.0f;
            for (int yy = -r; yy < rh + r; ++yy) {
                int in = yy + r, outy = yy - r - 1;
                if (in < rh) sum += blur_a[(size_t)in * rw + xx];
                if (outy >= 0) sum -= blur_a[(size_t)outy * rw + xx];
                if (yy >= 0 && yy < rh) over(c, x0 + xx, y0 + yy, rgb, clampf(sum * k * 1.3f, 0.0f, 1.0f) * 0.6f);
            }
        }
    }
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            float cover = COVER(xx, yy);
            if (cover <= 0.0f) continue;
            float rgb[3], a;
            paint_at(p, (float)xx + 0.5f, (float)yy + 0.5f, rgb, &a);
            over(c, xx, yy, rgb, a * cover);
        }
#undef COVER
}

/* ------------------------------------------------------------------ the three looks */

typedef struct {
    float s;
    const unsigned char *light, *accent, *second;
    const WalkGaugeData *d;
    unsigned long long now;
    int value, max, step; /* speed as shown, top of the scale, numbered every step */
    float raw;            /* speed as shown, not rounded */
    const wchar_t *unit;  /* km/h or mph */
    wchar_t distance[24]; /* "34 m" */
    int close;             /* close enough to get in */
} Look;

static const wchar_t *mode_name(const Look *k, int mode) {
    static const wchar_t *const german[4] = {L"STEHT", L"GEHEN", L"RENNEN", L"DUCKEN"};
    static const wchar_t *const english[4] = {L"STANDING", L"WALKING", L"RUNNING", L"CROUCHING"};
    if (mode < 0 || mode > 3) mode = 0;
    return k->d->german ? german[mode] : english[mode];
}
static const wchar_t *get_in(const Look *k) {
    return k->d->german ? L"F / Y  EINSTEIGEN" : L"F / Y  GET IN";
}
static const wchar_t *car_word(const Look *k) {
    return k->d->german ? L"AUTO" : L"CAR";
}

static void number_text(int v, wchar_t *out) {
    wchar_t digits[12];
    unsigned d = 0, n = 0;
    unsigned u = v < 0 ? 0u : (unsigned)v;
    do digits[d++] = (wchar_t)(L'0' + u % 10); while ((u /= 10) && d < 11);
    while (d) out[n++] = digits[--d];
    out[n] = 0;
}

static void join(wchar_t *out, unsigned cap, const wchar_t *a, const wchar_t *b) {
    unsigned n = 0;
    while (*a && n + 1 < cap) out[n++] = *a++;
    while (*b && n + 1 < cap) out[n++] = *b++;
    out[n] = 0;
}

/* car distance (or "get in") right aligned at x, baseline y, the arrow in front */
static void car_line(Canvas *c, const Look *k, HFONT font, float x, float y, int with_word, const Paint *ink,
                     float shadow) {
    if (k->close) {
        Paint p = with_word ? *ink : solid(k->light, 1.0f);
        text(c, font, get_in(k), x, y, ALIGN_RIGHT, AT_BASELINE, &p, shadow);
        return;
    }
    if (k->d->distance < 0.0f) return;
    wchar_t t[48];
    join(t, 48, with_word ? car_word(k) : L"", with_word ? L"  " : L"");
    join(t, 48, t, k->distance);
    text(c, font, t, x, y, ALIGN_RIGHT, AT_BASELINE, ink, shadow);
    float w = text_width(font, t), s = k->s;
    Paint arrow_paint = solid(k->light, 1.0f);
    if (shadow > 0.0f) {
        Paint dark = black(0.55f);
        arrow(c, x - w - 15.0f * s, y - 6.0f * s + 1.5f * s, 8.5f * s, k->d->car_ahead, k->d->car_right, &dark);
    }
    arrow(c, x - w - 15.0f * s, y - 6.0f * s, 8.5f * s, k->d->car_ahead, k->d->car_right, &arrow_paint);
}

static void draw_ring(Canvas *c, const Look *k) {
    float s = k->s, R = 124.0f * s, cx = 165.0f * s, cy = 170.0f * s;
    static const unsigned char core[3] = {14, 18, 30}, edge[3] = {8, 10, 18};
    Paint back = solid(core, 0.80f);
    for (int i = 0; i < 3; ++i) back.c1[i] = edge[i];
    back.a1 = 0.55f, back.kind = 2, back.x0 = cx, back.y0 = cy, back.x1 = R + 22.0f * s;
    disc(c, cx, cy, R + 22.0f * s, &back, 1.0f);
    Paint rim = white(0.14f);
    arc(c, cx, cy, R + 22.0f * s, 1.5f * s, 0.0f, 2.0f * PI_F, &rim, 1.0f);
    float a0 = 0.75f * PI_F, span = 1.5f * PI_F;
    Paint track = white(0.10f);
    arc(c, cx, cy, R, 13.0f * s, a0, span, &track, 1.0f);
    float f = clampf(k->raw / (float)k->max, 0.0f, 1.0f);
    if (f > 0.004f) {
        Paint colour = fade(k->accent, 1.0f, k->second, 1.0f, cx - R, cy, cx + R, cy);
        Paint glow = fade(k->accent, 0.55f, k->second, 0.75f, cx - R, cy, cx + R, cy);
        arc(c, cx, cy, R, 13.0f * s + 30.0f * s, a0, span * f, &glow, 18.0f * s);
        arc(c, cx, cy, R, 13.0f * s, a0, span * f, &colour, 1.0f);
        float sn, cs;
        sin_cos(a0 + span * f, &sn, &cs);
        Paint dot = white(1.0f);
        disc(c, cx + cs * R, cy + sn * R, 5.0f * s, &dot, 1.0f);
    }
    HFONT small = make_font(FONT_UI, 15.0f * s, FW_SEMIBOLD, 0);
    for (int v = 0; v <= k->max; ++v) {
        float sn, cs;
        int major = v % k->step == 0;
        sin_cos(a0 + span * (float)v / (float)k->max, &sn, &cs);
        float r1 = R - 14.0f * s, r2 = R - (major ? 26.0f : 20.0f) * s;
        Paint tick = white(major ? 0.75f : 0.30f);
        line(c, cx + cs * r1, cy + sn * r1, cx + cs * r2, cy + sn * r2, (major ? 2.4f : 1.2f) * s, &tick, 1.0f);
        if (major) {
            wchar_t t[8];
            number_text(v, t);
            Paint label = white(0.55f);
            float r3 = R - 40.0f * s;
            text(c, small, t, cx + cs * r3, cy + sn * r3, ALIGN_CENTRE, AT_MIDDLE, &label, 0.0f);
        }
    }
    gdi.DeleteObject(small);
    HFONT big = make_font(FONT_DIGITS, 70.0f * s, FW_BOLD, 0), unit = make_font(FONT_UI, 17.0f * s, FW_SEMIBOLD, 0),
          chip = make_font(FONT_UI, 14.0f * s, FW_BOLD, 0), info = make_font(FONT_UI, 17.0f * s, FW_SEMIBOLD, 0);
    wchar_t t[8];
    number_text(k->value, t);
    Paint ink = white(1.0f), light = solid(k->light, 1.0f);
    text(c, big, t, cx, cy - 8.0f * s, ALIGN_CENTRE, AT_MIDDLE, &ink, 0.0f);
    text(c, unit, k->unit, cx, cy + 32.0f * s, ALIGN_CENTRE, AT_MIDDLE, &light, 0.0f);
    const wchar_t *mode = mode_name(k, k->d->mode);
    float mw = text_width(chip, mode) + 22.0f * s, my = cy + R * 0.66f;
    RoundBox pill = {cx - mw * 0.5f, my - 12.0f * s, mw, 24.0f * s, 12.0f * s};
    Paint pill_paint = fade(k->accent, 1.0f, k->second, 1.0f, cx - mw * 0.5f, my, cx + mw * 0.5f, my);
    fill(c, pill.x, pill.y, pill.x + pill.w, pill.y + pill.h, round_box_distance, &pill, &pill_paint, 1.0f);
    text(c, chip, mode, cx, my, ALIGN_CENTRE, AT_MIDDLE, &ink, 0.0f);
    Paint soft_ink = white(0.92f);
    car_line(c, k, info, cx + R * 0.72f, cy + R + 50.0f * s, 1, &soft_ink, 2.0f * s);
    gdi.DeleteObject(big);
    gdi.DeleteObject(unit);
    gdi.DeleteObject(chip);
    gdi.DeleteObject(info);
}

static void draw_bar(Canvas *c, const Look *k) {
    float s = k->s, w = 440.0f * s, h = 116.0f * s, x = 172.0f * s, y = 10.0f * s, lean = 26.0f * s;
    float run = clampf((k->d->speed - 3.0f) / 3.0f, 0.0f, 1.0f);
    if (run > 0.02f) /* speed streaks running past on the left */
        for (int i = 0; i < 4; ++i) {
            float yy = y + 18.0f * s + (float)i * 26.0f * s;
            float off = (float)((k->now / 4u + (unsigned long long)i * 97u) % 220u) * s;
            float lx = x - 40.0f * s - off;
            Paint streak = fade(k->light, 0.0f, k->light, 0.5f * run, lx, yy, lx + 120.0f * s, yy);
            line(c, lx, yy, lx + 120.0f * s, yy, 2.2f * s, &streak, 1.0f);
        }
    Shape body = slanted(x, y, w, h, lean), drop = slanted(x, y + 6.0f * s, w, h, lean);
    Paint shade = black(0.55f);
    shape(c, &drop, &shade, 18.0f * s);
    static const unsigned char glass[3] = {10, 12, 22};
    Paint back = solid(glass, 0.84f);
    shape(c, &body, &back, 1.0f);
    float bw = 168.0f * s;
    Shape block = slanted(x, y, bw, h, lean);
    Paint block_paint = fade(k->accent, 1.0f, k->second, 1.0f, x, y, x + bw, y + h);
    shape(c, &block, &block_paint, 1.0f);
    Shape gloss = block; /* the top half of the block, a little lighter */
    gloss.part[0].x[2] = x + bw - lean * 0.5f, gloss.part[0].y[2] = y + h * 0.5f;
    gloss.part[0].x[3] = x + lean * 0.5f, gloss.part[0].y[3] = y + h * 0.5f;
    static const unsigned char white_rgb[3] = {255, 255, 255};
    Paint gloss_paint = fade(white_rgb, 0.28f, white_rgb, 0.0f, x, y, x, y + h * 0.5f);
    shape(c, &gloss, &gloss_paint, 1.0f);
    Shape rim = body;
    rim.outline = 1.2f * s;
    Paint rim_paint = white(0.18f);
    shape(c, &rim, &rim_paint, 1.0f);
    HFONT big = make_font(FONT_DIGITS, 76.0f * s, FW_HEAVY, 1), unit = make_font(FONT_UI, 16.0f * s, FW_BOLD, 1),
          mode = make_font(FONT_UI, 21.0f * s, FW_HEAVY, 1), info = make_font(FONT_UI, 17.0f * s, FW_SEMIBOLD, 0);
    wchar_t t[8], u[8];
    number_text(k->value, t);
    unsigned n = 0;
    for (; k->unit[n] && n < 7; ++n) u[n] = k->unit[n] >= L'a' && k->unit[n] <= L'z' ? (wchar_t)(k->unit[n] - 32) : k->unit[n];
    u[n] = 0;
    Paint ink = white(1.0f), unit_ink = white(0.9f);
    text(c, big, t, x + bw - 30.0f * s, y + 78.0f * s, ALIGN_RIGHT, AT_BASELINE, &ink, 0.0f);
    text(c, unit, u, x + bw - 34.0f * s, y + 101.0f * s, ALIGN_RIGHT, AT_BASELINE, &unit_ink, 0.0f);
    int segments = 16;
    float sx = x + bw + 8.0f * s, sw = w - bw - lean - 28.0f * s, seg = sw / (float)segments;
    float f = clampf(k->raw / (float)k->max, 0.0f, 1.0f);
    for (int i = 0; i < segments; ++i) {
        float x0 = sx + (float)i * seg, sy = y + 20.0f * s, sh = 30.0f * s;
        Shape cell;
        cell.parts = 1, cell.outline = 0.0f, cell.part[0].n = 4;
        cell.part[0].x[0] = x0 + 7.0f * s, cell.part[0].y[0] = sy;
        cell.part[0].x[1] = x0 + seg - 2.0f * s, cell.part[0].y[1] = sy;
        cell.part[0].x[2] = x0 + seg - 9.0f * s, cell.part[0].y[2] = sy + sh;
        cell.part[0].x[3] = x0, cell.part[0].y[3] = sy + sh;
        if (((float)i + 0.5f) / (float)segments <= f) {
            float t_ = (float)i / (float)(segments - 1);
            unsigned char col[3];
            for (int j = 0; j < 3; ++j) {
                float v = k->accent[j] + (k->second[j] - k->accent[j]) * t_;
                col[j] = (unsigned char)(v + (255.0f - v) * 0.15f);
            }
            Paint glow = solid(col, 0.6f), lit = solid(col, 1.0f);
            shape(c, &cell, &glow, 8.0f * s);
            shape(c, &cell, &lit, 1.0f);
        } else {
            Paint off = white(0.08f);
            shape(c, &cell, &off, 1.0f);
        }
    }
    text(c, mode, mode_name(k, k->d->mode), sx + 4.0f * s, y + 88.0f * s, ALIGN_LEFT, AT_BASELINE, &ink, 0.0f);
    Paint soft_ink = white(0.88f);
    car_line(c, k, info, x + w - lean - 16.0f * s, y + 88.0f * s, 0, &soft_ink, 0.0f);
    gdi.DeleteObject(big);
    gdi.DeleteObject(unit);
    gdi.DeleteObject(mode);
    gdi.DeleteObject(info);
}

/* the pulse line: one sample every 16 ms, the newest at the right */
#define PULSE_SAMPLES 140
static float pulse[PULSE_SAMPLES];
static unsigned long long pulse_at;

static void pulse_feed(float step, unsigned long long now) {
    if (!pulse_at || now < pulse_at || now - pulse_at > 2000u) pulse_at = now;
    while (now - pulse_at >= 16u) {
        for (int i = 0; i < PULSE_SAMPLES - 1; ++i) pulse[i] = pulse[i + 1];
        pulse[PULSE_SAMPLES - 1] = step;
        pulse_at += 16u;
    }
}

static void draw_pulse(Canvas *c, const Look *k) {
    float s = k->s, right = (float)c->w - 10.0f * s, base = 150.0f * s, sh = 3.0f * s;
    HFONT unit = make_font(FONT_UI, 20.0f * s, FW_SEMIBOLD, 0), big = make_font(FONT_DIGITS, 118.0f * s, FW_LIGHT, 0),
          modes = make_font(FONT_UI, 14.0f * s, FW_BOLD, 0), info = make_font(FONT_UI, 17.0f * s, FW_SEMIBOLD, 0);
    Paint light = solid(k->light, 1.0f), ink = white(1.0f), dim = white(0.45f);
    text(c, unit, k->unit, right, base - 6.0f * s, ALIGN_RIGHT, AT_BASELINE, &light, sh);
    float uw = text_width(unit, k->unit);
    wchar_t t[8];
    number_text(k->value, t);
    text(c, big, t, right - uw - 12.0f * s, base, ALIGN_RIGHT, AT_BASELINE, &ink, sh * 1.3f);
    /* the three ways of walking, the one now lit */
    static const int order[3] = {WALK_MODE_RUN, WALK_MODE_WALK, WALK_MODE_CROUCH};
    static const wchar_t *const german[3] = {L"RENNEN", L"GEHEN", L"DUCKEN"}, *const english[3] = {L"RUN", L"WALK", L"CROUCH"};
    float mx = right;
    for (int i = 0; i < 3; ++i) {
        const wchar_t *name = k->d->german ? german[i] : english[i];
        float tw = text_width(modes, name);
        int on = k->d->mode == order[i];
        text(c, modes, name, mx, base - 112.0f * s, ALIGN_RIGHT, AT_BASELINE, on ? &ink : &dim, sh);
        if (on) line(c, mx - tw + 1.5f * s, base - 102.5f * s, mx - 1.5f * s, base - 102.5f * s, 3.0f * s, &light, 1.0f);
        mx -= tw + 18.0f * s;
    }
    /* the pulse: a spike at every footfall */
    float pw = 300.0f * s, px = right - pw, py = base + 40.0f * s, ph = 30.0f * s;
    static const unsigned char white_rgb[3] = {255, 255, 255};
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 1; i < PULSE_SAMPLES; ++i) {
            float x0 = px + pw * (float)(i - 1) / (PULSE_SAMPLES - 1), x1 = px + pw * (float)i / (PULSE_SAMPLES - 1);
            float y0 = py - pulse[i - 1] * ph, y1 = py - pulse[i] * ph;
            float t_ = (float)i / (PULSE_SAMPLES - 1), a = t_ < 0.35f ? t_ / 0.35f * 0.7f : 0.7f + (t_ - 0.35f) / 0.65f * 0.3f;
            if (pass == 0) {
                Paint glow = solid(k->accent, 0.35f * a);
                line(c, x0, y0, x1, y1, 6.0f * s, &glow, 5.0f * s);
            } else {
                Paint stroke = fade(k->light, a, white_rgb, a, px, py, right, py);
                line(c, x0, y0, x1, y1, 2.4f * s, &stroke, 1.0f);
            }
        }
    Paint soft_ink = white(0.92f);
    car_line(c, k, info, right, py + 34.0f * s, 1, &soft_ink, sh);
    gdi.DeleteObject(unit);
    gdi.DeleteObject(big);
    gdi.DeleteObject(modes);
    gdi.DeleteObject(info);
}

int walk_gauge_draw(int which, const WalkGaugeData *data, unsigned long long now, int screen_h,
                    const unsigned char light[3], const unsigned char accent[3], const unsigned char second[3],
                    unsigned char *pixels) {
    if (which <= 0 || which >= WALK_GAUGE_COUNT || !gdi_ready()) return 0;
    Canvas c;
    walk_gauge_size(which, screen_h, &c.w, &c.h);
    if (c.w <= 0 || c.h <= 0 || !mask_ready(c.w, c.h)) return 0;
    c.px = pixels;
    for (size_t i = 0, n = (size_t)c.w * c.h; i < n; ++i) ((unsigned *)pixels)[i] = 0;
    pulse_feed(data->mode == WALK_MODE_STAND ? 0.0f : data->step, now);
    Look k;
    k.s = scale_for(screen_h), k.light = light, k.accent = accent, k.second = second, k.d = data, k.now = now;
    float kmh = data->speed * 3.6f;
    k.raw = data->imperial ? kmh / 1.609344f : kmh;
    k.value = (int)(k.raw + 0.5f);
    k.max = data->imperial ? 15 : 25;
    k.step = data->imperial ? 3 : 5;
    k.unit = data->imperial ? L"mph" : L"km/h";
    k.close = data->distance >= 0.0f && data->distance <= 4.0f;
    wchar_t number[12];
    number_text((int)((data->imperial ? data->distance * 3.28084f : data->distance) + 0.5f), number);
    join(k.distance, 24, number, data->imperial ? L" ft" : L" m");
    if (which == WALK_GAUGE_RING) draw_ring(&c, &k);
    else if (which == WALK_GAUGE_BAR) draw_bar(&c, &k);
    else draw_pulse(&c, &k);
    return 1;
}

#ifdef FH2_TWEAKS_TEST
/* tests/test_walk_gauge.py and tools/render_cards.py: one picture of a look */
__declspec(dllexport) int Fh2TestWalkGauge(int which, int screen_h, int mode, int speed_x100, int distance_cm,
                                           int ahead_x1000, int right_x1000, int flags, unsigned char *pixels) {
    static const unsigned char light[3] = {110, 185, 255}, accent[3] = {38, 120, 225}, second[3] = {150, 60, 235};
    WalkGaugeData d;
    d.speed = (float)speed_x100 / 100.0f, d.mode = mode, d.distance = (float)distance_cm / 100.0f;
    d.car_ahead = (float)ahead_x1000 / 1000.0f, d.car_right = (float)right_x1000 / 1000.0f;
    d.step = (flags & 4) ? 1.0f : 0.0f;
    d.german = flags & 1, d.imperial = (flags & 2) != 0;
    return walk_gauge_draw(which, &d, 100000, screen_h, light, accent, second, pixels);
}
#endif
