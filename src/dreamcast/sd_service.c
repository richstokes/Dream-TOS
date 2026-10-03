/* Native formatter service. The only writable target is the physical serial
 * SD card; C:/D: and arbitrary GEMDOS drives are not accepted. GPL-2.0-or-later. */
#include <string.h>
#include "dreamcast/sd_format.h"

static struct dc_sd_format_job job;
static uint32_t state;
static int reboot_required;

static int card_read(void *ctx, uint32_t s, uint32_t n, void *b)
{ (void)ctx; return dc_hal_sd_read(s, n, b); }
static int card_write(void *ctx, uint32_t s, uint32_t n, const void *b)
{ (void)ctx; return dc_hal_sd_write(s, n, b); }

long dc_sd_card_info(void *buffer, uint32_t bytes)
{
    if (!buffer || bytes < sizeof(struct dc_sd_format_info)) return DC_SD_INVALID;
    struct dc_sd_format_info info;
    struct dc_sd_format_job plan;
    memset(&info, 0, sizeof(info));
    info.version = 1; info.bytes = sizeof(info); info.state = state;
    dc_storage_sd_status(&info.card_sectors, &info.mounted_mask);
    if (!dc_sd_format_plan(info.card_sectors, &plan)) {
        info.volume_sectors = plan.volume.sectors;
        info.cluster_sectors = plan.volume.clsiz;
    }
    info.completed = job.completed; info.total = job.total;
    memcpy(buffer, &info, sizeof(info));
    return sizeof(info);
}

long dc_sd_card_format(uint32_t target, uint32_t action)
{
    /* Check before probing, detaching, or writing anything. */
    if (target != DC_SD_TARGET_SERIAL) return DC_SD_INVALID;
    if (action == DC_SD_FORMAT_START) {
        uint32_t sectors, mask;
        uint8_t check[512];
        if (state == DC_SD_FORMATTING) return DC_SD_BUSY;
        if (state == DC_SD_REBOOT || state == DC_SD_FAILED) return DC_SD_NEEDS_REBOOT;
        dc_storage_sd_status(&sectors, &mask);
        if (!sectors) return DC_SD_ABSENT;
        int r = dc_sd_format_plan(sectors, &job);
        if (r) return r;
        /* A missing/removed card must fail before detaching any old mounts. */
        if (dc_hal_sd_read(0, 1, check)) return DC_SD_IO;
        r = dc_storage_sd_prepare();
        if (r < 0) return r;
        reboot_required = r;
        state = DC_SD_FORMATTING;
    } else if (action == DC_SD_FORMAT_STEP) {
        if (state != DC_SD_FORMATTING) return DC_SD_INVALID;
        int r = dc_sd_format_step(&job, card_read, card_write, NULL);
        if (r < 0) { state = DC_SD_FAILED; return r; }
        if (r) state = reboot_required || dc_storage_sd_remount() ? DC_SD_REBOOT : DC_SD_MOUNTED;
    } else return DC_SD_INVALID;
    return state;
}
