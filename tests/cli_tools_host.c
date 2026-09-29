/* Run the real command utilities under host sanitizers with fixed OS snapshots. */
#include "app.h"
#include "dreamcast/system_info.h"
#include <stdarg.h>
#include <string.h>
static long gemdos(int op, ...)
{
    if (op == 0x2a) return ((2026-1980)<<9) | (9<<5) | 29;
    if (op == 0x2c) return (13<<11) | (42<<5) | 15;
    va_list ap; va_start(ap, op);
    if (op == 0x36) {
        long *space = va_arg(ap, long *);
        space[0] = 1024; space[1] = 8192; space[2] = 512; space[3] = 1;
    }
    va_end(ap); return 0;
}
static long snapshot(void *buffer, uint32_t bytes)
{
    if (bytes != sizeof(struct dc_system_info)) return -64;
    struct dc_system_info *s = buffer;
    memset(s, 0, sizeof(*s));
    strcpy(s->os_version, "1.4.0 Dreamcast native"); strcpy(s->kos_version, "2.3.0");
    s->drive_mask = 12; s->readonly_mask = 8; s->volatile_mask = 4;
    s->gem_pool_bytes = 3145728; s->gem_free_bytes = 3000000; s->gem_largest_bytes = 2999968;
    s->ram_bytes = 16777216; s->heap_used_bytes = 123456; s->uptime_seconds = 60;
    s->video_width = 640; s->video_height = 480;
    return sizeof(*s);
}
static const struct dc_native_api api = {.version = 1, .size = sizeof(api), .gemdos = gemdos, .system_info = snapshot};
const struct dc_native_api *dc_os = &api;
extern int app_main(int, char **);
int main(int argc, char **argv) { return app_main(argc, argv); }
