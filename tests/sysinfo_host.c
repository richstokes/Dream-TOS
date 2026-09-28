/* Query-to-report tests using deliberately non-Dreamcast-sized mock values. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../apps/sysinfo.c"

static struct dc_system_info fixture;
static char saved[16000], saved_path[64];
static size_t saved_bytes;
static int fail_write, closed, free_queries;

static long query(void *buffer, uint32_t bytes)
{
    assert(bytes == sizeof(fixture));
    memcpy(buffer, &fixture, sizeof(fixture));
    return sizeof(fixture);
}
static long gemdos(int op, ...)
{
    va_list args;
    va_start(args, op);
    long result = 0;
    switch (op) {
    case 0x36: {
        uint32_t *disk = va_arg(args, uint32_t *);
        int drive = va_arg(args, int);
        assert(drive == 6 || drive == 7); /* F/G, not assumed C/D */
        disk[0] = 30;
        disk[1] = 100;
        disk[2] = 512;
        disk[3] = 2;
        free_queries++;
        break;
    }
    case 0x3c:
        strcpy(saved_path, va_arg(args, const char *));
        assert(va_arg(args, int) == 0);
        saved_bytes = 0;
        closed = 0;
        result = 9;
        break;
    case 0x40: {
        assert(va_arg(args, int) == 9);
        long size = va_arg(args, long);
        const char *data = va_arg(args, const char *);
        assert(size >= 0 && saved_bytes + size < sizeof(saved));
        if (fail_write) {
            result = -10;
            break;
        }
        memcpy(saved + saved_bytes, data, size);
        saved_bytes += size;
        saved[saved_bytes] = 0;
        result = size;
        break;
    }
    case 0x3e:
        assert(va_arg(args, int) == 9);
        closed++;
        break;
    default:
        assert(!"Unexpected GEMDOS call");
    }
    va_end(args);
    return result;
}
static int contains(const char *s)
{
    for (unsigned i = 0; i < line_count; i++)
        if (strstr(lines[i], s))
            return 1;
    return 0;
}

int main(void)
{
    _Static_assert(sizeof(struct dc_system_info) == 1180, "snapshot ABI layout");
    _Static_assert(offsetof(struct dc_system_info, devices) == 124, "device ABI layout");
    struct dc_native_api mock = {
        .version = 1, .size = sizeof(mock), .gemdos = gemdos, .system_info = query};
    api = &mock;
    fixture.version = DC_SYSTEM_INFO_VERSION;
    fixture.bytes = sizeof(fixture);
    strcpy(fixture.os_version, "test-os");
    strcpy(fixture.kos_version, "9.8.7");
    fixture.system_type = 9;
    fixture.ram_bytes = 32768 * 1024u;
    fixture.gem_pool_bytes = 1024 * 1024;
    fixture.gem_free_bytes = 768 * 1024;
    fixture.gem_largest_bytes = 512 * 1024;
    fixture.flags = DC_INFO_VIDEO;
    fixture.video_width = 720;
    fixture.video_height = 576;
    fixture.video_pixel_mode = 2;
    fixture.video_flags = 1;
    fixture.video_cable = 2;
    fixture.drive_mask = (1 << 5) | (1 << 6);
    fixture.readonly_mask = 1 << 6;
    fixture.volatile_mask = 1 << 5;
    fixture.device_count = 1;
    fixture.devices[0].port = 3;
    fixture.devices[0].unit = 2;
    fixture.devices[0].functions = 0x40000000;
    strcpy(fixture.devices[0].name, "Test Keyboard");
    screen_width = 800;
    screen_height = 600;
    colours = 256;
    planes = 8;
    report();
    assert(snapshot_valid && free_queries == 2);
    assert(contains("32768 KiB"));
    assert(contains("largest block: 512 KiB"));
    assert(contains("800 x 600, 256 colours, 8 planes"));
    assert(contains("720 x 576, RGB888 packed, interlaced"));
    assert(contains("RGB / SCART"));
    assert(contains("100 KiB total; 30 KiB free"));
    assert(contains("Port D / unit 2: Test Keyboard"));
    assert(contains("Clock speed and VRAM capacity are not queried"));
    assert(page_count(19) == 2);
    save_report();
    assert(!strcmp(saved_path, "F:\\SYSINFO.TXT"));
    assert(strstr(saved, "test-os") && strstr(saved, "Test Keyboard"));
    assert(strstr(saved, "\r\n") && closed == 1);
    assert(!strcmp(status, "Saved F:\\SYSINFO.TXT"));
    fail_write = 1;
    save_report();
    assert(closed == 1 && strstr(status, "write failed"));
    fixture.readonly_mask = fixture.drive_mask;
    report();
    save_report();
    assert(strstr(status, "No writable"));
    fixture.device_count = 0;
    fixture.flags = 0;
    report();
    assert(contains("No devices enumerated"));
    assert(contains("Output framebuffer: unavailable"));
    mock.size = offsetof(struct dc_native_api, system_info);
    mock.system_info = (void *)1; /* Must not call a field outside the advertised table. */
    report();
    assert(!snapshot_valid && contains("does not provide"));
    mock.size = sizeof(mock);
    mock.system_info = query;
    fixture.version++;
    report();
    assert(!snapshot_valid && contains("unsupported format"));
    puts("SYSINFO query, report, save/error paths and ABI fallback: PASS");
    return 0;
}
