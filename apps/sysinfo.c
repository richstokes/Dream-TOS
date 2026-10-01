/* Native GEM system information utility. GPL-2.0-or-later.
 * No guessed hardware capacities: all numbers come from OS queries. This
 * standalone app deliberately needs no C runtime or shared application library.
 */
#include <stddef.h>
#include "dc_version.h"
#include "dreamcast/native.h"
#include "dreamcast/system_info.h"

#define MAX_LINES 128
#define LINE_BYTES 80
static const struct dc_native_api *api;
static struct dc_system_info info;
static char lines[MAX_LINES][LINE_BYTES];
static unsigned line_count, column;
static const char *status;
static int snapshot_valid, screen_width, screen_height, colours, planes;
static int16_t ac[5], ag[16], ai[16], ao[16];
static intptr_t aa[4], az[4];
static struct {
    int16_t *c, *g, *i, *o;
    intptr_t *a, *z;
} ap = {ac, ag, ai, ao, aa, az};
static int16_t vc[12], vi[128], vo[128], vp[128], vq[128];
static struct {
    int16_t *c, *i, *p, *o, *q;
} vpb = {vc, vi, vp, vo, vq};

static void aes(int op, int ni, int no, int na)
{
    ac[0] = op;
    ac[1] = ni;
    ac[2] = no;
    ac[3] = na;
    ac[4] = 0;
    api->aes(&ap);
}
static void vdi(int op, int np, int ni)
{
    vc[0] = op;
    vc[1] = np;
    vc[3] = ni;
    vc[5] = 0;
    api->vdi(&vpb);
}
static void attribute(int op, int value)
{
    vi[0] = value;
    vdi(op, 0, 1);
}
static void text(int x, int y, const char *s)
{
    int n = 0;
    while (*s && n < 79)
        vi[n++] = (unsigned char)*s++;
    vp[0] = x;
    vp[1] = y;
    vdi(8, 1, n);
}
static void bar(int x, int y, int w, int h, int colour)
{
    attribute(25, colour);
    vp[0] = x;
    vp[1] = y;
    vp[2] = x + w - 1;
    vp[3] = y + h - 1;
    vdi(114, 2, 0);
}
static void update_lock(int value)
{
    ai[0] = value;
    aes(107, 1, 1, 0);
}
static void add(const char *s)
{
    if (!line_count)
        return;
    while (*s && column < LINE_BYTES - 1)
        lines[line_count - 1][column++] = *s++;
    lines[line_count - 1][column] = 0;
}
static void line(const char *s)
{
    if (line_count == MAX_LINES - 1) /* Reserve the last line for the page footer. */
        return;
    line_count++;
    column = 0;
    add(s);
}
static void number(uint32_t n)
{
    char buffer[11];
    unsigned pos = sizeof(buffer) - 1;
    buffer[pos] = 0;
    do {
        buffer[--pos] = '0' + n % 10;
        n /= 10;
    } while (n);
    add(buffer + pos);
}
static void hex(uint32_t n)
{
    char buffer[11] = "0x00000000";
    for (int i = 9; i >= 2; i--, n >>= 4)
        buffer[i] = "0123456789ABCDEF"[n & 15];
    add(buffer);
}
static void kib(uint32_t bytes)
{
    number(bytes >> 10);
    add(" KiB");
}
static const char *system_name(uint32_t type)
{
    switch (type) {
    case 0:
        return "Dreamcast (retail)";
    case 9:
        return "Dreamcast SET5 development system";
    case 10:
        return "NAOMI";
    default:
        return "Unknown system";
    }
}
static const char *pixel_name(uint32_t mode)
{
    switch (mode) {
    case 0:
        return "RGB555";
    case 1:
        return "RGB565";
    case 2:
        return "RGB888 packed";
    case 3:
        return "RGB0888";
    default:
        return "unknown pixel format";
    }
}
static const char *cable_name(int32_t cable)
{
    switch (cable) {
    case 0:
        return "VGA";
    case 1:
        return "No cable detected";
    case 2:
        return "RGB / SCART";
    case 3:
        return "Composite / RF";
    default:
        return "Unknown";
    }
}
static void functions(uint32_t mask)
{
    /* Protocol bit names, not assumptions about connected hardware. */
    if (mask & 0x01000000)
        add("Controller ");
    if (mask & 0x02000000)
        add("Storage ");
    if (mask & 0x04000000)
        add("LCD ");
    if (mask & 0x08000000)
        add("Clock ");
    if (mask & 0x10000000)
        add("Microphone ");
    if (mask & 0x40000000)
        add("Keyboard ");
    if (mask & 0x80000000)
        add("Lightgun ");
    if (mask & 0x00010000)
        add("Rumble ");
    if (mask & 0x00020000)
        add("Mouse ");
    if (mask & 0x00080000)
        add("Camera ");
}
static void report(void)
{
    line_count = 0;
    snapshot_valid = 0;
    line(DC_PROJECT_NAME " system information");
    if (api->size < offsetof(struct dc_native_api, system_info) + sizeof(api->system_info) ||
        !api->system_info) {
        line("This OS does not provide the system-information query.");
        line("Rebuild the OS and SYSINFO.PRG together.");
        return;
    }
    long result = api->system_info(&info, sizeof(info));
    if (result != sizeof(info) || info.version != DC_SYSTEM_INFO_VERSION ||
        info.bytes != sizeof(info) || info.device_count > DC_SYSTEM_INFO_DEVICES) {
        line("System-information query failed or has an unsupported format.");
        return;
    }
    snapshot_valid = 1;
    line(DC_PROJECT_NAME ": ");
    add(info.os_version);
    add("   KallistiOS: ");
    add(info.kos_version);
    line("Native application ABI: ");
    number(api->version);
    add(" (SH-4 build; CPU clock is not measured)");
    line("System mode: ");
    add(system_name(info.system_type));
    add(" [");
    hex(info.system_type);
    add("]");
    line("Uptime: ");
    number(info.uptime_seconds);
    add(" seconds");
    line("Installed RAM (KOS detection): ");
    kib(info.ram_bytes);
    line("KOS heap allocated: ");
    kib(info.heap_used_bytes);
    line("GEMDOS arena (configured): ");
    kib(info.gem_pool_bytes);
    line("GEMDOS free payload: ");
    kib(info.gem_free_bytes);
    add("; largest block: ");
    kib(info.gem_largest_bytes);
    line("VDI desktop: ");
    number(screen_width);
    add(" x ");
    number(screen_height);
    add(", ");
    number(colours);
    add(" colours, ");
    number(planes);
    add(" planes");
    if (info.flags & DC_INFO_VIDEO) {
        line("Output framebuffer: ");
        number(info.video_width);
        add(" x ");
        number(info.video_height);
        add(", ");
        add(pixel_name(info.video_pixel_mode));
        add((info.video_flags & 1) ? ", interlaced" : ", progressive");
    } else {
        line("Output framebuffer: unavailable");
    }
    line("Detected cable: ");
    add(cable_name(info.video_cable));
    line("");
    line("MOUNTED GEMDOS VOLUMES (filesystem data capacity)");
    for (unsigned d = 0; d < 26; d++) {
        if (!(info.drive_mask & (1u << d)))
            continue;
        char drive[] = "A: ";
        drive[0] += d;
        line(drive);
        add((info.readonly_mask & (1u << d)) ? "read-only" : "writable");
        if (info.volatile_mask & (1u << d))
            add("; RAM-backed, lost at reset");
        uint32_t disk[4]; /* Dfree: free clusters, total, bytes/sector, sectors/cluster */
        if (api->gemdos(0x36, disk, (int)d + 1) == 0) {
            line("   ");
            kib(disk[1] * disk[2] * disk[3]);
            add(" total; ");
            kib(disk[0] * disk[2] * disk[3]);
            add(" free");
        } else {
            line("   Capacity query unavailable");
        }
    }
    if (!info.drive_mask)
        line("No mounted volumes reported.");
    line("");
    line("MAPLE DEVICES (device-reported names and capabilities)");
    if (!info.device_count)
        line("No devices enumerated. Press R after connecting a device.");
    for (unsigned i = 0; i < info.device_count; i++) {
        const struct dc_device_info *dev = &info.devices[i];
        char port[] = "Port A / unit ";
        if (dev->port < 26)
            port[5] += dev->port;
        else
            port[5] = '?';
        line(port);
        number(dev->unit);
        add(": ");
        add(dev->name);
        line("   ");
        hex(dev->functions);
        add("  ");
        functions(dev->functions);
    }
    line("");
    line("Press R to take a new snapshot. Emulator values describe its guest.");
    line("Clock speed and VRAM capacity are not queried by this OS interface.");
}
static void save_report(void)
{
    if (!snapshot_valid) {
        status = "No valid system snapshot to save.";
        return;
    }
    uint32_t writable = info.drive_mask & ~info.readonly_mask;
    uint32_t preferred = writable & info.volatile_mask;
    if (preferred)
        writable = preferred;
    unsigned d;
    for (d = 0; d < 26 && !(writable & (1u << d)); d++) {
    }
    if (d == 26) {
        status = "No writable volume is available.";
        return;
    }
    static char path[] = "A:\\SYSINFO.TXT";
    path[0] = 'A' + d;
    long handle = api->gemdos(0x3c, path, 0);
    if (handle < 0) {
        status = "Could not create SYSINFO.TXT on the writable volume.";
        return;
    }
    int ok = 1;
    for (unsigned i = 0; i < line_count && ok; i++) {
        long len = 0;
        while (lines[i][len])
            len++;
        ok = api->gemdos(0x40, (int)handle, len, lines[i]) == len &&
             api->gemdos(0x40, (int)handle, 2L, "\r\n") == 2;
    }
    if (api->gemdos(0x3e, (int)handle) < 0)
        ok = 0;
    static char saved_status[] = "Saved A:\\SYSINFO.TXT";
    saved_status[6] = 'A' + d;
    status = ok ? saved_status : "Report write failed (the drive may be full).";
}
static unsigned page_count(unsigned per_page)
{
    unsigned pages = 1;
    for (unsigned n = per_page; n < line_count; n += per_page)
        pages++;
    return pages;
}
static void draw(unsigned page, unsigned per_page)
{
    attribute(23, 1); /* solid fill */
    attribute(32, 1); /* replace */
    attribute(22, 1); /* black text */
    vp[0] = 0;
    vp[1] = 13;
    vdi(12, 1, 0);
    bar(0, 0, screen_width, screen_height, 0);
    text(16, 26, "SYSTEM INFORMATION");
    text(16, 48, "R: refresh   S: save report   Arrows/Space: pages   Esc: close");
    bar(16, 56, screen_width - 32, 1, 1);
    for (unsigned i = 0; i < per_page && page * per_page + i < line_count; i++)
        text(16, 82 + 18 * i, lines[page * per_page + i]);
    bar(16, screen_height - 55, screen_width - 32, 1, 1);
    text(16, screen_height - 34, status ? status : "S saves SYSINFO.TXT on the RAM disk, else the first writable drive.");
    /* Reuse a spare line as a bounded footer scratch buffer, not part of report. */
    unsigned count = line_count, col = column, pages = page_count(per_page);
    line_count = MAX_LINES;
    column = 0;
    add("Page ");
    number(page + 1);
    add(" / ");
    number(pages);
    text(16, screen_height - 12, lines[MAX_LINES - 1]);
    line_count = count;
    column = col;
}
long dc_app_main(const struct dc_native_api *os, const char *tail, const char *env)
{
    (void)tail;
    (void)env;
    api = os;
    if (os->version != DC_NATIVE_ABI)
        return -32;
    aes(10, 0, 1, 0);
    aes(77, 0, 5, 0);
    vc[6] = ao[0];
    for (int i = 0; i < 10; i++)
        vi[i] = 1;
    vi[10] = 2;
    vdi(100, 0, 11);
    if (!vc[6]) {
        aes(19, 0, 1, 0);
        return -65;
    }
    screen_width = vo[0] + 1;
    screen_height = vo[1] + 1;
    colours = vo[13];
    vi[0] = 1;
    vdi(102, 0, 1);
    planes = vo[4];
    update_lock(1); /* BEG_UPDATE */
    update_lock(3); /* BEG_MCTRL: prevent desktop menus consuming input */
    vdi(123, 0, 0);
    report();
    unsigned page = 0, per_page = (screen_height > 138) ? (screen_height - 138) / 18 : 1;
    for (;;) {
        draw(page, per_page);
        aes(20, 0, 1, 0);
        unsigned key = (uint16_t)ao[0], ch = key & 255, scan = key >> 8;
        if (ch == 27 || ch == 13 || ch == 'q' || ch == 'Q')
            break;
        if (ch == 'r' || ch == 'R') {
            report();
            status = "Snapshot refreshed.";
            page = 0;
        } else if (ch == 's' || ch == 'S') {
            save_report();
        } else if (ch == ' ' || scan == 0x4d || scan == 0x50 || scan == 0x51) {
            if (++page == page_count(per_page))
                page = 0;
        } else if (scan == 0x4b || scan == 0x48 || scan == 0x49) {
            page = page ? page - 1 : page_count(per_page) - 1;
        }
    }
    vi[0] = 1;
    vdi(122, 0, 1);
    vdi(101, 0, 0);
    update_lock(2); /* END_MCTRL */
    update_lock(0); /* END_UPDATE */
    ai[0] = 3;      /* FMD_FINISH: redraw covered desktop */
    for (int i = 1; i < 7; i++)
        ai[i] = 0;
    ai[7] = screen_width;
    ai[8] = screen_height;
    aes(51, 9, 1, 0);
    aes(19, 0, 1, 0);
    return 0;
}
