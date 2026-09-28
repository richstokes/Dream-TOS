/* Resident native SH-4 GEM desk accessory. GPL-2.0-or-later. */
#include "window.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static AppWindow window = {.handle = -1};
static int menu_id;
static unsigned clock_time, clock_date;
static char time_text[9], date_text[11];

static int read_clock(void)
{
    /* Read across midnight consistently. GEMDOS time has two-second precision. */
    unsigned date, time, next;
    do {
        date = dc_os->gemdos(0x2a);
        time = dc_os->gemdos(0x2c);
        next = dc_os->gemdos(0x2a);
    } while (date != next);
    int changed = time != clock_time || date != clock_date;
    clock_time = time;
    clock_date = date;
    snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u",
             (time >> 11) & 31, (time >> 5) & 63, (time & 31) * 2);
    snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
             1980 + ((date >> 9) & 127), (date >> 5) & 15, date & 31);
    return changed;
}
static void radial(int cx, int cy, double tick, int inner, int outer, int colour)
{
    double angle = tick * (6.283185307179586 / 60.0);
    app_line(cx + (int)(sin(angle) * inner), cy - (int)(cos(angle) * inner),
             cx + (int)(sin(angle) * outer), cy - (int)(cos(angle) * outer), colour);
}
static void digit(int x, int y, int number)
{
    static const unsigned char segments[] = {0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    static const unsigned char boxes[7][4] = {
        {3,0,14,3},{17,3,3,13},{17,19,3,13},{3,32,14,3},
        {0,19,3,13},{0,3,3,13},{3,16,14,3}
    };
    for (int i = 0; i < 7; i++)
        if (segments[number] & (1 << i))
            app_box(x+boxes[i][0], y+boxes[i][1], boxes[i][2], boxes[i][3], 4);
}
static void draw(void)
{
    AppRect r = window.work;
    app_box(r.x, r.y, r.w, r.h, 0);
    int radius = (r.h - 90) / 2;
    if (radius > r.w / 2 - 20) radius = r.w / 2 - 20;
    if (radius > 110) radius = 110;
    int cx = r.x+r.w/2, cy = r.y+radius+12;
    /* The face is a polygon so it uses the same clipped VDI lines as the hands. */
    for (int i = 0; i < 60; i++) {
        double a = i * (6.283185307179586 / 60.0);
        double b = (i+1) * (6.283185307179586 / 60.0);
        app_line(cx+(int)(sin(a)*radius), cy-(int)(cos(a)*radius),
                 cx+(int)(sin(b)*radius), cy-(int)(cos(b)*radius), 9);
        radial(cx,cy,i,radius-(i%5 ? 3 : 8),radius-1,i%5 ? 9 : 1);
    }
    int second = (clock_time & 31)*2, minute = (clock_time >> 5)&63;
    int hour = (clock_time >> 11)&31;
    radial(cx,cy,(hour%12)*5+minute/12.0,0,radius*5/10,4);
    radial(cx,cy,minute+second/60.0,0,radius*8/10,1);
    radial(cx,cy,second,-radius/6,radius*8/10,2);
    app_box(cx-2,cy-2,5,5,1);
    int x = cx-82, y = cy+radius+14;
    for (int i = 0; i < 8; i++) {
        if (time_text[i] == ':') {
            app_box(x+2,y+10,3,3,4);
            app_box(x+2,y+23,3,3,4);
            x += 10;
        } else {
            digit(x,y,time_text[i]-'0');
            x += 24;
        }
    }
    app_text(cx-40,y+55,date_text,1);
}
static void redraw(void)
{
    if (window.handle >= 0)
        app_window_redraw(&window, window.work, draw);
}
static void message(const int16_t m[8])
{
    if (m[0] == 40 && m[4] == menu_id) { /* AC_OPEN */
        if (window.handle < 0) {
            if (!app_window_open(&window,"Clock",260,240,220,210))
                return;
            AppRect r = window.border;
            r.x = window.desktop.x+window.desktop.w-r.w-20;
            r.y = window.desktop.y+24;
            app_window_bounds(&window,r);
        } else {
            int16_t top[8] = {21,0,0,window.handle};
            app_window_message(&window,top);
        }
        read_clock();
        redraw();
    } else if (m[0] == 41) { /* AC_CLOSE: the shell discards every window. */
        window.handle = -1;
    } else if (window.handle >= 0) {
        int action = app_window_message(&window,m);
        if (action == WINDOW_CLOSE)
            app_window_close(&window); /* Hide, but stay registered and resident. */
        else if (action == WINDOW_REDRAW) {
            if (m[0] == 20)
                app_window_redraw(&window,(AppRect){m[4],m[5],m[6],m[7]},draw);
            else
                redraw();
        } else if (action == WINDOW_CHANGED)
            redraw();
    }
}
static void key(int code, int modifiers)
{
    if (window.handle < 0) return;
    if ((code & 255) == 27) {
        app_window_close(&window);
    } else if ((modifiers & 4) && (code & 0xff00) >= KEY_UP) {
        AppRect r = window.border;
        int scan = code & 0xff00, dx = 0, dy = 0;
        if (scan == KEY_LEFT) dx = -8;
        if (scan == KEY_RIGHT) dx = 8;
        if (scan == KEY_UP) dy = -8;
        if (scan == KEY_DOWN) dy = 8;
        if (modifiers & 3) { r.w += dx; r.h += dy; }
        else { r.x += dx; r.y += dy; }
        if (dx || dy) {
            app_window_bounds(&window,r);
            window.full = 0;
            redraw();
        }
    }
}
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!app_begin_windowed()) return 1;
    ai[0] = ag[2]; /* appl_init's AES process id, independent of window handle */
    aa[0] = (intptr_t)"  Clock";
    aes_call(35,1,1,1); /* menu_register */
    menu_id = ao[0];
    if (menu_id < 0) return 1;
    for (;;) {
        int16_t m[8] = {0};
        memset(ai,0,sizeof(ai));
        /* A hidden accessory sleeps until the next AES message. While visible,
         * a 500ms timer refreshes only when the GEMDOS clock actually changes. */
        ai[0] = APP_MESSAGE | (window.handle >= 0 ? APP_KEY | APP_TIMER : 0);
        ai[14] = 500;
        aa[0] = (intptr_t)m;
        aes_call(25,16,7,1);
        int flags = ao[0], code = ao[5], modifiers = ao[4];
        if (flags & APP_MESSAGE) message(m);
        if (flags & APP_KEY) key(code,modifiers);
        if ((flags & APP_TIMER) && window.handle >= 0 && read_clock()) redraw();
    }
}
