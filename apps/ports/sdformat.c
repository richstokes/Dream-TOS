/* Graphical FAT16 formatter for the physical serial SD card. GPL-2.0-or-later. */
#include "app.h"
#include "window.h"
#include "dreamcast/sd_format.h"
#include <string.h>

#define HAS(m) (dc_os->size >= offsetof(struct dc_native_api, m) + sizeof(dc_os->m) && dc_os->m)
static AppWindow window;
static struct dc_sd_format_info card;
static int available, done, buttons, pressed = -1;
static char message[80];

static void refresh(void)
{
    available = HAS(sd_card_info) && HAS(sd_card_format);
    if (!available || dc_os->sd_card_info(&card, sizeof(card)) != sizeof(card) || card.version != 1) {
        available = 0;
        memset(&card, 0, sizeof(card));
    }
}
static int can_format(void)
{
    return available && card.card_sectors && card.volume_sectors && card.state == DC_SD_READY;
}
static void text_at(int x, int y, const char *s, int colour)
{ app_text(window.work.x + x, window.work.y + y, s, colour); }
static void button(int x, int width, const char *label, int id, int enabled)
{
    int sx = window.work.x + x, sy = window.work.y + 302;
    app_box(sx, sy, width, 26, enabled && pressed == id ? 9 : 0);
    app_line(sx, sy, sx + width - 1, sy, 1);
    app_line(sx, sy + 25, sx + width - 1, sy + 25, 1);
    app_line(sx, sy, sx, sy + 25, 1);
    app_line(sx + width - 1, sy, sx + width - 1, sy + 25, 1);
    text_at(x + 8, 320, label, enabled ? 1 : 9);
}
static void draw(void)
{
    char line[80];
    app_box(window.work.x, window.work.y, window.work.w, window.work.h, 8);
    text_at(16, 26, "SD CARD FORMATTER  /  FAT16", 1);
    text_at(16, 54, "Target: SD card in the serial-port adapter", 1);
    text_at(16, 78, "C: RAM disk and D: disc are protected.", 4);
    if (card.card_sectors) {
        snprintf(line, sizeof(line), "Card: %lu MiB    New volume: %lu MiB (E:)",
                 (unsigned long)(card.card_sectors / 2048), (unsigned long)(card.volume_sectors / 2048));
        text_at(16, 112, line, 1);
        snprintf(line, sizeof(line), "MBR / FAT16 / %lu KiB clusters / DREAM SD",
                 (unsigned long)(card.cluster_sectors / 2));
        if (card.cluster_sectors == 1) strcpy(line, "MBR / FAT16 / 512-byte clusters / DREAM SD");
        text_at(16, 136, line, 1);
    } else {
        text_at(16, 112, available ? "No SD card was detected at boot." : "Update the OS to use the SD formatter.", 1);
        text_at(16, 136, "Insert the card and adapter before rebooting.", 1);
    }
    text_at(16, 170, "Erases ALL files and partitions on the SD card.", 2);
    text_at(16, 194, "Uses up to 2 GiB; remaining space is left unused.", 1);
    text_at(16, 218, "Quick format. Keep the card inserted and power on.", 1);
    const char *status = message;
    if (card.state == DC_SD_FORMATTING) status = "Formatting and verifying... Please wait.";
    else if (card.state == DC_SD_MOUNTED) status = "Ready on E:. Use Options > Install devices for its icon.";
    else if (card.state == DC_SD_REBOOT) status = "Format complete. Reboot before using the SD card.";
    else if (card.state == DC_SD_FAILED) status = "Format failed. Reboot, then retry; SD access is disabled.";
    else if (!message[0]) status = !available ? "Formatter service unavailable." :
        !card.card_sectors ? "No card to format." : !card.volume_sectors ? "Card too small (minimum 4 MiB)." :
        "Close SD files. Format will ask you to confirm.";
    text_at(16, 254, status, 1);
    if (card.total) {
        int percent = (int)(100 * card.completed / card.total);
        app_box(window.work.x + 16, window.work.y + 270, 544, 10, 0);
        if (percent) app_box(window.work.x + 16, window.work.y + 270, 544 * percent / 100, 10, 4);
    }
    button(16, 184, "Format FAT16 [F]", 1, can_format());
    button(440, 120, "Quit [Esc]", 2, card.state != DC_SD_FORMATTING);
}
static int confirm(void)
{
    ai[0] = 1; /* Return defaults to Cancel. */
    aa[0] = (intptr_t)"[3][Erase the entire serial SD card?|ALL files and partitions will be lost.|Create one FAT16 volume, up to 2 GiB?|C: and D: will not be formatted.][Cancel|Erase SD]";
    aes_call(52, 1, 1, 1);
    return ao[0] == 2;
}
static void activate(int id)
{
    if (card.state == DC_SD_FORMATTING) return;
    if (id == 2) { done = 1; return; }
    if (id != 1 || !can_format() || !confirm()) return;
    long r = dc_os->sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_START);
    message[0] = 0;
    if (r < 0) strcpy(message, r == DC_SD_BUSY ? "SD files are open. Close them before formatting." :
        r == DC_SD_ABSENT ? "No card detected. Insert it before rebooting." :
        r == DC_SD_TOO_SMALL ? "Card too small (minimum 4 MiB)." :
        r == DC_SD_NEEDS_REBOOT ? "Reboot before using the SD card." : "Cannot access the card. Check the adapter and reboot.");
    refresh();
}
static void advance(void)
{
    if (card.state != DC_SD_FORMATTING) return;
    long r = dc_os->sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_STEP);
    refresh();
    if (r == DC_SD_MOUNTED)
        app_alert("Format complete. SD card is ready on E:.|For its desktop icon, choose|Options > Install devices.");
    else if (r == DC_SD_REBOOT)
        app_alert("Format complete.|Reboot the console before using the card.|Save any C: files you need first.");
    else if (r < 0)
        app_alert("Formatting or verification failed.|SD access is disabled to protect the card.|Reboot, check the adapter, then retry.");
}
static void keypress(int key)
{
    int c = key & 255;
    if (c == 27) activate(2);
    else if (c == 'f' || c == 'F') activate(1);
}
static int hit(int x, int y)
{
    x -= window.work.x; y -= window.work.y;
    if (y < 302 || y >= 328) return -1;
    if (x >= 16 && x < 200) return 1;
    if (x >= 440 && x < 560) return 2;
    return -1;
}
int app_main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!app_begin_windowed()) return 1;
    if (!app_window_open(&window, "SD Card Formatter", 576, 342, 576, 342)) { app_end(); return 1; }
    refresh();
    app_window_redraw(&window, window.work, draw);
    while (!done) {
        AppEvent ev;
        app_window_event(&ev, 20, buttons);
        int redraw = 0;
        if (ev.flags & APP_MESSAGE) {
            int r = app_window_message(&window, ev.message);
            if (r == WINDOW_CLOSE) activate(2);
            redraw = r == WINDOW_REDRAW || r == WINDOW_CHANGED;
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
        if (card.state == DC_SD_FORMATTING) { advance(); redraw = 1; }
        if (redraw) app_window_redraw(&window, window.work, draw);
    }
    app_window_close(&window);
    app_end();
    return 0;
}
