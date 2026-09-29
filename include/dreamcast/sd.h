/* SD card on the serial port (SCIF SPI adapter): partition scan and block HAL.
 * GPL-2.0-or-later. Fixed-width scalars only, so the layout is identical on
 * both sides of the KOS/GEM structure packing boundary.
 * TOS only understands FAT12/FAT16 with 512-byte sectors, so those are the
 * volumes exposed. FAT32 volumes are detected and reported, but not mounted. */
#ifndef EMUTOS_DC_SD_H
#define EMUTOS_DC_SD_H
#include <stdint.h>

#define DC_SD_MAX_VOLUMES 4 /* MBR primary partitions; mounted as E: to H: */

/* dc_sd_scan skipped-volume flags */
#define DC_SD_SKIP_FAT32 1u /* FAT32 partition: not supported by GEMDOS */
#define DC_SD_SKIP_OTHER 2u /* unrecognised or damaged FAT volume */

/* One mountable FAT12/FAT16 volume, in the units of the GEMDOS BPB. */
struct dc_sd_volume {
    uint32_t start;   /* first card sector of the volume */
    uint32_t sectors; /* volume length in sectors */
    uint32_t clsiz;   /* sectors per cluster */
    uint32_t rdlen;   /* root directory sectors */
    uint32_t fsiz;    /* sectors per FAT */
    uint32_t fatrec;  /* volume sector of the last FAT (GEMDOS BPB rule) */
    uint32_t datrec;  /* volume sector of the first data cluster */
    uint32_t numcl;   /* data clusters */
    uint32_t fat16;   /* 1: 16-bit FAT entries, 0: 12-bit */
    uint32_t nfats;   /* 1 or 2 */
};

typedef int (*dc_sd_read_fn)(void *context, uint32_t sector, uint32_t count, void *buffer);

/* Find mountable volumes: a bare FAT volume in sector 0, else the primary MBR
 * partitions in table order. Returns the volume count (0..max) or a negative
 * value on a read failure. `skipped` (optional) receives DC_SD_SKIP_* flags. */
int dc_sd_scan(dc_sd_read_fn read, void *context, uint32_t card_sectors,
               struct dc_sd_volume *volumes, int max, uint32_t *skipped);

/* Raw card access (src/dreamcast/hal_sd.c, KOS side). Sector = 512 bytes. */
int dc_hal_sd_init(uint32_t *sectors); /* 0 ok, <0 no adapter/card */
int dc_hal_sd_read(uint32_t sector, uint32_t count, void *buffer);        /* 0 ok */
int dc_hal_sd_write(uint32_t sector, uint32_t count, const void *buffer); /* 0 ok */
#endif
