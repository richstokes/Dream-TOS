/* Real editor frontend + window/VDI bindings, including damage batching. */
#include "gem_model.h"
static int text_calls, slider_updates, menus;
unsigned long app_millis(void) { return 1000; }
static void editor_aes(void *v)
{
    struct aes_pb *p = v;
    if (p->c[0] == 30) { menus += p->i[0] ? 1 : -1; p->o[0] = 1; return; }
    if (p->c[0] == 100) {
        int kind = p->i[0]; assert(kind == (1 | 2 | 4 | 8 | 32 | 64 | 128 | 256 | 512 | 1024 | 2048));
        p->i[0] = 1 | 2 | 4 | 8 | 32; mock_aes(v); p->i[0] = kind; return;
    }
    if (p->c[0] == 105 && (p->i[1] == 8 || p->i[1] == 9 || p->i[1] == 15 || p->i[1] == 16)) {
        assert(p->i[2] >= 0 && p->i[2] <= 1000); ++slider_updates; p->o[0] = 1; return;
    }
    mock_aes(v);
}
static void editor_vdi(void *v)
{
    struct vdi_pb *p = v; if (p->c[0] == 8) ++text_calls;
    mock_vdi(v);
}
static const struct dc_native_api api = {.aes = editor_aes, .vdi = editor_vdi};
const struct dc_native_api *dc_os = &api;
#define app_main unused_editor_main
#include "../apps/ports/editor.c"
int main(void)
{
    assert(ed_init(&doc)); assert(app_begin_windowed()); menu_build(); assert(menus == 1);
    assert(app_window_open_kind(&window, "Editor", 608, 414, 320, 180, 4095 - 16));
    assert(ed_load(&doc, "one two three\nsecond line", 25)); set_syntax();
    visible[0] = work; visible[1] = (AppRect){0}; inspect_drawing = 1;
    reveal(); refresh(1); assert(text_calls && slider_updates == 4);
    text_calls = 0; refresh(0); assert(!text_calls); /* idle does not redraw */
    keypress('X', 0); refresh(0); assert(text_calls > 0 && text_calls < 20);
    assert(!updates && !mouse_control && !mouse_hidden && !clips && !palette_writes);
    /* All saved pointers in the menu tree stay within the static resource. */
    for (int i = 0; i < menu_count; ++i) {
        assert(menu[i].next < menu_count && menu[i].head < menu_count && menu[i].tail < menu_count);
    }
    char *tabs = malloc(ED_MAX_BYTES); assert(tabs); memset(tabs, '\t', ED_MAX_BYTES);
    assert(ed_load(&doc, tabs, ED_MAX_BYTES)); free(tabs); doc.tabstop = 8;
    ed_select(&doc, doc.len, 0); set_syntax(); reveal(); refresh(1); assert(left > 8000000);
    int16_t slider[8] = {25, 0, 0, window.handle, 500}; window_message(slider);
    assert(left > 4000000 && left < 4200000); refresh(0);
    finish(); assert(!menus && closed == 1 && deleted == 1);
    puts("Editor GEM menus, scrollbars, clipped row redraw and batched text: PASS");
    return 0;
}
