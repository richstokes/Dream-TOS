/* VMU Toolbox: card browser, hex/ASCII, LCD and VMS save-icon editors.
 * Native GEM application. GPL-2.0-or-later.
 *
 * Every card write goes through the OS vmu_file_* service, which validates the
 * card, refuses on damage, verifies by read-back and never formats. This
 * program adds confirmations before anything destructive. */
#include "app.h"
#include "accessory.h"
#include "drives.h"
#include "dreamcast/system_info.h"
#include "dreamcast/vmu_info.h"
#include "dreamcast/vmu_file.h"
#include "vmuedit_core.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WHITE = 0, BLACK = 1, RED = 2, GREEN = 3, BLUE = 4, GREY = 8, DARK = 9 };
enum { VIEW_FILES, VIEW_LCD, VIEW_ICON, VIEW_HEX };
enum {
    C_NONE = -1, C_PREV, C_NEXT, C_REFRESH, C_IMPORT, C_EXPORT, C_RENAME, C_DELETE, C_LCD, C_ICON, C_QUIT,
    C_L_INVERT, C_L_CLEAR, C_L_FLIPH, C_L_FLIPV, C_L_UNDO, C_L_LIVE, C_L_LOAD, C_L_SAVE, C_L_SEND, C_L_BACK,
    C_L_LEFT, C_L_RIGHT, C_L_UP, C_L_DOWN,
    C_I_PREVF, C_I_NEXTF, C_I_FLIPH, C_I_FLIPV, C_I_FILL, C_I_UNDO, C_I_SAVE, C_I_BACK,
    C_HEX, C_H_EDIT, C_H_GOTO, C_H_UNDO, C_H_SAVE, C_H_BACK, C_H_PREV, C_H_NEXT,
    C_SD_IMPORT, C_SD_EXPORT,
    C_ROW = 100, C_PAL = 200
};
#define LIST_ROWS 10
#define MAX_FILE DC_VMUF_MAX_BYTES

/* ---- state --------------------------------------------------------------------- */
typedef struct { int x, y, w, h, id; } Hot;
static Hot hot[64];
static int hot_count, view, need_draw = 1, old_buttons, press_hit = C_NONE, quitting;
static char message[128];
static int message_colour = BLACK;
static struct dc_device_info cards[DC_SYSTEM_INFO_DEVICES];
static int card_count, card_pos, service_ok, read_ok, write_ok, lcd_ok;
static unsigned sd_read_drives, sd_write_drives;
static struct dc_vmu_info card;
static int selected, top;
static uint8_t file_buffer[MAX_FILE];
/* Hex editing keeps the exact original for change tracking and a pre-write
 * comparison. No implicit checksum or header changes are made in raw mode. */
static uint8_t hex_original[MAX_FILE];
static char hex_name[13];
static uint32_t hex_bytes, hex_pos, hex_top, hex_changes, hex_port, hex_unit;
static int hex_writable, hex_edit, hex_ascii, hex_nibble, hex_have_undo;
static uint32_t hex_undo_pos;
static uint8_t hex_undo_value;
#define HEX_COLS 16
#define HEX_ROWS 16
#define HEX_PAGE (HEX_COLS * HEX_ROWS)
#define HEX_X 88
#define ASCII_X 492
#define HEX_Y 96
/* LCD editor */
static uint8_t lcd[LCD_BYTES], lcd_undo[LCD_BYTES];
static int lcd_have_undo, lcd_dirty, lcd_live = 1, lcd_pending, pen, lcd_cx, lcd_cy, lcd_last_x = -1, lcd_last_y = -1,
           lcd_stroke = -1;
static unsigned long lcd_pushed;
/* Icon editor */
static char icon_name[13];
static uint32_t icon_bytes;
static struct vms_info vms;
static uint8_t icon_undo[VMS_HEADER + VMS_MAX_FRAMES * VMS_ICON_BYTES];
static int icon_have_undo, icon_dirty, icon_frame, icon_colour, icon_cx, icon_cy, icon_stroke = -1;
static int icon_slot[16], icon_transparent[16];
static int16_t saved_palette[16][3];
static int palette_swapped;

/* Dialogs are indirect so the host test can script them. */
static int real_confirm(const char *text, const char *yes);
static int real_choose(const char *title, int pick_dir, char *out, size_t cap);
static void real_alert(const char *text);
static int (*ui_confirm)(const char *, const char *) = real_confirm;
static int (*ui_choose)(const char *, int, char *, size_t) = real_choose;
static int real_prompt(const char *label, char *buffer, size_t cap);
static int (*ui_prompt)(const char *, char *, size_t) = real_prompt;
static void (*ui_alert)(const char *) = real_alert;

/* ---- messages ------------------------------------------------------------------ */
static void say(int colour, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    message_colour = colour;
    need_draw = 1;
}
static const char *error_text(long code)
{
    static char other[40];
    switch (code) {
    case DC_VMUF_ABSENT: return "The card was removed or is not a memory card. Nothing was changed.";
    case DC_VMUF_IO: return "Card error; the file was left as it was.";
    case DC_VMUF_CARD: return "Card is unformatted or its file system looks damaged. Nothing was changed.";
    case DC_VMUF_NOT_FOUND: return "That file is not on the card.";
    case DC_VMUF_EXISTS: return "A file with that name already exists.";
    case DC_VMUF_FULL: return "Not enough free blocks on the card.";
    case DC_VMUF_DIR_FULL: return "The card directory is full.";
    case DC_VMUF_PROTECTED: return "Copy-protected, game or unusual file: left alone.";
    case DC_VMUF_BAD_NAME: return "Names are 1 to 12 printable characters.";
    case DC_VMUF_TOO_BIG: return "Too big: a VMU file is at most 200 blocks (100 KiB).";
    case DC_VMUF_VERIFY: return "WRITE NOT VERIFIED. The card may be damaged: check it in the Dreamcast file manager.";
    case DC_VMUF_RESTORED: return "The write failed; the previous file was restored.";
    case DC_VMUF_AMBIGUOUS: return "Similar names on the card make this unsafe; nothing was changed.";
    case DC_VMUF_BUSY: return "The card is busy; try again.";
    case DC_VMUF_UNSUPPORTED: return "This device has no LCD.";
    case DC_VMUF_BUFFER: return "Internal buffer too small.";
    case DC_VMUF_BADARG: return "The OS rejected the request.";
    default: snprintf(other, sizeof(other), "Unexpected error %ld.", code); return other;
    }
}

/* ---- cards --------------------------------------------------------------------- */
static struct dc_system_info sysinfo;
/* Serial SD partitions are mounted as E:-H:. Never treat C: as an SD fallback. */
static unsigned sd_mask(const struct dc_system_info *info, int writing)
{
    if (info->version != DC_SYSTEM_INFO_VERSION) return 0;
    unsigned mask = info->drive_mask & ~info->volatile_mask & 0xf0u;
    return writing ? mask & ~info->readonly_mask : mask;
}
static unsigned sd_drive_mask(int writing)
{
    static struct dc_system_info info;
    if (!APP_HAS(system_info) || dc_os->system_info(&info, sizeof(info)) != sizeof(info)) return 0;
    return sd_mask(&info, writing);
}
static void scan_cards(void)
{
    unsigned port = card_count ? cards[card_pos].port : 99, unit = card_count ? cards[card_pos].unit : 99;
    card_count = 0;
    card_pos = 0;
    sd_read_drives = sd_write_drives = 0;
    service_ok = APP_HAS(vmu_info) && APP_HAS(system_info) && dc_os->system_info(&sysinfo, sizeof(sysinfo)) == sizeof(sysinfo);
    read_ok = service_ok && APP_HAS(vmu_file_read);
    write_ok = read_ok && APP_HAS(vmu_file_write) && APP_HAS(vmu_file_delete);
    lcd_ok = APP_HAS(vmu_screen);
    if (!service_ok)
        return;
    sd_read_drives = sd_mask(&sysinfo, 0);
    sd_write_drives = sd_mask(&sysinfo, 1);
    for (unsigned i = 0; i < sysinfo.device_count && i < DC_SYSTEM_INFO_DEVICES; i++)
        if (sysinfo.devices[i].functions & 0x02000000) {
            cards[card_count] = sysinfo.devices[i];
            if (cards[card_count].port == port && cards[card_count].unit == unit)
                card_pos = card_count;
            card_count++;
        }
}
static void read_card(void)
{
    memset(&card, 0, sizeof(card));
    card.status = card_count ? DC_VMU_NOT_READ : DC_VMU_ABSENT;
    if (!service_ok || !card_count)
        return;
    long r = dc_os->vmu_info(cards[card_pos].port, cards[card_pos].unit, &card, sizeof(card));
    if (r != sizeof(card) || card.version != DC_VMU_VERSION) {
        memset(&card, 0, sizeof(card));
        card.status = DC_VMU_IO;
    }
    if (card.file_count > DC_VMU_FILES) {
        card.file_count = 0;
        card.status = DC_VMU_CORRUPT;
    }
    if (selected >= (int)card.file_count)
        selected = card.file_count ? card.file_count - 1 : 0;
    if (top > selected)
        top = selected;
    need_draw = 1;
}
static const char *card_status_text(int status)
{
    switch (status) {
    case DC_VMU_NOT_READ: return "Press R to read the card.";
    case DC_VMU_ABSENT: return "No memory card connected.";
    case DC_VMU_IO: return "Card read failed. Check it and press R.";
    case DC_VMU_UNFORMATTED: return "Card is unformatted; nothing will be written to it.";
    case DC_VMU_UNSUPPORTED: return "This card layout is not supported.";
    default: return "Card metadata is damaged; nothing will be written to it.";
    }
}
static int select_card(int direction)
{
    if (!card_count)
        return 0;
    card_pos = (card_pos + direction + card_count) % card_count;
    selected = top = 0;
    read_card();
    message[0] = 0;
    return 1;
}
static const struct dc_device_info *current_card(void)
{
    return card_count ? &cards[card_pos] : NULL;
}
static int card_ready(void)
{
    return current_card() && card.status == DC_VMU_OK;
}
static const struct dc_vmu_file *selected_file(void)
{
    return card_ready() && selected < (int)card.file_count ? &card.files[selected] : NULL;
}
static int find_on_card(const char *name)
{
    for (unsigned i = 0; i < card.file_count; i++)
        if (!strcmp(card.files[i].name, name))
            return (int)i;
    return -1;
}
static void select_named(const char *name)
{
    int i = find_on_card(name);
    if (i >= 0) {
        selected = i;
        if (selected < top || selected >= top + LIST_ROWS)
            top = selected > LIST_ROWS / 2 ? selected - LIST_ROWS / 2 : 0;
    }
}

/* ---- drawing helpers ----------------------------------------------------------- */
static void frame(int x, int y, int w, int h, int c)
{
    app_line(x, y, x + w - 1, y, c);
    app_line(x, y, x, y + h - 1, c);
    app_line(x + w - 1, y, x + w - 1, y + h - 1, c);
    app_line(x, y + h - 1, x + w - 1, y + h - 1, c);
}
static void add_hot(int x, int y, int w, int h, int id)
{
    if (hot_count < (int)(sizeof(hot) / sizeof(hot[0])))
        hot[hot_count++] = (Hot){x, y, w, h, id};
}
static void button(int x, int y, int w, const char *label, int id, int enabled)
{
    app_box(x, y, w, 24, enabled ? BLACK : DARK);
    app_box(x + 2, y + 2, w - 4, 20, enabled ? WHITE : GREY);
    app_text(x + (w - (int)strlen(label) * 8) / 2, y + 17, label, enabled ? BLACK : DARK);
    if (enabled)
        add_hot(x, y, w, 24, id);
}
static int hit_test(int x, int y)
{
    for (int i = hot_count - 1; i >= 0; i--)
        if (x >= hot[i].x && y >= hot[i].y && x < hot[i].x + hot[i].w && y < hot[i].y + hot[i].h)
            return hot[i].id;
    return C_NONE;
}
static void clear_screen(void)
{
    app_unclip();
    app_box(0, 24, 640, 430, WHITE);
    hot_count = 0;
}
static void status_bar(const char *s)
{
    app_status(s);
}
/* Up to two lines of 76 characters, broken at a space. */
static void draw_message(int y)
{
    char first[80];
    size_t n = strlen(message);
    if (!n)
        return;
    if (n <= 76) {
        app_text(12, y, message, message_colour);
        return;
    }
    size_t cut = 76;
    while (cut > 40 && message[cut] != ' ')
        cut--;
    snprintf(first, sizeof(first), "%.*s", (int)cut, message);
    app_text(12, y, first, message_colour);
    app_text(12, y + 16, message + cut + (message[cut] == ' '), message_colour);
}
static const char *card_label(char *out, size_t n)
{
    const struct dc_device_info *dev = current_card();
    if (!dev)
        snprintf(out, n, "No card");
    else
        snprintf(out, n, "%c%lu: %.24s   (%d of %d)", 'A' + (int)dev->port, (unsigned long)dev->unit, dev->name,
                 card_pos + 1, card_count);
    return out;
}

/* Mono planar blit for previews: 4-plane pixel value 15 is black, 0 white. */
static const unsigned char physical[16] = {0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13};
static uint16_t planes[6 * 4 * 96];
static void blit_pixels(int w, int h, int x, int y, int (*pixel)(int, int, void *), void *ctx)
{
    int stride = (w + 15) / 16 * 4;
    if (w > 96 || h > 96)
        return;
    memset(planes, 0, stride * h * sizeof(uint16_t));
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            int value = physical[pixel(px, py, ctx) & 15];
            uint16_t bit = 0x8000 >> (px & 15);
            for (int p = 0; p < 4; p++)
                if (value & (1 << p))
                    planes[py * stride + px / 16 * 4 + p] |= bit;
        }
    app_blit(planes, w, h, x, y);
}
struct scaled { int scale; int kind; };
static int lcd_pixel_fn(int x, int y, void *ctx)
{
    struct scaled *s = ctx;
    return lcd_get(lcd, x / s->scale, y / s->scale) ? BLACK : WHITE;
}
static int icon_pixel_fn(int x, int y, void *ctx)
{
    struct scaled *s = ctx;
    return icon_slot[vms_pixel(file_buffer, icon_frame, x / s->scale, y / s->scale)];
}

/* ---- file view ----------------------------------------------------------------- */
static void draw_files(void)
{
    char line[128];
    clear_screen();
    app_text(12, 47, card_label(line, sizeof(line)), BLACK);
    button(360, 30, 40, "<", C_PREV, card_count > 1);
    button(404, 30, 40, ">", C_NEXT, card_count > 1);
    button(456, 30, 172, "Refresh [R]", C_REFRESH, service_ok);
    if (!service_ok) {
        app_text(12, 100, "The VMU service is not available in this OS build.", RED);
        return;
    }
    if (!card_count) {
        app_text(12, 100, "No VMU or memory card is connected.", RED);
    } else if (card.status != DC_VMU_OK) {
        app_text(12, 100, card_status_text(card.status), RED);
    } else {
        snprintf(line, sizeof(line), "%lu / %lu blocks free   %lu files   512 B/block%s",
                 (unsigned long)card.free_blocks, (unsigned long)card.total_blocks, (unsigned long)card.file_count,
                 write_ok ? "" : "   READ ONLY OS");
        app_text(12, 68, line, BLACK);
        app_box(12, 76, 616, 18, BLACK);
        app_text(16, 90, "NAME           BLOCKS  KIND   COPY       MODIFIED", WHITE);
        for (int row = 0; row < LIST_ROWS && top + row < (int)card.file_count; row++) {
            const struct dc_vmu_file *f = &card.files[top + row];
            int y = 96 + 20 * row, chosen = top + row == selected;
            if (chosen)
                app_box(12, y, 616, 20, BLUE);
            snprintf(line, sizeof(line), "%-14.12s %5lu   %-6s %-10s %s", f->name, (unsigned long)f->blocks,
                     f->type == 0xcc ? "Game" : "Data", f->protected_file ? "Protected" : "Allowed", f->modified);
            app_text(16, y + 15, line, chosen ? WHITE : BLACK);
            add_hot(12, y, 616, 20, C_ROW + row);
        }
        if (!card.file_count)
            app_text(16, 116, "This card has no files.", BLACK);
        const struct dc_vmu_file *f = selected_file();
        if (f) {
            snprintf(line, sizeof(line), "%s: %lu bytes   first block %lu   header +%lu blocks", f->name,
                     (unsigned long)(f->blocks * 512), (unsigned long)f->first_block, (unsigned long)f->header_block);
            app_text(12, 312, line, DARK);
        }
    }
    draw_message(366);
    const struct dc_vmu_file *f = selected_file();
    int can_write = write_ok && card_ready();
    int plain = f && f->type == 0x33 && !f->protected_file && !f->header_block;
    if (sd_read_drives) {
        snprintf(line, sizeof(line), "SD:");
        for (int d = 4; d < 8; d++)
            if (sd_read_drives & (1u << d)) {
                size_t n = strlen(line);
                snprintf(line + n, sizeof(line) - n, " %c:%s", 'A' + d,
                         sd_write_drives & (1u << d) ? "" : "(RO)");
            }
    } else snprintf(line, sizeof(line), "SD: no mounted card");
    app_text(12, 347, line, sd_read_drives ? BLACK : DARK);
    button(328, 330, 150, "From SD [F6]", C_SD_IMPORT, sd_read_drives && can_write);
    button(486, 330, 142, "To SD [F7]", C_SD_EXPORT, sd_write_drives && read_ok && f && !f->protected_file);
    button(12, 392, 150, "Import [I]", C_IMPORT, can_write);
    button(170, 392, 150, "Export [E]", C_EXPORT, read_ok && f && !f->protected_file);
    button(328, 392, 150, "Rename [N]", C_RENAME, can_write && plain);
    button(486, 392, 142, "Delete [D]", C_DELETE, can_write && plain);
    button(12, 422, 150, "LCD [L]", C_LCD, 1);
    button(170, 422, 150, "Icon [C]", C_ICON, write_ok && plain);
    button(328, 422, 150, "Hex/ASCII [H]", C_HEX, read_ok && f && !f->protected_file);
    button(486, 422, 142, "Quit [Esc]", C_QUIT, 1);
    status_bar("Files: arrows select | H hex/ASCII | L LCD | C icon | R refresh | Esc quit");
}
static void ensure_visible(void)
{
    if (selected < top)
        top = selected;
    if (selected >= top + LIST_ROWS)
        top = selected - LIST_ROWS + 1;
}
static int move_selection(int delta)
{
    if (!card_ready() || !card.file_count)
        return 0;
    int n = selected + delta;
    if (n < 0)
        n = 0;
    if (n >= (int)card.file_count)
        n = card.file_count - 1;
    selected = n;
    ensure_visible();
    return 1;
}

/* ---- dialogs ------------------------------------------------------------------- */
static void with_mouse(int on)
{
    app_mouse(on);
}
static void restore_palette(void)
{
    if (!palette_swapped)
        return;
    for (int i = 2; i < 16; i++)
        app_palette(i, saved_palette[i][0], saved_palette[i][1], saved_palette[i][2]);
    palette_swapped = 0;
}
static void apply_icon_palette(void);
static int real_confirm(const char *text, const char *yes)
{
    char b[300];
    int swapped = palette_swapped;
    /* Default button is the second one (Cancel): Return alone never destroys anything. */
    snprintf(b, sizeof(b), "[2][%.220s][%.14s|Cancel]", text, yes);
    restore_palette();
    with_mouse(1);
    ai[0] = 2;
    aa[0] = (intptr_t)b;
    aes_call(52, 1, 1, 1);
    int answer = ao[0];
    with_mouse(0);
    if (swapped)
        apply_icon_palette();
    need_draw = 1;
    return answer == 1;
}
static void real_alert(const char *text)
{
    int swapped = palette_swapped;
    restore_palette();
    with_mouse(1);
    app_alert(text);
    with_mouse(0);
    if (swapped)
        apply_icon_palette();
    need_draw = 1;
}
static int real_prompt(const char *label, char *buffer, size_t cap)
{
    int ok = app_prompt(label, buffer, cap);
    need_draw = 1; /* the prompt box is drawn straight onto the screen */
    return ok;
}
static void settle(void)
{
    /* After a modal dialog: forget a stale press and wait for the button to be released. */
    int b = 1, mx, my;
    for (int i = 0; i < 25 && b; i++) {
        with_mouse(1);
        app_event(20, &mx, &my, &b);
        with_mouse(0);
    }
    old_buttons = 0;
    press_hit = C_NONE;
}

/* ---- GEMDOS file chooser -------------------------------------------------------- */
struct entry { char name[14]; unsigned long size; int dir; };
#define MAX_ENTRIES 300
static struct entry entries[MAX_ENTRIES];
static int entry_count;
static char dta_buffer[44] __attribute__((aligned(4)));
static char choose_dir[80] = "";
static char sd_choose_dir[80] = "";
static int choose_sd_only, choose_writing;
static int entry_order(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir;
    if (!strcmp(x->name, ".."))
        return -1;
    if (!strcmp(y->name, ".."))
        return 1;
    return strcmp(x->name, y->name);
}
static int at_root(const char *dir)
{
    return dir[2] == '\\' && !dir[3];
}
static void join(char *out, size_t n, const char *dir, const char *name)
{
    snprintf(out, n, "%s%s%s", dir, at_root(dir) ? "" : "\\", name);
}
static void list_directory(const char *dir)
{
    char pattern[100];
    entry_count = 0;
    void *previous = (void *)dc_os->gemdos(0x2f);
    dc_os->gemdos(0x1a, dta_buffer);
    join(pattern, sizeof(pattern), dir, "*.*");
    long r = dc_os->gemdos(0x4e, pattern, 0x16);
    while (r == 0 && entry_count < MAX_ENTRIES) {
        unsigned attr = (unsigned char)dta_buffer[21];
        uint32_t size;
        memcpy(&size, dta_buffer + 26, 4);
        const char *name = dta_buffer + 30;
        if (strcmp(name, ".") && !(attr & 8)) {
            struct entry *e = &entries[entry_count++];
            snprintf(e->name, sizeof(e->name), "%s", name);
            e->size = size;
            e->dir = (attr & 0x10) != 0;
        }
        r = dc_os->gemdos(0x4f);
    }
    dc_os->gemdos(0x1a, previous);
    qsort(entries, entry_count, sizeof(entries[0]), entry_order);
}
static unsigned drive_mask(void)
{
    static struct dc_system_info info;
    if (APP_HAS(system_info) && dc_os->system_info(&info, sizeof(info)) == sizeof(info) && info.version == DC_SYSTEM_INFO_VERSION)
        return info.drive_mask;
    return 0x0c; /* C: and D: */
}
static unsigned chooser_drive_mask(void)
{
    return choose_sd_only ? sd_drive_mask(choose_writing) : drive_mask();
}
static int path_on_drives(const char *path, unsigned mask)
{
    if (!path[0] || path[1] != ':') return 0;
    unsigned d = (unsigned)(toupper((unsigned char)path[0]) - 'A');
    return d < 26 && (mask & (1u << d));
}
static void first_drive(char *dir, unsigned mask)
{
    for (int d = 0; d < 26; d++)
        if (mask & (1u << d)) { snprintf(dir, 80, "%c:\\", 'A' + d); return; }
}
static void next_drive(char *dir)
{
    unsigned mask = chooser_drive_mask();
    int d = toupper((unsigned char)dir[0]) - 'A';
    for (int i = 1; i <= 26; i++) {
        int n = (d + i) % 26;
        if (mask & (1u << n)) {
            snprintf(dir, 80, "%c:\\", 'A' + n);
            return;
        }
    }
}
static void parent_dir(char *dir)
{
    char *slash = strrchr(dir, '\\');
    if (slash && slash > dir + 2)
        *slash = 0;
    else
        dir[3] = 0;
}
static void draw_chooser(const char *title, int pick_dir, int selected_entry, int first)
{
    char line[120];
    clear_screen();
    app_text(12, 47, title, BLACK);
    snprintf(line, sizeof(line), "Folder: %s", choose_dir);
    app_text(12, 68, line, BLUE);
    app_box(12, 76, 616, 2, BLACK);
    for (int row = 0; row < 14 && first + row < entry_count; row++) {
        const struct entry *e = &entries[first + row];
        int y = 80 + 20 * row, chosen = first + row == selected_entry;
        if (chosen)
            app_box(12, y, 616, 20, BLUE);
        if (e->dir)
            snprintf(line, sizeof(line), "%-14s <folder>", e->name);
        else
            snprintf(line, sizeof(line), "%-14s %10lu bytes", e->name, e->size);
        app_text(16, y + 15, line, chosen ? WHITE : (pick_dir && !e->dir ? DARK : BLACK));
        add_hot(12, y, 616, 20, C_ROW + row);
    }
    if (!entry_count)
        app_text(16, 100, "(empty or unreadable)", DARK);
    button(12, 390, 110, pick_dir ? "Open [Ret]" : "Choose [Ret]", C_ROW - 1, 1);
    button(128, 390, 110, "Up [Bksp]", C_ROW - 2, 1);
    button(244, 390, 110, "Drive [Tab]", C_ROW - 3, 1);
    button(360, 390, 150, "Use folder [S]", C_ROW - 4, pick_dir);
    button(516, 390, 112, "Cancel [Esc]", C_ROW - 5, 1);
    app_status(choose_sd_only ? (pick_dir ? "SD only: Tab changes volume, Enter opens, S uses folder, Esc cancels"
                                                       : "SD only: Tab changes volume, Enter opens/selects, Esc cancels")
                             : (pick_dir ? "Choose a folder: Enter opens, S uses the current folder" : "Choose a file: Enter opens or selects"));
}
static int real_choose(const char *title, int pick_dir, char *out, size_t cap)
{
    if (!choose_dir[0])
        snprintf(choose_dir, sizeof(choose_dir), "%c:\\", dc_storage_drive());
    int sel = 0, first = 0, redraw = 1, result = 0, last_click = -1, buttons = 0, old = 0, press = C_NONE;
    list_directory(choose_dir);
    for (;;) {
        if (redraw) {
            if (sel >= entry_count)
                sel = entry_count ? entry_count - 1 : 0;
            if (sel < first)
                first = sel;
            if (sel >= first + 14)
                first = sel - 13;
            draw_chooser(title, pick_dir, sel, first);
            redraw = 0;
        }
        int mx, my, activate = 0, cmd = C_NONE;
        with_mouse(1);
        int key = app_event(20, &mx, &my, &buttons);
        with_mouse(0);
        int scan = key & 0xff00, ch = key & 255;
        if (key) {
            if (ch == 27) break;
            if (scan == KEY_UP && sel > 0) { sel--; redraw = 1; }
            else if (scan == KEY_DOWN && sel + 1 < entry_count) { sel++; redraw = 1; }
            else if (scan == 0x4900) { sel = sel > 14 ? sel - 14 : 0; redraw = 1; }
            else if (scan == 0x5100) { sel = sel + 14 < entry_count ? sel + 14 : entry_count - 1; redraw = 1; }
            else if (scan == KEY_HOME) { sel = 0; redraw = 1; }
            else if (scan == KEY_END) { sel = entry_count ? entry_count - 1 : 0; redraw = 1; }
            else if (ch == 13) activate = 1;
            else if (ch == 8) cmd = C_ROW - 2;
            else if (ch == 9) cmd = C_ROW - 3;
            else if ((ch == 's' || ch == 'S') && pick_dir) cmd = C_ROW - 4;
        }
        if ((buttons & 1) && !(old & 1))
            press = hit_test(mx, my);
        if (!(buttons & 1) && (old & 1)) {
            int id = hit_test(mx, my);
            if (id == press && id != C_NONE) {
                if (id >= C_ROW && id < C_ROW + 14) {
                    int index = first + id - C_ROW;
                    if (index < entry_count) {
                        if (index == sel && last_click == index)
                            activate = 1;
                        sel = index;
                        last_click = index;
                        redraw = 1;
                    }
                } else if (id == C_ROW - 1) activate = 1;
                else cmd = id;
            }
            press = C_NONE;
        }
        old = buttons;
        if (cmd == C_ROW - 2) {
            parent_dir(choose_dir);
            list_directory(choose_dir);
            sel = first = 0; last_click = -1; redraw = 1;
        } else if (cmd == C_ROW - 3) {
            next_drive(choose_dir);
            list_directory(choose_dir);
            sel = first = 0; last_click = -1; redraw = 1;
        } else if (cmd == C_ROW - 4) {
            snprintf(out, cap, "%s", choose_dir);
            result = 1;
            break;
        } else if (cmd == C_ROW - 5) break;
        if (activate && sel < entry_count) {
            const struct entry *e = &entries[sel];
            if (e->dir) {
                if (!strcmp(e->name, ".."))
                    parent_dir(choose_dir);
                else {
                    char next[80];
                    join(next, sizeof(next), choose_dir, e->name);
                    snprintf(choose_dir, sizeof(choose_dir), "%s", next);
                }
                list_directory(choose_dir);
                sel = first = 0; last_click = -1; redraw = 1;
            } else if (!pick_dir) {
                join(out, cap, choose_dir, e->name);
                result = 1;
                break;
            }
        }
    }
    settle();
    need_draw = 1;
    return result;
}

static int sd_path_ok(const char *path, int writing)
{
    if (path_on_drives(path, sd_drive_mask(writing))) return 1;
    say(RED, writing ? "No writable SD volume at that path. No file was exported."
                    : "No mounted SD volume at that path. Nothing was imported.");
    return 0;
}
static int choose_transfer(int writing, int sd_only, char *out, size_t cap)
{
    const char *title = writing ? "Export: choose the destination folder" : "Import: choose the file to copy onto the VMU";
    if (!sd_only) return ui_choose(title, writing, out, cap);
    unsigned mask = sd_drive_mask(writing);
    if (!mask) {
        say(RED, writing ? "No writable SD volume. Insert a FAT16 SD card before booting."
                        : "No mounted SD volume. Insert a FAT16 SD card before booting.");
        return 0;
    }
    char previous[sizeof(choose_dir)];
    memcpy(previous, choose_dir, sizeof(previous));
    memcpy(choose_dir, sd_choose_dir, sizeof(choose_dir));
    if (!path_on_drives(choose_dir, mask)) first_drive(choose_dir, mask);
    choose_sd_only = 1;
    choose_writing = writing;
    int ok = ui_choose(writing ? "To SD: choose a folder for the selected VMU save"
                               : "From SD: choose a save to import onto the VMU", writing, out, cap);
    memcpy(sd_choose_dir, choose_dir, sizeof(sd_choose_dir));
    memcpy(choose_dir, previous, sizeof(choose_dir));
    choose_sd_only = choose_writing = 0;
    return ok && sd_path_ok(out, writing);
}

/* ---- host file helpers --------------------------------------------------------- */
static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}
static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}
static int read_file(const char *path, uint8_t *buffer, size_t max, size_t *got)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    *got = fread(buffer, 1, max, f);
    fclose(f);
    return 1;
}
static int write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(data, 1, n, f) == n;
    if (fclose(f))
        ok = 0;
    return ok;
}
static int verify_file(const char *path, const uint8_t *data, size_t n)
{
    uint8_t block[512];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = 1;
    for (size_t at = 0; at < n; at += sizeof(block)) {
        size_t count = n - at < sizeof(block) ? n - at : sizeof(block);
        if (fread(block, 1, count, f) != count || memcmp(block, data + at, count)) { ok = 0; break; }
    }
    if (fgetc(f) != EOF || ferror(f)) ok = 0;
    if (fclose(f)) ok = 0;
    return ok;
}
/* Refuses read-only and unmounted drives before any attempt. */
static int drive_writable(const char *path, char *why, size_t n)
{
    if (!isalpha((unsigned char)path[0]) || path[1] != ':') {
        snprintf(why, n, "Give a drive, e.g. C:\\NAME.EXT");
        return 0;
    }
    if (!dc_drive_state(path[0])) {
        snprintf(why, n, "Drive %c: is read-only or not mounted.", toupper((unsigned char)path[0]));
        return 0;
    }
    return 1;
}
static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}
static int has_extension(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    if (n < e)
        return 0;
    for (size_t i = 0; i < e; i++)
        if (toupper((unsigned char)path[n - e + i]) != ext[i])
            return 0;
    return 1;
}

/* ---- file operations ----------------------------------------------------------- */
static const char *tail_text(const char *s, size_t keep)
{
    static char out[48];
    size_t n = strlen(s);
    if (n <= keep)
        return s;
    snprintf(out, sizeof(out), "...%s", s + n - (keep - 3));
    return out;
}
static void after_card_change(const char *select)
{
    read_card();
    if (select)
        select_named(select);
    ensure_visible();
}
static void busy_note(const char *what)
{
    char s[100];
    snprintf(s, sizeof(s), "%s - keep the card connected...", what);
    app_status(s);
}
static void do_import(int sd_only)
{
    const struct dc_device_info *dev = current_card();
    char path[100], name[13], text[260], suggestion[13];
    if (!write_ok || !card_ready() || !dev)
        return;
    if (!choose_transfer(0, sd_only, path, sizeof(path)))
        return;
    long size = file_size(path);
    if (size < 0) { say(RED, "Cannot open %s.", path); return; }
    if (size == 0) { say(RED, "%s is empty; a VMU file needs at least one byte.", base_name(path)); return; }
    if (size > (long)MAX_FILE) {
        say(RED, "%s is %ld bytes: over the VMU limit of 100 KiB (200 blocks).", base_name(path), size);
        return;
    }
    vmu_name_for_dos(base_name(path), suggestion);
    snprintf(name, sizeof(name), "%s", suggestion);
    if (!ui_prompt("Name on the VMU (1-12 characters):", name, sizeof(name)))
        return;
    if (!vmu_name_ok(name)) { say(RED, "%s", error_text(DC_VMUF_BAD_NAME)); return; }
    size_t n = name[0] ? strlen(name) : 0;
    while (n && name[n - 1] == ' ')
        name[--n] = 0;
    unsigned blocks = ((unsigned)size + 511) / 512;
    int existing = find_on_card(name);
    if (existing >= 0) {
        const struct dc_vmu_file *old = &card.files[existing];
        if (old->type != 0x33 || old->protected_file || old->header_block) {
            say(RED, "%s", error_text(DC_VMUF_PROTECTED));
            return;
        }
        if (blocks > card.free_blocks + old->blocks) { say(RED, "%s", error_text(DC_VMUF_FULL)); return; }
        snprintf(text, sizeof(text), "Replace %s on %c%lu?|Old: %lu blocks. New: %ld bytes,|%u blocks. Old contents are lost.",
                 name, 'A' + (int)dev->port, (unsigned long)dev->unit, (unsigned long)old->blocks, size, blocks);
    } else {
        if (blocks > card.free_blocks) { say(RED, "%s (%u blocks needed, %lu free)", error_text(DC_VMUF_FULL), blocks, (unsigned long)card.free_blocks); return; }
        snprintf(text, sizeof(text), "Copy to %c%lu as %s?|From %s, %ld bytes|(%u blocks). Nothing else changes.",
                 'A' + (int)dev->port, (unsigned long)dev->unit, name, base_name(path), size, blocks);
    }
    if (!ui_confirm(text, existing >= 0 ? "Replace" : "Copy"))
        return;
    if (sd_only && !sd_path_ok(path, 0)) return;
    if (sd_only) app_status("Reading save - keep the SD card connected...");
    size_t got = 0;
    if (!read_file(path, file_buffer, MAX_FILE, &got) || got != (size_t)size) { say(RED, "Could not read %s.", path); return; }
    busy_note("Writing the VMU");
    long r = dc_os->vmu_file_write(dev->port, dev->unit, name, file_buffer, got, existing >= 0 ? DC_VMUF_OVERWRITE : 0);
    after_card_change(name);
    if (r >= 0)
        say(BLACK, "Copied %s to %c%lu as %s (%ld bytes); read back and verified.", base_name(path), 'A' + (int)dev->port,
            (unsigned long)dev->unit, name, r);
    else {
        say(RED, "%s", error_text(r));
        if (r == DC_VMUF_VERIFY)
            ui_alert(error_text(r));
    }
}
static void do_export(int sd_only)
{
    const struct dc_device_info *dev = current_card();
    const struct dc_vmu_file *f = selected_file();
    char folder[100], path[120], name[13], text[260], dos[13], why[80], vname[13];
    if (!read_ok || !f || !dev)
        return;
    snprintf(vname, sizeof(vname), "%s", f->name);
    if (f->blocks * 512 > MAX_FILE || f->protected_file) { say(RED, "%s", error_text(f->protected_file ? DC_VMUF_PROTECTED : DC_VMUF_TOO_BIG)); return; }
    if (!choose_transfer(1, sd_only, folder, sizeof(folder)))
        return;
    dos_name_for_vmu(vname, dos);
    snprintf(name, sizeof(name), "%s", dos);
    if (!ui_prompt("File name (8.3) to create:", name, sizeof(name)))
        return;
    if (!name[0] || strchr(name, '\\') || strchr(name, ':') || strchr(name, '*') || strchr(name, '?')) {
        say(RED, "Use a plain 8.3 file name.");
        return;
    }
    join(path, sizeof(path), folder, name);
    if (!drive_writable(path, why, sizeof(why))) { say(RED, "%s", why); return; }
    if (file_exists(path)) {
        snprintf(text, sizeof(text), "%s|already exists. Replace it with|the VMU file %s?", tail_text(path, 34), vname);
        if (!ui_confirm(text, "Replace"))
            return;
    } else {
        snprintf(text, sizeof(text), "Copy %s (%lu bytes) from|%c%lu to:|%s", vname, (unsigned long)(f->blocks * 512),
                 'A' + (int)dev->port, (unsigned long)dev->unit, tail_text(path, 34));
        if (!ui_confirm(text, "Copy"))
            return;
    }
    busy_note("Reading the VMU");
    long r = dc_os->vmu_file_read(dev->port, dev->unit, vname, file_buffer, MAX_FILE);
    if (r < 0) { say(RED, "%s", error_text(r)); return; }
    if (sd_only && !sd_path_ok(path, 1)) return;
    if (!drive_writable(path, why, sizeof(why))) { say(RED, "%s", why); return; }
    app_status("Writing backup - keep the destination drive connected...");
    if (!write_file(path, file_buffer, (size_t)r)) { say(RED, "Could not write %s (disk full or protected).", path); return; }
    app_status("Verifying backup - keep the destination drive connected...");
    if (!verify_file(path, file_buffer, (size_t)r)) { say(RED, "Export NOT verified: %s. Check the destination before using this backup.", path); return; }
    say(BLACK, "Saved %s (%ld raw bytes); read back and verified%s.", path, r, dc_drive_note(path[0]));
}
static void do_delete(void)
{
    const struct dc_device_info *dev = current_card();
    const struct dc_vmu_file *f = selected_file();
    char text[240], vname[13];
    if (!write_ok || !f || !dev)
        return;
    snprintf(vname, sizeof(vname), "%s", f->name);
    snprintf(text, sizeof(text), "Delete %s from %c%lu?|(%lu blocks) This cannot be undone.|Export a copy first if unsure.", vname,
             'A' + (int)dev->port, (unsigned long)dev->unit, (unsigned long)f->blocks);
    if (!ui_confirm(text, "Delete"))
        return;
    busy_note("Deleting");
    long r = dc_os->vmu_file_delete(dev->port, dev->unit, vname);
    after_card_change(NULL);
    if (r >= 0)
        say(BLACK, "Deleted %s; the card was re-read and checked.", vname);
    else {
        say(RED, "%s", error_text(r));
        if (r == DC_VMUF_VERIFY)
            ui_alert(error_text(r));
    }
}
static void do_rename(void)
{
    const struct dc_device_info *dev = current_card();
    const struct dc_vmu_file *f = selected_file();
    char name[13], old[13], text[260];
    if (!write_ok || !f || !dev)
        return;
    if (f->type != 0x33 || f->protected_file || f->header_block || f->blocks * 512 > MAX_FILE) {
        say(RED, "%s", error_text(DC_VMUF_PROTECTED));
        return;
    }
    snprintf(old, sizeof(old), "%s", f->name);
    unsigned blocks = f->blocks;
    snprintf(name, sizeof(name), "%s", old);
    if (!ui_prompt("New name (1-12 characters):", name, sizeof(name)))
        return;
    size_t n = strlen(name);
    while (n && name[n - 1] == ' ')
        name[--n] = 0;
    if (!vmu_name_ok(name)) { say(RED, "%s", error_text(DC_VMUF_BAD_NAME)); return; }
    if (!strcmp(name, old)) { say(BLACK, "The name is unchanged."); return; }
    if (find_on_card(name) >= 0) { say(RED, "%s is already on the card; rename never overwrites.", name); return; }
    if (blocks > card.free_blocks) {
        say(RED, "Rename copies the file first: it needs %u free blocks (%lu free).", blocks, (unsigned long)card.free_blocks);
        return;
    }
    snprintf(text, sizeof(text), "Rename %s to %s?|It is copied, verified, then|the old file is deleted.", old, name);
    if (!ui_confirm(text, "Rename"))
        return;
    busy_note("Reading the VMU");
    long r = dc_os->vmu_file_read(dev->port, dev->unit, old, file_buffer, MAX_FILE);
    if (r < 0) { say(RED, "%s", error_text(r)); return; }
    busy_note("Writing the VMU");
    long w = dc_os->vmu_file_write(dev->port, dev->unit, name, file_buffer, (uint32_t)r, 0);
    if (w < 0) {
        after_card_change(old);
        say(RED, "Rename stopped, the original is untouched: %s", error_text(w));
        if (w == DC_VMUF_VERIFY)
            ui_alert(error_text(w));
        return;
    }
    long d = dc_os->vmu_file_delete(dev->port, dev->unit, old);
    after_card_change(name);
    if (d >= 0)
        say(BLACK, "Renamed %s to %s.", old, name);
    else {
        say(RED, "Copied to %s but %s could not be deleted: %s", name, old, error_text(d));
        if (d == DC_VMUF_VERIFY)
            ui_alert(error_text(d));
    }
}

/* ---- raw hex/ASCII viewer and editor -------------------------------------------- */
static void hex_move(long offset)
{
    if (offset < 0) offset = 0;
    if ((uint32_t)offset >= hex_bytes) offset = hex_bytes - 1;
    hex_pos = (uint32_t)offset;
    if (hex_pos < hex_top) hex_top = hex_pos / HEX_COLS * HEX_COLS;
    if (hex_pos >= hex_top + HEX_PAGE)
        hex_top = (hex_pos / HEX_COLS - HEX_ROWS + 1) * HEX_COLS;
    hex_nibble = 0;
    need_draw = 1;
}
static void hex_open(void)
{
    const struct dc_device_info *dev = current_card();
    const struct dc_vmu_file *f = selected_file();
    if (!read_ok || !f || !dev) return;
    if (f->protected_file) { say(RED, "%s", error_text(DC_VMUF_PROTECTED)); return; }
    if (!f->blocks || f->blocks > MAX_FILE / 512) { say(RED, "%s", error_text(DC_VMUF_TOO_BIG)); return; }
    busy_note("Reading the VMU");
    long r = dc_os->vmu_file_read(dev->port, dev->unit, f->name, file_buffer, MAX_FILE);
    if (r < 0) { say(RED, "%s", error_text(r)); return; }
    if ((uint32_t)r != f->blocks * 512) { say(RED, "The file size changed. Refresh the directory and try again."); return; }
    hex_bytes = (uint32_t)r;
    memcpy(hex_original, file_buffer, hex_bytes);
    snprintf(hex_name, sizeof(hex_name), "%s", f->name);
    hex_port = dev->port;
    hex_unit = dev->unit;
    hex_writable = write_ok && f->type == 0x33 && !f->header_block;
    hex_pos = hex_top = hex_changes = 0;
    hex_edit = hex_ascii = hex_nibble = hex_have_undo = 0;
    view = VIEW_HEX;
    say(BLACK, hex_writable ? "View mode. Choose Edit [F2] to change bytes; Save [F5] asks first."
                           : "Read only: games, unusual headers or an OS without the write service.");
}
static void hex_put(uint32_t at, uint8_t value)
{
    if (file_buffer[at] != hex_original[at]) hex_changes--;
    file_buffer[at] = value;
    if (file_buffer[at] != hex_original[at]) hex_changes++;
    need_draw = 1;
}
static void hex_type(int ch)
{
    if (!hex_edit || !hex_writable) return;
    int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (hex_ascii ? (ch < 32 || ch > 126) : digit < 0) return;
    if (hex_ascii || !hex_nibble) {
        hex_undo_pos = hex_pos;
        hex_undo_value = file_buffer[hex_pos];
        hex_have_undo = 1;
    }
    if (hex_ascii) {
        hex_put(hex_pos, (uint8_t)ch);
        hex_move(hex_pos + 1);
    } else if (!hex_nibble) {
        hex_put(hex_pos, (uint8_t)((digit << 4) | (file_buffer[hex_pos] & 15)));
        hex_nibble = 1;
    } else {
        hex_put(hex_pos, (uint8_t)((file_buffer[hex_pos] & 0xf0) | digit));
        hex_move(hex_pos + 1);
    }
    message[0] = 0;
}
static void hex_undo(void)
{
    if (!hex_have_undo) return;
    uint8_t value = file_buffer[hex_undo_pos];
    hex_put(hex_undo_pos, hex_undo_value);
    hex_undo_value = value;
    hex_move(hex_undo_pos);
    message[0] = 0;
}
static void hex_goto(void)
{
    char input[16], *end;
    snprintf(input, sizeof(input), "%05lX", (unsigned long)hex_pos);
    if (!ui_prompt("Go to byte offset (hex, e.g. 0200):", input, sizeof(input))) return;
    /* At most five digits are needed for the largest VMU file. Avoid strtoul
     * overflow differences between the 32-bit target and the host tests. */
    const char *p = input;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    size_t digits = strlen(p);
    if (!digits || digits > 5) { say(RED, "Enter a hexadecimal offset within the file."); return; }
    for (size_t i = 0; i < digits; i++)
        if (!isxdigit((unsigned char)p[i])) { say(RED, "Enter a hexadecimal offset within the file."); return; }
    unsigned long pos = strtoul(p, &end, 16);
    if (*end || pos >= hex_bytes) { say(RED, "That offset is outside this file."); return; }
    hex_move((long)pos);
    message[0] = 0;
}
static void hex_save(void)
{
    if (!hex_writable || !hex_changes) return;
    char text[240], alert_name[25];
    /* GEM alert delimiters are legal VMU filename characters. Quote them so
     * the filename cannot end the warning or replace the Cancel button. */
    size_t n = 0;
    for (size_t i = 0; hex_name[i]; i++) {
        if (hex_name[i] == '|' || hex_name[i] == ']') alert_name[n++] = hex_name[i];
        alert_name[n++] = hex_name[i];
    }
    alert_name[n] = 0;
    snprintf(text, sizeof(text), "Overwrite ORIGINAL %s|on %c%lu? %lu edited bytes.|All %lu bytes will be rewritten.|Raw edit: checksums are NOT fixed.|Export a backup first if unsure.",
             alert_name, 'A' + (int)hex_port, (unsigned long)hex_unit, (unsigned long)hex_changes,
             (unsigned long)hex_bytes);
    if (!ui_confirm(text, "Overwrite")) return;
    /* The card may have been swapped while editing or inside the dialog.
     * Compare the complete source again after confirmation, before any write. */
    uint8_t *check = malloc(MAX_FILE);
    if (!check) { say(RED, "Not enough memory to verify the original. Edits are kept."); return; }
    busy_note("Checking the original");
    long r = dc_os->vmu_file_read(hex_port, hex_unit, hex_name, check, MAX_FILE);
    int same = r == (long)hex_bytes && !memcmp(check, hex_original, hex_bytes);
    free(check);
    if (r < 0) { say(RED, "%s", error_text(r)); return; }
    if (!same) {
        say(RED, "The original file changed or the card was replaced. No write made; edits kept.");
        return;
    }
    busy_note("Writing the VMU");
    r = dc_os->vmu_file_write(hex_port, hex_unit, hex_name, file_buffer, hex_bytes, DC_VMUF_OVERWRITE);
    after_card_change(hex_name);
    if (r >= 0) {
        memcpy(hex_original, file_buffer, hex_bytes);
        hex_changes = 0;
        hex_have_undo = hex_nibble = 0;
        say(BLACK, "%s overwritten; read back and verified. No automatic checksum repair.", hex_name);
    } else {
        say(RED, "%s", error_text(r));
        if (r == DC_VMUF_VERIFY) ui_alert(error_text(r));
    }
}
static void hex_leave(void)
{
    if (hex_changes && !ui_confirm("Discard unsaved hex/ASCII edits?|Your edits have not been saved.", "Discard")) return;
    view = VIEW_FILES;
    message[0] = 0;
    need_draw = 1;
}
static void draw_hex(void)
{
    char line[100];
    clear_screen();
    snprintf(line, sizeof(line), "Hex/ASCII - %c%lu / %s", 'A' + (int)hex_port, (unsigned long)hex_unit, hex_name);
    app_text(12, 47, line, BLACK);
    button(432, 30, 94, "< Page", C_H_PREV, hex_pos > 0);
    button(534, 30, 94, "Page >", C_H_NEXT, hex_pos + 1 < hex_bytes);
    snprintf(line, sizeof(line), "%lu bytes | %s | %lu changed | %s pane (Tab switches)", (unsigned long)hex_bytes,
             hex_edit ? "EDIT" : "VIEW", (unsigned long)hex_changes, hex_ascii ? "ASCII" : "HEX");
    app_text(12, 68, line, BLACK);
    app_box(12, 76, 616, 18, BLACK);
    app_text(16, 90, "OFFSET", WHITE);
    for (int col = 0; col < HEX_COLS; col++) {
        snprintf(line, sizeof(line), "%02X", col);
        app_text(HEX_X + col * 24, 90, line, WHITE);
    }
    app_text(ASCII_X, 90, "ASCII", WHITE);
    app_line(480, 76, 480, HEX_Y + HEX_ROWS * 16, BLACK);
    for (int row = 0; row < HEX_ROWS; row++) {
        uint32_t start = hex_top + row * HEX_COLS;
        int y = HEX_Y + row * 16;
        if (start >= hex_bytes) break;
        snprintf(line, sizeof(line), "%05lX", (unsigned long)start);
        app_text(16, y + 13, line, DARK);
        for (int col = 0; col < HEX_COLS && start + col < hex_bytes; col++) {
            uint32_t at = start + col;
            int hx = HEX_X + col * 24, ax = ASCII_X + col * 8;
            int chosen = at == hex_pos, changed = file_buffer[at] != hex_original[at];
            int ink = chosen ? WHITE : changed ? RED : BLACK;
            if (chosen) {
                app_box(hx - 1, y, 18, 16, BLUE);
                app_box(ax, y, 8, 16, BLUE);
            }
            snprintf(line, sizeof(line), "%02X", file_buffer[at]);
            app_text(hx, y + 13, line, ink);
            line[0] = file_buffer[at] >= 32 && file_buffer[at] <= 126 ? file_buffer[at] : '.';
            line[1] = 0;
            app_text(ax, y + 13, line, ink);
            if (chosen) {
                int x = hex_ascii ? ax : hx + hex_nibble * 8;
                app_line(x, y + 15, x + 7, y + 15, WHITE);
            }
        }
    }
    snprintf(line, sizeof(line), "Offset %05lX (%lu)  Byte %02X / %u  Original %02X | Changed bytes in red",
             (unsigned long)hex_pos, (unsigned long)hex_pos, file_buffer[hex_pos], file_buffer[hex_pos], hex_original[hex_pos]);
    app_text(12, 370, line, BLACK);
    draw_message(388);
    button(12, 422, 112, hex_edit ? "View [F2]" : "Edit [F2]", C_H_EDIT, hex_writable);
    button(132, 422, 112, "Go to [F3]", C_H_GOTO, 1);
    button(252, 422, 112, "Undo [F4]", C_H_UNDO, hex_have_undo);
    button(372, 422, 112, "Save [F5]", C_H_SAVE, hex_writable && hex_changes);
    button(492, 422, 136, "Files [Esc]", C_H_BACK, 1);
    status_bar("Raw bytes: checksums are NOT repaired. Arrows/PgUp/PgDn/Home/End move.");
}
static void hex_key(int key)
{
    int scan = key & 0xff00, ch = key & 255;
    if (scan == KEY_LEFT) hex_move((long)hex_pos - 1);
    else if (scan == KEY_RIGHT) hex_move(hex_pos + 1);
    else if (scan == KEY_UP) hex_move((long)hex_pos - HEX_COLS);
    else if (scan == KEY_DOWN) hex_move(hex_pos + HEX_COLS);
    else if (scan == 0x4900) hex_move((long)hex_pos - HEX_PAGE);
    else if (scan == 0x5100) hex_move(hex_pos + HEX_PAGE);
    else if (scan == KEY_HOME) hex_move(0);
    else if (scan == KEY_END) hex_move(hex_bytes - 1);
    else if (ch == 27) hex_leave();
    else if (ch == 9) { hex_ascii = !hex_ascii; hex_nibble = 0; need_draw = 1; }
    else if (scan == 0x3c00 && hex_writable) { hex_edit = !hex_edit; hex_nibble = 0; need_draw = 1; }
    else if (scan == 0x3d00) hex_goto();
    else if (scan == 0x3e00) hex_undo();
    else if (scan == 0x3f00) hex_save();
    else hex_type(ch);
}
static void hex_click(int mx, int my)
{
    if (my < HEX_Y || my >= HEX_Y + HEX_ROWS * 16) return;
    int col, ascii;
    if (mx >= ASCII_X && mx < ASCII_X + HEX_COLS * 8) {
        col = (mx - ASCII_X) / 8;
        ascii = 1;
    } else if (mx >= HEX_X && mx < HEX_X + HEX_COLS * 24 && (mx - HEX_X) % 24 < 16) {
        col = (mx - HEX_X) / 24;
        ascii = 0;
    } else return;
    uint32_t at = hex_top + (my - HEX_Y) / 16 * HEX_COLS + col;
    if (at >= hex_bytes) return;
    hex_move(at);
    hex_ascii = ascii;
}

/* ---- LCD editor ---------------------------------------------------------------- */
#define GX 12
#define GY 56
#define CELL 9
static void lcd_snapshot(void)
{
    memcpy(lcd_undo, lcd, LCD_BYTES);
    lcd_have_undo = 1;
}
static void lcd_changed(void)
{
    lcd_dirty = 1;
    lcd_pending = 1;
}
static void push_lcd(int report)
{
    const struct dc_device_info *dev = current_card();
    lcd_pending = 0;
    if (!lcd_ok || !dev) {
        if (report)
            say(RED, "%s", lcd_ok ? "No card selected." : "This OS build has no LCD service.");
        return;
    }
    long r = dc_os->vmu_screen(dev->port, dev->unit, lcd, LCD_BYTES);
    lcd_pushed = app_millis();
    if (r == DC_VMUF_BUSY)
        lcd_pending = 1; /* retry on the next tick */
    else if (r < 0)
        say(RED, "%s", error_text(r));
    else if (report)
        say(BLACK, "Sent to the LCD of %c%lu.", 'A' + (int)dev->port, (unsigned long)dev->unit);
}
static void draw_lcd_cell(int x, int y)
{
    int px = GX + x * CELL, py = GY + y * CELL;
    app_box(px, py, CELL, CELL, GREY);
    app_box(px + 1, py + 1, CELL - 1, CELL - 1, lcd_get(lcd, x, y) ? BLACK : WHITE);
    if (x == lcd_cx && y == lcd_cy) {
        frame(px, py, CELL + 1, CELL + 1, RED);
        frame(px + 1, py + 1, CELL - 1, CELL - 1, RED);
    }
}
static void draw_lcd_previews(void)
{
    struct scaled one = {1, 0}, two = {2, 0};
    frame(461, 59, 50, 34, BLACK);
    blit_pixels(48, 32, 462, 60, lcd_pixel_fn, &one);
    frame(519, 59, 98, 66, BLACK);
    blit_pixels(96, 64, 520, 60, lcd_pixel_fn, &two);
}
static void draw_lcd_info(void)
{
    char line[120];
    app_box(12, 348, 616, 68, WHITE);
    const struct dc_device_info *dev = current_card();
    if (dev)
        snprintf(line, sizeof(line), "Target %c%lu %.24s%s   Cursor %d,%d   Pen %s   Live %s", 'A' + (int)dev->port,
                 (unsigned long)dev->unit, dev->name, (dev->functions & 0x04000000) ? "" : " (no LCD?)", lcd_cx, lcd_cy,
                 pen == 1 ? "draw" : pen == 2 ? "erase" : "off", lcd_live ? "on" : "off");
    else
        snprintf(line, sizeof(line), "No card connected. Cursor %d,%d", lcd_cx, lcd_cy);
    app_text(12, 364, line, BLACK);
    draw_message(380);
}
static void draw_lcd(void)
{
    clear_screen();
    app_text(12, 47, lcd_dirty ? "LCD editor - left button draws, right erases   [not saved to a file]" : "LCD editor - 48 x 32 pixels; left button draws, right button erases", BLACK);
    app_box(GX - 1, GY - 1, 48 * CELL + 2, 32 * CELL + 2, BLACK);
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            draw_lcd_cell(x, y);
    draw_lcd_previews();
    button(456, 140, 84, "Invert", C_L_INVERT, 1);
    button(544, 140, 84, "Clear", C_L_CLEAR, 1);
    button(456, 168, 84, "Flip H", C_L_FLIPH, 1);
    button(544, 168, 84, "Flip V", C_L_FLIPV, 1);
    button(456, 196, 84, "Undo", C_L_UNDO, lcd_have_undo);
    button(544, 196, 84, lcd_live ? "Live: on" : "Live: off", C_L_LIVE, lcd_ok);
    button(456, 224, 84, "Load..", C_L_LOAD, 1);
    button(544, 224, 84, "Save..", C_L_SAVE, 1);
    button(456, 252, 84, "Send now", C_L_SEND, lcd_ok && card_count);
    button(544, 252, 84, "Back", C_L_BACK, 1);
    button(456, 288, 40, "<", C_L_LEFT, 1);
    button(500, 288, 40, ">", C_L_RIGHT, 1);
    button(544, 288, 40, "^", C_L_UP, 1);
    button(588, 288, 40, "v", C_L_DOWN, 1);
    app_text(456, 330, "Shift image (wraps)", DARK);
    draw_lcd_info();
    app_text(12, 420, "Arrows move, Space toggles, D dark, E light, P pen, I invert, C clear", DARK);
    app_text(12, 436, "H V flip, Z undo, L load, S save, U send, T live, [ ] - = shift, Esc back", DARK);
    status_bar("The VMU LCD image is not stored on the card. Live sends after each edit.");
}
static void lcd_paint_cell(int x, int y, int dark)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H || lcd_get(lcd, x, y) == dark)
        return;
    lcd_put(lcd, x, y, dark);
    lcd_changed();
    draw_lcd_cell(x, y);
}
static void lcd_paint_line(int x0, int y0, int x1, int y1, int dark)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        lcd_paint_cell(x0, y0, dark);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void lcd_refresh_views(void)
{
    draw_lcd_previews();
    draw_lcd_info();
}
static void lcd_move_cursor(int dx, int dy)
{
    int ox = lcd_cx, oy = lcd_cy;
    lcd_cx = (lcd_cx + dx + LCD_W) % LCD_W;
    lcd_cy = (lcd_cy + dy + LCD_H) % LCD_H;
    draw_lcd_cell(ox, oy);
    if (pen)
        lcd_paint_cell(lcd_cx, lcd_cy, pen == 1);
    draw_lcd_cell(lcd_cx, lcd_cy);
    lcd_refresh_views();
}
static void lcd_swap_undo(void)
{
    uint8_t tmp[LCD_BYTES];
    if (!lcd_have_undo)
        return;
    memcpy(tmp, lcd, LCD_BYTES);
    memcpy(lcd, lcd_undo, LCD_BYTES);
    memcpy(lcd_undo, tmp, LCD_BYTES);
    lcd_changed();
}
static void lcd_operation(int id)
{
    if (id == C_L_UNDO) {
        lcd_swap_undo();
    } else {
        lcd_snapshot();
        if (id == C_L_INVERT) lcd_invert(lcd);
        else if (id == C_L_CLEAR) lcd_clear(lcd);
        else if (id == C_L_FLIPH) lcd_flip_h(lcd);
        else if (id == C_L_FLIPV) lcd_flip_v(lcd);
        else if (id == C_L_LEFT) lcd_shift(lcd, -1, 0);
        else if (id == C_L_RIGHT) lcd_shift(lcd, 1, 0);
        else if (id == C_L_UP) lcd_shift(lcd, 0, -1);
        else if (id == C_L_DOWN) lcd_shift(lcd, 0, 1);
        lcd_changed();
    }
    need_draw = 1;
}
static void lcd_load(void)
{
    char path[100];
    if (!ui_choose("Load LCD image: 48x32 .BMP or 192-byte raw file", 0, path, sizeof(path)))
        return;
    long n = file_size(path);
    if (n < 0) { say(RED, "Cannot open %s.", path); return; }
    if (n > (long)MAX_FILE) { say(RED, "Image files over 100 KiB are not supported."); return; }
    size_t got = 0;
    if (!read_file(path, file_buffer, MAX_FILE, &got) || got != (size_t)n) { say(RED, "Could not read %s.", path); return; }
    uint8_t next[LCD_BYTES];
    int adjusted = 0;
    if (n == LCD_BYTES && !(file_buffer[0] == 'B' && file_buffer[1] == 'M'))
        memcpy(next, file_buffer, LCD_BYTES);
    else {
        int r = lcd_from_bmp(file_buffer, got, next, &adjusted);
        if (r != BMP_OK) { say(RED, "%s", bmp_error_text(r)); return; }
    }
    lcd_snapshot();
    memcpy(lcd, next, LCD_BYTES);
    lcd_dirty = 0;
    lcd_pending = 1;
    say(BLACK, "Loaded %s%s.", base_name(path), adjusted ? " (cropped or padded to 48x32)" : "");
}
static void lcd_save(void)
{
    char folder[100], name[13] = "VMULCD.BMP", path[120], text[200], why[80];
    if (!ui_choose("Save LCD image: choose the destination folder", 1, folder, sizeof(folder)))
        return;
    if (!ui_prompt("File name (.BMP 1-bit image, or .LCD raw):", name, sizeof(name)))
        return;
    int bmp = has_extension(name, ".BMP"), raw = has_extension(name, ".LCD");
    if ((!bmp && !raw) || strchr(name, '\\') || strchr(name, ':') || strchr(name, '*') || strchr(name, '?')) {
        say(RED, "Use a plain 8.3 name ending in .BMP or .LCD.");
        return;
    }
    join(path, sizeof(path), folder, name);
    if (!drive_writable(path, why, sizeof(why))) { say(RED, "%s", why); return; }
    if (file_exists(path)) {
        snprintf(text, sizeof(text), "%s|already exists. Replace it?", tail_text(path, 34));
        if (!ui_confirm(text, "Replace"))
            return;
    }
    uint8_t out[LCD_BMP_BYTES];
    size_t n;
    if (bmp)
        n = lcd_to_bmp(lcd, out);
    else {
        memcpy(out, lcd, LCD_BYTES);
        n = LCD_BYTES;
    }
    if (!write_file(path, out, n)) { say(RED, "Could not write %s.", path); return; }
    lcd_dirty = 0;
    say(BLACK, "Saved %s (%s)%s.", path, bmp ? "1-bit BMP" : "192 raw bytes", dc_drive_note(path[0]));
}
static void enter_lcd(void)
{
    view = VIEW_LCD;
    message[0] = 0;
    need_draw = 1;
}
static void leave_lcd(void)
{
    view = VIEW_FILES;
    message[0] = 0;
    need_draw = 1;
}
static void lcd_key(int key)
{
    int scan = key & 0xff00, ch = tolower(key & 255);
    if (scan == KEY_LEFT) lcd_move_cursor(-1, 0);
    else if (scan == KEY_RIGHT) lcd_move_cursor(1, 0);
    else if (scan == KEY_UP) lcd_move_cursor(0, -1);
    else if (scan == KEY_DOWN) lcd_move_cursor(0, 1);
    else if (ch == 27 || ch == 8) leave_lcd();
    else if (ch == ' ') {
        lcd_snapshot();
        lcd_put(lcd, lcd_cx, lcd_cy, !lcd_get(lcd, lcd_cx, lcd_cy));
        lcd_changed();
        draw_lcd_cell(lcd_cx, lcd_cy);
        lcd_refresh_views();
    } else if (ch == 'd' || ch == 'e') {
        lcd_snapshot();
        lcd_put(lcd, lcd_cx, lcd_cy, ch == 'd');
        lcd_changed();
        draw_lcd_cell(lcd_cx, lcd_cy);
        lcd_refresh_views();
    } else if (ch == 'p') {
        pen = (pen + 1) % 3;
        if (pen)
            lcd_snapshot();
        lcd_refresh_views();
    } else if (ch == 'i') lcd_operation(C_L_INVERT);
    else if (ch == 'c') lcd_operation(C_L_CLEAR);
    else if (ch == 'h') lcd_operation(C_L_FLIPH);
    else if (ch == 'v') lcd_operation(C_L_FLIPV);
    else if (ch == 'z') lcd_operation(C_L_UNDO);
    else if (ch == '[') lcd_operation(C_L_LEFT);
    else if (ch == ']') lcd_operation(C_L_RIGHT);
    else if (ch == '-') lcd_operation(C_L_UP);
    else if (ch == '=') lcd_operation(C_L_DOWN);
    else if (ch == 'l') lcd_load();
    else if (ch == 's') lcd_save();
    else if (ch == 'u') push_lcd(1);
    else if (ch == 't') {
        lcd_live = !lcd_live;
        if (lcd_live)
            lcd_pending = 1;
        need_draw = 1;
    }
}
static int lcd_cell_at(int mx, int my, int *x, int *y)
{
    if (mx < GX || my < GY || mx >= GX + LCD_W * CELL || my >= GY + LCD_H * CELL)
        return 0;
    *x = (mx - GX) / CELL;
    *y = (my - GY) / CELL;
    return 1;
}
static void lcd_canvas(int mx, int my, int b, int began)
{
    int x, y;
    if (!lcd_cell_at(mx, my, &x, &y)) {
        lcd_last_x = lcd_last_y = -1;
        return;
    }
    int dark = (b & 1) ? 1 : 0;
    if (began) {
        lcd_snapshot();
        lcd_last_x = lcd_last_y = -1;
    }
    int ox = lcd_cx, oy = lcd_cy;
    lcd_cx = x;
    lcd_cy = y;
    draw_lcd_cell(ox, oy);
    if (lcd_last_x < 0)
        lcd_paint_cell(x, y, dark);
    else
        lcd_paint_line(lcd_last_x, lcd_last_y, x, y, dark);
    draw_lcd_cell(x, y);
    lcd_last_x = x;
    lcd_last_y = y;
    lcd_refresh_views();
}

/* ---- icon editor --------------------------------------------------------------- */
static int icon_approximated;
static void apply_icon_palette(void)
{
    long use[16] = {0};
    int rgb[16][3], order[16], slot_rgb[16][3], used = 2;
    for (unsigned f = 0; f < vms.icons; f++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
                use[vms_pixel(file_buffer, f, x, y)]++;
    for (int i = 0; i < 16; i++) {
        icon_transparent[i] = argb4444_rgb(vms_palette(file_buffer, i), &rgb[i][0], &rgb[i][1], &rgb[i][2]);
        if (icon_transparent[i])
            rgb[i][0] = rgb[i][1] = rgb[i][2] = 255;
        order[i] = i;
    }
    for (int a = 1; a < 16; a++) /* the selected colour first, then most used */
        for (int b = a; b > 0; b--) {
            int p = order[b - 1], q = order[b];
            long up = use[p] + (p == icon_colour ? 1000000 : 0), uq = use[q] + (q == icon_colour ? 1000000 : 0);
            if (uq > up) { order[b - 1] = q; order[b] = p; } else break;
        }
    slot_rgb[0][0] = slot_rgb[0][1] = slot_rgb[0][2] = 255;
    slot_rgb[1][0] = slot_rgb[1][1] = slot_rgb[1][2] = 0;
    icon_approximated = 0;
    for (int k = 0; k < 16; k++) {
        int i = order[k], found = -1;
        for (int s = 0; s < used && found < 0; s++)
            if (slot_rgb[s][0] == rgb[i][0] && slot_rgb[s][1] == rgb[i][1] && slot_rgb[s][2] == rgb[i][2])
                found = s;
        if (found < 0 && used < 16) {
            found = used++;
            memcpy(slot_rgb[found], rgb[i], sizeof(rgb[i]));
        }
        if (found < 0) {
            long best = -1;
            for (int s = 0; s < 16; s++) {
                long d = 0;
                for (int c = 0; c < 3; c++)
                    d += (long)(slot_rgb[s][c] - rgb[i][c]) * (slot_rgb[s][c] - rgb[i][c]);
                if (best < 0 || d < best) { best = d; found = s; }
            }
            if (use[i])
                icon_approximated = 1;
        }
        icon_slot[i] = found;
    }
    for (int s = 2; s < 16; s++) {
        if (s < used)
            app_palette(s, slot_rgb[s][0] * 1000 / 255, slot_rgb[s][1] * 1000 / 255, slot_rgb[s][2] * 1000 / 255);
        else
            app_palette(s, 500, 500, 500);
    }
    palette_swapped = 1;
}
static void icon_snapshot(void)
{
    memcpy(icon_undo, file_buffer, VMS_HEADER + vms.icons * VMS_ICON_BYTES);
    icon_have_undo = 1;
}
static void draw_icon_cell(int x, int y)
{
    int px = GX + x * CELL, py = GY + y * CELL, index = vms_pixel(file_buffer, icon_frame, x, y);
    app_box(px, py, CELL, CELL, WHITE);
    app_box(px, py, CELL - 1, CELL - 1, icon_slot[index]);
    if (icon_transparent[index])
        app_box(px + 3, py + 3, 2, 2, BLACK);
    if (x == icon_cx && y == icon_cy) {
        frame(px, py, CELL, CELL, BLACK);
        frame(px + 1, py + 1, CELL - 2, CELL - 2, WHITE);
    }
}
static void draw_icon_previews(void)
{
    struct scaled one = {1, 1}, two = {2, 1};
    frame(451, 59, 34, 34, BLACK);
    blit_pixels(32, 32, 452, 60, icon_pixel_fn, &one);
    frame(495, 59, 66, 66, BLACK);
    blit_pixels(64, 64, 496, 60, icon_pixel_fn, &two);
}
static void draw_swatch(int i)
{
    int x = 316 + (i % 4) * 34, y = 56 + (i / 4) * 34;
    app_box(x, y, 32, 32, BLACK);
    app_box(x + 1, y + 1, 30, 30, WHITE);
    app_box(x + 2, y + 2, 28, 28, icon_slot[i]);
    if (icon_transparent[i]) {
        app_line(x + 4, y + 4, x + 27, y + 27, BLACK);
        app_line(x + 27, y + 4, x + 4, y + 27, BLACK);
    }
    add_hot(x, y, 32, 32, C_PAL + i);
    if (i == icon_colour) {
        frame(x - 1, y - 1, 34, 34, BLACK);
        frame(x + 1, y + 1, 30, 30, WHITE);
    }
}
static void draw_icon_info(void)
{
    char line[120];
    unsigned v = vms_palette(file_buffer, icon_colour);
    app_box(316, 194, 316, 66, WHITE);
    snprintf(line, sizeof(line), "Colour %d: A %X R %X G %X B %X", icon_colour, (v >> 12) & 15, (v >> 8) & 15, (v >> 4) & 15, v & 15);
    app_text(316, 208, line, BLACK);
    snprintf(line, sizeof(line), "Frame %d of %u   cursor %d,%d%s", icon_frame + 1, vms.icons, icon_cx, icon_cy,
             icon_dirty ? "   EDITED" : "");
    app_text(316, 228, line, BLACK);
    snprintf(line, sizeof(line), "%.24s", vms.short_text[0] ? vms.short_text : "(no title)");
    app_text(316, 248, line, BLACK);
}
static void draw_icon(void)
{
    char line[120];
    clear_screen();
    snprintf(line, sizeof(line), "Icon editor - %s", icon_name);
    app_text(12, 47, line, BLACK);
    app_box(GX - 1, GY - 1, 32 * CELL + 2, 32 * CELL + 2, BLACK);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            draw_icon_cell(x, y);
    for (int i = 0; i < 16; i++)
        draw_swatch(i);
    draw_icon_previews();
    draw_icon_info();
    button(316, 264, 92, "< Frame", C_I_PREVF, 1);
    button(412, 264, 92, "Frame >", C_I_NEXTF, 1);
    button(508, 264, 120, "Undo", C_I_UNDO, 1);
    button(316, 292, 92, "Flip H", C_I_FLIPH, 1);
    button(412, 292, 92, "Flip V", C_I_FLIPV, 1);
    button(508, 292, 120, "Fill frame", C_I_FILL, 1);
    button(316, 320, 148, "Save to VMU", C_I_SAVE, 1);
    button(468, 320, 160, "Back", C_I_BACK, 1);
    if (icon_approximated)
        app_text(316, 362, "Only 14 colours can show at once; rare ones look approximate.", BLACK);
    draw_message(378);
    app_text(12, 414, "Left paints, right picks a colour. Arrows move, Space paints, X picks.", BLACK);
    app_text(12, 430, ", . choose colour   R G B A raise the channel, r g b a lower it", BLACK);
    app_text(12, 446, "[ ] frame, H V flip, F fill, Z undo, S save to the VMU, Esc back", BLACK);
    status_bar("Only palette and pixels change; text and data are kept; the CRC is fixed.");
}
static void icon_open(void)
{
    const struct dc_device_info *dev = current_card();
    const struct dc_vmu_file *f = selected_file();
    if (!write_ok || !f || !dev)
        return;
    if (f->type != 0x33 || f->protected_file || f->header_block) { say(RED, "%s", error_text(DC_VMUF_PROTECTED)); return; }
    if (f->blocks * 512 > MAX_FILE) { say(RED, "%s", error_text(DC_VMUF_TOO_BIG)); return; }
    char name[13];
    snprintf(name, sizeof(name), "%s", f->name);
    busy_note("Reading the VMU");
    long r = dc_os->vmu_file_read(dev->port, dev->unit, name, file_buffer, MAX_FILE);
    if (r < 0) { say(RED, "%s", error_text(r)); return; }
    int e = vms_parse(file_buffer, (size_t)r, &vms);
    if (e != VMS_OK) { say(RED, "%s: %s", name, vms_error_text(e)); return; }
    snprintf(icon_name, sizeof(icon_name), "%s", name);
    icon_bytes = (uint32_t)r;
    icon_frame = icon_cx = icon_cy = 0;
    icon_colour = vms_pixel(file_buffer, 0, 0, 0);
    icon_dirty = icon_have_undo = 0;
    view = VIEW_ICON;
    message[0] = 0;
    apply_icon_palette();
    need_draw = 1;
}
static void icon_leave(void)
{
    if (icon_dirty && !ui_confirm("Discard the icon changes?|Nothing was written to the card.", "Discard"))
        return;
    restore_palette();
    view = VIEW_FILES;
    message[0] = 0;
    need_draw = 1;
}
static void icon_paint_cell(int x, int y, int colour)
{
    if (x < 0 || y < 0 || x >= 32 || y >= 32 || vms_pixel(file_buffer, icon_frame, x, y) == colour)
        return;
    vms_set_pixel(file_buffer, icon_frame, x, y, colour);
    icon_dirty = 1;
    draw_icon_cell(x, y);
}
static void icon_paint_line(int x0, int y0, int x1, int y1, int colour)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        icon_paint_cell(x0, y0, colour);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void icon_select_colour(int c)
{
    icon_colour = (c + 16) % 16;
    apply_icon_palette();
    need_draw = 1;
}
static void icon_adjust(int shift, int delta)
{
    unsigned v = vms_palette(file_buffer, icon_colour), n = (v >> shift) & 15;
    int next = (int)n + delta;
    if (next < 0 || next > 15)
        return;
    icon_snapshot();
    v = (v & ~(15u << shift)) | ((unsigned)next << shift);
    vms_set_palette(file_buffer, icon_colour, v);
    icon_dirty = 1;
    apply_icon_palette();
    need_draw = 1;
}
static void icon_frame_op(int id)
{
    if (id == C_I_UNDO) {
        if (!icon_have_undo)
            return;
        uint8_t tmp[sizeof(icon_undo)];
        size_t n = VMS_HEADER + vms.icons * VMS_ICON_BYTES;
        memcpy(tmp, file_buffer, n);
        memcpy(file_buffer, icon_undo, n);
        memcpy(icon_undo, tmp, n);
    } else {
        icon_snapshot();
        if (id == C_I_FLIPH) vms_flip_h(file_buffer, icon_frame);
        else if (id == C_I_FLIPV) vms_flip_v(file_buffer, icon_frame);
        else if (id == C_I_FILL) vms_fill(file_buffer, icon_frame, icon_colour);
    }
    icon_dirty = 1;
    apply_icon_palette();
    need_draw = 1;
}
static void icon_change_frame(int delta)
{
    icon_frame = (icon_frame + delta + (int)vms.icons) % (int)vms.icons;
    apply_icon_palette();
    need_draw = 1;
}
static void icon_save(void)
{
    const struct dc_device_info *dev = current_card();
    char text[240];
    if (!dev)
        return;
    if (!icon_dirty) { say(BLACK, "There are no icon changes to save."); return; }
    snprintf(text, sizeof(text), "Write the edited icon into %s|on %c%lu? The file (%lu blocks)|is rewritten and verified.",
             icon_name, 'A' + (int)dev->port, (unsigned long)dev->unit, (unsigned long)(icon_bytes / 512));
    if (!ui_confirm(text, "Write"))
        return;
    vms_seal(file_buffer, &vms);
    busy_note("Writing the VMU");
    long r = dc_os->vmu_file_write(dev->port, dev->unit, icon_name, file_buffer, icon_bytes, DC_VMUF_OVERWRITE);
    if (r >= 0) {
        icon_dirty = 0;
        after_card_change(icon_name);
        say(BLACK, "Icon saved to %s; read back and verified.", icon_name);
    } else {
        after_card_change(icon_name);
        say(RED, "%s", error_text(r));
        if (r == DC_VMUF_VERIFY)
            ui_alert(error_text(r));
    }
}
static void icon_key(int key)
{
    int scan = key & 0xff00, ch = key & 255, lower = tolower(ch);
    int ox = icon_cx, oy = icon_cy;
    if (scan == KEY_LEFT || scan == KEY_RIGHT || scan == KEY_UP || scan == KEY_DOWN) {
        icon_cx = (icon_cx + (scan == KEY_LEFT ? -1 : scan == KEY_RIGHT ? 1 : 0) + 32) % 32;
        icon_cy = (icon_cy + (scan == KEY_UP ? -1 : scan == KEY_DOWN ? 1 : 0) + 32) % 32;
        draw_icon_cell(ox, oy);
        draw_icon_cell(icon_cx, icon_cy);
        draw_icon_info();
    } else if (ch == 27 || ch == 8) icon_leave();
    else if (ch == ' ') {
        icon_snapshot();
        icon_paint_cell(icon_cx, icon_cy, icon_colour);
        draw_icon_previews();
        draw_icon_info();
    } else if (lower == 'x') {
        icon_colour = vms_pixel(file_buffer, icon_frame, icon_cx, icon_cy);
        apply_icon_palette();
        need_draw = 1;
    } else if (ch == ',') icon_select_colour(icon_colour - 1);
    else if (ch == '.') icon_select_colour(icon_colour + 1);
    else if (ch == 'R') icon_adjust(8, 1);
    else if (ch == 'r') icon_adjust(8, -1);
    else if (ch == 'G') icon_adjust(4, 1);
    else if (ch == 'g') icon_adjust(4, -1);
    else if (ch == 'B') icon_adjust(0, 1);
    else if (ch == 'b') icon_adjust(0, -1);
    else if (ch == 'A') icon_adjust(12, 1);
    else if (ch == 'a') icon_adjust(12, -1);
    else if (ch == '[') icon_change_frame(-1);
    else if (ch == ']') icon_change_frame(1);
    else if (lower == 'h') icon_frame_op(C_I_FLIPH);
    else if (lower == 'v') icon_frame_op(C_I_FLIPV);
    else if (lower == 'f') icon_frame_op(C_I_FILL);
    else if (lower == 'z') icon_frame_op(C_I_UNDO);
    else if (lower == 's') icon_save();
}
static int icon_cell_at(int mx, int my, int *x, int *y)
{
    if (mx < GX || my < GY || mx >= GX + 32 * CELL || my >= GY + 32 * CELL)
        return 0;
    *x = (mx - GX) / CELL;
    *y = (my - GY) / CELL;
    return 1;
}
static int icon_last_x = -1, icon_last_y = -1;
static void icon_canvas(int mx, int my, int b, int began)
{
    int x, y;
    if (!icon_cell_at(mx, my, &x, &y)) {
        icon_last_x = icon_last_y = -1;
        return;
    }
    int ox = icon_cx, oy = icon_cy;
    if (b & 2) { /* the right button picks the colour under the pointer */
        icon_cx = x;
        icon_cy = y;
        icon_colour = vms_pixel(file_buffer, icon_frame, x, y);
        if (began) {
            apply_icon_palette();
            need_draw = 1;
        }
        return;
    }
    if (began) {
        icon_snapshot();
        icon_last_x = icon_last_y = -1;
    }
    icon_cx = x;
    icon_cy = y;
    draw_icon_cell(ox, oy);
    if (icon_last_x < 0)
        icon_paint_cell(x, y, icon_colour);
    else
        icon_paint_line(icon_last_x, icon_last_y, x, y, icon_colour);
    draw_icon_cell(x, y);
    icon_last_x = x;
    icon_last_y = y;
    draw_icon_previews();
    draw_icon_info();
}

/* ---- commands and events ------------------------------------------------------- */
static void quit_app(void)
{
    if (lcd_dirty && !ui_confirm("The LCD bitmap was not saved|to a file. Quit and lose it?", "Quit"))
        return;
    quitting = 1;
}
static void command(int id)
{
    if (id >= C_ROW && id < C_ROW + LIST_ROWS && view == VIEW_FILES) {
        if (card_ready() && top + id - C_ROW < (int)card.file_count) {
            selected = top + id - C_ROW;
            need_draw = 1;
        }
        return;
    }
    if (id >= C_PAL && id < C_PAL + 16) {
        icon_select_colour(id - C_PAL);
        return;
    }
    switch (id) {
    case C_PREV: select_card(-1); need_draw = 1; break;
    case C_NEXT: select_card(1); need_draw = 1; break;
    case C_REFRESH: scan_cards(); read_card(); message[0] = 0; break;
    case C_IMPORT: do_import(0); break;
    case C_EXPORT: do_export(0); break;
    case C_SD_IMPORT: do_import(1); break;
    case C_SD_EXPORT: do_export(1); break;
    case C_RENAME: do_rename(); break;
    case C_DELETE: do_delete(); break;
    case C_LCD: enter_lcd(); break;
    case C_ICON: icon_open(); break;
    case C_HEX: hex_open(); break;
    case C_H_EDIT: hex_key(0x3c00); break;
    case C_H_GOTO: hex_goto(); break;
    case C_H_UNDO: hex_undo(); break;
    case C_H_SAVE: hex_save(); break;
    case C_H_BACK: hex_leave(); break;
    case C_H_PREV: hex_move((long)hex_pos - HEX_PAGE); break;
    case C_H_NEXT: hex_move(hex_pos + HEX_PAGE); break;
    case C_QUIT: quit_app(); break;
    case C_L_INVERT: case C_L_CLEAR: case C_L_FLIPH: case C_L_FLIPV: case C_L_UNDO:
    case C_L_LEFT: case C_L_RIGHT: case C_L_UP: case C_L_DOWN: lcd_operation(id); break;
    case C_L_LIVE:
        lcd_live = !lcd_live;
        if (lcd_live)
            lcd_pending = 1;
        need_draw = 1;
        break;
    case C_L_LOAD: lcd_load(); break;
    case C_L_SAVE: lcd_save(); break;
    case C_L_SEND: push_lcd(1); break;
    case C_L_BACK: leave_lcd(); break;
    case C_I_PREVF: icon_change_frame(-1); break;
    case C_I_NEXTF: icon_change_frame(1); break;
    case C_I_FLIPH: case C_I_FLIPV: case C_I_FILL: case C_I_UNDO: icon_frame_op(id); break;
    case C_I_SAVE: icon_save(); break;
    case C_I_BACK: icon_leave(); break;
    default: break;
    }
}
static void files_key(int key)
{
    int scan = key & 0xff00, ch = tolower(key & 255);
    if (scan == KEY_LEFT) { select_card(-1); need_draw = 1; }
    else if (scan == KEY_RIGHT) { select_card(1); need_draw = 1; }
    else if (scan == KEY_UP) need_draw |= move_selection(-1);
    else if (scan == KEY_DOWN) need_draw |= move_selection(1);
    else if (scan == 0x4900) need_draw |= move_selection(-LIST_ROWS);
    else if (scan == 0x5100) need_draw |= move_selection(LIST_ROWS);
    else if (scan == KEY_HOME) need_draw |= move_selection(-100000);
    else if (scan == KEY_END) need_draw |= move_selection(100000);
    else if (ch == 27) quit_app();
    else if (ch == 'r') command(C_REFRESH);
    else if (ch == 'i') command(C_IMPORT);
    else if (ch == 'e') command(C_EXPORT);
    else if (ch == 'n') command(C_RENAME);
    else if (ch == 'd' || key == KEY_DELETE) command(C_DELETE);
    else if (ch == 'l') command(C_LCD);
    else if (ch == 'c') command(C_ICON);
    else if (ch == 'h' || ch == 13) command(C_HEX);
    else if (scan == 0x4000) command(C_SD_IMPORT);
    else if (scan == 0x4100) command(C_SD_EXPORT);
}
static unsigned long last_scan;
static void watch_devices(void)
{
    /* Enumeration only: a card is never read in the background. */
    unsigned long now = app_millis();
    if (view != VIEW_FILES || now - last_scan < 1000)
        return;
    last_scan = now;
    struct dc_device_info before[DC_SYSTEM_INFO_DEVICES];
    int count = card_count, pos = card_pos;
    unsigned old_sd_read = sd_read_drives, old_sd_write = sd_write_drives;
    memcpy(before, cards, sizeof(before));
    scan_cards();
    if (sd_read_drives != old_sd_read || sd_write_drives != old_sd_write) need_draw = 1;
    int changed = count != card_count;
    for (int i = 0; !changed && i < count; i++)
        changed = before[i].port != cards[i].port || before[i].unit != cards[i].unit;
    if (changed || (count && pos != card_pos)) {
        memset(&card, 0, sizeof(card));
        card.status = card_count ? DC_VMU_NOT_READ : DC_VMU_ABSENT;
        selected = top = 0;
        say(BLACK, card_count ? "A card was connected or removed: press R to read it." : "No card connected.");
    }
}
static void draw_all(void)
{
    if (view == VIEW_LCD)
        draw_lcd();
    else if (view == VIEW_ICON)
        draw_icon();
    else if (view == VIEW_HEX)
        draw_hex();
    else
        draw_files();
}
static void step(int key, int mx, int my, int b)
{
    int old = old_buttons, clicked = C_NONE;
    old_buttons = b;
    if (key) {
        if (view == VIEW_LCD) lcd_key(key);
        else if (view == VIEW_ICON) icon_key(key);
        else if (view == VIEW_HEX) hex_key(key);
        else files_key(key);
        if (quitting)
            return;
    }
    if ((b & 3) && !(old & 3)) {
        press_hit = (b & 1) ? hit_test(mx, my) : C_NONE;
        if (press_hit == C_NONE) {
            int x, y;
            if (view == VIEW_LCD && lcd_cell_at(mx, my, &x, &y)) { lcd_stroke = 1; lcd_canvas(mx, my, b, 1); }
            else if (view == VIEW_ICON && icon_cell_at(mx, my, &x, &y)) { icon_stroke = 1; icon_canvas(mx, my, b, 1); }
            else if (view == VIEW_HEX && (b & 1)) hex_click(mx, my);
        }
    } else if (b & 3) {
        if (lcd_stroke > 0 && view == VIEW_LCD) lcd_canvas(mx, my, b, 0);
        if (icon_stroke > 0 && view == VIEW_ICON) icon_canvas(mx, my, b, 0);
    }
    if (!(b & 3) && (old & 3)) {
        if (press_hit != C_NONE && hit_test(mx, my) == press_hit)
            clicked = press_hit;
        press_hit = C_NONE;
        lcd_stroke = icon_stroke = -1;
        lcd_last_x = lcd_last_y = icon_last_x = icon_last_y = -1;
        if (view == VIEW_LCD && lcd_live && lcd_pending)
            push_lcd(0);
    }
    if (clicked != C_NONE)
        command(clicked);
    if (view == VIEW_LCD && lcd_live && lcd_pending && app_millis() - lcd_pushed >= 80)
        push_lcd(0);
    watch_devices();
}
static void save_palette(void)
{
    for (int i = 0; i < 16; i++) {
        vi[0] = i;
        vi[1] = 0;
        vdi_call(26, 0, 2);
        memcpy(saved_palette[i], vo + 1, 6);
    }
}
#ifndef VMUEDIT_TEST
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!app_begin("VMU Toolbox - Files, Hex/ASCII, LCD and Icons"))
        return 1;
    atexit(app_end);
    save_palette();
    scan_cards();
    read_card();
    if (!card_count)
        say(BLACK, "Connect a VMU. Only the LCD editor works without a card.");
    while (!quitting) {
        if (need_draw) {
            need_draw = 0;
            draw_all();
        }
        int mx = 0, my = 0, b = 0;
        with_mouse(1);
        int key = app_event(20, &mx, &my, &b);
        with_mouse(0);
        step(key, mx, my, b);
    }
    restore_palette();
    return 0;
}
#endif
