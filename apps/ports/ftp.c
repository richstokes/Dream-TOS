/* Graphical anonymous FTP server and drive/folder picker. GPL-2.0-or-later. */
#include "app.h"
#include "window.h"
#include "drives.h"
#include "ftp_core.h"
#include "dreamcast/net.h"
#include <string.h>

#define ROWS 10
#define HAS(m) (dc_os->size >= offsetof(struct dc_native_api, m) + sizeof(dc_os->m) && dc_os->m)
static AppWindow window;
static FtpServer server;
static struct dc_net_info network;
static char folder[FTP_PATH], dirs[ROWS][14], drive_letters[7];
static int offset, count, more, selected, pressed = -1, buttons, done;
static uint32_t refreshed, painted;
static char message[96];

static void scan(void)
{
    FtpDir dir;
    FtpEntry e;
    count = more = 0;
    selected = -1;
    message[0] = 0;
    if (!ftp_fs_first(&dir, folder)) { strcpy(message, "Cannot read this folder."); return; }
    int skipped = 0, rc;
    while ((rc = ftp_fs_next(&dir, &e)) > 0) {
        if (!(e.attr & 16)) continue;
        if (skipped++ < offset) continue;
        if (count == ROWS) { more = 1; break; }
        strcpy(dirs[count++], e.name);
    }
    if (rc < 0) strcpy(message, "Folder read failed.");
}
static void select_drive(int letter)
{
    snprintf(folder, sizeof(folder), "%c:\\", letter);
    offset = 0;
    scan();
}
static void parent(void)
{
    char *p = strrchr(folder, '\\');
    if (p == folder + 2) p[1] = 0;
    else if (p) *p = 0;
    offset = 0;
    scan();
}
static void enter(void)
{
    if (selected < 0 || selected >= count) return;
    char path[FTP_PATH];
    int n = snprintf(path, sizeof(path), "%s%s%s", folder, strlen(folder) == 3 ? "" : "\\", dirs[selected]);
    if (n >= (int)sizeof(path)) { strcpy(message, "Folder path is too long."); return; }
    strcpy(folder, path);
    offset = 0;
    scan();
}
static void refresh_network(void)
{
    memset(&network, 0, sizeof(network));
    if (!HAS(net_info) || dc_os->net_info(&network, sizeof(network)) < 0)
        network.state = DC_NET_NO_ADAPTER;
    if (server.listener && (network.state != DC_NET_UP || memcmp(network.ip, server.ip, 4))) {
        ftp_stop(&server);
        strcpy(message, "Network changed. Check connection, then Start.");
    }
}
static const char *net_status(void)
{
    if (!ftp_available()) return "TCP unavailable: update the OS to run this app.";
    if (network.state == DC_NET_STARTING) return "Network starting; waiting for DHCP...";
    if (network.state == DC_NET_NO_ADAPTER) return "No network adapter detected.";
    if (network.state != DC_NET_UP) return "Adapter has no IPv4 address; check DHCP.";
    return "Network ready. Anonymous access; no password.";
}
/* Fixed content geometry fits a 560 x 390 work area; normal GEM clipping
 * and window furniture handle movement, covering and maximization. */
static void text_at(int x, int y, const char *s, int colour)
{
    app_text(window.work.x + x, window.work.y + y, s, colour);
}
static void button(int x, int y, int w, const char *label, int id, int enabled)
{
    int sx = window.work.x + x, sy = window.work.y + y;
    app_box(sx, sy, w, 24, enabled && pressed == id ? 9 : 0);
    app_line(sx, sy, sx + w - 1, sy, 1);
    app_line(sx, sy + 23, sx + w - 1, sy + 23, 1);
    app_line(sx, sy, sx, sy + 23, 1);
    app_line(sx + w - 1, sy, sx + w - 1, sy + 23, 1);
    text_at(x + 6, y + 17, label, enabled ? 1 : 9);
}
static void draw(void)
{
    app_box(window.work.x, window.work.y, window.work.w, window.work.h, 8);
    char line[100];
    text_at(12, 22, server.listener ? "Anonymous FTP server is running" : "Choose the drive or folder to share", 1);
    text_at(12, 46, net_status(), 1);
    if (server.listener) {
        snprintf(line, sizeof(line), "ftp://%u.%u.%u.%u/   Port 21", network.ip[0], network.ip[1], network.ip[2], network.ip[3]);
        text_at(12, 86, line, 4);
        text_at(12, 120, "Sharing:", 1);
        snprintf(line, sizeof(line), "%.65s", server.root);
        text_at(12, 144, line, 1);
        text_at(12, 178, server.writable ? "Read / write - clients can replace or delete files." : "Read-only drive - browsing and downloads.", 1);
        text_at(12, 206, "Use passive mode and binary file transfers.", 1);
        text_at(12, 230, "One client at a time. Keep this application open.", 1);
        snprintf(line, sizeof(line), "Transferred: %lu KiB", (unsigned long)(server.transferred / 1024));
        text_at(12, 270, line, 1);
    } else {
        for (int i = 0; drive_letters[i]; i++) {
            snprintf(line, sizeof(line), "%c: %s", drive_letters[i], drive_letters[i] >= 'E' ? "SD" : drive_letters[i] == 'C' ? "RAM" : "Disc");
            button(12 + i * 88, 58, 82, line, 10 + i, 1);
        }
        size_t len = strlen(folder);
        snprintf(line, sizeof(line), "%s%s", len > 65 ? "..." : "", folder + (len > 65 ? len - 62 : 0));
        text_at(12, 104, line, 1);
        button(12, 112, 82, "Up [U]", 20, strlen(folder) > 3);
        button(104, 112, 100, "Open [O]", 21, selected >= 0);
        button(344, 112, 92, "Prev [<]", 22, offset > 0);
        button(446, 112, 100, "Next [>]", 23, more);
        app_box(window.work.x + 12, window.work.y + 144, 534, 180, 0);
        for (int i = 0; i < count; i++) {
            if (i == selected) app_box(window.work.x + 12, window.work.y + 144 + 18 * i, 534, 18, 4);
            snprintf(line, sizeof(line), "  %-12s  <folder>", dirs[i]);
            text_at(16, 158 + 18 * i, line, i == selected ? 0 : 1);
        }
        if (!count) text_at(22, 168, "No subfolders. Start shares this folder.", 1);
    }
    snprintf(line, sizeof(line), "%.67s", message[0] ? message : server.status);
    text_at(12, 345, line, 1);
    button(12, 356, 156, server.listener ? "Stop [S]" : "Start here [S]", 30,
           server.listener || (ftp_available() && network.state == DC_NET_UP));
    button(434, 356, 112, "Quit [Esc]", 31, 1);
    if (!server.listener) text_at(180, 374, folder[0] == 'C' ? "C: lost at reset" : folder[0] == 'D' ? "D: read-only" : "SD card", 1);
}
static int hit(int x, int y)
{
    x -= window.work.x; y -= window.work.y;
    if (y >= 356 && y < 380) {
        if (x >= 12 && x < 168) return 30;
        if (x >= 434 && x < 546) return 31;
    }
    if (server.listener) return -1;
    if (y >= 58 && y < 82 && x >= 12) {
        int i = (x - 12) / 88;
        if (i < (int)strlen(drive_letters) && (x - 12) % 88 < 82) return 10 + i;
    }
    if (y >= 112 && y < 136) {
        if (x >= 12 && x < 94) return 20;
        if (x >= 104 && x < 204) return 21;
        if (x >= 344 && x < 436) return 22;
        if (x >= 446 && x < 546) return 23;
    }
    if (y >= 144 && y < 324 && x >= 12 && x < 546 && (y - 144) / 18 < count) return 40 + (y - 144) / 18;
    return -1;
}
static void activate(int id)
{
    if (id == 31) { done = 1; return; }
    if (id == 30) {
        message[0] = 0;
        if (server.listener) { ftp_stop(&server); scan(); }
        else {
            refresh_network();
            if (network.state == DC_NET_UP && ftp_available())
                ftp_start(&server, folder, dc_drive_state(folder[0]) != DC_DRIVE_NONE, network.ip, 21);
        }
        return;
    }
    if (server.listener) return;
    if (id >= 10 && id < 10 + (int)strlen(drive_letters)) select_drive(drive_letters[id - 10]);
    else if (id == 20) parent();
    else if (id == 21) enter();
    else if (id == 22 && offset) { offset -= ROWS; scan(); }
    else if (id == 23 && more) { offset += ROWS; scan(); }
    else if (id >= 40 && id < 40 + count) selected = id - 40;
}
static void keypress(int key)
{
    int c = key & 255;
    if (c == 27) activate(31);
    else if (c == 's' || c == 'S') activate(30);
    else if (!server.listener) {
        if (key == KEY_UP && count) selected = selected > 0 ? selected - 1 : 0;
        else if (key == KEY_DOWN && count) selected = selected + 1 < count ? selected + 1 : count - 1;
        else if (c == 13 || c == 'o' || c == 'O') enter();
        else if (c == 8 || c == 'u' || c == 'U') parent();
        else if (c == '<' || key == 0x4900) activate(22);
        else if (c == '>' || key == 0x5100) activate(23);
        else for (int i = 0; drive_letters[i]; i++) if (toupper(c) == drive_letters[i]) activate(10 + i);
    }
}
int app_main(int argc, char **argv)
{
    (void)argc; (void)argv;
    ftp_init(&server);
    if (!app_begin_windowed()) return 1;
    if (!app_window_open(&window, "FTP Server", 560, 390, 560, 390)) { app_end(); return 1; }
    struct dc_system_info info;
    uint32_t mask = 1u << 2;
    if (HAS(system_info) && dc_os->system_info(&info, sizeof(info)) == sizeof(info)) mask = info.drive_mask;
    int n = 0;
    for (char d = 'C'; d <= 'H'; d++) if (mask & (1u << (d - 'A'))) drive_letters[n++] = d;
    drive_letters[n] = 0;
    select_drive(dc_storage_drive());
    refresh_network();
    app_window_redraw(&window, window.work, draw);
    while (!done) {
        AppEvent ev;
        app_window_event(&ev, 20, buttons);
        int redraw = 0;
        if (ev.flags & APP_MESSAGE) {
            int rc = app_window_message(&window, ev.message);
            if (rc == WINDOW_CLOSE) done = 1;
            if (rc == WINDOW_REDRAW || rc == WINDOW_CHANGED) redraw = 1;
            pressed = -1;
        }
        if (ev.flags & APP_KEY) { keypress(ev.key); redraw = 1; }
        if (ev.flags & APP_BUTTON) {
            int id = app_window_contains(&window, ev.x, ev.y) ? hit(ev.x, ev.y) : -1;
            if ((ev.buttons & 1) && !(buttons & 1)) pressed = id;
            if (!(ev.buttons & 1) && (buttons & 1)) {
                if (pressed >= 0 && id == pressed) activate(id);
                pressed = -1;
            }
            redraw = 1;
        }
        buttons = ev.buttons;
        uint32_t now = (uint32_t)app_millis();
        if (now - refreshed >= 1000) { refresh_network(); refreshed = now; redraw = 1; }
        ftp_poll(&server, now);
        if (server.listener && now - painted >= 250) redraw = 1;
        if (redraw) { app_window_redraw(&window, window.work, draw); painted = now; }
    }
    ftp_stop(&server);
    app_window_close(&window);
    app_end();
    return 0;
}
