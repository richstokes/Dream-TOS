/* One MBR primary FAT16 partition, 512-byte sectors, <=32 KiB clusters.
 * Quick format only: old file contents are not securely erased.
 * GPL-2.0-or-later. */
#include <string.h>
#include "dreamcast/sd_format.h"

static void put16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

int dc_sd_format_plan(uint32_t sectors, struct dc_sd_format_job *job)
{
    if (!job) return DC_SD_INVALID;
    memset(job, 0, sizeof(*job));
    /* Require 4 MiB; leave room for the alignment gap and old backup GPT.
     * 2046 MiB stays below FAT16's reserved cluster numbers at 32 KiB. */
    if (sectors < 8192) return DC_SD_TOO_SMALL;
    struct dc_sd_volume *v = &job->volume;
    v->start = 2048;
    v->sectors = sectors - v->start - 33;
    if (v->sectors > 4190208u) v->sectors = 4190208u;
    v->rdlen = 32; /* 512 root entries */
    v->nfats = 2;
    v->fat16 = 1;
    for (v->clsiz = 1; v->clsiz <= 64; v->clsiz *= 2) {
        /* Conservative upper bound, including FAT entries 0 and 1. */
        v->fsiz = (v->sectors / v->clsiz + 2 + 255) / 256;
        v->fatrec = 1 + v->fsiz;
        v->datrec = 1 + 2 * v->fsiz + v->rdlen;
        v->numcl = (v->sectors - v->datrec) / v->clsiz;
        if (v->numcl >= 4085 && v->numcl <= 65518) {
            job->card_sectors = sectors;
            job->total = 68 + v->datrec;
            return 0;
        }
    }
    return DC_SD_TOO_SMALL;
}

static uint32_t make_sector(const struct dc_sd_format_job *j, uint8_t *b)
{
    const struct dc_sd_volume *v = &j->volume;
    uint32_t i = j->completed;
    memset(b, 0, 512);
    /* Invalidate the old MBR first. Also remove standard primary/backup GPT
     * metadata so a PC won't resurrect an old partition map. */
    if (i < 34) return i;
    if (i < 67) return j->card_sectors - 33 + i - 34;
    if (i == j->total - 1) {
        uint8_t *e = b + 446;
        /* LBA FAT16; saturated CHS values for LBA-aware hosts. */
        e[1] = e[5] = 0xfe;
        e[2] = e[3] = e[6] = e[7] = 0xff;
        e[4] = 0x0e;
        put32(e + 8, v->start);
        put32(e + 12, v->sectors);
        b[510] = 0x55; b[511] = 0xaa;
        return 0; /* Publish the new partition only after all metadata verifies. */
    }
    i -= 67;
    if (!i) {
        memcpy(b, "\xeb\x3c\x90" "EMUTOSDC", 11);
        put16(b + 11, 512); b[13] = v->clsiz;
        put16(b + 14, 1); b[16] = 2; put16(b + 17, 512);
        if (v->sectors < 65536) put16(b + 19, v->sectors);
        else put32(b + 32, v->sectors);
        b[21] = 0xf8; put16(b + 22, v->fsiz);
        put16(b + 24, 63); put16(b + 26, 255);
        put32(b + 28, v->start);
        b[36] = 0x80; b[38] = 0x29;
        put32(b + 39, 0xdc160000u ^ j->card_sectors);
        memcpy(b + 43, "DREAM SD   FAT16   ", 19);
        /* Non-bootable x86 volume, with a well-formed short jump target. */
        b[62] = 0xeb; b[63] = 0xfe;
        b[510] = 0x55; b[511] = 0xaa;
    } else if (i == 1 || i == 1 + v->fsiz) {
        put16(b, 0xfff8); put16(b + 2, 0xffff);
    } else if (i == 1 + 2 * v->fsiz) {
        memcpy(b, "DREAM SD   ", 11); b[11] = 8;
    }
    return v->start + i;
}

int dc_sd_format_step(struct dc_sd_format_job *job, dc_sd_read_fn read,
                      dc_sd_write_fn write, void *context)
{
    uint8_t data[512], verify[512];
    if (!job || !job->total || !read || !write) return DC_SD_INVALID;
    for (int n = 0; n < 8 && job->completed < job->total; n++) {
        uint32_t sector = make_sector(job, data);
        if (write(context, sector, 1, data) || read(context, sector, 1, verify) ||
            memcmp(data, verify, 512)) return DC_SD_IO;
        job->completed++;
    }
    return job->completed == job->total;
}
