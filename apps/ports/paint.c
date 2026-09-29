/* Native GEM paint program. GPL-2.0-or-later. Original code.
 *
 * 640x480 canvas of 16 fixed colours (one byte per pixel) in a 572x426 view.
 * Tools: pencil, brush, eraser, line, rectangle, filled rectangle, ellipse,
 * filled ellipse, flood fill, colour picker and pan. Multi-level undo/redo
 * stores only the changed rectangle of each action, packed to 4 bits per
 * pixel and bounded by UNDO_BUDGET bytes. The screen is repainted only for
 * the rectangle that changed. Files are 8-bit uncompressed BMPs with a
 * 16-entry palette, the format the IMAGES viewer exports. */
#include "app.h"
#include "drives.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CW 640 /* canvas size */
#define CH 480
#define VX 68 /* view on screen */
#define VY 26
#define VW 572
#define VH 426
#define UNDO_MAX 64
#define UNDO_BUDGET (1024 * 1024)
#define BMP_MAX_FILE (2 * 1024 * 1024)
#define FILL_STACK 16384

/* Same 16 pens as the VDI default palette, in VDI order (0..255). */
static const uint8_t pal[16][3] = {
    {255, 255, 255}, {0, 0, 0},     {255, 0, 0},   {0, 255, 0},   {0, 0, 255},   {0, 255, 255},
    {255, 255, 0},   {255, 0, 255}, {192, 192, 192}, {128, 128, 128}, {128, 0, 0},   {0, 128, 0},
    {0, 0, 128},     {0, 128, 128}, {128, 128, 0}, {128, 0, 128}};
/* VDI pen to plane value used when blitting planar data (as in the viewer). */
static const unsigned char physical[16] = {0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13};

enum { T_PEN, T_BRUSH, T_ERASE, T_LINE, T_RECT, T_FRECT, T_ELL, T_FELL, T_FILL, T_PICK, T_PAN, NTOOLS };
static const char *const tool_label[NTOOLS] = {"Pen", "Brs", "Ers", "Lin", "Rec", "FRc", "Ell", "FEl", "Fil", "Pik", "Pan"};
static const char *const tool_name[NTOOLS] = {"Pencil", "Brush", "Eraser", "Line", "Rectangle", "Filled rect",
                                              "Ellipse", "Filled ellipse", "Fill", "Picker", "Pan"};
static const char tool_key[NTOOLS + 1] = "pbxlrfekgih";
static const int sizes[4] = {1, 2, 4, 8};

/* ---- canvas and dirty rectangles (pure logic, host tested) ---- */
static uint8_t *cv, *bk; /* current canvas, last committed canvas */
static int dx0, dy0, dx1 = -1, dy1 = -1; /* changed since last commit (undo rectangle) */
static int sx0, sy0, sx1 = -1, sy1 = -1; /* changed since last repaint */
static int modified;

static int canvas_alloc(void)
{
    if (!cv)
        cv = calloc(CW * CH, 1);
    if (!bk)
        bk = calloc(CW * CH, 1);
    return cv && bk;
}
static void grow(int *a0, int *b0, int *a1, int *b1, int x0, int y0, int x1, int y1)
{
    if (*a1 < *a0) {
        *a0 = x0;
        *b0 = y0;
        *a1 = x1;
        *b1 = y1;
        return;
    }
    if (x0 < *a0)
        *a0 = x0;
    if (y0 < *b0)
        *b0 = y0;
    if (x1 > *a1)
        *a1 = x1;
    if (y1 > *b1)
        *b1 = y1;
}
static void mark_screen(int x0, int y0, int x1, int y1)
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= CW)
        x1 = CW - 1;
    if (y1 >= CH)
        y1 = CH - 1;
    if (x1 >= x0 && y1 >= y0)
        grow(&sx0, &sy0, &sx1, &sy1, x0, y0, x1, y1);
}
static void mark(int x0, int y0, int x1, int y1)
{
    grow(&dx0, &dy0, &dx1, &dy1, x0, y0, x1, y1);
    grow(&sx0, &sy0, &sx1, &sy1, x0, y0, x1, y1);
}
static void pset(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= CW || y >= CH)
        return;
    cv[y * CW + x] = c;
    mark(x, y, x, y);
}
static void hspan(int x0, int x1, int y, int c)
{
    if (y < 0 || y >= CH)
        return;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (x0 < 0)
        x0 = 0;
    if (x1 >= CW)
        x1 = CW - 1;
    if (x0 > x1)
        return;
    memset(cv + y * CW + x0, c, x1 - x0 + 1);
    mark(x0, y, x1, y);
}
static void stamp(int x, int y, int s, int c)
{
    if (s <= 1) {
        pset(x, y, c);
        return;
    }
    int o = s / 2;
    for (int j = 0; j < s; j++)
        for (int i = 0; i < s; i++) {
            int a = 2 * i - (s - 1), b = 2 * j - (s - 1);
            if (s <= 2 || a * a + b * b <= s * s)
                pset(x - o + i, y - o + j, c);
        }
}
static void draw_line(int x0, int y0, int x1, int y1, int s, int c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        stamp(x0, y0, s, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
static void order(int *a, int *b)
{
    if (*a > *b) {
        int t = *a;
        *a = *b;
        *b = t;
    }
}
static void fill_rect(int x0, int y0, int x1, int y1, int c)
{
    order(&x0, &x1);
    order(&y0, &y1);
    for (int y = y0; y <= y1; y++)
        hspan(x0, x1, y, c);
}
static void draw_rect(int x0, int y0, int x1, int y1, int s, int c)
{
    order(&x0, &x1);
    order(&y0, &y1);
    draw_line(x0, y0, x1, y0, s, c);
    draw_line(x1, y0, x1, y1, s, c);
    draw_line(x1, y1, x0, y1, s, c);
    draw_line(x0, y1, x0, y0, s, c);
}
static uint32_t isqrt64(uint64_t n)
{
    uint64_t r = 0, bit = 1ULL << 62;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}
/* Ellipse inscribed in the box (x0,y0)-(x1,y1), integer only. */
static void draw_ellipse(int x0, int y0, int x1, int y1, int s, int c, int filled)
{
    order(&x0, &x1);
    order(&y0, &y1);
    int a = x1 - x0, b = y1 - y0;
    if (a == 0 || b == 0) {
        draw_line(x0, y0, x1, y1, filled ? 1 : s, c);
        return;
    }
    int pl = 0, pr = 0;
    for (int y = 0; y <= b; y++) {
        int d = 2 * y - b;
        uint64_t w = isqrt64((uint64_t)(b * b - d * d) * (uint64_t)a * (uint64_t)a);
        int hw2 = (int)((w + b / 2) / b); /* twice the half width */
        if (hw2 > a)
            hw2 = a;
        int l = (2 * x0 + a - hw2 + 1) / 2, r = (2 * x0 + a + hw2) / 2;
        if (filled)
            hspan(l, r, y0 + y, c);
        else {
            if (y == 0) {
                pl = l;
                pr = r;
            }
            draw_line(pl, y0 + y - (y > 0), l, y0 + y, s, c);
            draw_line(pr, y0 + y - (y > 0), r, y0 + y, s, c);
            pl = l;
            pr = r;
        }
    }
}
/* Scanline flood fill (Heckbert). Returns filled bounding state via marks.
 * If the seed stack overflows the fill is left incomplete, never unsafe. */
struct seg {
    int16_t y, xl, xr, dy;
};
static int flood(int x, int y, int c)
{
    if (x < 0 || y < 0 || x >= CW || y >= CH)
        return 0;
    int ov = cv[y * CW + x];
    if (ov == c)
        return 0;
    struct seg *st = malloc(FILL_STACK * sizeof(*st)), *sp = st;
    if (!st)
        return 0;
#define PUSH(Y, XL, XR, DY)                                                                        \
    do {                                                                                           \
        if (sp < st + FILL_STACK && (Y) + (DY) >= 0 && (Y) + (DY) < CH) {                          \
            sp->y = (Y);                                                                           \
            sp->xl = (XL);                                                                         \
            sp->xr = (XR);                                                                         \
            sp->dy = (DY);                                                                         \
            sp++;                                                                                  \
        }                                                                                          \
    } while (0)
    PUSH(y, x, x, 1);
    PUSH(y + 1, x, x, -1);
    while (sp > st) {
        sp--;
        int dy = sp->dy, yy = sp->y + dy, x1 = sp->xl, x2 = sp->xr, l, xx;
        uint8_t *row = cv + yy * CW;
        for (xx = x1; xx >= 0 && row[xx] == ov; xx--)
            row[xx] = c;
        if (xx < x1)
            mark(xx + 1, yy, x1, yy);
        if (xx >= x1)
            goto skip;
        l = xx + 1;
        if (l < x1)
            PUSH(yy, l, x1 - 1, -dy);
        xx = x1 + 1;
        do {
            int start = xx;
            for (; xx < CW && row[xx] == ov; xx++)
                row[xx] = c;
            if (xx > start)
                mark(start, yy, xx - 1, yy);
            PUSH(yy, l, xx - 1, dy);
            if (xx > x2 + 1)
                PUSH(yy, x2 + 1, xx - 1, -dy);
        skip:
            for (xx++; xx <= x2 && row[xx] != ov; xx++)
                ;
            l = xx;
        } while (xx <= x2);
    }
#undef PUSH
    free(st);
    return 1;
}

/* ---- undo / redo: swap records of the changed rectangle ---- */
typedef struct {
    int x, y, w, h;
    size_t bytes;
    uint8_t *d;
} Undo;
static Undo ul[UNDO_MAX];
static int un, uc; /* records, current position (records below uc can be undone) */
static size_t ubytes;

static void pack(const uint8_t *src, int x, int y, int w, int h, uint8_t *out)
{
    int stride = (w + 1) / 2;
    for (int j = 0; j < h; j++) {
        const uint8_t *r = src + (y + j) * CW + x;
        uint8_t *o = out + j * stride;
        for (int i = 0; i < w; i += 2)
            o[i / 2] = (r[i] & 15) | (i + 1 < w ? r[i + 1] << 4 : 0);
    }
}
static void unpack(uint8_t *dst, int x, int y, int w, int h, const uint8_t *in)
{
    int stride = (w + 1) / 2;
    for (int j = 0; j < h; j++) {
        uint8_t *r = dst + (y + j) * CW + x;
        const uint8_t *o = in + j * stride;
        for (int i = 0; i < w; i++)
            r[i] = (i & 1) ? o[i / 2] >> 4 : o[i / 2] & 15;
    }
}
static void drop_record(int i)
{
    ubytes -= ul[i].bytes;
    free(ul[i].d);
}
static void history_clear(void)
{
    for (int i = 0; i < un; i++)
        drop_record(i);
    un = uc = 0;
    ubytes = 0;
}
static void dirty_reset(void)
{
    dx0 = dy0 = 0;
    dx1 = dy1 = -1;
}
/* Throw away uncommitted drawing (shape rubber-banding, cancelled stroke). */
static void revert_dirty(void)
{
    if (dx1 >= dx0 && dy1 >= dy0)
        for (int y = dy0; y <= dy1; y++)
            memcpy(cv + y * CW + dx0, bk + y * CW + dx0, dx1 - dx0 + 1);
    dirty_reset();
}
/* Make the uncommitted changes a single undoable action. */
static int commit(void)
{
    if (dx1 < dx0 || dy1 < dy0)
        return 0;
    int x = dx0, y = dy0, w = dx1 - dx0 + 1, h = dy1 - dy0 + 1;
    size_t bytes = (size_t)((w + 1) / 2) * h;
    for (int i = uc; i < un; i++)
        drop_record(i);
    un = uc;
    while (un > 0 && (un >= UNDO_MAX || ubytes + bytes > UNDO_BUDGET)) {
        drop_record(0);
        memmove(ul, ul + 1, (size_t)(un - 1) * sizeof(*ul));
        un--;
    }
    uint8_t *d = malloc(bytes);
    if (d) {
        pack(bk, x, y, w, h, d);
        ul[un++] = (Undo){x, y, w, h, bytes, d};
        ubytes += bytes;
        uc = un;
    } else
        history_clear(); /* out of memory: keep drawing, lose history */
    for (int j = 0; j < h; j++)
        memcpy(bk + (y + j) * CW + x, cv + (y + j) * CW + x, w);
    dirty_reset();
    modified = 1;
    return 1;
}
static int swap_record(Undo *u)
{
    uint8_t *t = malloc(u->bytes);
    if (!t)
        return 0;
    pack(cv, u->x, u->y, u->w, u->h, t);
    unpack(cv, u->x, u->y, u->w, u->h, u->d);
    for (int j = 0; j < u->h; j++)
        memcpy(bk + (u->y + j) * CW + u->x, cv + (u->y + j) * CW + u->x, u->w);
    free(u->d);
    u->d = t;
    mark_screen(u->x, u->y, u->x + u->w - 1, u->y + u->h - 1);
    modified = 1;
    return 1;
}
static int undo(void)
{
    return uc > 0 && swap_record(&ul[uc - 1]) && (uc--, 1);
}
static int redo(void)
{
    return uc < un && swap_record(&ul[uc]) && (uc++, 1);
}
static void canvas_fill_all(int c)
{
    fill_rect(0, 0, CW - 1, CH - 1, c);
}

/* ---- BMP: 8-bit, 16-entry palette; reads 1/4/8/24/32-bit uncompressed ---- */
static unsigned rd16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}
static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | (uint32_t)rd16(p + 2) << 16;
}
static void put16(uint8_t *p, unsigned n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void put32(uint8_t *p, unsigned n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}
static size_t bmp_size(int w, int h)
{
    return 118 + (size_t)((w + 3) & ~3) * h;
}
static size_t bmp_encode(const uint8_t *px, int w, int h, uint8_t *out)
{
    int stride = (w + 3) & ~3;
    size_t n = bmp_size(w, h);
    memset(out, 0, 118);
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, n);
    put32(out + 10, 118);
    put32(out + 14, 40);
    put32(out + 18, w);
    put32(out + 22, h);
    put16(out + 26, 1);
    put16(out + 28, 8);
    put32(out + 34, stride * h);
    put32(out + 46, 16);
    for (int i = 0; i < 16; i++) {
        out[54 + i * 4] = pal[i][2];
        out[55 + i * 4] = pal[i][1];
        out[56 + i * 4] = pal[i][0];
    }
    for (int y = 0; y < h; y++) {
        uint8_t *row = out + 118 + (size_t)(h - 1 - y) * stride;
        memcpy(row, px + y * w, w);
        memset(row + w, 0, stride - w);
    }
    return n;
}
static int nearest16(int r, int g, int b)
{
    int best = 0, err = 1 << 30;
    for (int i = 0; i < 16; i++) {
        int dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2], d = dr * dr + dg * dg + db * db;
        if (d < err) {
            err = d;
            best = i;
        }
    }
    return best;
}
/* Decode into dst (CW*CH, white-padded). Returns NULL or an error message. */
static const char *bmp_decode(const uint8_t *d, size_t n, uint8_t *dst)
{
    if (n < 54 || d[0] != 'B' || d[1] != 'M')
        return "Not a BMP file";
    uint32_t off = rd32(d + 10), hs = rd32(d + 14);
    int32_t w = (int32_t)rd32(d + 18), h = (int32_t)rd32(d + 22);
    unsigned planes = rd16(d + 26), bpp = rd16(d + 28), ncol = rd32(d + 46);
    if (hs < 40 || hs > 4096 || 14 + (size_t)hs > n)
        return "Unsupported BMP header";
    int topdown = h < 0;
    if (topdown)
        h = -h;
    if (w <= 0 || h <= 0 || w > CW || h > CH)
        return "Image must be at most 640 x 480";
    if (planes != 1 || rd32(d + 30) != 0 || !(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32))
        return "Only uncompressed 1/4/8/24/32-bit BMPs";
    uint8_t map[256];
    memset(map, 0, sizeof(map));
    if (bpp <= 8) {
        unsigned nc = ncol ? ncol : 1u << bpp;
        if (nc > (1u << bpp))
            nc = 1u << bpp;
        if (14 + (size_t)hs + 4 * (size_t)nc > n)
            return "Truncated BMP palette";
        for (unsigned i = 0; i < nc; i++) {
            const uint8_t *e = d + 14 + hs + 4 * i;
            map[i] = nearest16(e[2], e[1], e[0]);
        }
    }
    uint64_t stride = (((uint64_t)w * bpp + 31) / 32) * 4;
    if (off > n || stride * h > n - off)
        return "Truncated BMP data";
    unsigned prev = 0x1000000;
    int prevc = 0;
    memset(dst, 0, CW * CH);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = d + off + stride * (topdown ? y : h - 1 - y);
        uint8_t *o = dst + y * CW;
        for (int x = 0; x < w; x++) {
            if (bpp == 8)
                o[x] = map[row[x]];
            else if (bpp == 4)
                o[x] = map[(x & 1) ? row[x / 2] & 15 : row[x / 2] >> 4];
            else if (bpp == 1)
                o[x] = map[(row[x / 8] >> (7 - (x & 7))) & 1];
            else {
                const uint8_t *p = row + x * (bpp / 8);
                unsigned rgb = p[2] << 16 | p[1] << 8 | p[0];
                if (rgb != prev) {
                    prev = rgb;
                    prevc = nearest16(p[2], p[1], p[0]);
                }
                o[x] = prevc;
            }
        }
    }
    return NULL;
}

/* ---- application state and screen ---- */
static int tool = T_PEN, fg = 1, bg = 0, szi = 1;
static int ox, oy;            /* scroll offset of the view */
static char fname[80];
static int kbd, kx = CW / 2, ky = CH / 2, kdown, kbtn; /* keyboard cursor */
static int stroke, sbtn, scol, ax, ay, lx, ly, pan_x, pan_y, pan_ox, pan_oy;
static int view_all, need_full;
static int hov_ok, hov_x, hov_y; /* mouse position on the canvas */
static char slast[96];
static uint16_t *planes;

#define TY 26
#define AY 172
#define SY 236
#define PY 260
#define IY 368

static int cur_size(void)
{
    return tool == T_PEN ? 1 : sizes[szi];
}
static void button(int x, int y, int w, int h, const char *s, int on)
{
    app_box(x, y, w, h, 1);
    app_box(x + 1, y + 1, w - 2, h - 2, on ? 1 : 0);
    app_text(x + (w - 8 * (int)strlen(s)) / 2, y + h - 6, s, on ? 0 : 1);
}
static int light(int c)
{
    return pal[c][0] + pal[c][1] + pal[c][2] > 380;
}
static void draw_panel(void)
{
    static const char *const act[6] = {"Und", "Red", "Clr", "New", "Opn", "Sav"};
    app_box(0, 24, VX - 2, 430, 0);
    app_box(VX - 2, 24, 2, 430, 1);
    for (int i = 0; i < NTOOLS; i++)
        button((i & 1) * 32, TY + (i / 2) * 24, 32, 24, tool_label[i], i == tool);
    for (int i = 0; i < 6; i++)
        button((i & 1) * 32, AY + (i / 2) * 20, 32, 20, act[i], 0);
    for (int i = 0; i < 4; i++) {
        char s[2] = {'0' + sizes[i], 0};
        button(i * 16, SY, 16, 20, s, i == szi);
    }
    for (int c = 0; c < 16; c++) {
        int x = (c & 1) * 32, y = PY + (c / 2) * 13;
        app_box(x, y, 32, 13, 1);
        app_box(x + 1, y + 1, 30, 11, c);
        if (c == fg)
            app_box(x + 5, y + 4, 8, 5, light(c) ? 1 : 0);
        if (c == bg)
            app_box(x + 19, y + 4, 8, 5, light(c) ? 1 : 0);
    }
    app_box(24, IY + 12, 34, 26, 1);
    app_box(25, IY + 13, 32, 24, bg);
    app_box(6, IY + 2, 34, 26, 1);
    app_box(7, IY + 3, 32, 24, fg);
}
static void paint_canvas(int x0, int y0, int x1, int y1)
{
    if (x0 < ox)
        x0 = ox;
    if (y0 < oy)
        y0 = oy;
    if (x1 > ox + VW - 1)
        x1 = ox + VW - 1;
    if (y1 > oy + VH - 1)
        y1 = oy + VH - 1;
    if (x1 < x0 || y1 < y0 || !planes)
        return;
    int w = x1 - x0 + 1, h = y1 - y0 + 1, stride = ((w + 15) / 16) * 4;
    memset(planes, 0, (size_t)stride * h * sizeof(*planes));
    for (int y = 0; y < h; y++) {
        const uint8_t *row = cv + (y0 + y) * CW + x0;
        uint16_t *o = planes + y * stride;
        for (int x = 0; x < w; x++) {
            int c = physical[row[x] & 15];
            uint16_t bit = 0x8000 >> (x & 15);
            uint16_t *g = o + (x >> 4) * 4;
            if (c & 1)
                g[0] |= bit;
            if (c & 2)
                g[1] |= bit;
            if (c & 4)
                g[2] |= bit;
            if (c & 8)
                g[3] |= bit;
        }
    }
    app_unclip();
    app_blit(planes, w, h, VX + x0 - ox, VY + y0 - oy);
}
static void crosshair(void)
{
    int x = VX + kx - ox, y = VY + ky - oy;
    if (x < VX || y < VY || x >= VX + VW || y >= VY + VH)
        return;
    app_clip(VX, VY, VW, VH);
    app_line(x - 5, y + 1, x + 7, y + 1, 0);
    app_line(x + 1, y - 5, x + 1, y + 7, 0);
    app_line(x - 6, y, x + 6, y, 1);
    app_line(x, y - 6, x, y + 6, 1);
    app_unclip();
}
static void paint_all(void)
{
    ox = ox < 0 ? 0 : ox > CW - VW ? CW - VW : ox;
    oy = oy < 0 ? 0 : oy > CH - VH ? CH - VH : oy;
    paint_canvas(ox, oy, ox + VW - 1, oy + VH - 1);
    view_all = 0;
    sx0 = sy0 = 0;
    sx1 = sy1 = -1;
}
static void ui_full(void)
{
    slast[0] = 0;
    app_clear(0);
    draw_panel();
    paint_all();
    if (kbd)
        crosshair();
    need_full = 0;
}
static void set_scroll(int nx, int ny)
{
    nx = nx < 0 ? 0 : nx > CW - VW ? CW - VW : nx;
    ny = ny < 0 ? 0 : ny > CH - VH ? CH - VH : ny;
    if (nx != ox || ny != oy) {
        ox = nx;
        oy = ny;
        view_all = 1;
    }
}
static int in_view(int x, int y)
{
    return x >= VX && x < VX + VW && y >= VY && y < VY + VH;
}
static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- strokes ---- */
static void shape(int x, int y)
{
    revert_dirty();
    int s = sizes[szi];
    switch (tool) {
    case T_LINE:
        draw_line(ax, ay, x, y, s, scol);
        break;
    case T_RECT:
        draw_rect(ax, ay, x, y, s, scol);
        break;
    case T_FRECT:
        fill_rect(ax, ay, x, y, scol);
        break;
    case T_ELL:
        draw_ellipse(ax, ay, x, y, s, scol, 0);
        break;
    case T_FELL:
        draw_ellipse(ax, ay, x, y, s, scol, 1);
        break;
    }
}
static void stroke_begin(int x, int y, int b)
{
    scol = b == 2 ? bg : fg;
    x = clampi(x, 0, CW - 1);
    y = clampi(y, 0, CH - 1);
    stroke = 1;
    sbtn = b;
    ax = lx = x;
    ay = ly = y;
    switch (tool) {
    case T_PEN:
    case T_BRUSH:
    case T_ERASE:
        if (tool == T_ERASE)
            scol = bg;
        stamp(x, y, cur_size(), scol);
        break;
    case T_FILL:
        flood(x, y, scol);
        break;
    case T_PICK:
        if (b == 2)
            bg = cv[y * CW + x];
        else
            fg = cv[y * CW + x];
        draw_panel();
        break;
    case T_PAN:
        pan_x = x - ox;
        pan_y = y - oy;
        pan_ox = ox;
        pan_oy = oy;
        break;
    default:
        shape(x, y);
    }
}
static void stroke_move(int x, int y)
{
    x = clampi(x, 0, CW - 1);
    y = clampi(y, 0, CH - 1);
    if (x == lx && y == ly)
        return;
    switch (tool) {
    case T_PEN:
    case T_BRUSH:
    case T_ERASE:
        draw_line(lx, ly, x, y, cur_size(), scol);
        break;
    case T_FILL:
    case T_PAN:
        break;
    case T_PICK:
        if (sbtn == 2)
            bg = cv[y * CW + x];
        else
            fg = cv[y * CW + x];
        draw_panel();
        break;
    default:
        shape(x, y);
    }
    lx = x;
    ly = y;
}
static void stroke_end(void)
{
    if (stroke && tool != T_PAN && tool != T_PICK)
        commit();
    stroke = 0;
}
static void stroke_cancel(void)
{
    if (!stroke)
        return;
    if (dx1 >= dx0) {
        mark_screen(dx0, dy0, dx1, dy1);
        revert_dirty();
    }
    stroke = 0;
    kdown = 0;
}

/* ---- files ---- */
static int confirm(const char *msg, const char *buttons)
{
    char b[220];
    snprintf(b, sizeof(b), "[2][%.120s][%s]", msg, buttons);
    ai[0] = 2;
    aa[0] = (intptr_t)b;
    aes_call(52, 1, 1, 1);
    return ao[0] == 1;
}
static int discard_ok(void)
{
    return !modified || confirm("Discard unsaved changes?", "Discard|Cancel");
}
static void new_canvas(void)
{
    memset(cv, 0, CW * CH);
    memset(bk, 0, CW * CH);
    history_clear();
    dirty_reset();
    modified = 0;
    fname[0] = 0;
    ox = oy = 0;
    mark_screen(0, 0, CW - 1, CH - 1);
}
static int load_bmp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        app_alert("Cannot open image");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0 || size > BMP_MAX_FILE) {
        fclose(f);
        app_alert("BMP file must be at most 2 MiB");
        return 0;
    }
    uint8_t *data = malloc(size), *tmp = malloc(CW * CH);
    int ok = data && tmp && fread(data, 1, size, f) == (size_t)size;
    fclose(f);
    const char *err = ok ? bmp_decode(data, size, tmp) : "Out of memory or read error";
    free(data);
    if (err) {
        free(tmp);
        app_alert(err);
        return 0;
    }
    memcpy(cv, tmp, CW * CH);
    memcpy(bk, tmp, CW * CH);
    free(tmp);
    history_clear();
    dirty_reset();
    modified = 0;
    ox = oy = 0;
    snprintf(fname, sizeof(fname), "%s", path);
    mark_screen(0, 0, CW - 1, CH - 1);
    return 1;
}
static int save_bmp(int as)
{
    char path[80];
    if (fname[0] && !as)
        snprintf(path, sizeof(path), "%s", fname);
    else {
        snprintf(path, sizeof(path), "%c:\\PAINT.BMP", dc_storage_drive());
        if (fname[0])
            snprintf(path, sizeof(path), "%s", fname);
        if (!app_prompt("Save 16-colour BMP (SD drive or C:):", path, sizeof(path)))
            return 0;
    }
    if (!isalpha((unsigned char)path[0]) || path[1] != ':') {
        app_alert("Give a drive, e.g. C:\\PAINT.BMP");
        return 0;
    }
    if (!dc_drive_state(path[0])) {
        app_alert("That drive is read-only or not mounted");
        return 0;
    }
    if (strcmp(path, fname)) {
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f);
            if (!confirm("Replace existing image?", "Replace|Cancel"))
                return 0;
        }
    }
    uint8_t *buf = malloc(bmp_size(CW, CH));
    if (!buf) {
        app_alert("Out of memory");
        return 0;
    }
    size_t n = bmp_encode(cv, CW, CH, buf);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(buf, 1, n, f) == n;
    if (f && fclose(f))
        ok = 0;
    free(buf);
    char done[64];
    snprintf(done, sizeof(done), "Image saved on %c:%s", toupper((unsigned char)path[0]), dc_drive_note(path[0]));
    app_alert(ok ? done : "Image save failed");
    if (ok) {
        snprintf(fname, sizeof(fname), "%s", path);
        modified = 0;
    }
    return ok;
}
static void open_dialog(void)
{
    if (!discard_ok())
        return;
    char p[80];
    snprintf(p, sizeof(p), "%c:\\PICTURE.BMP", dc_storage_drive());
    if (fname[0])
        snprintf(p, sizeof(p), "%s", fname);
    if (app_prompt("Open BMP (paint or IMAGES export):", p, sizeof(p)))
        load_bmp(p);
}
static void help(void)
{
    app_alert("Tools: P pen B brush X eraser L line|R rect F filled rect E ellipse K filled|G fill "
              "I picker H pan. Colours 0-9 and !@#$%^, [ ] change, Tab swaps.|+ - brush size, "
              "U undo, Y redo, C clear.|Arrows move cross, Space/Return draw (left/right).|Ctrl+N "
              "new, O open, S save, A save as, Esc quit.");
}

/* ---- input ---- */
static void do_action(int i)
{
    if (i == 0)
        undo();
    else if (i == 1)
        redo();
    else if (i == 2) {
        canvas_fill_all(bg);
        commit();
    } else if (i == 3) {
        if (discard_ok())
            new_canvas();
        need_full = 1;
    } else if (i == 4) {
        open_dialog();
        need_full = 1;
    } else if (i == 5) {
        save_bmp(0);
        need_full = 1;
    }
}
static void click_panel(int x, int y, int b)
{
    if (y >= TY && y < TY + 144 && x < 64) {
        int i = (y - TY) / 24 * 2 + x / 32;
        if (i < NTOOLS)
            tool = i;
    } else if (y >= AY && y < AY + 60 && x < 64)
        do_action((y - AY) / 20 * 2 + x / 32);
    else if (y >= SY && y < SY + 20 && x < 64)
        szi = x / 16;
    else if (y >= PY && y < PY + 104 && x < 64) {
        int c = (y - PY) / 13 * 2 + x / 32;
        if (b == 2)
            bg = c;
        else
            fg = c;
    } else if (y >= IY && y < IY + 40 && x < 64) {
        int t = fg;
        fg = bg;
        bg = t;
    } else
        return;
    if (!need_full)
        draw_panel();
}
static void kbd_click(int b)
{
    if (tool == T_FILL || tool == T_PICK) {
        stroke_begin(kx, ky, b);
        stroke_end();
    } else if (stroke)
        kdown = 0;
    else {
        kdown = 1;
        kbtn = b;
    }
}
static int colour_key(int a)
{
    static const char shifted[] = ")!@#$%^";
    if (a >= '0' && a <= '9')
        return a - '0';
    for (int i = 1; shifted[i]; i++)
        if (a == shifted[i])
            return 9 + i;
    return -1;
}
static void set_tool(int t)
{
    tool = t;
    draw_panel();
}
/* Returns 1 to quit. */
static int handle_key(int key)
{
    int a = key & 255;
    static unsigned long lastms;
    static int lastkey, streak;
    if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT) {
        unsigned long now = app_millis();
        streak = (key == lastkey && now - lastms < 150) ? streak + 1 : 0;
        lastkey = key;
        lastms = now;
        int step = 1 + streak / 3;
        if (step > 8)
            step = 8;
        int ddx = key == KEY_LEFT ? -step : key == KEY_RIGHT ? step : 0;
        int ddy = key == KEY_UP ? -step : key == KEY_DOWN ? step : 0;
        if (tool == T_PAN && !stroke) {
            set_scroll(ox + 8 * ddx, oy + 8 * ddy);
            return 0;
        }
        if (!kbd) {
            kbd = 1;
            if (hov_ok) {
                kx = hov_x;
                ky = hov_y;
            }
        }
        mark_screen(kx - 8, ky - 8, kx + 8, ky + 8);
        kx = clampi(kx + ddx, 0, CW - 1);
        ky = clampi(ky + ddy, 0, CH - 1);
        if (kx - ox < 6)
            set_scroll(kx - 6, oy);
        if (kx - ox > VW - 7)
            set_scroll(kx - VW + 7, oy);
        if (ky - oy < 6)
            set_scroll(ox, ky - 6);
        if (ky - oy > VH - 7)
            set_scroll(ox, ky - VH + 7);
        mark_screen(kx - 8, ky - 8, kx + 8, ky + 8);
        return 0;
    }
    if (a == 27) {
        if (stroke) {
            stroke_cancel();
            return 0;
        }
        return discard_ok();
    }
    if (a == ' ' || a == 13) {
        kbd_click(a == ' ' ? 1 : 2);
        return 0;
    }
    if (stroke)
        return 0;
    if (key == 0x4900 || key == 0x5100 || key == KEY_HOME || key == KEY_END) {
        set_scroll(ox + (key == KEY_HOME ? -64 : key == KEY_END ? 64 : 0),
                   oy + (key == 0x4900 ? -64 : key == 0x5100 ? 64 : 0));
        return 0;
    }
    if (key == KEY_DELETE)
        do_action(2);
    else if (a == 9) {
        int t = fg;
        fg = bg;
        bg = t;
        draw_panel();
    } else if (a == 14 || a == 15 || a == 19)
        do_action(a == 14 ? 3 : a == 15 ? 4 : 5);
    else if (a == 1) {
        save_bmp(1);
        need_full = 1;
    } else if (a == 26 || a == 'u' || a == 'U')
        undo();
    else if (a == 25 || a == 'y' || a == 'Y')
        redo();
    else if (a == 'c' || a == 'C')
        do_action(2);
    else if (a == '+' || a == '=' || a == '-' || a == '_') {
        szi = clampi(szi + (a == '+' || a == '=' ? 1 : -1), 0, 3);
        draw_panel();
    } else if (a == '[' || a == ']') {
        fg = (fg + (a == ']' ? 1 : 15)) & 15;
        draw_panel();
    } else if (a == '{' || a == '}') {
        bg = (bg + (a == '}' ? 1 : 15)) & 15;
        draw_panel();
    } else if (a == '?' || a == '/') {
        help();
        need_full = 1;
    } else if (colour_key(a) >= 0) {
        fg = colour_key(a);
        draw_panel();
    } else if (a && strchr(tool_key, tolower(a)))
        set_tool((int)(strchr(tool_key, tolower(a)) - tool_key));
    return 0;
}
static void status(int cx, int cy)
{
    char s[96];
    snprintf(s, sizeof(s), "%s %dpx  x%d y%d  %.28s%s  ?:help", tool_name[tool], cur_size(), cx, cy,
             fname[0] ? fname : "(untitled)", modified ? "*" : "");
    if (strcmp(s, slast)) {
        strcpy(slast, s);
        app_status(s);
    }
}
static void redraw_dirty(void)
{
    if (need_full)
        ui_full();
    else if (view_all || sx1 >= sx0) {
        if (view_all)
            paint_all();
        else {
            paint_canvas(sx0, sy0, sx1, sy1);
            sx0 = sy0 = 0;
            sx1 = sy1 = -1;
        }
        if (kbd)
            crosshair();
    }
}
int app_main(int argc, char **argv)
{
    if (!app_begin("PAINT - native GEM paint"))
        return 1;
    atexit(app_end);
    planes = malloc(((VW + 15) / 16) * 4 * VH * sizeof(*planes));
    if (!canvas_alloc() || !planes) {
        app_alert("Not enough memory");
        return 1;
    }
    for (int i = 0; i < 16; i++)
        app_palette(i, pal[i][0] * 1000 / 255, pal[i][1] * 1000 / 255, pal[i][2] * 1000 / 255);
    if (argc > 1)
        load_bmp(argv[1]);
    ui_full();
    int lmx = -1, lmy = -1, lbtn = 0, pb = 0;
    for (;;) {
        int mx, my, btn;
        app_mouse(1);
        int key = app_event(20, &mx, &my, &btn);
        app_mouse(0);
        int moved = mx != lmx || my != lmy || btn != lbtn;
        lmx = mx;
        lmy = my;
        lbtn = btn;
        if (key && handle_key(key))
            break;
        if (moved && kbd && !kdown && !stroke) {
            kbd = 0;
            mark_screen(kx - 8, ky - 8, kx + 8, ky + 8);
        }
        hov_ok = in_view(mx, my);
        hov_x = ox + mx - VX;
        hov_y = oy + my - VY;
        int ex = mx, ey = my, down = (btn & 3) != 0, eb = btn & 3;
        if (kbd) {
            ex = VX + kx - ox;
            ey = VY + ky - oy;
            down = kdown;
            eb = kbtn;
        }
        if (down && !pb) {
            pb = 1;
            if (!kbd && ex < VX - 2)
                click_panel(ex, ey, eb);
            else if (kbd || in_view(ex, ey))
                stroke_begin(ex - VX + ox, ey - VY + oy, eb);
        } else if (down && stroke) {
            if (tool == T_PAN)
                set_scroll(pan_ox - (ex - VX - pan_x), pan_oy - (ey - VY - pan_y));
            else {
                int sxd = ex < VX ? -8 : ex >= VX + VW ? 8 : 0, syd = ey < VY ? -8 : ey >= VY + VH ? 8 : 0;
                if (sxd || syd)
                    set_scroll(ox + sxd, oy + syd);
                stroke_move(ex - VX + ox, ey - VY + oy);
            }
        } else if (!down && pb) {
            pb = 0;
            stroke_end();
        }
        redraw_dirty();
        status(kbd ? kx : hov_ok ? hov_x : 0, kbd ? ky : hov_ok ? hov_y : 0);
    }
    free(cv);
    free(bk);
    free(planes);
    return 0;
}
