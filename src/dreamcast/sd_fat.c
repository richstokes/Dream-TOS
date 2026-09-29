/* SD card volume discovery: MBR and FAT12/FAT16 boot-sector validation.
 * No KOS or GEM dependencies, so it is unit-tested on the host.
 * GPL-2.0-or-later. */
#include <stdint.h>
#include "dreamcast/sd.h"

static unsigned rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

enum { BAD, FAT32, OK };

/* Validate a boot sector. `limit` is the space available for the volume. */
static int parse_boot(const uint8_t *s, uint32_t start, uint32_t limit, struct dc_sd_volume *v)
{
    if (s[510] != 0x55 || s[511] != 0xaa || rd16(s + 11) != 512)
        return BAD;
    uint32_t spc = s[13], reserved = rd16(s + 14), nfats = s[16], rootent = rd16(s + 17);
    uint32_t total = rd16(s + 19) ? rd16(s + 19) : rd32(s + 32), fatsz = rd16(s + 22);
    if (!spc || (spc & (spc - 1)) || spc > 64 || !reserved || nfats < 1 || nfats > 2)
        return BAD;
    if (!fatsz && !rootent && rd32(s + 36))
        return FAT32;
    if (!fatsz || !rootent || !total || total > limit)
        return BAD;
    uint32_t rdlen = (rootent * 32 + 511) / 512;
    uint32_t overhead = reserved + nfats * fatsz + rdlen;
    if (total <= overhead)
        return BAD;
    uint32_t numcl = (total - overhead) / spc;
    if (!numcl || numcl > 65524) /* larger counts are FAT32 territory */
        return BAD;
    if (overhead > 0xffff) /* BPB record numbers are 16-bit */
        return BAD;
    int fat16 = numcl >= 4085;
    if ((uint32_t)fatsz * 512 < (fat16 ? (numcl + 2) * 2 : ((numcl + 2) * 3 + 1) / 2))
        return BAD;
    v->start = start;
    v->sectors = total;
    v->clsiz = spc;
    v->rdlen = rdlen;
    v->fsiz = fatsz;
    /* TOS keeps these in 16 bits and points fatrec at the last FAT. */
    v->fatrec = reserved + (nfats > 1 ? fatsz : 0);
    v->datrec = overhead;
    v->numcl = numcl;
    v->fat16 = fat16;
    v->nfats = nfats;
    return OK;
}

int dc_sd_scan(dc_sd_read_fn read, void *context, uint32_t card_sectors,
               struct dc_sd_volume *volumes, int max, uint32_t *skipped)
{
    uint8_t sector[512], mbr[512];
    uint32_t skip = 0;
    int count = 0;
    if (skipped)
        *skipped = 0;
    if (!read || !volumes || max < 1)
        return 0;
    if (read(context, 0, 1, mbr))
        return -1;
    /* Floppy-style card: the boot sector is sector 0. An MBR has no BPB. */
    struct dc_sd_volume v;
    int kind = parse_boot(mbr, 0, card_sectors, &v);
    if (kind == OK)
        volumes[count++] = v;
    else if (kind == FAT32)
        skip |= DC_SD_SKIP_FAT32;
    else if (mbr[510] == 0x55 && mbr[511] == 0xaa) {
        for (int i = 0; i < 4; i++) {
            const uint8_t *e = mbr + 446 + 16 * i;
            uint32_t start = rd32(e + 8), size = rd32(e + 12);
            if (!e[4] || !size)
                continue;
            if (e[4] == 0x0b || e[4] == 0x0c) { /* FAT32: CHS / LBA */
                skip |= DC_SD_SKIP_FAT32;
                continue;
            }
            if (e[4] != 0x01 && e[4] != 0x04 && e[4] != 0x06 && e[4] != 0x0e)
                continue; /* extended, GPT protective, Linux, ... */
            if (start > card_sectors || size > card_sectors - start ||
                read(context, start, 1, sector)) {
                skip |= DC_SD_SKIP_OTHER;
                continue;
            }
            kind = parse_boot(sector, start, size, &v);
            if (kind == OK && count < max)
                volumes[count++] = v;
            else if (kind == FAT32)
                skip |= DC_SD_SKIP_FAT32;
            else if (kind != OK)
                skip |= DC_SD_SKIP_OTHER;
        }
    }
    if (skipped)
        *skipped = skip;
    return count;
}
