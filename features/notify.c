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

#define MAX_CARDS 3
#define CARD_MS 5000u
#define STARTUP_CARD_MS 9000u /* start cards (mods found, Mapper) stay longer */
#define FADE_IN_MS 250u
#define FADE_OUT_MS 500u
#define TEXT_CAPACITY 96

typedef struct {
    wchar_t line1[TEXT_CAPACITY], line2[TEXT_CAPACITY];
    ULONGLONG shown; /* tick of the first frame it was drawn in; 0 = not shown yet */
    ULONGLONG hold; /* ms fully visible before the fade-out */
    unsigned id;
    unsigned tag; /* notify_show_step: the card a later step replaces (0 = none) */
} Card;

static SRWLOCK lock = SRWLOCK_INIT;
static Card cards[MAX_CARDS]; /* [0] = newest */
static unsigned card_count, next_id = 1;
static volatile LONG cards_changed;
static volatile LONG enabled = 1, broken;

static wchar_t panel_title[TEXT_CAPACITY];
static NotifyPanelRow panel_rows[NOTIFY_PANEL_ROWS];
static volatile LONG panel_count; /* 0 = panel off */
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

static void show_card(const char *text, ULONGLONG hold, unsigned tag);

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

static void show_card(const char *text, ULONGLONG hold, unsigned tag) {
    if (!text) return;
    log_append("GRIPLINE: ");
    for (const char *p = text; *p; ++p) {
        char one[2] = {*p == '\n' ? ' ' : *p, 0};
        if (*p == '\n') {
            log_append(" - ");
        } else {
            log_append(one);
        }
    }
    log_append("\r\n");
    size_t first = 0;
    while (text[first] && text[first] != '\n') ++first;
    Card card;
    utf8_line(text, first, card.line1);
    utf8_line(text[first] ? text + first + 1 : text + first, text[first] ? text_length(text + first + 1) : 0,
              card.line2);
    card.shown = 0;
    card.hold = hold;
    card.tag = tag;
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

void notify_set_enabled(int on) {
    InterlockedExchange(&enabled, on ? 1 : 0);
}


int notify_enabled(void) {
    return (int)enabled;
}

void notify_panel_set(const wchar_t *title, const NotifyPanelRow *rows, unsigned count) {
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
    int panel_header, panel_row, panel_height; /* key list panel; panel_height = all rows */
} Layout;

static Layout layout_for(UINT screen_height) {
    float s = (float)screen_height / 1080.0f;
    if (s < 0.6f) s = 0.6f;
    if (s > 2.5f) s = 2.5f;
    Layout l;
    l.width = (int)(430 * s);
    l.height = (int)(88 * s);
    l.gap = (int)(10 * s);
    l.margin = (int)(28 * s);
    l.radius = (int)(12 * s);
    l.icon = (int)(58 * s);
    l.pad = (int)(14 * s);
    l.bar = (int)(5 * s) > 2 ? (int)(5 * s) : 2;
    l.panel_header = (int)(60 * s);
    l.panel_row = (int)(20 * s);
    l.panel_height = l.panel_header + NOTIFY_PANEL_ROWS * l.panel_row + l.pad;
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
static int draw_cards(const Card *list, unsigned count, const Layout *l, unsigned char *pixels, int pitch) {
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
    const unsigned char accent[3] = {225, 120, 38}; /* B, G, R: Gripline blue */
    const unsigned char logo[3] = {255, 185, 110};
    for (unsigned c = 0; c < count; ++c) {
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
                float fx = (x + 0.5f) * GRIPLINE_LOGO_SIZE / l->icon - 0.5f;
                float fy = (y + 0.5f) * GRIPLINE_LOGO_SIZE / l->icon - 0.5f;
                int x0 = (int)fx, y0 = (int)fy;
                if (fx < 0) { fx = 0; x0 = 0; }
                if (fy < 0) { fy = 0; y0 = 0; }
                int x1 = x0 + 1 < GRIPLINE_LOGO_SIZE ? x0 + 1 : x0;
                int y1 = y0 + 1 < GRIPLINE_LOGO_SIZE ? y0 + 1 : y0;
                float ax = fx - x0, ay = fy - y0;
                float a = (gripline_logo_alpha[y0 * GRIPLINE_LOGO_SIZE + x0] * (1 - ax) +
                           gripline_logo_alpha[y0 * GRIPLINE_LOGO_SIZE + x1] * ax) * (1 - ay) +
                          (gripline_logo_alpha[y1 * GRIPLINE_LOGO_SIZE + x0] * (1 - ax) +
                           gripline_logo_alpha[y1 * GRIPLINE_LOGO_SIZE + x1] * ax) * ay;
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
        HGDIOBJ old_font = gdi.SelectObject(dc, title);
        gdi.SetTextColor(dc, RGB(110, 185, 255));
        gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &clip, L"GRIPLINE", 8, NULL);
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

/* height in pixels of the key list panel with `count` rows */
static int panel_pixels(const Layout *l, unsigned count) {
    return l->panel_header + (int)count * l->panel_row + l->pad;
}

/* Draws the key list panel into l->panel_height rows (premultiplied BGRA, unused rows clear). */
static int draw_panel(const wchar_t *title, const NotifyPanelRow *rows, unsigned count, const Layout *l,
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
    const unsigned char accent[3] = {225, 120, 38};
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
    gdi.SetTextColor(dc, RGB(110, 185, 255));
    gdi.ExtTextOutW(dc, tx, (int)(10 * s), ETO_CLIPPED, &clip, L"GRIPLINE", 8, NULL);
    gdi.SelectObject(dc, main);
    gdi.SetTextColor(dc, RGB(245, 247, 250));
    gdi.ExtTextOutW(dc, tx, (int)(28 * s), ETO_CLIPPED, &clip, title, (UINT)text_width_chars(title), NULL);
    for (unsigned i = 0; i < count; ++i) {
        int y = l->panel_header + (int)i * l->panel_row;
        RECT left = {tx, y, kx - l->pad / 2, y + l->panel_row};
        RECT right = {kx, y, w - l->pad, y + l->panel_row};
        gdi.SelectObject(dc, label);
        gdi.SetTextColor(dc, RGB(200, 207, 218));
        gdi.ExtTextOutW(dc, tx, y, ETO_CLIPPED, &left, rows[i].label, (UINT)text_width_chars(rows[i].label), NULL);
        gdi.SelectObject(dc, keys);
        gdi.SetTextColor(dc, RGB(130, 195, 255));
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
    if (height != gpu.screen_height) {
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
        float y0 = ((float)height - ph) * 0.5f;
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
        float x0 = (float)width - l->margin - l->width - shift + slide * 40.0f * l->height / 88.0f;
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
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    ID3D12CommandQueue_Signal(queue, gpu.fence, ++gpu.fence_value);
    gpu.allocator_fence[index] = gpu.fence_value;
    ID3D12Resource_Release(back);
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
static void adopt_swapchain(IDXGISwapChain *swapchain) {
    if (swapchain_seen || !late_adopt || broken) return;
    static volatile LONG reported;
    if (!InterlockedExchange(&reported, 1)) log_append("NOTIFY: first Present through the late hook.\r\n");
    IUnknown *queue_object = NULL;
    __try {
        if (SUCCEEDED(IDXGISwapChain_GetDevice(swapchain, &IID_IUnknown, (void **)&queue_object)) && queue_object) {
            hook_swapchain(queue_object, swapchain);
            IUnknown_Release(queue_object);
            if (!swapchain_seen && reported == 1) {
                InterlockedExchange(&reported, 2);
                log_append("NOTIFY: that swap chain's device is no D3D12 queue.\r\n");
            }
        } else if (reported == 1) {
            InterlockedExchange(&reported, 2);
            log_append("NOTIFY: GetDevice failed on the presented swap chain.\r\n");
        }
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
            ok = swap_slot(&vtable[8], (void *)hook_present, (void **)&original_present) &&
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
__declspec(dllexport) unsigned Fh2TestNotifyCount(void) {
    return card_count;
}

__declspec(dllexport) int Fh2TestNotifyText(unsigned index, wchar_t *line1, wchar_t *line2) {
    if (index >= card_count) return 0;
    memory_copy(line1, cards[index].line1, sizeof(cards[index].line1));
    memory_copy(line2, cards[index].line2, sizeof(cards[index].line2));
    return 1;
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
