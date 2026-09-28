/* GEM owns window frames, movement, stacking and resizing. Applications draw
 * only visible work rectangles while holding a short wind_update lock.
 * Native pointers in intin use SH-4 word order, not a 68000 high/low split.
 * GPL-2.0-or-later. */
#include "window.h"
#include <string.h>
enum { KIND = 1 | 2 | 4 | 8 | 32 }; /* NAME, CLOSER, FULLER, MOVER, SIZER */
enum { WF_NAME = 2, WF_WORK = 4, WF_CURRENT = 5, WF_TOP = 10, WF_FIRST = 11, WF_NEXT = 12 };

static int get(int handle, int field, AppRect *r)
{
    ai[0] = handle;
    ai[1] = field;
    aes_call(104, 2, 5, 0);
    *r = (AppRect){ao[1], ao[2], ao[3], ao[4]};
    return ao[0];
}
static void update(int begin)
{
    ai[0] = begin;
    aes_call(107, 1, 1, 0);
}
static AppRect borders(int width, int height)
{
    ai[0] = 0; /* WC_BORDER */
    ai[1] = KIND;
    ai[2] = ai[3] = 0;
    ai[4] = width;
    ai[5] = height;
    aes_call(108, 6, 5, 0);
    return (AppRect){ao[1], ao[2], ao[3], ao[4]};
}
void app_window_bounds(AppWindow *w, AppRect r)
{
    if (r.w < w->min_w) r.w = w->min_w;
    if (r.h < w->min_h) r.h = w->min_h;
    if (r.w > w->desktop.w) r.w = w->desktop.w;
    if (r.h > w->desktop.h) r.h = w->desktop.h;
    if (r.x + r.w > w->desktop.x + w->desktop.w)
        r.x = w->desktop.x + w->desktop.w - r.w;
    if (r.y + r.h > w->desktop.y + w->desktop.h)
        r.y = w->desktop.y + w->desktop.h - r.h;
    if (r.x < w->desktop.x) r.x = w->desktop.x;
    if (r.y < w->desktop.y) r.y = w->desktop.y;
    ai[0] = w->handle;
    ai[1] = WF_CURRENT;
    memcpy(ai + 2, &r, sizeof(r));
    aes_call(105, 6, 1, 0);
    get(w->handle, WF_CURRENT, &w->border);
    get(w->handle, WF_WORK, &w->work);
}
int app_window_open(AppWindow *w, const char *title, int width, int height,
                    int min_width, int min_height)
{
    memset(w, 0, sizeof(*w));
    w->handle = -1;
    if (!get(0, WF_WORK, &w->desktop))
        return 0;
    AppRect minimum = borders(min_width, min_height);
    if (minimum.w > w->desktop.w || minimum.h > w->desktop.h)
        return 0;
    w->min_w = minimum.w;
    w->min_h = minimum.h;
    AppRect r = borders(width, height);
    if (r.w > w->desktop.w) r.w = w->desktop.w;
    if (r.h > w->desktop.h) r.h = w->desktop.h;
    r.x = w->desktop.x + (w->desktop.w - r.w) / 2;
    r.y = w->desktop.y + (w->desktop.h - r.h) / 2;
    ai[0] = KIND;
    memcpy(ai + 1, &w->desktop, sizeof(w->desktop));
    aes_call(100, 5, 1, 0);
    if (ao[0] < 0)
        return 0;
    w->handle = ao[0];
    ai[0] = w->handle;
    ai[1] = WF_NAME;
    uint32_t name = (uint32_t)(uintptr_t)title;
    memcpy(ai + 2, &name, sizeof(name));
    ai[4] = ai[5] = 0;
    aes_call(105, 6, 1, 0);
    ai[0] = w->handle;
    memcpy(ai + 1, &r, sizeof(r));
    aes_call(101, 5, 1, 0);
    if (!ao[0]) {
        ai[0] = w->handle;
        aes_call(103, 1, 1, 0);
        w->handle = -1;
        return 0;
    }
    get(w->handle, WF_CURRENT, &w->border);
    get(w->handle, WF_WORK, &w->work);
    w->restore = w->border;
    return 1;
}
void app_window_close(AppWindow *w)
{
    if (w->handle < 0)
        return;
    ai[0] = w->handle;
    aes_call(102, 1, 1, 0);
    ai[0] = w->handle;
    aes_call(103, 1, 1, 0);
    w->handle = -1;
}
void app_window_full(AppWindow *w)
{
    if (!w->full)
        w->restore = w->border;
    app_window_bounds(w, w->full ? w->restore : w->desktop);
    w->full = !w->full;
}
int app_window_message(AppWindow *w, const int16_t m[8])
{
    if (m[3] != w->handle)
        return WINDOW_IGNORE;
    switch (m[0]) {
    case 20: return WINDOW_REDRAW; /* WM_REDRAW */
    case 21: /* WM_TOPPED */
        memset(ai, 0, sizeof(ai));
        ai[0] = w->handle;
        ai[1] = WF_TOP;
        aes_call(105, 6, 1, 0);
        return WINDOW_REDRAW;
    case 22: return WINDOW_CLOSE;
    case 23:
        app_window_full(w);
        return WINDOW_CHANGED;
    case 27: /* WM_SIZED */
    case 28: /* WM_MOVED */
        app_window_bounds(w, (AppRect){m[4], m[5], m[6], m[7]});
        w->full = 0;
        return WINDOW_CHANGED;
    default: return WINDOW_IGNORE;
    }
}
static int intersect(AppRect a, AppRect b, AppRect *out)
{
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    *out = (AppRect){x, y, right - x, bottom - y};
    return right > x && bottom > y;
}
void app_window_redraw(AppWindow *w, AppRect damage, void (*draw)(void))
{
    AppRect visible, clip;
    update(1);
    app_mouse(0);
    get(w->handle, WF_FIRST, &visible);
    while (visible.w > 0 && visible.h > 0) {
        if (intersect(visible, w->work, &clip) && intersect(clip, damage, &clip)) {
            app_clip(clip.x, clip.y, clip.w, clip.h);
            draw();
        }
        get(w->handle, WF_NEXT, &visible);
    }
    app_unclip();
    app_mouse(1);
    update(0);
}
int app_window_contains(AppWindow *w, int x, int y)
{
    if (x < w->work.x || y < w->work.y || x >= w->work.x + w->work.w ||
        y >= w->work.y + w->work.h)
        return 0;
    ai[0] = x;
    ai[1] = y;
    aes_call(106, 2, 1, 0); /* wind_find: reject content obscured by another window. */
    return ao[0] == w->handle;
}
void app_window_event(AppEvent *e, int ms, int old_buttons)
{
    memset(e, 0, sizeof(*e));
    memset(ai, 0, sizeof(ai));
    ai[0] = APP_KEY | APP_BUTTON | APP_MESSAGE | APP_TIMER;
    ai[1] = 1;
    ai[2] = 1;
    ai[3] = !(old_buttons & 1);
    ai[14] = ms;
    ai[15] = (unsigned)ms >> 16;
    aa[0] = (intptr_t)e->message;
    aes_call(25, 16, 7, 1);
    e->flags = ao[0];
    e->x = ao[1];
    e->y = ao[2];
    e->buttons = ao[3];
    e->modifiers = ao[4];
    e->key = (ao[0] & APP_KEY) ? (uint16_t)ao[5] : 0;
}
