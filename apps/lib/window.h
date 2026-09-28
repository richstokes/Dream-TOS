/* Small native GEM window binding. GPL-2.0-or-later. */
#ifndef DC_WINDOW_H
#define DC_WINDOW_H
#include "app.h"
typedef struct { int16_t x, y, w, h; } AppRect;
typedef struct {
    int handle, full, min_w, min_h;
    AppRect desktop, border, work, restore;
} AppWindow;
typedef struct {
    int flags, key, x, y, buttons, modifiers;
    int16_t message[8];
} AppEvent;
enum { APP_KEY = 1, APP_BUTTON = 2, APP_MESSAGE = 16, APP_TIMER = 32 };
enum { WINDOW_IGNORE, WINDOW_REDRAW, WINDOW_CHANGED, WINDOW_CLOSE };
int app_window_open(AppWindow *w, const char *title, int width, int height,
                    int min_width, int min_height);
void app_window_close(AppWindow *w);
void app_window_bounds(AppWindow *w, AppRect bounds);
void app_window_full(AppWindow *w);
int app_window_message(AppWindow *w, const int16_t message[8]);
void app_window_redraw(AppWindow *w, AppRect damage, void (*draw)(void));
int app_window_contains(AppWindow *w, int x, int y);
void app_window_event(AppEvent *event, int ms, int old_buttons);
#endif
