/* Exercise the real picker/window drawing and actions with a mounted SD fixture. */
#include "gem_model.h"
#include "dreamcast/tcp.h"
#include <stdarg.h>
#include "../apps/ports/ftp.c"
static struct dc_system_info fixture;
static uint32_t state = DC_NET_UP;
static int sockets, next_socket, fail_listen;
static long system_api(void *buf, uint32_t bytes)
{
    assert(bytes == sizeof(fixture)); memcpy(buf, &fixture, bytes); return bytes;
}
static long net_api(void *buf, uint32_t bytes)
{
    struct dc_net_info *n = buf; assert(bytes == sizeof(*n));
    memset(n, 0, bytes); n->state = state; n->ip[0] = 192; n->ip[1] = 168; n->ip[2] = 1; n->ip[3] = 50;
    return bytes;
}
static long listen_api(uint32_t port) { (void)port; if (fail_listen) return DC_TCP_IO; sockets++; return ++next_socket; }
static long accept_api(int h, uint8_t p[4]) { (void)h; (void)p; return DC_TCP_AGAIN; }
static long recv_api(int h, void *b, uint32_t n) { (void)h; (void)b; (void)n; return DC_TCP_AGAIN; }
static long send_api(int h, const void *b, uint32_t n) { (void)h; (void)b; return n; }
static long port_api(int h) { (void)h; return 21; }
static long close_api(int h) { assert(h > 0 && sockets > 0); sockets--; return 0; }
static long gemdos_api(int op, ...) { (void)op; assert(!"Unexpected file operation"); return -1; }
/* Fixture with 13 folders, enough to exercise paging without a fixed list cap. */
int ftp_fs_first(FtpDir *d, const char *p)
{
    if (!strcmp(p, "E:\\")) d->result = 13;
    else if (!strcmp(p, "E:\\DIR00")) d->result = 1;
    else d->result = 0;
    return 1;
}
int ftp_fs_next(FtpDir *d, FtpEntry *e)
{
    if (!d->result) return 0;
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "DIR%02ld", 13 - d->result--);
    e->attr = 16;
    return 1;
}
int ftp_fs_stat(const char *p, FtpEntry *e) { (void)p; memset(e, 0, sizeof(*e)); e->attr = 16; return 1; }
unsigned long app_millis(void) { return 0; }
static struct dc_native_api api = {.size = sizeof(api), .aes = mock_aes, .vdi = mock_vdi,
    .gemdos = gemdos_api, .system_info = system_api, .net_info = net_api,
    .tcp_listen = listen_api, .tcp_accept = accept_api, .tcp_recv = recv_api,
    .tcp_send = send_api, .tcp_port = port_api, .tcp_close = close_api};
const struct dc_native_api *dc_os = &api;
int main(void)
{
    fixture.version = 1; fixture.bytes = sizeof(fixture);
    fixture.drive_mask = (1 << 2) | (1 << 3) | (1 << 4);
    fixture.readonly_mask = 1 << 3; fixture.volatile_mask = 1 << 2;
    assert(dc_storage_drive() == 'E');
    ftp_init(&server);
    strcpy(drive_letters, "CDE");
    assert(app_begin_windowed());
    assert(app_window_open(&window, "FTP Server", 560, 390, 560, 390));
    visible[0] = window.work; inspect_drawing = 1;
    select_drive(dc_storage_drive());
    refresh_network();
    assert(count == ROWS && more && !strcmp(folder, "E:\\") && !server.listener);
    app_window_redraw(&window, window.work, draw);
    activate(23); assert(offset == 10 && count == 3 && !more);
    activate(22); assert(offset == 0 && count == 10);
    keypress(KEY_DOWN); assert(selected == 0);
    keypress(13); assert(!strcmp(folder, "E:\\DIR00"));
    keypress('u'); assert(!strcmp(folder, "E:\\"));
    activate(40); activate(21); assert(!strcmp(folder, "E:\\DIR00"));
    activate(30); assert(server.listener && server.writable && sockets == 1);
    app_window_redraw(&window, window.work, draw);
    keypress('c'); assert(!strcmp(folder, "E:\\DIR00")); /* cannot change a live share */
    keypress('s'); assert(!server.listener && !sockets);
    keypress('d'); assert(!strcmp(folder, "D:\\"));
    keypress('s'); assert(server.listener && !server.writable);
    state = DC_NET_NO_ADDRESS; refresh_network(); assert(!server.listener && !sockets);
    keypress('s'); assert(!server.listener);
    state = DC_NET_STARTING; refresh_network(); assert(strstr(net_status(), "DHCP"));
    state = DC_NET_NO_ADAPTER; refresh_network(); assert(strstr(net_status(), "No network"));
    state = DC_NET_UP; refresh_network();
    fail_listen = 1; keypress('s'); assert(!server.listener && strstr(server.status, "Cannot listen"));
    fail_listen = 0;
    api.size = offsetof(struct dc_native_api, tcp_listen);
    assert(!ftp_available()); keypress('s'); assert(!server.listener && strstr(net_status(), "update"));
    api.size = sizeof(api);
    fixture.drive_mask &= ~(1 << 4); assert(dc_storage_drive() == 'C');
    keypress('c'); keypress('s'); assert(server.listener && server.writable);
    keypress(27); assert(done);
    ftp_stop(&server); assert(!sockets);
    app_window_close(&window); app_end();
    assert(created == 1 && opened == 1 && closed == 1 && deleted == 1);
    assert(!updates && !mouse_hidden && !mouse_control && !palette_writes);
    puts("FTP picker, SD preference, network states, window drawing and stop: PASS");
    return 0;
}
