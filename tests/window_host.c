/* Shared fake AES/VDI; production bindings are compiled unchanged. */
#include "gem_model.h"
static const struct dc_native_api api = {.aes = mock_aes, .vdi = mock_vdi};
const struct dc_native_api *dc_os = &api;
static AppRect seen[2];
static void draw(void)
{
    assert(draws < 2);
    seen[draws++] = clip;
    app_box(0, 0, 640, 480, 0); /* Even a large fill must retain the visible clipping rectangle. */
}
int main(void)
{
    assert(app_begin_windowed());
    assert(!updates && !mouse_control && !mouse_hidden && !palette_writes);
    AppWindow w;
    assert(app_window_open(&w, "Calculator", 480, 360, 432, 328));
    assert(w.handle == 7 && w.work.w == 480 && w.work.h == 360);
    assert(app_window_contains(&w, w.work.x, w.work.y));
    assert(!app_window_contains(&w, w.border.x, w.border.y));
    find_handle = 9;
    assert(!app_window_contains(&w, w.work.x, w.work.y));
    find_handle = 7;

    visible[0] = (AppRect){work.x, work.y, 100, work.h};
    visible[1] = (AppRect){work.x + 180, work.y, work.w - 180, work.h};
    inspect_drawing = 1;
    app_window_redraw(&w, (AppRect){work.x + 25, work.y + 10, 250, 40}, draw);
    assert(draws == 2);
    assert(seen[0].x == work.x + 25 && seen[0].w == 75 && seen[0].h == 40);
    assert(seen[1].x == work.x + 180 && seen[1].w == 95 && seen[1].h == 40);
    assert(!updates && !mouse_hidden && !clips && !palette_writes);
    draws = 0;
    app_window_redraw(&w, (AppRect){0, 0, 1, 1}, draw);
    assert(!draws && !updates && !mouse_hidden);
    inspect_drawing = 0;

    int16_t msg[8] = {28, 0, 0, 99, 350, 300, 498, 398};
    assert(app_window_message(&w, msg) == WINDOW_IGNORE);
    msg[3] = w.handle;
    assert(app_window_message(&w, msg) == WINDOW_CHANGED);
    assert(w.border.x == 142 && w.border.y == 82);
    msg[0] = 27; msg[6] = 1; msg[7] = 1;
    assert(app_window_message(&w, msg) == WINDOW_CHANGED);
    assert(w.work.w == 432 && w.work.h == 328);
    AppRect previous = w.border;
    msg[0] = 23;
    assert(app_window_message(&w, msg) == WINDOW_CHANGED && w.full);
    assert(!memcmp(&w.border, &desktop, sizeof(desktop)));
    app_window_full(&w);
    assert(!w.full && !memcmp(&w.border, &previous, sizeof(previous)));
    msg[0] = 21;
    assert(app_window_message(&w, msg) == WINDOW_REDRAW);
    msg[0] = 20;
    assert(app_window_message(&w, msg) == WINDOW_REDRAW);
    msg[0] = 22;
    assert(app_window_message(&w, msg) == WINDOW_CLOSE);
    AppEvent e;
    expected_button_state = 1;
    app_window_event(&e, 20, 0);
    assert(e.flags == (APP_KEY | APP_MESSAGE) && e.key == 0x1c0d);
    assert(e.x == 111 && e.y == 222 && e.modifiers == 4 && e.message[3] == 7);
    expected_button_state = 0;
    app_window_event(&e, 20, 1);
    app_window_close(&w);
    app_window_close(&w);
    assert(w.handle == -1 && closed == 1 && deleted == 1);
    fail_open = 1;
    assert(!app_window_open(&w, "Fail", 480, 360, 432, 328));
    assert(w.handle == -1 && deleted == 2);
    app_end();
    assert(!updates && !mouse_control && !mouse_hidden && !palette_writes);

    /* Existing fullscreen users still receive and release their old locks/palette. */
    assert(app_begin("Fullscreen regression"));
    assert(updates == 1 && mouse_control == 1 && mouse_hidden == 1);
    app_end();
    assert(!updates && !mouse_control && !mouse_hidden && palette_writes == 16);
    puts("Native GEM window geometry, redraw clipping, input and lifecycle: PASS");
    return 0;
}
