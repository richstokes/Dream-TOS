#include "gem_model.h"
#include "../apps/ports/sdformat.c"
static struct dc_sd_format_info fixture = {.version = 1, .card_sectors = 65536, .volume_sectors = 63455, .cluster_sectors = 1};
static int confirmation = 1, confirms, alerts, starts, steps;
static long start_error, finish = DC_SD_MOUNTED;
static void aes_api(void *v)
{
    struct aes_pb *p = v;
    if (p->c[0] != 52) { mock_aes(v); return; }
    const char *s = (const char *)p->a[0];
    if (strstr(s, "Erase SD")) {
        assert(p->i[0] == 1 && strstr(s, "ALL files and partitions"));
        assert(strstr(s, "C: and D:")); confirms++; p->o[0] = confirmation;
    } else { alerts++; p->o[0] = 1; }
}
static long info_api(void *b, uint32_t n) { assert(n == sizeof(fixture)); memcpy(b, &fixture, n); return n; }
static long format_api(uint32_t target, uint32_t action)
{
    assert(target == DC_SD_TARGET_SERIAL);
    if (action == DC_SD_FORMAT_START) {
        starts++; if (start_error) return start_error;
        fixture.state = DC_SD_FORMATTING;
    } else {
        assert(action == DC_SD_FORMAT_STEP && fixture.state == DC_SD_FORMATTING); steps++;
        fixture.state = finish < 0 ? DC_SD_FAILED : (uint32_t)finish;
        return finish;
    }
    return fixture.state;
}
unsigned long app_millis(void) { return 0; }
static struct dc_native_api api = {.size = sizeof(api), .aes = aes_api, .vdi = mock_vdi,
    .sd_card_info = info_api, .sd_card_format = format_api};
const struct dc_native_api *dc_os = &api;
int main(void)
{
    assert(app_begin_windowed());
    assert(app_window_open(&window, "SD Card Formatter", 576, 342, 576, 342));
    visible[0] = window.work; inspect_drawing = 1;
    refresh(); assert(can_format()); app_window_redraw(&window, window.work, draw);
    keypress('c'); keypress('d'); keypress(13); assert(!starts && !confirms);
    keypress('f'); assert(confirms == 1 && !starts); /* Cancel default. */
    confirmation = 2; start_error = DC_SD_BUSY;
    activate(1); assert(starts == 1 && strstr(message, "open") && card.state == DC_SD_READY);
    start_error = 0; activate(1); assert(starts == 2 && card.state == DC_SD_FORMATTING);
    keypress(27); activate(1); assert(!done && starts == 2);
    app_window_redraw(&window, window.work, draw);
    advance(); assert(steps == 1 && card.state == DC_SD_MOUNTED && alerts == 1 && !can_format());
    for (int i = 0; i < 2; i++) {
        fixture.state = DC_SD_READY; refresh(); activate(1);
        finish = i ? DC_SD_IO : DC_SD_REBOOT; advance();
        assert(card.state == (i ? DC_SD_FAILED : DC_SD_REBOOT));
        assert(!can_format()); app_window_redraw(&window, window.work, draw);
    }
    fixture.state = DC_SD_READY; fixture.card_sectors = 0; refresh(); assert(!can_format());
    int before = confirms; keypress('f'); assert(confirms == before);
    api.size = offsetof(struct dc_native_api, sd_card_info); refresh(); assert(!available && !can_format());
    api.size = sizeof(api); api.sd_card_format = NULL; refresh(); assert(!available);
    assert(hit(window.work.x + 20, window.work.y + 310) == 1);
    assert(hit(window.work.x + 450, window.work.y + 310) == 2);
    keypress(27); assert(done);
    app_window_close(&window); app_end();
    assert(!updates && !mouse_hidden && closed == 1);
    return 0;
}
