/* Gripline notifications drawn into the game picture - see notify.h.

   How: azimuth.exe imports CreateDXGIFactory1/2. Gripline swaps those imports (pointer swap
   at process start, like the gamepad import) and wraps the factory's CreateSwapChainForHwnd /
   CreateSwapChain. For FH2's swap chain the "device" argument is its D3D12 command queue.
   The swap chain's Present is then wrapped: only while a card is visible, Gripline records one
   small command list on that queue (a textured quad per card, alpha-blended into the back
   buffer) right before the game's Present. The cards are drawn with GDI into a bitmap, which
   is uploaded as a texture whenever the set of cards changes. Nothing runs while no card is
   shown. Any failure switches the overlay off for the session (the log says why); the
   messages still go to the log. */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include "gripline.h"
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "logging.h"
#include "support.h"
#include "notify.h"
#include "gripline_logo.h"
#include "gripline_symbol.h"

#define MAX_CARDS 3
#define CARD_MS 5000u
#define STARTUP_CARD_MS 15000u /* start cards (start, key menu, mods found, version check) stay 15 s (Marvin, V7) */
#define FADE_IN_MS 250u
#define FADE_OUT_MS 500u
#define TEXT_CAPACITY 96

typedef struct {
    wchar_t line1[TEXT_CAPACITY], line2[TEXT_CAPACITY];
    ULONGLONG shown; /* tick of the first frame it was drawn in; 0 = not shown yet */
    ULONGLONG hold; /* ms fully visible before the fade-out */
    unsigned id;
    unsigned tag; /* notify_show_step: the card a later step replaces (0 = none) */
    int color; /* fixed NOTIFY_COLOR_* of this card, -1 = the colour from the settings */
    wchar_t badge[24]; /* line above the text, e.g. WARNUNG; empty = none (the logo carries the name) */
} Card;

static SRWLOCK lock = SRWLOCK_INIT;
static Card cards[MAX_CARDS]; /* [0] = newest */
static unsigned card_count, next_id = 1;
static volatile LONG cards_changed;
static volatile LONG enabled = 1, broken;

/* card colours (NOTIFY_COLOR_*): bar, and the light tone of the logo and the GRIPLINE title; R, G, B */
static const unsigned char palette[NOTIFY_COLOR_COUNT][2][3] = {
    {{38, 120, 225}, {110, 185, 255}},  /* blue (original) */
    {{215, 45, 45}, {255, 135, 130}},   /* red */
    {{40, 165, 70}, {125, 230, 145}},   /* green */
    {{135, 70, 210}, {195, 155, 255}},  /* purple */
    {{235, 120, 20}, {255, 180, 100}},  /* orange */
    {{225, 185, 20}, {255, 230, 110}},  /* yellow */
    {{225, 60, 150}, {255, 150, 205}},  /* pink */
    {{215, 220, 228}, {255, 255, 255}}, /* white */
};
static volatile LONG color;
/* Overdrive: second colour of the two-colour fade, per card colour (R, G, B) */
static const unsigned char palette_second[NOTIFY_COLOR_COUNT][3] = {
    {150, 60, 235},  /* blue -> violet */
    {255, 120, 30},  /* red -> orange */
    {20, 190, 170},  /* green -> teal */
    {235, 70, 170},  /* purple -> pink */
    {235, 45, 70},   /* orange -> red */
    {255, 135, 20},  /* yellow -> orange */
    {140, 70, 230},  /* pink -> purple */
    {140, 165, 205}, /* white -> steel blue */
};
static volatile LONG card_style = NOTIFY_STYLE_OVERDRIVE;

static wchar_t panel_title[TEXT_CAPACITY];
static NotifyPanelRow panel_rows[NOTIFY_PANEL_ROWS];
static volatile LONG panel_count; /* 0 = panel off */
static volatile LONG panel_at_top; /* key menu: top right instead of centred */
static unsigned panel_id; /* changes with every notify_panel_set: the panel is drawn again */

/* ------------------------------------------------------------------ public */

static void utf8_line(const char *text, size_t length, wchar_t *out) {
    int n = length ? MultiByteToWideChar(CP_UTF8, 0, text, (int)length, out, TEXT_CAPACITY - 1) : 0;
    out[n > 0 ? n : 0] = 0;
}

#define MAX_PENDING 6
#define DEFER_MS 4000 /* after the game's first picture, so the card is really seen */
typedef struct {
    char text[160];
    NotifyTextFn make;
} Pending;
static Pending pending[MAX_PENDING];
static volatile LONG pending_count;
static ULONGLONG first_present;

static void show_card_full(const char *text, ULONGLONG hold, unsigned tag, int logged, int color, const char *badge);
#define show_card_logged(text, hold, tag, logged) show_card_full(text, hold, tag, logged, -1, NULL)
#define show_card(text, hold, tag) show_card_logged(text, hold, tag, 1)

static void defer(const char *text, NotifyTextFn make) {
    AcquireSRWLockExclusive(&lock);
    if (pending_count < MAX_PENDING) {
        Pending *p = &pending[pending_count];
        unsigned n = 0;
        while (text && text[n] && n < sizeof(p->text) - 1) {
            p->text[n] = text[n];
            ++n;
        }
        p->text[n] = 0;
        p->make = make;
        InterlockedIncrement(&pending_count);
    }
    ReleaseSRWLockExclusive(&lock);
}

void notify_show_deferred(const char *text) {
    if (text) defer(text, NULL);
}

void notify_show_deferred_call(NotifyTextFn make) {
    if (make) defer(NULL, make);
}

static void flush_pending(void) {
    static Pending local[MAX_PENDING];
    LONG count;
    AcquireSRWLockExclusive(&lock);
    count = pending_count;
    for (LONG i = 0; i < count; ++i) local[i] = pending[i];
    InterlockedExchange(&pending_count, 0);
    ReleaseSRWLockExclusive(&lock);
    for (LONG i = count; i > 0; --i) { /* the first one ends up on top */
        Pending *p = &local[i - 1];
        if (p->make && !p->make(p->text, sizeof(p->text))) continue;
        show_card(p->text, STARTUP_CARD_MS, 0);
    }
}

void notify_show(const char *text) {
    show_card(text, CARD_MS, 0);
}

void notify_show_step(const char *text, unsigned tag, unsigned hold_ms) {
    show_card(text, hold_ms, tag);
}

void notify_show_live(const char *text, unsigned tag, unsigned hold_ms) {
    show_card_logged(text, hold_ms, tag, 0);
}

void notify_show_tinted(const char *text, int color, const char *badge, int long_hold) {
    if (color < 0 || color >= NOTIFY_COLOR_COUNT) color = -1;
    show_card_full(text, long_hold ? STARTUP_CARD_MS : CARD_MS, 0, 1, color, badge);
}

static void show_card_full(const char *text, ULONGLONG hold, unsigned tag, int logged, int color, const char *badge) {
    if (!text) return;
    if (logged && badge && *badge) {
        log_append("[");
        log_append(badge);
        log_append("] ");
    }
    if (logged) log_append("GRIPLINE: ");
    for (const char *p = text; *p && logged; ++p) {
        char one[2] = {*p == '\n' ? ' ' : *p, 0};
        if (*p == '\n') {
            log_append(" - ");
        } else {
            log_append(one);
        }
    }
    if (logged) log_append("\r\n");
    size_t first = 0;
    while (text[first] && text[first] != '\n') ++first;
    Card card;
    utf8_line(text, first, card.line1);
    utf8_line(text[first] ? text + first + 1 : text + first, text[first] ? text_length(text + first + 1) : 0,
              card.line2);
    card.shown = 0;
    card.hold = hold;
    card.tag = tag;
    card.color = color;
    card.badge[0] = 0;
    if (badge && *badge) {
        wchar_t wide[TEXT_CAPACITY];
        utf8_line(badge, text_length(badge), wide);
        unsigned i = 0;
        for (const wchar_t *b = wide; *b && i + 1 < sizeof(card.badge) / sizeof(card.badge[0]); ++b) card.badge[i++] = *b;
        card.badge[i] = 0;
    }
    AcquireSRWLockExclusive(&lock);
    if (tag && card_count && cards[0].tag == tag) { /* the next step of the same card: new text in place */
        if (cards[0].shown) card.hold += GetTickCount64() - cards[0].shown;
        card.shown = cards[0].shown;
        card.id = next_id++; /* new id: the text is drawn again */
        cards[0] = card;
        InterlockedExchange(&cards_changed, 1);
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    card.id = next_id++;
    for (unsigned i = MAX_CARDS - 1; i > 0; --i) cards[i] = cards[i - 1];
    cards[0] = card;
    if (card_count < MAX_CARDS) ++card_count;
    InterlockedExchange(&cards_changed, 1);
    ReleaseSRWLockExclusive(&lock);
}

void notify_set_color(int index) {
    if (index < 0 || index >= NOTIFY_COLOR_COUNT || InterlockedExchange(&color, index) == index) return;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < card_count; ++i) cards[i].id = next_id++; /* drawn again in the new colour */
    ++panel_id;
    InterlockedExchange(&cards_changed, 1);
    ReleaseSRWLockExclusive(&lock);
}

void notify_set_style(int style) {
    if (style < 0 || style >= NOTIFY_STYLE_COUNT || InterlockedExchange(&card_style, style) == style) return;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < card_count; ++i) cards[i].id = next_id++; /* drawn again in the new look */
    ++panel_id;
    InterlockedExchange(&cards_changed, 1);
    ReleaseSRWLockExclusive(&lock);
}

int notify_style(void) {
    return (int)card_style;
}

int notify_color(void) {
    return (int)color;
}

void notify_set_enabled(int on) {
    InterlockedExchange(&enabled, on ? 1 : 0);
}


int notify_enabled(void) {
    return (int)enabled;
}

void notify_panel_set(const wchar_t *title, const NotifyPanelRow *rows, unsigned count) {
    notify_panel_show(title, rows, count, 0);
}

void notify_panel_show(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, int at_top) {
    if (count > NOTIFY_PANEL_ROWS) count = NOTIFY_PANEL_ROWS;
    if (!rows) count = 0;
    AcquireSRWLockExclusive(&lock);
    unsigned n = 0;
    while (title && title[n] && n < TEXT_CAPACITY - 1) {
        panel_title[n] = title[n];
        ++n;
    }
    panel_title[n] = 0;
    for (unsigned i = 0; i < count; ++i) panel_rows[i] = rows[i];
    ++panel_id;
    InterlockedExchange(&panel_at_top, at_top ? 1 : 0);
    InterlockedExchange(&panel_count, (LONG)count);
    InterlockedExchange(&cards_changed, 1);
    ReleaseSRWLockExclusive(&lock);
}

int notify_panel_visible(void) {
    return panel_count != 0;
}

/* ------------------------------------------------------------------ GDI card bitmap */

typedef HDC(WINAPI *CreateCompatibleDCFn)(HDC);
typedef HBITMAP(WINAPI *CreateDIBSectionFn)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
typedef HGDIOBJ(WINAPI *SelectObjectFn)(HDC, HGDIOBJ);
typedef BOOL(WINAPI *DeleteObjectFn)(HGDIOBJ);
typedef BOOL(WINAPI *DeleteDCFn)(HDC);
typedef HFONT(WINAPI *CreateFontWFn)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                     DWORD, DWORD, LPCWSTR);
typedef COLORREF(WINAPI *SetTextColorFn)(HDC, COLORREF);
typedef int(WINAPI *SetBkModeFn)(HDC, int);
typedef BOOL(WINAPI *ExtTextOutWFn)(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *);
typedef BOOL(WINAPI *GdiFlushFn)(void);

static struct {
    CreateCompatibleDCFn CreateCompatibleDC;
    CreateDIBSectionFn CreateDIBSection;
    SelectObjectFn SelectObject;
    DeleteObjectFn DeleteObject;
    DeleteDCFn DeleteDC;
    CreateFontWFn CreateFontW;
    SetTextColorFn SetTextColor;
    SetBkModeFn SetBkMode;
    ExtTextOutWFn ExtTextOutW;
    GdiFlushFn GdiFlush;
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
    LOAD(DeleteDC);
    LOAD(CreateFontW);
    LOAD(SetTextColor);
    LOAD(SetBkMode);
    LOAD(ExtTextOutW);
    LOAD(GdiFlush);
#undef LOAD
    return gdi.CreateCompatibleDC && gdi.CreateDIBSection && gdi.SelectObject && gdi.DeleteObject &&
           gdi.DeleteDC && gdi.CreateFontW && gdi.SetTextColor && gdi.SetBkMode && gdi.ExtTextOutW &&
           gdi.GdiFlush;
}

/* layout in pixels for the current screen height */
typedef struct {
    int width, height, gap, margin, radius, icon, pad, bar;
    int glow;    /* V7: the cards glow out this far; width/height include it, gap/margin are shrunk by it */
    float scale; /* screen height / 1080 */
    int style;   /* NOTIFY_STYLE_* this layout was made for */
    int skew;    /* Overdrive: the left/right edges lean over this many pixels (a racing banner) */
    int panel_header, panel_row, panel_height; /* key list panel; panel_height = all rows */
} Layout;

static Layout layout_for(UINT screen_height) {
    float s = (float)screen_height / 1080.0f;
    if (s < 0.6f) s = 0.6f;
    if (s > 2.5f) s = 2.5f;
    Layout l;
    l.scale = s;
    l.style = (int)card_style;
    l.glow = l.style == NOTIFY_STYLE_CLASSIC ? 0 : l.style == NOTIFY_STYLE_OVERDRIVE ? (int)(12 * s) : (int)(9 * s);
    l.width = (int)(430 * s) + 2 * l.glow;
    l.height = (int)(88 * s) + 2 * l.glow;
    /* visible gap between two cards = gap + 2 * glow: at least the glow plus a little, so the next
       card's glow and shadow never fall onto the card above (they overlapped by 2-4 px before) */
    int visible_gap = (int)(10 * s) > l.glow + (int)(4 * s) ? (int)(10 * s) : l.glow + (int)(4 * s);
    l.gap = visible_gap - 2 * l.glow;
    l.margin = (int)(28 * s) - l.glow;
    l.radius = (int)(12 * s);
    l.icon = l.style == NOTIFY_STYLE_CLASSIC ? (int)(58 * s) : (int)(70 * s);
    l.pad = (int)(14 * s);
    l.bar = (int)(5 * s) > 2 ? (int)(5 * s) : 2;
    l.skew = 0;
    if (l.style == NOTIFY_STYLE_OVERDRIVE) {
        l.bar = (int)(6 * s) > 3 ? (int)(6 * s) : 3;
        l.skew = (int)(18 * s);
        l.width += l.skew; /* the lean takes room; the text keeps the width it has in the other looks */
    }
    l.panel_header = (int)(60 * s);
    l.panel_row = (int)(20 * s);
    l.panel_height = l.panel_header + NOTIFY_PANEL_ROWS * l.panel_row + l.pad + 2 * l.glow;
    return l;
}

static int text_width_chars(const wchar_t *t) {
    int n = 0;
    while (t[n]) ++n;
    return n;
}

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* rounded-rectangle coverage of pixel (x, y) in a w*h card */
static float card_coverage(int x, int y, int w, int h, int r) {
    float px = (float)x + 0.5f, py = (float)y + 0.5f;
    float cx = px < r ? (float)r : (px > w - r ? (float)(w - r) : px);
    float cy = py < r ? (float)r : (py > h - r ? (float)(h - r) : py);
    float dx = px - cx, dy = py - cy;
    float d2 = dx * dx + dy * dy;
    if (d2 <= (r - 1.0f) * (r - 1.0f)) return 1.0f;
    if (d2 >= (r + 0.0f) * (r + 0.0f)) return 0.0f;
    /* thin edge: linear falloff over one pixel (sqrt-free approximation) */
    float inner = (r - 1.0f) * (r - 1.0f), outer = (float)r * r;
    return clamp01((outer - d2) / (outer - inner));
}

/* Draws every card into a top-down BGRA bitmap (premultiplied alpha), one row per card. */
/* ------------------------------------------------------------------ card look (V7)
   Every card / the panel sits in its bitmap with a margin of l->glow pixels: there the card colour
   glows out softly. Inside: dark navy that takes on the card colour from the left, light from the
   colour bar, a glassy sheen at the top, a rim that is bright at the top, and the Gripline logo
   (with its name, so there is no extra GRIPLINE heading) on a soft light. */

static unsigned char mix8(int a, int b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return (unsigned char)(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v + 0.5f);
}

static float root(float v) {
    if (v <= 0.0f) return 0.0f;
    float r = v > 1.0f ? v * 0.5f : 1.0f;
    for (int i = 0; i < 8; ++i) r = 0.5f * (r + v / r);
    return r;
}

/* ------------------------------------------------------------------ Overdrive look
   A bright racing banner instead of a dark box: the card leans forward (slanted sides), it is filled
   with a fade between two colours (blue -> violet ...), a glossy lacquer highlight cuts diagonally
   across the top, speed streaks run to the right, a white light bar sits at the leading edge, and
   it floats on a dark drop shadow plus a glow in its colours. */

static const unsigned char *second_colour(const unsigned char (*tone)[3]) {
    for (int i = 0; i < NOTIFY_COLOR_COUNT; ++i)
        if (palette[i] == tone) return palette_second[i];
    return palette_second[0];
}

/* left edge of the slanted box at row y (0 = top) */
static float lean_left(const Layout *l, int ch, float y) {
    return (float)l->skew * (1.0f - y / (float)ch);
}

/* 1 when white text would be hard to read on this card colour (yellow, white) */
static int over_light_card(const unsigned char (*tone)[3]) {
    const unsigned char *b = second_colour(tone);
    float lum = 0.299f * (tone[0][0] + b[0]) * 0.5f + 0.587f * (tone[0][1] + b[1]) * 0.5f +
                0.114f * (tone[0][2] + b[2]) * 0.5f;
    return lum > 150.0f;
}

static void paint_overdrive_back(unsigned char *px, int w, int x0, int y0, int cw, int ch, int body_from,
                                 const Layout *l, const unsigned char (*tone)[3]) {
    const unsigned char *b2 = second_colour(tone);
    const unsigned char one[3] = {tone[0][2], tone[0][1], tone[0][0]}; /* B, G, R */
    const unsigned char two[3] = {b2[2], b2[1], b2[0]};
    const unsigned char navy[3] = {30, 18, 12};
    float s = l->scale;
    float span = (float)(cw - l->skew);
    /* speed streaks: row (fraction of the height), start (fraction of the width), thickness */
    static const float streaks[][3] = {{0.22f, 0.62f, 1.0f}, {0.30f, 0.80f, 1.0f}, {0.71f, 0.55f, 2.0f},
                                       {0.80f, 0.72f, 1.0f}, {0.87f, 0.48f, 1.0f}};
    int banner_h = body_from > 0 ? body_from : ch; /* panel: colour only in the header */
    for (int y = 0; y < ch; ++y) {
        unsigned char *row = px + (size_t)(y0 + y) * w * 4 + (size_t)x0 * 4;
        float le = lean_left(l, ch, (float)y);
        int banner = y < banner_h;
        float fy = banner ? (float)y / (float)(banner_h > 1 ? banner_h - 1 : 1) : 1.0f;
        float gloss_edge = (float)banner_h * 0.52f; /* lacquer highlight: diagonal lower edge */
        for (int x = 0; x < cw; ++x) {
            unsigned char *p = row + x * 4;
            p[3] = 0;
            float xr = (float)x - le;
            if (xr < 0.0f) {
                p[0] = p[1] = p[2] = 0;
                continue;
            }
            if (xr < (float)l->bar) { /* white light bar at the leading edge, coloured at its foot */
                float t = banner ? fy * 0.5f : 0.6f;
                for (int k = 0; k < 3; ++k) p[k] = mix8(255, one[k], t);
                continue;
            }
            float fx = (xr - (float)l->bar) / span;
            if (fx > 1.0f) fx = 1.0f;
            if (!banner) { /* panel body: deep navy that takes on the colours faintly */
                float vy = (float)(y - banner_h) / (float)(ch - banner_h > 1 ? ch - banner_h : 1);
                for (int k = 0; k < 3; ++k) {
                    float v = (float)navy[k];
                    v += ((float)mix8(one[k], two[k], fx) - v) * (0.10f + 0.10f * (1.0f - vy) * (1.0f - fx));
                    p[k] = mix8(0, 255, v / 255.0f);
                }
                continue;
            }
            float shade = 1.0f - 0.30f * fy; /* darker towards the bottom */
            for (int k = 0; k < 3; ++k) {
                float v = (float)mix8(one[k], two[k], fx) * shade;
                p[k] = mix8(0, 255, v / 255.0f);
            }
            float edge = gloss_edge - xr * 0.18f;
            if ((float)y < edge) { /* glossy lacquer, strongest at the very top */
                float t = 0.10f + 0.20f * (1.0f - (float)y / edge);
                float soft = edge - (float)y < 1.5f * s ? (edge - (float)y) / (1.5f * s) : 1.0f;
                for (int k = 0; k < 3; ++k) p[k] = mix8(p[k], 255, t * soft);
            }
            for (unsigned i = 0; i < sizeof(streaks) / sizeof(streaks[0]); ++i) {
                int sy = (int)(streaks[i][0] * (float)banner_h);
                int th = (int)(streaks[i][2] * s) > 1 ? (int)(streaks[i][2] * s) : 1;
                if (y >= sy && y < sy + th && fx > streaks[i][1]) {
                    float t = (fx - streaks[i][1]) / (1.0f - streaks[i][1]);
                    for (int k = 0; k < 3; ++k) p[k] = mix8(p[k], 255, 0.35f * t);
                }
            }
        }
    }
}

/* Overdrive: finished pixel of the slanted banner, its colour glow and drop shadow (premultiplied) */
static float over_inside(const Layout *l, int cw, int ch, float px_, float py_) {
    float le = lean_left(l, ch, py_);
    float re = (float)cw - ((float)l->skew - lean_left(l, ch, py_));
    float d = px_ - le;
    if (re - px_ < d) d = re - px_;
    if (py_ < d) d = py_;
    if ((float)ch - py_ < d) d = (float)ch - py_;
    return d; /* > 0 inside, distance to the nearest edge */
}

static void finish_pixel_over(const unsigned char *src, unsigned char *dst, int x, int y, int w, int h, int g,
                              const Layout *l, float opacity, const unsigned char (*tone)[3]) {
    int cw = w - 2 * g, ch = h - 2 * g;
    float s = l->scale;
    float px_ = (float)(x - g) + 0.5f, py_ = (float)(y - g) + 0.5f;
    float d = over_inside(l, cw, ch, px_, py_);
    float outer = clamp01(d + 0.5f), inner = clamp01(d - 0.7f);
    const unsigned char *b2 = second_colour(tone);
    float fx = clamp01(px_ / (float)cw);
    unsigned char c[3] = {src[0], src[1], src[2]};
    float rim = outer - inner;
    if (rim > 0.0f) /* thin bright edge */
        for (int k = 0; k < 3; ++k) c[k] = mix8(c[k], 255, rim * (0.85f - 0.45f * clamp01(py_ / (float)ch)));
    /* drop shadow (down right) and glow in the two colours */
    float sd = over_inside(l, cw, ch, px_ - 4.0f * s, py_ - 5.0f * s);
    float shadow = sd > -(float)g ? 0.55f * clamp01(1.0f + sd / (float)g) * clamp01(1.0f + sd / (float)g) : 0.0f;
    float e = d < 0.0f ? -d : 0.0f;
    float glow = e < (float)g ? 0.60f * (1.0f - e / (float)g) * (1.0f - e / (float)g) : 0.0f;
    unsigned char gc[3] = {mix8(tone[0][2], b2[2], fx), mix8(tone[0][1], b2[1], fx), mix8(tone[0][0], b2[0], fx)};
    float a = opacity * outer;
    float ga = glow * (1.0f - a);
    float sa = shadow * (1.0f - a - ga > 0.0f ? 1.0f - a - ga : 0.0f);
    for (int k = 0; k < 3; ++k) {
        float v = c[k] * a + gc[k] * ga; /* shadow adds darkness only (black) */
        dst[k] = (unsigned char)(v > 255.0f ? 255.0f : v + 0.5f);
    }
    float alpha = a + ga + sa;
    dst[3] = (unsigned char)(255.0f * (alpha > 1.0f ? 1.0f : alpha) + 0.5f);
}

/* inside of one card or panel: the cw x ch box at (x0, y0) of a w wide BGRA bitmap */
static void paint_card_back(unsigned char *px, int w, int x0, int y0, int cw, int ch, const Layout *l,
                            const unsigned char (*tone)[3]) {
    const unsigned char navy[3] = {28, 17, 11}; /* B, G, R */
    const unsigned char accent[3] = {tone[0][2], tone[0][1], tone[0][0]};
    const unsigned char light[3] = {tone[1][2], tone[1][1], tone[1][0]};
    float glow_width = 30.0f * l->scale;
    for (int y = 0; y < ch; ++y) {
        unsigned char *row = px + (size_t)(y0 + y) * w * 4 + (size_t)x0 * 4;
        float fy = ch > 1 ? (float)y / (float)(ch - 1) : 0.0f;
        float sheen = fy < 0.45f ? 0.09f * (1.0f - fy / 0.45f) * (1.0f - fy / 0.45f) : 0.0f;
        for (int x = 0; x < cw; ++x) {
            unsigned char *p = row + x * 4;
            if (x < l->bar) { /* colour bar: almost white at the top, full colour below */
                for (int k = 0; k < 3; ++k) p[k] = mix8(mix8(light[k], 255, 0.35f), accent[k], fy);
            } else {
                float fx = (float)(x - l->bar) / (float)(cw - l->bar);
                float left = 1.0f - fx;
                float tint = 0.26f * left * left * left; /* colour wash from the left */
                float d = (float)(x - l->bar) / glow_width;
                float glow = 0.42f / (1.0f + d * d);    /* light from the bar */
                float right = fx > 0.6f ? (fx - 0.6f) / 0.4f : 0.0f;
                float back = 0.10f * right * right;     /* a faint second light at the right edge */
                for (int k = 0; k < 3; ++k) {
                    float v = (float)navy[k];
                    v += ((float)accent[k] - v) * (tint + glow + back);
                    v += (255.0f - v) * sheen;
                    p[k] = mix8(0, 255, v / 255.0f);
                }
            }
            p[3] = 0;
        }
    }
}

/* soft round light in the card colour (behind the logo) */
static void paint_halo(unsigned char *px, int w, int y_min, int y_end, int cx, int cy, int radius, float strength,
                       const unsigned char (*tone)[3]) {
    const unsigned char accent[3] = {tone[0][2], tone[0][1], tone[0][0]};
    float r2 = (float)radius * (float)radius;
    for (int y = cy - radius; y < cy + radius; ++y) {
        for (int x = cx - radius; x < cx + radius; ++x) {
            if (x < 0 || x >= w || y < y_min || y >= y_end) continue;
            float dx = (float)(x - cx) + 0.5f, dy = (float)(y - cy) + 0.5f, d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            float t = 1.0f - d2 / r2;
            unsigned char *p = px + ((size_t)y * w + x) * 4;
            for (int k = 0; k < 3; ++k) p[k] = mix8(p[k], accent[k], strength * t * t);
        }
    }
}

/* the logo (alpha mask) in colour col into a size x size box, 2x2 samples per pixel */
static void paint_logo(unsigned char *px, int w, int ix, int iy, int size, const unsigned char col[3], float strength) {
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float a = 0.0f;
            for (int sy = 0; sy < 2; ++sy) {
                for (int sx = 0; sx < 2; ++sx) {
                    float fx = (x + 0.25f + 0.5f * sx) * GRIPLINE_LOGO_SIZE / size - 0.5f;
                    float fy = (y + 0.25f + 0.5f * sy) * GRIPLINE_LOGO_SIZE / size - 0.5f;
                    if (fx < 0) fx = 0;
                    if (fy < 0) fy = 0;
                    int x0 = (int)fx, y0 = (int)fy;
                    int x1 = x0 + 1 < GRIPLINE_LOGO_SIZE ? x0 + 1 : x0;
                    int y1 = y0 + 1 < GRIPLINE_LOGO_SIZE ? y0 + 1 : y0;
                    float ax = fx - x0, ay = fy - y0;
                    a += (gripline_logo_alpha[y0 * GRIPLINE_LOGO_SIZE + x0] * (1 - ax) +
                          gripline_logo_alpha[y0 * GRIPLINE_LOGO_SIZE + x1] * ax) * (1 - ay) +
                         (gripline_logo_alpha[y1 * GRIPLINE_LOGO_SIZE + x0] * (1 - ax) +
                          gripline_logo_alpha[y1 * GRIPLINE_LOGO_SIZE + x1] * ax) * ay;
                }
            }
            a *= strength / (4.0f * 255.0f);
            unsigned char *p = px + ((size_t)(iy + y) * w + ix + x) * 4;
            for (int k = 0; k < 3; ++k) p[k] = (unsigned char)(p[k] * (1 - a) + col[k] * a);
        }
    }
}

/* One finished pixel of a cell (w x h, card inside at glow g): rounded corners, opacity, the rim
   (bright at the top) and outside the card the glow in the card colour. Premultiplied BGRA. */
static void finish_pixel(const unsigned char *src, unsigned char *dst, int x, int y, int w, int h, int g,
                         const Layout *l, float opacity, const unsigned char (*tone)[3]) {
    if (l->style == NOTIFY_STYLE_OVERDRIVE) {
        finish_pixel_over(src, dst, x, y, w, h, g, l, opacity, tone);
        return;
    }
    int cw = w - 2 * g, ch = h - 2 * g;
    int over = 0;
    int r = l->radius;
    float outer = card_coverage(x - g, y - g, cw, ch, r);
    float inner = card_coverage(x - g - 1, y - g - 1, cw - 2, ch - 2, r > 1 ? r - 1 : 1);
    const unsigned char accent[3] = {tone[0][2], tone[0][1], tone[0][0]};
    const unsigned char light[3] = {tone[1][2], tone[1][1], tone[1][0]};
    float fy = ch > 1 ? (float)(y - g) / (float)(ch - 1) : 0.0f;
    fy = fy < 0.0f ? 0.0f : fy > 1.0f ? 1.0f : fy;
    unsigned char c[3] = {src[0], src[1], src[2]};
    float rim = outer - inner;
    if (rim > 0.0f) {
        float strength = rim * (over ? 1.0f - 0.35f * fy : 0.95f - 0.65f * fy);
        const unsigned char rim_col[3] = {mix8(light[0], 255, 0.4f), mix8(light[1], 255, 0.4f), mix8(light[2], 255, 0.4f)};
        for (int k = 0; k < 3; ++k) c[k] = mix8(c[k], rim_col[k], strength);
    }
    /* glow: distance outside the rounded rectangle */
    float glow = 0.0f;
    if (g > 0) {
        float px_ = (float)(x - g) + 0.5f, py_ = (float)(y - g) + 0.5f;
        float qx = px_ < cw * 0.5f ? cw * 0.5f - px_ : px_ - cw * 0.5f;
        float qy = py_ < ch * 0.5f ? ch * 0.5f - py_ : py_ - ch * 0.5f;
        qx -= cw * 0.5f - r, qy -= ch * 0.5f - r;
        if (qx < 0.0f) qx = 0.0f;
        if (qy < 0.0f) qy = 0.0f;
        float e = root(qx * qx + qy * qy) - (float)r;
        if (e < 0.0f) e = 0.0f;
        if (e < (float)g) {
            float t = 1.0f - e / (float)g;
            float side = (float)x / (float)w; /* a little stronger at the left, where the bar is */
            glow = (over ? 0.70f : 0.50f) * t * t * (1.15f - 0.4f * side);
        }
    }
    float a = opacity * outer;
    float ga = glow * (1.0f - a);
    for (int k = 0; k < 3; ++k) {
        float v = c[k] * a + accent[k] * ga;
        dst[k] = (unsigned char)(v > 255.0f ? 255.0f : v + 0.5f);
    }
    float alpha = a + ga;
    dst[3] = (unsigned char)(255.0f * (alpha > 1.0f ? 1.0f : alpha) + 0.5f);
}

/* text with a dark shadow below right, for depth */
static void shadow_text(HDC dc, int x, int y, const RECT *clip, const wchar_t *text, COLORREF colour, int offset) {
    UINT n = (UINT)text_width_chars(text);
    RECT shifted = {clip->left + offset, clip->top + offset, clip->right + offset, clip->bottom + offset};
    gdi.SetTextColor(dc, RGB(4, 6, 12));
    gdi.ExtTextOutW(dc, x + offset, y + offset, ETO_CLIPPED, &shifted, text, n, NULL);
    gdi.SetTextColor(dc, colour);
    gdi.ExtTextOutW(dc, x, y, ETO_CLIPPED, clip, text, n, NULL);
}

static void clear_bitmap(unsigned char *px, int w, int h) {
    for (size_t i = 0; i < (size_t)w * h * 4; ++i) px[i] = 0;
}

/* Draws every card into a top-down BGRA bitmap (premultiplied alpha), one cell per card. */
static int draw_cards_v7(const Card *list, unsigned count, const Layout *l, unsigned char *pixels, int pitch) {
    static BITMAPINFO info;
    void *bits = NULL;
    int w = l->width, h = l->height * MAX_CARDS, g = l->glow;
    int cw = w - 2 * g, ch = l->height - 2 * g;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = gdi.CreateCompatibleDC(NULL);
    if (!dc) return 0;
    HBITMAP bitmap = gdi.CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || !bits) {
        gdi.DeleteDC(dc);
        return 0;
    }
    HGDIOBJ old_bitmap = gdi.SelectObject(dc, bitmap);
    unsigned char *px = (unsigned char *)bits;
    float s = l->scale;
    clear_bitmap(px, w, h);
    for (unsigned c = 0; c < count; ++c) {
        const unsigned char(*tone)[3] = palette[list[c].color >= 0 ? list[c].color : color];
        const unsigned char logo[3] = {mix8(tone[1][2], 255, 0.15f), mix8(tone[1][1], 255, 0.15f),
                                       mix8(tone[1][0], 255, 0.15f)};
        int top = (int)c * l->height;
        int ix = g + l->skew * 6 / 10 + l->bar + (int)(9 * s), iy = top + g + (ch - l->icon) / 2;
        if (l->style == NOTIFY_STYLE_OVERDRIVE) { /* banner; logo in white (dark on light cards) with a shadow */
            paint_overdrive_back(px, w, g, top + g, cw, ch, 0, l, tone);
            const unsigned char ink[3] = {0, 0, 0}, white[3] = {255, 255, 255}, dark[3] = {40, 26, 18};
            int off = (int)(2 * s) > 1 ? (int)(2 * s) : 1;
            paint_logo(px, w, ix + off, iy + off, l->icon, ink, 0.45f);
            paint_logo(px, w, ix, iy, l->icon, over_light_card(tone) ? dark : white, 1.0f);
        } else {
            paint_card_back(px, w, g, top + g, cw, ch, l, tone);
            paint_halo(px, w, top + g, top + g + ch, ix + l->icon / 2, iy + l->icon / 2, l->icon * 7 / 10, 0.38f, tone);
            paint_logo(px, w, ix, iy, l->icon, logo, 1.0f);
        }
    }
    gdi.GdiFlush();
    HFONT badge_font = gdi.CreateFontW(-(int)(13 * s), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                       ANTIALIASED_QUALITY, 0, L"Segoe UI");
    int italic = l->style == NOTIFY_STYLE_OVERDRIVE;
    HFONT main = gdi.CreateFontW(-(int)(20 * s), 0, 0, 0, italic ? FW_HEAVY : FW_BOLD, (DWORD)italic, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT small = gdi.CreateFontW(-(int)(14 * s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  ANTIALIASED_QUALITY, 0, L"Segoe UI");
    gdi.SetBkMode(dc, TRANSPARENT);
    int tx = g + l->skew * 6 / 10 + l->bar + (int)(9 * s) + l->icon + (int)(12 * s);
    int shade = s >= 1.5f ? 2 : 1;
    for (unsigned c = 0; c < count; ++c) {
        int top = (int)c * l->height + g;
        RECT clip = {tx, top, g + cw - l->pad - l->skew, top + ch};
        int has2 = list[c].line2[0] != 0, badge = list[c].badge[0] != 0;
        const unsigned char(*tone)[3] = palette[list[c].color >= 0 ? list[c].color : color];
        int dark_text = italic && over_light_card(tone);
        COLORREF text1 = dark_text ? RGB(18, 22, 32) : RGB(252, 253, 255);
        COLORREF text2 = italic ? (dark_text ? RGB(40, 46, 60) : RGB(236, 240, 248)) : RGB(180, 190, 205);
        COLORREF badge_col = italic ? text1 : RGB(tone[1][0], tone[1][1], tone[1][2]);
        int y = top + (int)((badge ? (has2 ? 10 : 20) : (has2 ? 19 : 31)) * s);
        HGDIOBJ old_font = gdi.SelectObject(dc, badge_font);
        if (badge) { /* WARNUNG / HINWEIS above the text */
            if (dark_text) {
                gdi.SetTextColor(dc, badge_col);
                gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, list[c].badge, (UINT)text_width_chars(list[c].badge), NULL);
            } else {
                shadow_text(dc, tx, y, &clip, list[c].badge, badge_col, shade);
            }
            y += (int)(19 * s);
        }
        gdi.SelectObject(dc, main);
        if (dark_text) {
            gdi.SetTextColor(dc, text1);
            gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, list[c].line1, (UINT)text_width_chars(list[c].line1), NULL);
        } else {
            shadow_text(dc, tx, y, &clip, list[c].line1, text1, shade);
        }
        if (has2) {
            y += (int)(27 * s);
            gdi.SelectObject(dc, small);
            gdi.SetTextColor(dc, text2);
            gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, list[c].line2, (UINT)text_width_chars(list[c].line2),
                            NULL);
        }
        gdi.SelectObject(dc, old_font);
    }
    gdi.GdiFlush();
    for (int y = 0; y < h; ++y) {
        unsigned char *src = px + (size_t)y * w * 4;
        unsigned char *dst = pixels + (size_t)y * pitch;
        int card = y / l->height, cy = y % l->height;
        const unsigned char(*tone)[3] =
            palette[(unsigned)card < count && list[card].color >= 0 ? list[card].color : color];
        for (int x = 0; x < w; ++x) {
            if ((unsigned)card >= count) {
                dst[x * 4 + 0] = dst[x * 4 + 1] = dst[x * 4 + 2] = dst[x * 4 + 3] = 0;
                continue;
            }
            finish_pixel(src + x * 4, dst + x * 4, x, cy, w, l->height, g, l, 0.95f, tone);
        }
    }
    gdi.SelectObject(dc, old_bitmap);
    gdi.DeleteObject(badge_font);
    gdi.DeleteObject(main);
    gdi.DeleteObject(small);
    gdi.DeleteObject(bitmap);
    gdi.DeleteDC(dc);
    return 1;
}

/* height in pixels of the key list panel with `count` rows (with its glow margin) */
static int panel_pixels(const Layout *l, unsigned count) {
    return l->panel_header + (int)count * l->panel_row + l->pad + 2 * l->glow;
}

/* Draws the key list panel into l->panel_height rows (premultiplied BGRA, unused rows clear). */
static int draw_panel_v7(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, const Layout *l,
                         unsigned char *pixels, int pitch) {
    static BITMAPINFO info;
    void *bits = NULL;
    int w = l->width, h = panel_pixels(l, count), g = l->glow;
    int cw = w - 2 * g, ch = h - 2 * g;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = gdi.CreateCompatibleDC(NULL);
    if (!dc) return 0;
    HBITMAP bitmap = gdi.CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || !bits) {
        gdi.DeleteDC(dc);
        return 0;
    }
    HGDIOBJ old_bitmap = gdi.SelectObject(dc, bitmap);
    unsigned char *px = (unsigned char *)bits;
    const unsigned char(*tone)[3] = palette[color];
    const unsigned char accent[3] = {tone[0][2], tone[0][1], tone[0][0]};
    float s = l->scale;
    clear_bitmap(px, w, h);
    int over = l->style == NOTIFY_STYLE_OVERDRIVE;
    if (over) paint_overdrive_back(px, w, g, g, cw, ch, l->panel_header, l, tone);
    else paint_card_back(px, w, g, g, cw, ch, l, tone);
    int left = g + l->bar + l->skew, right = g + cw - l->skew; /* inside, right of the colour bar */
    int first_row = g + l->panel_header;
    /* header: the logo on a soft light, the title next to it */
    int logo_size = l->panel_header - (int)(14 * s);
    int lx = left + (int)(8 * s), ly = g + (int)(5 * s);
    const unsigned char logo[3] = {mix8(tone[1][2], 255, 0.15f), mix8(tone[1][1], 255, 0.15f), mix8(tone[1][0], 255, 0.15f)};
    if (over) {
        const unsigned char ink[3] = {0, 0, 0}, white[3] = {255, 255, 255}, dark[3] = {40, 26, 18};
        paint_logo(px, w, lx + 1, ly + 1, logo_size, ink, 0.45f);
        paint_logo(px, w, lx, ly, logo_size, over_light_card(tone) ? dark : white, 1.0f);
    } else {
        paint_halo(px, w, g, g + ch, lx + logo_size / 2, ly + logo_size / 2, logo_size * 7 / 10, 0.38f, tone);
        paint_logo(px, w, lx, ly, logo_size, logo, 1.0f);
    }
    /* every second row a touch lighter: long lists read easier */
    for (unsigned i = 1; i < count; i += 2) {
        int y0 = first_row + (int)i * l->panel_row;
        for (int y = y0; y < y0 + l->panel_row && y < g + ch; ++y) {
            unsigned char *row = px + (size_t)y * w * 4;
            for (int x = left + l->pad / 2; x < right - l->pad / 2; ++x)
                for (int k = 0; k < 3; ++k) row[x * 4 + k] = mix8(row[x * 4 + k], 255, 0.04f);
        }
    }
    /* line in the card colour under the header, fading to the right */
    int line_y = first_row - (int)(5 * s);
    for (int y = line_y; y < line_y + (s >= 1.5f ? 2 : 1); ++y) {
        unsigned char *row = px + (size_t)y * w * 4;
        for (int x = left + l->pad / 2; x < right - l->pad; ++x) {
            float fade = 1.0f - (float)(x - left) / (float)(right - left);
            for (int k = 0; k < 3; ++k) row[x * 4 + k] = mix8(row[x * 4 + k], mix8(accent[k], 255, 0.3f), 0.95f * fade);
        }
    }
    gdi.GdiFlush();
    HFONT small_bold = gdi.CreateFontW(-(int)(13 * s), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                       ANTIALIASED_QUALITY, 0, L"Segoe UI");
    int italic = l->style == NOTIFY_STYLE_OVERDRIVE;
    HFONT main = gdi.CreateFontW(-(int)(20 * s), 0, 0, 0, italic ? FW_HEAVY : FW_BOLD, (DWORD)italic, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT label = gdi.CreateFontW(-(int)(15 * s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT keys = gdi.CreateFontW(-(int)(15 * s), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 ANTIALIASED_QUALITY, 0, L"Segoe UI");
    gdi.SetBkMode(dc, TRANSPARENT);
    int tx = left + l->pad, kx = g + cw * 58 / 100;
    int title_x = lx + logo_size + (int)(12 * s);
    RECT clip = {title_x, g, right - l->pad, g + ch};
    HGDIOBJ old_font = gdi.SelectObject(dc, main);
    shadow_text(dc, title_x, g + (l->panel_header - (int)(26 * s)) / 2 - (int)(2 * s), &clip, title, over && over_light_card(tone) ? RGB(18, 22, 32) : RGB(252, 253, 255),
                s >= 1.5f ? 2 : 1);
    for (unsigned i = 0; i < count; ++i) {
        int y = first_row + (int)i * l->panel_row;
        RECT left_clip = {tx, y, kx - l->pad / 2, y + l->panel_row};
        RECT right_clip = {kx, y, right - l->pad, y + l->panel_row};
        int style = rows[i].style;
        COLORREF label_color = RGB(205, 212, 224), keys_color = RGB(tone[1][0], tone[1][1], tone[1][2]);
        if (style == NOTIFY_ROW_SELECTED) {
            /* key menu: the chosen row glows in the card colour */
            gdi.GdiFlush(); /* text of the rows above is in the bitmap before it is touched directly */
            for (int by = y; by < y + l->panel_row && by < g + ch; ++by) {
                unsigned char *row = px + (size_t)by * w * 4;
                float vy = (float)(by - y) / (float)l->panel_row;
                float bright = 0.12f * (1.0f - vy); /* lighter at the top of the band */
                for (int x = left + l->pad / 2; x < right - l->pad / 2; ++x) {
                    float t = (float)(x - left) / (float)(right - left);
                    for (int k = 0; k < 3; ++k)
                        row[x * 4 + k] = mix8(mix8(row[x * 4 + k], accent[k], 0.90f - 0.55f * t), 255, bright);
                }
                for (int x = left + l->pad / 2; x < left + l->pad / 2 + (s >= 1.5f ? 4 : 3); ++x)
                    for (int k = 0; k < 3; ++k) row[x * 4 + k] = 255; /* marker */
            }
            gdi.GdiFlush();
            label_color = keys_color = RGB(255, 255, 255);
        } else if (style == NOTIFY_ROW_INFO) {
            label_color = RGB(150, 158, 170);
        } else if (style == NOTIFY_ROW_HINT) {
            label_color = keys_color = RGB(135, 143, 156);
        }
        gdi.SelectObject(dc, style == NOTIFY_ROW_HINT ? small_bold : label);
        gdi.SetTextColor(dc, label_color);
        gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &left_clip, rows[i].label, (UINT)text_width_chars(rows[i].label), NULL);
        gdi.SelectObject(dc, style == NOTIFY_ROW_HINT ? small_bold : keys);
        gdi.SetTextColor(dc, keys_color);
        gdi.ExtTextOutW(dc, kx, y, ETO_CLIPPED, &right_clip, rows[i].keys, (UINT)text_width_chars(rows[i].keys), NULL);
    }
    gdi.SelectObject(dc, old_font);
    gdi.GdiFlush();
    for (int y = 0; y < l->panel_height; ++y) {
        unsigned char *src = px + (size_t)y * w * 4;
        unsigned char *dst = pixels + (size_t)y * pitch;
        for (int x = 0; x < w; ++x) {
            if (y >= h) {
                dst[x * 4 + 0] = dst[x * 4 + 1] = dst[x * 4 + 2] = dst[x * 4 + 3] = 0;
                continue;
            }
            finish_pixel(src + x * 4, dst + x * 4, x, y, w, h, g, l, 0.94f, tone);
        }
    }
    gdi.SelectObject(dc, old_bitmap);
    gdi.DeleteObject(small_bold);
    gdi.DeleteObject(main);
    gdi.DeleteObject(label);
    gdi.DeleteObject(keys);
    gdi.DeleteObject(bitmap);
    gdi.DeleteDC(dc);
    return 1;
}

/* ------------------------------------------------------------------ classic card look (V6, "Classic") */

/* Draws every card into a top-down BGRA bitmap (premultiplied alpha), one row per card. */
static int draw_cards_classic(const Card *list, unsigned count, const Layout *l, unsigned char *pixels, int pitch) {
    static BITMAPINFO info;
    void *bits = NULL;
    int w = l->width, h = l->height * MAX_CARDS;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = gdi.CreateCompatibleDC(NULL);
    if (!dc) return 0;
    HBITMAP bitmap = gdi.CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || !bits) {
        gdi.DeleteDC(dc);
        return 0;
    }
    HGDIOBJ old_bitmap = gdi.SelectObject(dc, bitmap);
    unsigned char *px = (unsigned char *)bits;
    const unsigned char bg[3] = {32, 23, 17};      /* B, G, R: dark navy */
    for (unsigned c = 0; c < count; ++c) {
        const unsigned char(*tone)[3] = palette[list[c].color >= 0 ? list[c].color : color];
        const unsigned char accent[3] = {tone[0][2], tone[0][1], tone[0][0]}; /* B, G, R */
        const unsigned char logo[3] = {tone[1][2], tone[1][1], tone[1][0]};
        int top = (int)c * l->height;
        for (int y = 0; y < l->height; ++y) {
            unsigned char *row = px + (size_t)(top + y) * w * 4;
            for (int x = 0; x < w; ++x) {
                const unsigned char *col = x < l->bar ? accent : bg;
                row[x * 4 + 0] = col[0];
                row[x * 4 + 1] = col[1];
                row[x * 4 + 2] = col[2];
                row[x * 4 + 3] = 0;
            }
        }
        /* logo symbol, bilinear */
        int ix = l->bar + l->pad, iy = top + (l->height - l->icon) / 2;
        for (int y = 0; y < l->icon; ++y) {
            for (int x = 0; x < l->icon; ++x) {
                float fx = (x + 0.5f) * GRIPLINE_SYMBOL_SIZE / l->icon - 0.5f;
                float fy = (y + 0.5f) * GRIPLINE_SYMBOL_SIZE / l->icon - 0.5f;
                int x0 = (int)fx, y0 = (int)fy;
                if (fx < 0) { fx = 0; x0 = 0; }
                if (fy < 0) { fy = 0; y0 = 0; }
                int x1 = x0 + 1 < GRIPLINE_SYMBOL_SIZE ? x0 + 1 : x0;
                int y1 = y0 + 1 < GRIPLINE_SYMBOL_SIZE ? y0 + 1 : y0;
                float ax = fx - x0, ay = fy - y0;
                float a = (gripline_symbol_alpha[y0 * GRIPLINE_SYMBOL_SIZE + x0] * (1 - ax) +
                           gripline_symbol_alpha[y0 * GRIPLINE_SYMBOL_SIZE + x1] * ax) * (1 - ay) +
                          (gripline_symbol_alpha[y1 * GRIPLINE_SYMBOL_SIZE + x0] * (1 - ax) +
                           gripline_symbol_alpha[y1 * GRIPLINE_SYMBOL_SIZE + x1] * ax) * ay;
                a /= 255.0f;
                unsigned char *p = px + ((size_t)(iy + y) * w + ix + x) * 4;
                for (int k = 0; k < 3; ++k) p[k] = (unsigned char)(p[k] * (1 - a) + logo[k] * a);
            }
        }
    }
    gdi.GdiFlush();
    float s = (float)l->height / 88.0f;
    HFONT title = gdi.CreateFontW(-(int)(13 * s), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT main = gdi.CreateFontW(-(int)(18 * s), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT small = gdi.CreateFontW(-(int)(14 * s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  ANTIALIASED_QUALITY, 0, L"Segoe UI");
    gdi.SetBkMode(dc, TRANSPARENT);
    int tx = l->bar + l->pad + l->icon + l->pad;
    for (unsigned c = 0; c < count; ++c) {
        int top = (int)c * l->height;
        RECT clip = {tx, top, w - l->pad, top + l->height};
        int has2 = list[c].line2[0] != 0;
        int y = top + (int)((has2 ? 12 : 20) * s);
        const unsigned char(*tone)[3] = palette[list[c].color >= 0 ? list[c].color : color];
        wchar_t heading[40] = L"GRIPLINE";
        if (list[c].badge[0]) { /* classic title line: GRIPLINE · WARNUNG */
            unsigned n = 8;
            for (const wchar_t *g = L" \x00b7 "; *g; ++g) heading[n++] = *g;
            for (const wchar_t *b = list[c].badge; *b && n + 1 < 40; ++b) heading[n++] = *b;
            heading[n] = 0;
        }
        HGDIOBJ old_font = gdi.SelectObject(dc, title);
        gdi.SetTextColor(dc, RGB(tone[1][0], tone[1][1], tone[1][2]));
        gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, heading, (UINT)text_width_chars(heading), NULL);
        y += (int)(19 * s);
        gdi.SelectObject(dc, main);
        gdi.SetTextColor(dc, RGB(245, 247, 250));
        gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, list[c].line1, (UINT)text_width_chars(list[c].line1), NULL);
        if (has2) {
            y += (int)(25 * s);
            gdi.SelectObject(dc, small);
            gdi.SetTextColor(dc, RGB(170, 180, 195));
            gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, list[c].line2, (UINT)text_width_chars(list[c].line2),
                            NULL);
        }
        gdi.SelectObject(dc, old_font);
    }
    gdi.GdiFlush();
    /* rounded corners + opacity, premultiplied, into the upload rows */
    for (int y = 0; y < h; ++y) {
        unsigned char *src = px + (size_t)y * w * 4;
        unsigned char *dst = pixels + (size_t)y * pitch;
        int card = y / l->height, cy = y % l->height;
        for (int x = 0; x < w; ++x) {
            float a = (unsigned)card < count ? 0.94f * card_coverage(x, cy, w, l->height, l->radius) : 0.0f;
            dst[x * 4 + 0] = (unsigned char)(src[x * 4 + 0] * a + 0.5f);
            dst[x * 4 + 1] = (unsigned char)(src[x * 4 + 1] * a + 0.5f);
            dst[x * 4 + 2] = (unsigned char)(src[x * 4 + 2] * a + 0.5f);
            dst[x * 4 + 3] = (unsigned char)(255.0f * a + 0.5f);
        }
    }
    gdi.SelectObject(dc, old_bitmap);
    gdi.DeleteObject(title);
    gdi.DeleteObject(main);
    gdi.DeleteObject(small);
    gdi.DeleteObject(bitmap);
    gdi.DeleteDC(dc);
    return 1;
}

/* Draws the key list panel into l->panel_height rows (premultiplied BGRA, unused rows clear). */
static int draw_panel_classic(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, const Layout *l,
                      unsigned char *pixels, int pitch) {
    static BITMAPINFO info;
    void *bits = NULL;
    int w = l->width, h = panel_pixels(l, count);
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = gdi.CreateCompatibleDC(NULL);
    if (!dc) return 0;
    HBITMAP bitmap = gdi.CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || !bits) {
        gdi.DeleteDC(dc);
        return 0;
    }
    HGDIOBJ old_bitmap = gdi.SelectObject(dc, bitmap);
    unsigned char *px = (unsigned char *)bits;
    const unsigned char bg[3] = {32, 23, 17};
    const unsigned char(*tone)[3] = palette[color];
    const unsigned char accent[3] = {tone[0][2], tone[0][1], tone[0][0]};
    for (int y = 0; y < h; ++y) {
        unsigned char *row = px + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            const unsigned char *col = x < l->bar ? accent : bg;
            row[x * 4 + 0] = col[0];
            row[x * 4 + 1] = col[1];
            row[x * 4 + 2] = col[2];
            row[x * 4 + 3] = 0;
        }
    }
    gdi.GdiFlush();
    float s = (float)l->height / 88.0f;
    HFONT small_bold = gdi.CreateFontW(-(int)(13 * s), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                       ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT main = gdi.CreateFontW(-(int)(18 * s), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT label = gdi.CreateFontW(-(int)(15 * s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT keys = gdi.CreateFontW(-(int)(15 * s), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 ANTIALIASED_QUALITY, 0, L"Segoe UI");
    gdi.SetBkMode(dc, TRANSPARENT);
    int tx = l->bar + l->pad, kx = w * 58 / 100;
    RECT clip = {tx, 0, w - l->pad, h};
    HGDIOBJ old_font = gdi.SelectObject(dc, small_bold);
    gdi.SetTextColor(dc, RGB(tone[1][0], tone[1][1], tone[1][2]));
    gdi.ExtTextOutW(dc, tx, (int)(10 * s), ETO_CLIPPED, &clip, L"GRIPLINE", 8, NULL);
    gdi.SelectObject(dc, main);
    gdi.SetTextColor(dc, RGB(245, 247, 250));
    gdi.ExtTextOutW(dc, tx, (int)(28 * s), ETO_CLIPPED, &clip, title, (UINT)text_width_chars(title), NULL);
    for (unsigned i = 0; i < count; ++i) {
        int y = l->panel_header + (int)i * l->panel_row;
        RECT left = {tx, y, kx - l->pad / 2, y + l->panel_row};
        RECT right = {kx, y, w - l->pad, y + l->panel_row};
        int style = rows[i].style;
        COLORREF label_color = RGB(200, 207, 218), keys_color = RGB(tone[1][0], tone[1][1], tone[1][2]);
        if (style == NOTIFY_ROW_SELECTED) {
            /* key menu: the chosen row on a band in the card colour */
            gdi.GdiFlush(); /* text of the rows above is in the bitmap before it is touched directly */
            for (int by = y; by < y + l->panel_row && by < h; ++by) {
                unsigned char *row = px + (size_t)by * w * 4;
                for (int x = l->bar + l->pad / 2; x < w - l->pad / 2; ++x) {
                    row[x * 4 + 0] = (unsigned char)((bg[0] + accent[0]) / 2);
                    row[x * 4 + 1] = (unsigned char)((bg[1] + accent[1]) / 2);
                    row[x * 4 + 2] = (unsigned char)((bg[2] + accent[2]) / 2);
                }
            }
            gdi.GdiFlush();
            label_color = keys_color = RGB(255, 255, 255);
        } else if (style == NOTIFY_ROW_INFO) {
            label_color = RGB(150, 158, 170);
        } else if (style == NOTIFY_ROW_HINT) {
            label_color = keys_color = RGB(130, 138, 150);
        }
        gdi.SelectObject(dc, style == NOTIFY_ROW_HINT ? small_bold : label);
        gdi.SetTextColor(dc, label_color);
        gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &left, rows[i].label, (UINT)text_width_chars(rows[i].label), NULL);
        gdi.SelectObject(dc, style == NOTIFY_ROW_HINT ? small_bold : keys);
        gdi.SetTextColor(dc, keys_color);
        gdi.ExtTextOutW(dc, kx, y, ETO_CLIPPED, &right, rows[i].keys, (UINT)text_width_chars(rows[i].keys), NULL);
    }
    gdi.SelectObject(dc, old_font);
    gdi.GdiFlush();
    for (int y = 0; y < l->panel_height; ++y) {
        unsigned char *src = px + (size_t)y * w * 4;
        unsigned char *dst = pixels + (size_t)y * pitch;
        for (int x = 0; x < w; ++x) {
            float a = y < h ? 0.92f * card_coverage(x, y, w, h, l->radius) : 0.0f;
            dst[x * 4 + 0] = y < h ? (unsigned char)(src[x * 4 + 0] * a + 0.5f) : 0;
            dst[x * 4 + 1] = y < h ? (unsigned char)(src[x * 4 + 1] * a + 0.5f) : 0;
            dst[x * 4 + 2] = y < h ? (unsigned char)(src[x * 4 + 2] * a + 0.5f) : 0;
            dst[x * 4 + 3] = (unsigned char)(255.0f * a + 0.5f);
        }
    }
    gdi.SelectObject(dc, old_bitmap);
    gdi.DeleteObject(small_bold);
    gdi.DeleteObject(main);
    gdi.DeleteObject(label);
    gdi.DeleteObject(keys);
    gdi.DeleteObject(bitmap);
    gdi.DeleteDC(dc);
    return 1;
}


static int draw_cards(const Card *list, unsigned count, const Layout *l, unsigned char *pixels, int pitch) {
    return l->style == NOTIFY_STYLE_CLASSIC ? draw_cards_classic(list, count, l, pixels, pitch)
                                            : draw_cards_v7(list, count, l, pixels, pitch);
}

static int draw_panel(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, const Layout *l,
                      unsigned char *pixels, int pitch) {
    return l->style == NOTIFY_STYLE_CLASSIC ? draw_panel_classic(title, rows, count, l, pixels, pitch)
                                            : draw_panel_v7(title, rows, count, l, pixels, pitch);
}

/* ------------------------------------------------------------------ D3D12 */

typedef HRESULT(WINAPI *SerializeRootSignatureFn)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION,
                                                  ID3DBlob **, ID3DBlob **);
typedef HRESULT(WINAPI *CompileFn)(const void *, SIZE_T, const char *, const void *, void *, const char *,
                                   const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

#define MAX_BUFFERS 8

static ID3D12CommandQueue *queue;
static IDXGISwapChain *swapchain_seen;
static struct {
    ID3D12Device *device;
    ID3D12DescriptorHeap *rtv_heap, *srv_heap;
    UINT rtv_step;
    ID3D12RootSignature *root;
    ID3D12PipelineState *pso;
    DXGI_FORMAT pso_format;
    ID3D12CommandAllocator *allocator[MAX_BUFFERS];
    UINT64 allocator_fence[MAX_BUFFERS];
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    UINT64 fence_value;
    ID3D12Resource *texture, *upload;
    int tex_width, tex_height;
    UINT upload_pitch;
    Layout layout;
    UINT screen_height;
    unsigned drawn_ids[MAX_CARDS], drawn_count, drawn_panel_id;
    int texture_ready;
} gpu;

static const char shader_source[] =
    "cbuffer C : register(b0) { float4 rect; float4 uv; float4 alpha; };\n"
    "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { V o; float2 c = float2(id & 1, id >> 1);\n"
    "  o.pos = float4(lerp(rect.xy, rect.zw, c), 0, 1); o.uv = lerp(uv.xy, uv.zw, c); return o; }\n"
    "float4 ps(V i) : SV_Target { return tex.Sample(smp, i.uv) * alpha.x; }\n";

/* late hook: Gripline draws on its own queue and first waits until the game's queues are done */
static int own_queue;
static ID3D12CommandQueue *game_queues[4];
static unsigned game_queue_count;
static SRWLOCK game_queue_lock = SRWLOCK_INIT;
static ID3D12Fence *sync_fence;
static UINT64 sync_value;

static void wait_game_queues(void) {
    if (!own_queue) return;
    if (!sync_fence && FAILED(ID3D12Device_CreateFence(gpu.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                                        (void **)&sync_fence)))
        return;
    AcquireSRWLockShared(&game_queue_lock);
    for (unsigned i = 0; i < game_queue_count; ++i) {
        ID3D12Device *d = NULL;
        if (FAILED(ID3D12CommandQueue_GetDevice(game_queues[i], &IID_ID3D12Device, (void **)&d))) continue;
        int same = d == gpu.device;
        ID3D12Device_Release(d);
        if (same) ID3D12CommandQueue_Signal(game_queues[i], sync_fence, ++sync_value);
    }
    ReleaseSRWLockShared(&game_queue_lock);
    if (sync_value && ID3D12Fence_GetCompletedValue(sync_fence) < sync_value)
        ID3D12Fence_SetEventOnCompletion(sync_fence, sync_value, NULL); /* blocks until the game's frame is done */
}

static void fail(const char *why) {
    if (InterlockedExchange(&broken, 1) == 0) {
        log_append("NOTIFY: in-game cards off for this session - ");
        log_append(why);
        log_append(" (messages stay in this log).\r\n");
    }
}

static void wait_fence(UINT64 value) {
    if (value && ID3D12Fence_GetCompletedValue(gpu.fence) < value) {
        ID3D12Fence_SetEventOnCompletion(gpu.fence, value, NULL); /* NULL event: blocks until done */
    }
}

static int create_pso(DXGI_FORMAT format) {
    static D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    if (gpu.pso && gpu.pso_format == format) return 1;
    HMODULE compiler_module = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (!compiler_module) compiler_module = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, 0);
    CompileFn compile = compiler_module ? (CompileFn)(uintptr_t)GetProcAddress(compiler_module, "D3DCompile") : NULL;
    if (!compile) return 0;
    ID3DBlob *vs = NULL, *ps = NULL, *errors = NULL;
    if (FAILED(compile(shader_source, sizeof(shader_source) - 1, "gripline", NULL, NULL, "vs", "vs_5_0", 0, 0,
                       &vs, &errors)) ||
        FAILED(compile(shader_source, sizeof(shader_source) - 1, "gripline", NULL, NULL, "ps", "ps_5_0", 0, 0,
                       &ps, &errors))) {
        if (errors) ID3D10Blob_Release(errors);
        if (vs) ID3D10Blob_Release(vs);
        return 0;
    }
    desc.pRootSignature = gpu.root;
    desc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs);
    desc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    desc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    desc.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    D3D12_RENDER_TARGET_BLEND_DESC *b = &desc.BlendState.RenderTarget[0];
    b->BlendEnable = TRUE;
    b->SrcBlend = D3D12_BLEND_ONE;
    b->DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    b->BlendOp = D3D12_BLEND_OP_ADD;
    b->SrcBlendAlpha = D3D12_BLEND_ZERO;
    b->DestBlendAlpha = D3D12_BLEND_ONE;
    b->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b->LogicOp = D3D12_LOGIC_OP_NOOP;
    b->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = 0xffffffffu;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = format;
    desc.SampleDesc.Count = 1;
    ID3D12PipelineState *pso = NULL;
    HRESULT hr = ID3D12Device_CreateGraphicsPipelineState(gpu.device, &desc, &IID_ID3D12PipelineState,
                                                          (void **)&pso);
    ID3D10Blob_Release(vs);
    ID3D10Blob_Release(ps);
    if (FAILED(hr)) return 0;
    if (gpu.pso) ID3D12PipelineState_Release(gpu.pso);
    gpu.pso = pso;
    gpu.pso_format = format;
    return 1;
}

static int gpu_init(void) {
    if (gpu.list) return 1;
    D3D12_COMMAND_QUEUE_DESC qd = ID3D12CommandQueue_GetDesc(queue);
    if (qd.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        fail("the game presents from a non-graphics queue");
        return 0;
    }
    if (FAILED(ID3D12CommandQueue_GetDevice(queue, &IID_ID3D12Device, (void **)&gpu.device))) return 0;
    static D3D12_DESCRIPTOR_HEAP_DESC hd;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = MAX_BUFFERS;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(gpu.device, &hd, &IID_ID3D12DescriptorHeap,
                                                 (void **)&gpu.rtv_heap)))
        return 0;
    gpu.rtv_step = ID3D12Device_GetDescriptorHandleIncrementSize(gpu.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(ID3D12Device_CreateDescriptorHeap(gpu.device, &hd, &IID_ID3D12DescriptorHeap,
                                                 (void **)&gpu.srv_heap)))
        return 0;
    /* root signature: SRV table t0 (pixel), 12 constants b0, static linear sampler s0 */
    static D3D12_DESCRIPTOR_RANGE range;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    static D3D12_ROOT_PARAMETER params[2];
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &range;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = 12;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    static D3D12_STATIC_SAMPLER_DESC sampler;
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = 1000.0f;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    static D3D12_ROOT_SIGNATURE_DESC rd;
    rd.NumParameters = 2;
    rd.pParameters = params;
    rd.NumStaticSamplers = 1;
    rd.pStaticSamplers = &sampler;
    rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    SerializeRootSignatureFn serialize =
        d3d12 ? (SerializeRootSignatureFn)(uintptr_t)GetProcAddress(d3d12, "D3D12SerializeRootSignature") : NULL;
    ID3DBlob *blob = NULL, *errors = NULL;
    if (!serialize || FAILED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors))) {
        if (errors) ID3D10Blob_Release(errors);
        return 0;
    }
    HRESULT hr = ID3D12Device_CreateRootSignature(gpu.device, 0, ID3D10Blob_GetBufferPointer(blob),
                                                  ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                                  (void **)&gpu.root);
    ID3D10Blob_Release(blob);
    if (FAILED(hr)) return 0;
    for (int i = 0; i < MAX_BUFFERS; ++i) {
        if (FAILED(ID3D12Device_CreateCommandAllocator(gpu.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       &IID_ID3D12CommandAllocator, (void **)&gpu.allocator[i])))
            return 0;
    }
    if (FAILED(ID3D12Device_CreateFence(gpu.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                        (void **)&gpu.fence)))
        return 0;
    if (FAILED(ID3D12Device_CreateCommandList(gpu.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator[0], NULL,
                                              &IID_ID3D12GraphicsCommandList, (void **)&gpu.list)))
        return 0;
    ID3D12GraphicsCommandList_Close(gpu.list);
    log_append("NOTIFY: in-game cards ready (D3D12 overlay on the game's swap chain).\r\n");
    return 1;
}

static int create_texture(int width, int height) {
    if (gpu.texture && gpu.tex_width == width && gpu.tex_height == height) return 1;
    wait_fence(gpu.fence_value);
    if (gpu.texture) ID3D12Resource_Release(gpu.texture);
    if (gpu.upload) ID3D12Resource_Release(gpu.upload);
    gpu.texture = gpu.upload = NULL;
    gpu.texture_ready = 0;
    static D3D12_HEAP_PROPERTIES heap;
    static D3D12_RESOURCE_DESC rd;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)width;
    rd.Height = (UINT)height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (FAILED(ID3D12Device_CreateCommittedResource(gpu.device, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource,
                                                    (void **)&gpu.texture)))
        return 0;
    gpu.upload_pitch = ((UINT)width * 4 + 255u) & ~255u;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (UINT64)gpu.upload_pitch * (UINT)height;
    rd.Height = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ID3D12Device_CreateCommittedResource(gpu.device, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource,
                                                    (void **)&gpu.upload)))
        return 0;
    static D3D12_SHADER_RESOURCE_VIEW_DESC sv;
    sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(gpu.device, gpu.texture, &sv,
                                          ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu.srv_heap));
    gpu.tex_width = width;
    gpu.tex_height = height;
    return 1;
}

static void barrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b;
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(gpu.list, 1, &b);
}

/* fade/slide of one card: alpha 0..1 and slide offset 0..1, 0 when gone */
static float card_alpha(ULONGLONG age, ULONGLONG hold, float *slide) {
    *slide = 0.0f;
    if (age < FADE_IN_MS) {
        float t = (float)age / FADE_IN_MS;
        float e = 1.0f - (1.0f - t) * (1.0f - t);
        *slide = 1.0f - e;
        return e;
    }
    if (age < hold) return 1.0f;
    if (age < hold + FADE_OUT_MS) return 1.0f - (float)(age - hold) / FADE_OUT_MS;
    return 0.0f;
}

static void render(IDXGISwapChain *swapchain) {
    Card list[MAX_CARDS];
    unsigned count;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    /* the time runs from the first picture a card is drawn in, so frames the game does not
       present (loading) do not use it up */
    if (queue)
        for (unsigned i = 0; i < card_count; ++i)
            if (!cards[i].shown) cards[i].shown = now;
    while (card_count && cards[card_count - 1].shown &&
           now - cards[card_count - 1].shown >= cards[card_count - 1].hold + FADE_OUT_MS)
        --card_count;
    count = card_count;
    for (unsigned i = 0; i < count; ++i) list[i] = cards[i];
    unsigned panel = (unsigned)panel_count, shown_panel_id = panel_id;
    ReleaseSRWLockExclusive(&lock);
    if ((!count && !panel) || !queue) return;
    if (!gpu_init()) {
        fail("D3D12 setup failed");
        return;
    }
    DXGI_SWAP_CHAIN_DESC sd;
    if (FAILED(IDXGISwapChain_GetDesc(swapchain, &sd)) || sd.BufferCount == 0 || sd.BufferCount > MAX_BUFFERS) {
        fail("unexpected swap chain");
        return;
    }
    IDXGISwapChain3 *sc3 = NULL;
    if (FAILED(IDXGISwapChain_QueryInterface(swapchain, &IID_IDXGISwapChain3, (void **)&sc3))) {
        fail("no IDXGISwapChain3");
        return;
    }
    UINT index = IDXGISwapChain3_GetCurrentBackBufferIndex(sc3);
    IDXGISwapChain3_Release(sc3);
    UINT width = sd.BufferDesc.Width, height = sd.BufferDesc.Height;
    if (!create_pso(sd.BufferDesc.Format)) {
        fail("could not build the overlay shader");
        return;
    }
    if (height != gpu.screen_height || gpu.layout.style != (int)card_style) {
        gpu.screen_height = height;
        gpu.layout = layout_for(height);
        gpu.drawn_count = (unsigned)-1;
    }
    const Layout *l = &gpu.layout;
    int card_rows = l->height * MAX_CARDS;
    if (!create_texture(l->width, card_rows + l->panel_height)) {
        fail("could not create the card texture");
        return;
    }
    int redraw = !gpu.texture_ready || gpu.drawn_count != count || gpu.drawn_panel_id != shown_panel_id;
    for (unsigned i = 0; !redraw && i < count; ++i) redraw = gpu.drawn_ids[i] != list[i].id;
    wait_fence(gpu.allocator_fence[index]);
    if (redraw) {
        void *mapped = NULL;
        D3D12_RANGE none = {0, 0};
        wait_fence(gpu.fence_value); /* the upload buffer may still be read */
        static wchar_t title[TEXT_CAPACITY];
        static NotifyPanelRow rows[NOTIFY_PANEL_ROWS];
        if (panel) {
            AcquireSRWLockExclusive(&lock);
            panel = (unsigned)panel_count;
            shown_panel_id = panel_id;
            memory_copy(title, panel_title, sizeof(title));
            memory_copy(rows, panel_rows, sizeof(rows[0]) * panel);
            ReleaseSRWLockExclusive(&lock);
        }
        if (!gdi_ready() || FAILED(ID3D12Resource_Map(gpu.upload, 0, &none, &mapped)) ||
            !draw_cards(list, count, l, (unsigned char *)mapped, (int)gpu.upload_pitch) ||
            (panel && !draw_panel(title, rows, panel, l, (unsigned char *)mapped + (size_t)card_rows * gpu.upload_pitch,
                                  (int)gpu.upload_pitch))) {
            if (mapped) ID3D12Resource_Unmap(gpu.upload, 0, NULL);
            fail("could not draw the cards");
            return;
        }
        ID3D12Resource_Unmap(gpu.upload, 0, NULL);
    }
    ID3D12CommandAllocator_Reset(gpu.allocator[index]);
    ID3D12GraphicsCommandList_Reset(gpu.list, gpu.allocator[index], gpu.pso);
    if (redraw) {
        if (gpu.texture_ready)
            barrier(gpu.texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst, src;
        dst.pResource = gpu.texture;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        src.pResource = gpu.upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = (UINT)gpu.tex_width;
        src.PlacedFootprint.Footprint.Height = (UINT)gpu.tex_height;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = gpu.upload_pitch;
        ID3D12GraphicsCommandList_CopyTextureRegion(gpu.list, &dst, 0, 0, 0, &src, NULL);
        barrier(gpu.texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gpu.texture_ready = 1;
        gpu.drawn_count = count;
        gpu.drawn_panel_id = shown_panel_id;
        for (unsigned i = 0; i < count; ++i) gpu.drawn_ids[i] = list[i].id;
    }
    ID3D12Resource *back = NULL;
    if (FAILED(IDXGISwapChain_GetBuffer(swapchain, index, &IID_ID3D12Resource, (void **)&back))) {
        ID3D12GraphicsCommandList_Close(gpu.list);
        fail("no back buffer");
        return;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu.rtv_heap);
    rtv.ptr += (SIZE_T)index * gpu.rtv_step;
    ID3D12Device_CreateRenderTargetView(gpu.device, back, NULL, rtv);
    barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_OMSetRenderTargets(gpu.list, 1, &rtv, FALSE, NULL);
    D3D12_VIEWPORT vp = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)width, (LONG)height};
    ID3D12GraphicsCommandList_RSSetViewports(gpu.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(gpu.list, 1, &scissor);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(gpu.list, gpu.root);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(gpu.list, 1, &gpu.srv_heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
        gpu.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu.srv_heap));
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(gpu.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    float shift = 0.0f; /* cards stand left of the key list panel while it is shown */
    if (panel) {
        float ph = (float)panel_pixels(l, panel);
        float x0 = (float)width - l->margin - l->width;
        float y0 = panel_at_top ? (float)l->margin : ((float)height - ph) * 0.5f;
        if (y0 < (float)l->margin) y0 = (float)l->margin;
        float c[12] = {x0 / width * 2.0f - 1.0f,
                       1.0f - y0 / height * 2.0f,
                       (x0 + l->width) / width * 2.0f - 1.0f,
                       1.0f - (y0 + ph) / height * 2.0f,
                       0.0f,
                       (float)card_rows / gpu.tex_height,
                       1.0f,
                       ((float)card_rows + ph) / gpu.tex_height,
                       1.0f, 0.0f, 0.0f, 0.0f};
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(gpu.list, 1, 12, c, 0);
        ID3D12GraphicsCommandList_DrawInstanced(gpu.list, 4, 1, 0, 0);
        shift = (float)(l->width + l->gap);
    }
    float y = (float)l->margin;
    for (unsigned i = 0; i < count; ++i) {
        float slide;
        float a = card_alpha(now - list[i].shown, list[i].hold, &slide);
        float x0 = (float)width - l->margin - l->width - shift + slide * 40.0f * l->scale;
        float c[12] = {x0 / width * 2.0f - 1.0f,
                       1.0f - y / height * 2.0f,
                       (x0 + l->width) / width * 2.0f - 1.0f,
                       1.0f - (y + l->height) / height * 2.0f,
                       0.0f,
                       (float)(i * l->height) / gpu.tex_height,
                       1.0f,
                       (float)((i + 1) * l->height) / gpu.tex_height,
                       a, 0.0f, 0.0f, 0.0f};
        if (a > 0.0f) {
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(gpu.list, 1, 12, c, 0);
            ID3D12GraphicsCommandList_DrawInstanced(gpu.list, 4, 1, 0, 0);
        }
        y += l->height + l->gap;
    }
    barrier(back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    ID3D12GraphicsCommandList_Close(gpu.list);
    ID3D12CommandList *lists[1] = {(ID3D12CommandList *)gpu.list};
    wait_game_queues();
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    ID3D12CommandQueue_Signal(queue, gpu.fence, ++gpu.fence_value);
    gpu.allocator_fence[index] = gpu.fence_value;
    if (own_queue) wait_fence(gpu.fence_value); /* done before the game presents */
    ID3D12Resource_Release(back);
}

/* ------------------------------------------------------------------ window overlay */
/* Azimuth 1.0.3 removes its D3D12 device when anything else draws into its back buffers. In the
   plugin build the cards therefore go into a small transparent, click-through window that lies on
   top of the game window (same pixels, drawn by the CPU, put on screen by Windows). */
typedef ATOM(WINAPI *RegisterClassExWFn)(const WNDCLASSEXW *);
typedef HWND(WINAPI *CreateWindowExWFn)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
typedef LRESULT(WINAPI *DefWindowProcWFn)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL(WINAPI *PeekMessageWFn)(MSG *, HWND, UINT, UINT, UINT);
typedef LRESULT(WINAPI *DispatchMessageWFn)(const MSG *);
typedef BOOL(WINAPI *UpdateLayeredWindowFn)(HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *,
                                           DWORD);
typedef BOOL(WINAPI *ShowWindowFn)(HWND, int);
typedef BOOL(WINAPI *GetClientRectFn)(HWND, RECT *);
typedef BOOL(WINAPI *ClientToScreenFn)(HWND, POINT *);
typedef BOOL(WINAPI *IsWindowFn)(HWND);
typedef BOOL(WINAPI *IsIconicFn)(HWND);
typedef HWND(WINAPI *GetForegroundWindowFn)(void);
typedef HWND(WINAPI *GetAncestorFn)(HWND, UINT);

static struct {
    RegisterClassExWFn RegisterClassExW;
    CreateWindowExWFn CreateWindowExW;
    DefWindowProcWFn DefWindowProcW;
    PeekMessageWFn PeekMessageW;
    DispatchMessageWFn DispatchMessageW;
    UpdateLayeredWindowFn UpdateLayeredWindow;
    ShowWindowFn ShowWindow;
    GetClientRectFn GetClientRect;
    ClientToScreenFn ClientToScreen;
    IsWindowFn IsWindow;
    IsIconicFn IsIconic;
    GetForegroundWindowFn GetForegroundWindow;
    GetAncestorFn GetAncestor;
} win;

static HWND game_window;
static volatile LONG overlay_started;

static int win_ready(void) {
    HMODULE m = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) return 0;
#define LOAD(name) win.name = (name##Fn)(uintptr_t)GetProcAddress(m, #name)
    LOAD(RegisterClassExW);
    LOAD(CreateWindowExW);
    LOAD(DefWindowProcW);
    LOAD(PeekMessageW);
    LOAD(DispatchMessageW);
    LOAD(UpdateLayeredWindow);
    LOAD(ShowWindow);
    LOAD(GetClientRect);
    LOAD(ClientToScreen);
    LOAD(IsWindow);
    LOAD(IsIconic);
    LOAD(GetForegroundWindow);
    LOAD(GetAncestor);
#undef LOAD
    return win.RegisterClassExW && win.CreateWindowExW && win.DefWindowProcW && win.PeekMessageW &&
           win.DispatchMessageW && win.UpdateLayeredWindow && win.ShowWindow && win.GetClientRect &&
           win.ClientToScreen && win.IsWindow && win.IsIconic && win.GetForegroundWindow && win.GetAncestor &&
           gdi_ready();
}

static LRESULT WINAPI overlay_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    return win.DefWindowProcW(window, message, w, l);
}

/* premultiplied BGRA: copy a w x h block, scaled by alpha, into the frame at (x, y) */
static void blend_block(unsigned char *frame, int frame_w, int frame_h, int x, int y, const unsigned char *src,
                        int src_pitch, int w, int h, float alpha) {
    unsigned scale = (unsigned)(alpha * 256.0f);
    if (scale > 256) scale = 256;
    for (int row = 0; row < h; ++row) {
        int fy = y + row;
        if (fy < 0 || fy >= frame_h) continue;
        const unsigned char *s = src + (size_t)row * src_pitch;
        unsigned char *d = frame + ((size_t)fy * frame_w) * 4;
        for (int col = 0; col < w; ++col) {
            int fx = x + col;
            if (fx < 0 || fx >= frame_w) continue;
            /* premultiplied "over": a card's transparent glow margin must not wipe out what is already
               there (the glow of the card below lay over the bottom of the card above) */
            unsigned sa = (s[col * 4 + 3] * scale) >> 8, keep = 255 - sa;
            for (int c = 0; c < 4; ++c) {
                unsigned v = ((s[col * 4 + c] * scale) >> 8) + (d[fx * 4 + c] * keep + 127) / 255;
                d[fx * 4 + c] = (unsigned char)(v > 255 ? 255 : v);
            }
        }
    }
}

static DWORD WINAPI overlay_thread(void *unused) {
    (void)unused;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = overlay_proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"GriplineCards";
    win.RegisterClassExW(&wc);
    HWND window = win.CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                          WS_EX_NOACTIVATE,
                                      L"GriplineCards", L"Gripline", WS_POPUP, 0, 0, 1, 1, NULL, NULL, instance, NULL);
    if (!window) {
        log_append("NOTIFY: could not create the card window; no Gripline cards.\r\n");
        return 0;
    }
    log_append("NOTIFY: in-game cards ready (window over the game picture).\r\n");
    static unsigned char scratch_cards[1200 * 110 * 3 * 4 * 3]; /* up to 2.5x scale (with the glow), see layout_for */
    static unsigned char scratch_panel[1200 * 1200 * 4];
    unsigned char *frame = NULL;
    size_t frame_bytes = 0;
    HDC dc = gdi.CreateCompatibleDC(NULL);
    HBITMAP bitmap = NULL;
    int visible = 0, frame_w = 0, frame_h = 0, drawn_screen = 0;
    unsigned drawn_count = (unsigned)-1, drawn_panel = (unsigned)-1, drawn_ids[MAX_CARDS];
    static wchar_t title[TEXT_CAPACITY];
    static NotifyPanelRow rows[NOTIFY_PANEL_ROWS];
    for (;;) {
        MSG msg;
        while (win.PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) win.DispatchMessageW(&msg);
        Sleep(16);
        if (!win.IsWindow(game_window)) break;
        Card list[MAX_CARDS];
        ULONGLONG now = GetTickCount64();
        AcquireSRWLockExclusive(&lock);
        for (unsigned i = 0; i < card_count; ++i)
            if (!cards[i].shown) cards[i].shown = now;
        while (card_count && cards[card_count - 1].shown &&
               now - cards[card_count - 1].shown >= cards[card_count - 1].hold + FADE_OUT_MS)
            --card_count;
        unsigned count = card_count;
        for (unsigned i = 0; i < count; ++i) list[i] = cards[i];
        unsigned panel = (unsigned)panel_count, shown_panel = panel_id;
        if (panel) {
            memory_copy(title, panel_title, sizeof(title));
            memory_copy(rows, panel_rows, sizeof(rows[0]) * panel);
        }
        ReleaseSRWLockExclusive(&lock);
        HWND top = win.GetForegroundWindow();
        int focused = top && (top == game_window || win.GetAncestor(game_window, GA_ROOT) == top);
        if ((!count && !panel) || !enabled || !focused || win.IsIconic(game_window)) {
            if (visible) win.ShowWindow(window, SW_HIDE);
            visible = 0;
            continue;
        }
        RECT client;
        POINT origin = {0, 0};
        if (!win.GetClientRect(game_window, &client) || !win.ClientToScreen(game_window, &origin)) continue;
        int screen_w = client.right - client.left, screen_h = client.bottom - client.top;
        if (screen_w < 320 || screen_h < 240) continue;
        Layout l = layout_for((UINT)screen_h);
        if ((size_t)l.width * l.height * MAX_CARDS * 4 > sizeof(scratch_cards) ||
            (size_t)l.width * l.panel_height * 4 > sizeof(scratch_panel))
            continue;
        int want_w = l.margin + 2 * l.width + l.gap + (int)(40.0f * l.scale) + 4;
        if (want_w > screen_w) want_w = screen_w;
        if (want_w != frame_w || screen_h != frame_h) {
            if (bitmap) gdi.DeleteObject(bitmap);
            BITMAPINFO info;
            memset(&info, 0, sizeof(info));
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = want_w;
            info.bmiHeader.biHeight = -screen_h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void *bits = NULL;
            bitmap = gdi.CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
            if (!bitmap || !bits) {
                bitmap = NULL;
                frame_w = 0;
                continue;
            }
            gdi.SelectObject(dc, bitmap);
            frame = (unsigned char *)bits;
            frame_w = want_w;
            frame_h = screen_h;
            frame_bytes = (size_t)frame_w * frame_h * 4;
            drawn_count = (unsigned)-1;
        }
        int redraw = drawn_screen != screen_h || drawn_count != count || drawn_panel != shown_panel;
        for (unsigned i = 0; !redraw && i < count; ++i) redraw = drawn_ids[i] != list[i].id;
        if (redraw) {
            if (!draw_cards(list, count, &l, scratch_cards, l.width * 4) ||
                (panel && !draw_panel(title, rows, panel, &l, scratch_panel, l.width * 4))) {
                fail("could not draw the cards");
                break;
            }
            drawn_screen = screen_h;
            drawn_count = count;
            drawn_panel = shown_panel;
            for (unsigned i = 0; i < count; ++i) drawn_ids[i] = list[i].id;
        }
        memset(frame, 0, frame_bytes);
        int shift = 0;
        if (panel) {
            int ph = panel_pixels(&l, panel);
            int y0 = panel_at_top ? l.margin : (screen_h - ph) / 2;
            if (y0 < l.margin) y0 = l.margin;
            blend_block(frame, frame_w, frame_h, frame_w - l.margin - l.width, y0, scratch_panel, l.width * 4, l.width,
                        ph, 1.0f);
            shift = l.width + l.gap;
        }
        int y = l.margin;
        for (unsigned i = 0; i < count; ++i) {
            float slide;
            float a = card_alpha(now - list[i].shown, list[i].hold, &slide);
            int x0 = frame_w - l.margin - l.width - shift + (int)(slide * 40.0f * l.scale);
            if (a > 0.0f)
                blend_block(frame, frame_w, frame_h, x0, y, scratch_cards + (size_t)i * l.height * l.width * 4,
                            l.width * 4, l.width, l.height, a);
            y += l.height + l.gap;
        }
        gdi.GdiFlush();
        POINT position = {origin.x + screen_w - frame_w, origin.y}, source = {0, 0};
        SIZE size = {frame_w, frame_h};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        win.UpdateLayeredWindow(window, NULL, &position, &size, dc, &source, 0, &blend, ULW_ALPHA);
        if (!visible) win.ShowWindow(window, SW_SHOWNOACTIVATE);
        visible = 1;
    }
    win.ShowWindow(window, SW_HIDE);
    return 0;
}

static void start_window_overlay(HWND window) {
    if (!window || InterlockedExchange(&overlay_started, 1)) return;
    game_window = window;
    if (!win_ready()) {
        fail("user32/gdi32 not available");
        return;
    }
    HANDLE thread = CreateThread(NULL, 0, overlay_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else fail("could not start the card window");
}

/* ------------------------------------------------------------------ hooks */

typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDXGISwapChain *, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *Present1Fn)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
typedef HRESULT(STDMETHODCALLTYPE *ResizeBuffersFn)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *ResizeBuffers1Fn)(IDXGISwapChain3 *, UINT, UINT, UINT, DXGI_FORMAT, UINT,
                                                     const UINT *, IUnknown *const *);
typedef HRESULT(STDMETHODCALLTYPE *CreateSwapChainForHwndFn)(IDXGIFactory2 *, IUnknown *, HWND,
                                                             const DXGI_SWAP_CHAIN_DESC1 *,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *,
                                                             IDXGISwapChain1 **);
typedef HRESULT(STDMETHODCALLTYPE *CreateSwapChainForCompositionFn)(IDXGIFactory2 *, IUnknown *,
                                                                    const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *,
                                                                    IDXGISwapChain1 **);
typedef HRESULT(STDMETHODCALLTYPE *CreateSwapChainFn)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *,
                                                      IDXGISwapChain **);
typedef HRESULT(WINAPI *CreateFactory1Fn)(REFIID, void **);
typedef HRESULT(WINAPI *CreateFactory2Fn)(UINT, REFIID, void **);

static PresentFn original_present;
static Present1Fn original_present1;
static ResizeBuffersFn original_resize;
static ResizeBuffers1Fn original_resize1;
static CreateSwapChainForHwndFn original_create_hwnd;
static CreateSwapChainFn original_create;
static CreateSwapChainForCompositionFn original_create_composition;
static CreateFactory1Fn original_factory1;
static CreateFactory2Fn original_factory2;

static int swap_slot(void **slot, void *hook, void **original) {
    DWORD old;
    if (*slot == hook) return 1;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return 0;
    *original = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void *), old, &old);
    return 1;
}

static void hook_swapchain(IUnknown *device, IDXGISwapChain *swapchain);
static volatile LONG late_adopt;
static volatile LONG making_helper; /* the helper swap chain must not count as the game's */

/* late hook: Present was hooked through a helper swap chain, so the game's swap chain is only met
   here; for D3D12 its GetDevice returns the command queue */
/* late hook: the game's D3D12 queue is not reachable from its swap chain, so ExecuteCommandLists
   (hooked through the helper queue, whose vtable the game's queue shares) remembers the last direct
   queue; the next Present takes it when it belongs to the swap chain's device */
typedef void(STDMETHODCALLTYPE *ExecuteFn)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
static ExecuteFn original_execute;

static void STDMETHODCALLTYPE hook_execute(ID3D12CommandQueue *self, UINT count, ID3D12CommandList *const *lists) {
    if (late_adopt && !broken && !swapchain_seen && self != queue) {
        int known = 0;
        AcquireSRWLockShared(&game_queue_lock);
        for (unsigned i = 0; i < game_queue_count; ++i) known |= game_queues[i] == self;
        ReleaseSRWLockShared(&game_queue_lock);
        if (!known && game_queue_count < ARRAY_COUNT(game_queues)) {
            __try {
                D3D12_COMMAND_QUEUE_DESC qd = ID3D12CommandQueue_GetDesc(self);
                if (qd.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                    AcquireSRWLockExclusive(&game_queue_lock);
                    known = 0;
                    for (unsigned i = 0; i < game_queue_count; ++i) known |= game_queues[i] == self;
                    if (!known && game_queue_count < ARRAY_COUNT(game_queues)) {
                        ID3D12CommandQueue_AddRef(self);
                        game_queues[game_queue_count++] = self;
                        log_append("NOTIFY: a game D3D12 queue was seen.\r\n");
                    }
                    ReleaseSRWLockExclusive(&game_queue_lock);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    original_execute(self, count, lists);
}

static void adopt_swapchain(IDXGISwapChain *swapchain) {
    if (swapchain_seen || !late_adopt || broken) return;
    static volatile LONG first;
    if (!InterlockedExchange(&first, 1)) log_append("NOTIFY: first Present through the late hook.\r\n");
    __try {
        DXGI_SWAP_CHAIN_DESC sd;
        if (FAILED(IDXGISwapChain_GetDesc(swapchain, &sd)) || !sd.OutputWindow) return;
        log_append_value("NOTIFY: game swap chain format ", (int)sd.BufferDesc.Format);
        log_append_value("NOTIFY: game swap chain width ", (int)sd.BufferDesc.Width);
        swapchain_seen = swapchain;
        log_append("NOTIFY: game swap chain found; cards go into a window over the game.\r\n");
        start_window_overlay(sd.OutputWindow);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fail("an exception while adopting the swap chain");
    }
}

static void before_present(IDXGISwapChain *swapchain) {
    adopt_swapchain(swapchain);
    if (pending_count && enabled && !broken && swapchain == swapchain_seen) {
        ULONGLONG now = GetTickCount64();
        if (!first_present) first_present = now;
        if (now - first_present >= DEFER_MS) flush_pending();
    }
    if (!enabled || broken || (!card_count && !panel_count) || swapchain != swapchain_seen) return;
    __try {
        render(swapchain);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fail("an exception while drawing");
    }
}

static HRESULT STDMETHODCALLTYPE hook_present(IDXGISwapChain *swapchain, UINT sync, UINT flags) {
    if (!(flags & DXGI_PRESENT_TEST)) before_present(swapchain);
    return original_present(swapchain, sync, flags);
}

static HRESULT STDMETHODCALLTYPE hook_present1(IDXGISwapChain1 *swapchain, UINT sync, UINT flags,
                                               const DXGI_PRESENT_PARAMETERS *params) {
    if (!(flags & DXGI_PRESENT_TEST)) before_present((IDXGISwapChain *)swapchain);
    return original_present1(swapchain, sync, flags, params);
}

static void idle_gpu(void) {
    if (gpu.fence) {
        __try {
            wait_fence(gpu.fence_value);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

static HRESULT STDMETHODCALLTYPE hook_resize(IDXGISwapChain *swapchain, UINT count, UINT width, UINT height,
                                             DXGI_FORMAT format, UINT flags) {
    idle_gpu();
    return original_resize(swapchain, count, width, height, format, flags);
}

static HRESULT STDMETHODCALLTYPE hook_resize1(IDXGISwapChain3 *swapchain, UINT count, UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags, const UINT *mask,
                                              IUnknown *const *queues) {
    idle_gpu();
    return original_resize1(swapchain, count, width, height, format, flags, mask, queues);
}

static void hook_swapchain(IUnknown *device, IDXGISwapChain *swapchain) {
    ID3D12CommandQueue *q = NULL;
    if (making_helper || !swapchain || !device ||
        FAILED(IUnknown_QueryInterface(device, &IID_ID3D12CommandQueue, (void **)&q)))
        return; /* not a D3D12 swap chain (or a helper window) */
    idle_gpu();
    if (queue && queue != q) {
        /* a new queue (device re-created): start over with fresh objects */
        log_append("NOTIFY: game created a new D3D12 queue; overlay objects are rebuilt.\r\n");
        static const char zero[sizeof(gpu)];
        memory_copy(&gpu, zero, sizeof(gpu));
        ID3D12CommandQueue_Release(queue);
    } else if (queue == q) {
        ID3D12CommandQueue_Release(q);
    }
    queue = q;
    swapchain_seen = swapchain;
    void **vtable = *(void ***)swapchain;
    int ok = swap_slot(&vtable[8], (void *)hook_present, (void **)&original_present) &&
             swap_slot(&vtable[13], (void *)hook_resize, (void **)&original_resize) &&
             swap_slot(&vtable[22], (void *)hook_present1, (void **)&original_present1);
    IDXGISwapChain3 *sc3 = NULL;
    if (ok && SUCCEEDED(IDXGISwapChain_QueryInterface(swapchain, &IID_IDXGISwapChain3, (void **)&sc3))) {
        void **v3 = *(void ***)sc3;
        swap_slot(&v3[39], (void *)hook_resize1, (void **)&original_resize1);
        IDXGISwapChain3_Release(sc3);
    }
    log_append(ok ? "NOTIFY: game swap chain found; Gripline cards can be shown.\r\n"
                  : "NOTIFY: could not hook the game's swap chain.\r\n");
}

static HRESULT STDMETHODCALLTYPE hook_create_hwnd(IDXGIFactory2 *factory, IUnknown *device, HWND window,
                                                  const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                  IDXGIOutput *output, IDXGISwapChain1 **result) {
    HRESULT hr = original_create_hwnd(factory, device, window, desc, fullscreen, output, result);
    if (SUCCEEDED(hr) && result && *result) {
        __try {
            hook_swapchain(device, (IDXGISwapChain *)*result);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fail("an exception while hooking the swap chain");
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_create(IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc,
                                             IDXGISwapChain **result) {
    HRESULT hr = original_create(factory, device, desc, result);
    if (SUCCEEDED(hr) && result && *result) {
        __try {
            hook_swapchain(device, *result);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fail("an exception while hooking the swap chain");
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hook_create_composition(IDXGIFactory2 *factory, IUnknown *device,
                                                         const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *output,
                                                         IDXGISwapChain1 **result) {
    HRESULT hr = original_create_composition(factory, device, desc, output, result);
    if (SUCCEEDED(hr) && result && *result) {
        __try {
            hook_swapchain(device, (IDXGISwapChain *)*result);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            fail("an exception while hooking the swap chain");
        }
    }
    return hr;
}

static void hook_factory(void *object) {
    IDXGIFactory2 *factory2 = NULL;
    IUnknown *unknown = (IUnknown *)object;
    void **vtable = *(void ***)object;
    swap_slot(&vtable[10], (void *)hook_create, (void **)&original_create);
    if (SUCCEEDED(IUnknown_QueryInterface(unknown, &IID_IDXGIFactory2, (void **)&factory2))) {
        void **v2 = *(void ***)factory2;
        swap_slot(&v2[15], (void *)hook_create_hwnd, (void **)&original_create_hwnd);
        swap_slot(&v2[24], (void *)hook_create_composition, (void **)&original_create_composition);
        IDXGIFactory2_Release(factory2);
    }
}

static HRESULT WINAPI hook_factory1(REFIID riid, void **out) {
    log_append("NOTIFY: CreateDXGIFactory1 called after the early hook.\r\n");
    HRESULT hr = original_factory1(riid, out);
    if (SUCCEEDED(hr) && out && *out && !broken) hook_factory(*out);
    return hr;
}

static HRESULT WINAPI hook_factory2(UINT flags, REFIID riid, void **out) {
    log_append("NOTIFY: CreateDXGIFactory2 called after the early hook.\r\n");
    HRESULT hr = original_factory2(flags, riid, out);
    if (SUCCEEDED(hr) && out && *out && !broken) hook_factory(*out);
    return hr;
}

static int same_ascii(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* IAT slot of `function` imported by name from `dll` */
static void **import_slot(unsigned char *module, const char *dll, const char *function) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(module + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(module + dir.VirtualAddress); d->Name; ++d) {
        if (!same_ascii((const char *)(module + d->Name), dll) || !d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(module + d->OriginalFirstThunk);
        void **slots = (void **)(module + d->FirstThunk);
        for (unsigned i = 0; names[i].u1.AddressOfData; ++i) {
            if (names[i].u1.AddressOfData & IMAGE_ORDINAL_FLAG64) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(module + (DWORD)names[i].u1.AddressOfData);
            if (same_ascii((const char *)n->Name, function)) return &slots[i];
        }
    }
    return NULL;
}

int notify_install(unsigned char *host) {
    void **slot1 = import_slot(host, "dxgi.dll", "CreateDXGIFactory1");
    void **slot2 = import_slot(host, "dxgi.dll", "CreateDXGIFactory2");
    int hooked = 0;
    if (slot1 && swap_slot(slot1, (void *)hook_factory1, (void **)&original_factory1)) ++hooked;
    if (slot2 && swap_slot(slot2, (void *)hook_factory2, (void **)&original_factory2)) ++hooked;
    return hooked > 0;
}

/* plugin build: the platform may load Gripline after Azimuth made its swap chain. A hidden helper
   swap chain of the same kind shares the game's vtable, so Present is hooked through it. */
typedef HRESULT(WINAPI *CreateDeviceFn)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
typedef HRESULT(WINAPI *CreateFactory2Fn)(UINT, REFIID, void **);
typedef HWND(WINAPI *CreateWindowFn)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL(WINAPI *DestroyWindowFn)(HWND);
int notify_install_late(void) {
    if (swapchain_seen) return 1;
    HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CreateDeviceFn create_device = d3d12 ? (CreateDeviceFn)(uintptr_t)GetProcAddress(d3d12, "D3D12CreateDevice") : NULL;
    CreateFactory2Fn create_factory =
        dxgi ? (CreateFactory2Fn)(uintptr_t)GetProcAddress(dxgi, "CreateDXGIFactory2") : NULL;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    CreateWindowFn create_window = user32 ? (CreateWindowFn)(uintptr_t)GetProcAddress(user32, "CreateWindowExW") : NULL;
    DestroyWindowFn destroy_window =
        user32 ? (DestroyWindowFn)(uintptr_t)GetProcAddress(user32, "DestroyWindow") : NULL;
    if (!create_device || !create_factory || !create_window || !destroy_window) return 0;
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *q = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *helper = NULL;
    HWND window = NULL;
    int ok = 0;
    __try {
        D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
        DXGI_SWAP_CHAIN_DESC1 sd = {0};
        sd.Width = 64;
        sd.Height = 64;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        InterlockedExchange(&making_helper, 1);
        window = create_window(0, L"STATIC", L"GriplineHelper", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
        if (window && SUCCEEDED(create_device(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device)) &&
            SUCCEEDED(ID3D12Device_CreateCommandQueue(device, &qd, &IID_ID3D12CommandQueue, (void **)&q)) &&
            SUCCEEDED(create_factory(0, &IID_IDXGIFactory2, (void **)&factory)) &&
            SUCCEEDED(IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)q, window, &sd, NULL, NULL, &helper))) {
            void **vtable = *(void ***)helper;
            log_append_module("NOTIFY: helper swap chain vtable ", vtable);
            log_append_module("NOTIFY: Present before the hook ", vtable[8]);
            InterlockedExchange(&late_adopt, 1);
            void **queue_vtable = *(void ***)q;
            log_append_module("NOTIFY: helper queue ExecuteCommandLists ", queue_vtable[10]);
            ok = swap_slot(&queue_vtable[10], (void *)hook_execute, (void **)&original_execute) &&
                 swap_slot(&vtable[8], (void *)hook_present, (void **)&original_present) &&
                 swap_slot(&vtable[22], (void *)hook_present1, (void **)&original_present1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    InterlockedExchange(&making_helper, 0);
    if (helper) IDXGISwapChain1_Release(helper);
    if (factory) IDXGIFactory2_Release(factory);
    if (q) ID3D12CommandQueue_Release(q);
    if (device) ID3D12Device_Release(device);
    if (window) destroy_window(window);
    log_append(ok ? "NOTIFY: late hook on Present; waiting for the game's swap chain.\r\n"
                  : "NOTIFY: late hook failed; no Gripline cards.\r\n");
    return ok;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestNotifyStyleGet(void) {
    return notify_style();
}
__declspec(dllexport) void Fh2TestNotifySetStyle(int style) {
    notify_set_style(style);
}
__declspec(dllexport) void Fh2TestNotifyShow(const char *text, int tint, const char *badge) {
    if (tint < 0) notify_show(text);
    else notify_show_tinted(text, tint, badge, 0);
}
__declspec(dllexport) unsigned Fh2TestNotifyCount(void) {
    return card_count;
}

__declspec(dllexport) int Fh2TestNotifyText(unsigned index, wchar_t *line1, wchar_t *line2) {
    if (index >= card_count) return 0;
    memory_copy(line1, cards[index].line1, sizeof(cards[index].line1));
    memory_copy(line2, cards[index].line2, sizeof(cards[index].line2));
    return 1;
}

/* the card's fixed colour (-1 = from the settings) and its title line */
__declspec(dllexport) int Fh2TestNotifyStyle(unsigned index, wchar_t *title) {
    if (index >= card_count) return -2;
    memory_copy(title, cards[index].badge, sizeof(cards[index].badge));
    return cards[index].color;
}

__declspec(dllexport) int Fh2TestNotifyInstall(void *host) {
    return notify_install((unsigned char *)host);
}

__declspec(dllexport) void *Fh2TestNotifyFactoryHooks(int which) {
    return which == 1 ? (void *)hook_factory1 : (void *)hook_factory2;
}

__declspec(dllexport) int Fh2TestNotifyDraw(unsigned height, unsigned char *pixels, int pitch, int *width,
                                            int *rows) {
    Layout l = layout_for(height);
    *width = l.width;
    *rows = l.height * MAX_CARDS;
    if (!pixels) return 1;
    if (!gdi_ready()) return 0;
    return draw_cards(cards, card_count, &l, pixels, pitch);
}

__declspec(dllexport) int Fh2TestNotifyPanelRow(unsigned index, wchar_t *label, wchar_t *keys) {
    if (index >= (unsigned)panel_count) return 0;
    memory_copy(label, panel_rows[index].label, sizeof(panel_rows[index].label));
    memory_copy(keys, panel_rows[index].keys, sizeof(panel_rows[index].keys));
    return 1;
}

/* Key list panel: rows/width of its bitmap; with pixels, draws it (l.panel_height rows). */
__declspec(dllexport) int Fh2TestNotifyPanelDraw(unsigned height, unsigned char *pixels, int pitch, int *width,
                                                 int *rows, unsigned *count, wchar_t *first_label,
                                                 wchar_t *first_keys) {
    Layout l = layout_for(height);
    *width = l.width;
    *rows = l.panel_height;
    *count = (unsigned)panel_count;
    if (panel_count) {
        memory_copy(first_label, panel_rows[0].label, sizeof(panel_rows[0].label));
        memory_copy(first_keys, panel_rows[0].keys, sizeof(panel_rows[0].keys));
    }
    if (!pixels) return 1;
    if (!gdi_ready()) return 0;
    return draw_panel(panel_title, panel_rows, (unsigned)panel_count, &l, pixels, pitch);
}
#endif
