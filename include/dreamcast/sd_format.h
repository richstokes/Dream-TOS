/* Serial SD quick formatter. GPL-2.0-or-later. Fixed-width native ABI. */
#ifndef EMUTOS_DC_SD_FORMAT_H
#define EMUTOS_DC_SD_FORMAT_H
#include "dreamcast/sd.h"

/* A physical device token, deliberately NOT a GEMDOS drive number/letter.
 * The service rejects every other target, including C: and D:. */
#define DC_SD_TARGET_SERIAL 0x53444344u
#define DC_SD_FORMAT_START 1u
#define DC_SD_FORMAT_STEP 2u
enum { DC_SD_READY, DC_SD_FORMATTING, DC_SD_MOUNTED, DC_SD_REBOOT, DC_SD_FAILED };
enum { DC_SD_INVALID = -1, DC_SD_ABSENT = -2, DC_SD_TOO_SMALL = -3,
       DC_SD_BUSY = -4, DC_SD_IO = -5, DC_SD_NEEDS_REBOOT = -6 };

struct dc_sd_format_info {
    uint32_t version, bytes, card_sectors, volume_sectors, cluster_sectors;
    uint32_t mounted_mask, state, completed, total;
};
long dc_sd_card_info(void *buffer, uint32_t bytes);
/* START requires explicit whole-card erase confirmation in the caller's UI.
 * STEP does bounded work; keep calling until it no longer returns FORMATTING.
 * No cancellation after START. Errors after START require a reboot. */
long dc_sd_card_format(uint32_t target, uint32_t action);

/* Portable formatter engine, not part of the application API. */
typedef int (*dc_sd_write_fn)(void *, uint32_t, uint32_t, const void *);
struct dc_sd_format_job {
    struct dc_sd_volume volume;
    uint32_t card_sectors, completed, total;
};
int dc_sd_format_plan(uint32_t sectors, struct dc_sd_format_job *job);
/* Write and read back up to eight metadata sectors; 0 pending, 1 done, <0 error. */
int dc_sd_format_step(struct dc_sd_format_job *job, dc_sd_read_fn read,
                      dc_sd_write_fn write, void *context);
/* OS storage hooks. No application entry points or arbitrary raw drive I/O. */
void dc_storage_sd_status(uint32_t *sectors, uint32_t *mask);
int dc_storage_sd_prepare(void); /* <0 busy; 1 cached references need reboot */
int dc_storage_sd_remount(void); /* 0 success */
#endif
