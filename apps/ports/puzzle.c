/* Native GEM frontend for Simon Tatham's MIT-licensed puzzle engines.
 * Frontend GPL-2.0-or-later. Keyboard and Maple mouse supported. */
#include "app.h"
#include "drives.h"
#include "puzzles.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
struct frontend {
    midend *me;
    int timer, ox, oy;
    int colours[32];
    char status[160];
};
static struct frontend fe;
char ver[] = "Dreamcast native / vendored source";
void frontend_default_colour(frontend *f, float *out)
{
    (void)f;
    out[0] = out[1] = out[2] = 0.75f;
}
void activate_timer(frontend *f)
{
    f->timer = 1;
}
void deactivate_timer(frontend *f)
{
    f->timer = 0;
}
void get_random_seed(void **seed, int *size)
{
    unsigned long *p = malloc(sizeof(*p));
    *p = app_millis();
    *seed = p;
    *size = sizeof(*p);
}
void fatal(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    app_alert(buf);
    exit(1);
}
static int col(int c)
{
    return c >= 0 && c < 32 ? fe.colours[c] : 1;
}
static void text_cb(drawing *d, int x, int y, int font, int size, int align, int c, const char *s)
{
    (void)d;
    (void)font;
    (void)size;
    if (align & ALIGN_HCENTRE)
        x -= (int)strlen(s) * 4;
    if (align & ALIGN_HRIGHT)
        x -= (int)strlen(s) * 8;
    if (align & ALIGN_VCENTRE)
        y += 5;
    app_text(x + fe.ox, y + fe.oy, s, col(c));
}
static void rect_cb(drawing *d, int x, int y, int w, int h, int c)
{
    (void)d;
    app_box(x + fe.ox, y + fe.oy, w, h, col(c));
}
static void line_cb(drawing *d, int x, int y, int xx, int yy, int c)
{
    (void)d;
    app_line(x + fe.ox, y + fe.oy, xx + fe.ox, yy + fe.oy, col(c));
}
static void polygon_cb(drawing *d, const int *xy, int n, int fill, int outline)
{
    (void)d;
    if (n < 2 || n > 250)
        return;
    if (fill >= 0) {
        vi[0] = col(fill);
        vdi_call(25, 0, 1);
        for (int i = 0; i < n; i++) {
            vp[i * 2] = xy[i * 2] + fe.ox;
            vp[i * 2 + 1] = xy[i * 2 + 1] + fe.oy;
        }
        vdi_call(9, n, 0);
    }
    if (outline >= 0)
        for (int i = 0; i < n; i++)
            line_cb(d, xy[2 * i], xy[2 * i + 1], xy[2 * ((i + 1) % n)], xy[2 * ((i + 1) % n) + 1],
                    outline);
}
static void circle_cb(drawing *d, int x, int y, int r, int fill, int outline)
{
    int xy[64];
    for (int i = 0; i < 32; i++) {
        xy[2 * i] = x + (int)(r * cos(i * 6.283185307 / 32));
        xy[2 * i + 1] = y + (int)(r * sin(i * 6.283185307 / 32));
    }
    polygon_cb(d, xy, 32, fill, outline);
}
static void update_cb(drawing *d, int x, int y, int w, int h)
{
    (void)d;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
static void clip_cb(drawing *d, int x, int y, int w, int h)
{
    (void)d;
    app_clip(x + fe.ox, y + fe.oy, w, h);
}
static void unclip_cb(drawing *d)
{
    (void)d;
    app_clip(0, 48, 640, 380);
}
static void nop_cb(drawing *d)
{
    (void)d;
}
static void status_cb(drawing *d, const char *text)
{
    (void)d;
    if (text != fe.status)
        snprintf(fe.status, sizeof(fe.status), "%s", text);
    app_unclip();
    app_box(0, 430, 640, 22, 0);
    app_text(8, 447, text, 1);
    unclip_cb(d);
}
static char *fallback_cb(drawing *d, const char *const *s, int n)
{
    (void)d;
    (void)n;
    return dupstr(s[0]);
}
static const drawing_api drawapi = {.version = 1,
                                    .draw_text = text_cb,
                                    .draw_rect = rect_cb,
                                    .draw_line = line_cb,
                                    .draw_polygon = polygon_cb,
                                    .draw_circle = circle_cb,
                                    .draw_update = update_cb,
                                    .clip = clip_cb,
                                    .unclip = unclip_cb,
                                    .start_draw = nop_cb,
                                    .end_draw = nop_cb,
                                    .status_bar = status_cb,
                                    .text_fallback = fallback_cb};
static void colours(void)
{
    static const int pal[16][3] = {
        {1000, 1000, 1000}, {0, 0, 0},       {1000, 0, 0},   {0, 650, 0},
        {0, 0, 1000},       {0, 750, 750},   {1000, 850, 0}, {1000, 500, 500},
        {750, 750, 750},    {450, 450, 450}, {600, 0, 0},    {0, 400, 0},
        {0, 0, 550},        {0, 400, 400},   {600, 400, 0},  {500, 0, 500}};
    for (int i = 0; i < 16; i++)
        app_palette(i, pal[i][0], pal[i][1], pal[i][2]);
    int n;
    float *rgb = midend_colours(fe.me, &n);
    for (int i = 0; i < n && i < 32; i++) {
        float best = 1e12;
        for (int j = 0; j < 16; j++) {
            float e = 0;
            for (int k = 0; k < 3; k++) {
                float v = rgb[3 * i + k] * 1000 - pal[j][k];
                e += v * v;
            }
            if (e < best) {
                best = e;
                fe.colours[i] = j;
            }
        }
    }
    sfree(rgb);
}
static void layout(void)
{
    app_unclip();
    app_clear(0);
    int w = 620, h = 372;
    midend_size(fe.me, &w, &h, true, 1.0);
    fe.ox = (640 - w) / 2;
    fe.oy = 50 + (374 - h) / 2;
    app_text(8, 41, "Arrows: move  Space/Enter: select  F: second action", 1);
    app_status("N:new U:undo R:redo S:save L:load H:help Esc:exit");
    midend_force_redraw(fe.me);
    status_cb(NULL, fe.status);
}
struct stream {
    FILE *file;
    int failed;
};
static void save_cb(void *v, const void *buf, int n)
{
    struct stream *s = v;
    if (fwrite(buf, 1, n, s->file) != (size_t)n)
        s->failed = 1;
}
static bool load_cb(void *v, void *buf, int n)
{
    return fread(buf, 1, n, v) == (size_t)n;
}
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char title[96];
    snprintf(title, sizeof(title), "%s - Simon Tatham's puzzles / native SH-4", thegame.name);
    if (!app_begin(title))
        return 1;
    atexit(app_end);
    fe.me = midend_new(&fe, &thegame, &drawapi, &fe);
    midend_new_game(fe.me);
    colours();
    layout();
    int oldbuttons = 0, oldx = -1, oldy = -1;
    unsigned long last = app_millis();
    for (;;) {
        app_mouse(1);
        int x, y, buttons;
        int key = app_event(20, &x, &y, &buttons);
        app_mouse(0);
        unsigned long now = app_millis();
        if (fe.timer)
            midend_timer(fe.me, (now - last) / 1000.0f);
        last = now;
        int a = key & 255, code = 0;
        if (a == 27)
            break;
        if (key == KEY_UP)
            code = CURSOR_UP;
        else if (key == KEY_DOWN)
            code = CURSOR_DOWN;
        else if (key == KEY_LEFT)
            code = CURSOR_LEFT;
        else if (key == KEY_RIGHT)
            code = CURSOR_RIGHT;
        else if (a == ' ' || a == 13)
            code = CURSOR_SELECT;
        else if (a == 'f' || a == 'F')
            code = CURSOR_SELECT2;
        else if (a == 'n' || a == 'N') {
            midend_new_game(fe.me);
            layout();
        } else if (a == 'u' || a == 'U')
            code = UI_UNDO;
        else if (a == 'r' || a == 'R')
            code = UI_REDO;
        else if (a == 's' || a == 'S' || a == 'l' || a == 'L') {
            char path[64];
            char drive = dc_storage_drive();
            snprintf(path, sizeof(path), "%c:\\%s.SAV", drive, thegame.name);
            if (a == 's' || a == 'S') {
                struct stream s = {fopen(path, "wb"), 0};
                if (!s.file)
                    app_alert("Cannot create the save file");
                else {
                    midend_serialise(fe.me, save_cb, &s);
                    if (fclose(s.file))
                        s.failed = 1;
                    char done[48];
                    snprintf(done, sizeof(done), "Saved on %c:%s", drive, dc_drive_note(drive));
                    app_alert(s.failed ? "Save failed" : done);
                }
            } else {
                FILE *f = fopen(path, "rb");
                if (!f && drive != 'C') { /* saved earlier on the RAM disk */
                    path[0] = 'C';
                    f = fopen(path, "rb");
                }
                if (!f)
                    app_alert("No saved game found");
                else {
                    const char *e = midend_deserialise(fe.me, load_cb, f);
                    fclose(f);
                    if (e)
                        app_alert(e);
                }
            }
            layout();
        } else if (a == 'h' || a == 'H') {
            app_alert(!strcmp(thegame.name, "Mines")
                          ? "Reveal safe squares. F flags a mine.|Arrows move the cursor; Space "
                            "reveals.|S/L save and load (SD card if present)."
                      : !strcmp(thegame.name, "Net")
                          ? "Connect all wires to the centre.|Space rotates. F locks a "
                            "tile.|Arrows move; U undoes a move."
                          : "Put the numbered tiles in order.|Arrows slide a tile into the gap.|U "
                            "undoes; R redoes; N starts again.");
            layout();
        }
        if (code)
            midend_process_key(fe.me, 0, 0, code);
        for (int b = 0; b < 2; b++) {
            int mask = 1 << b, base = b ? RIGHT_BUTTON : LEFT_BUTTON;
            if ((buttons & mask) && !(oldbuttons & mask))
                midend_process_key(fe.me, x - fe.ox, y - fe.oy, base);
            else if (!(buttons & mask) && (oldbuttons & mask))
                midend_process_key(fe.me, x - fe.ox, y - fe.oy, base + 6);
            else if ((buttons & mask) && (x != oldx || y != oldy))
                midend_process_key(fe.me, x - fe.ox, y - fe.oy, base + 3);
        }
        oldbuttons = buttons;
        oldx = x;
        oldy = y;
        midend_redraw(fe.me);
    }
    midend_free(fe.me);
    return 0;
}
