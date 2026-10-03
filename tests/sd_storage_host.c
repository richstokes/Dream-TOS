/* Exercise actual storage routing and cache detachment; no real devices. */
#include <assert.h>
#include "../src/dreamcast/storage.c"
static int raw_reads, raw_writes;
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int dc_hal_sd_init(uint32_t *s) { *s = sd_sectors; return 0; }
int dc_hal_sd_read(uint32_t s, uint32_t n, void *b)
{ (void)s; (void)n; (void)b; raw_reads++; return -1; }
int dc_hal_sd_write(uint32_t s, uint32_t n, const void *b)
{ (void)s; (void)n; (void)b; raw_writes++; return 0; }
long dc_disc_read(void *b, unsigned long o, unsigned long n) { (void)b; (void)o; (void)n; return -1; }
int main(void)
{
    UBYTE ram[512] = {0}, data[512] = {0x12};
    ramdisk = ram; sd_count = 2; sd_sectors = 65536; drvbits = 0x3c;
    sd_vol[0].start = 2048; sd_vol[0].sectors = 10000;
    sd_vol[1].start = 12048; sd_vol[1].sectors = 10000;
    BCB sd_cache = {.b_bufdrv = 4, .b_dirty = 1}, ram_cache = {.b_bufdrv = 2, .b_dirty = 1};
    sd_cache.b_link = &ram_cache; bufl[0] = &sd_cache;
    DMD dmd = {.m_drvnum = 4}; OFD ofd = {.o_dmd = &dmd};
    sft[0].f_ofd = &ofd;
    assert(dc_storage_sd_prepare() == DC_SD_BUSY && sd_count == 2 && drvbits == 0x3c);
    assert(sd_cache.b_dirty && !raw_writes);
    sft[0].f_ofd = NULL; sft[1].f_ofd = (OFD *)-1L;
    drvsel = 1 << 4;
    assert(dc_storage_sd_prepare() == 1 && !sd_count && drvbits == 0x0c);
    assert(sd_cache.b_bufdrv == -1 && !sd_cache.b_dirty);
    assert(ram_cache.b_bufdrv == 2 && ram_cache.b_dirty);
    assert(dc_rwabs(1, data, 1, 0, 4) == EDRVNR && !raw_writes);
    assert(dc_rwabs(0, data, 1, 0, 5) == EDRVNR && !raw_reads);
    assert(!dc_getbpb(4) && !writable_drive(4));
    assert(dc_storage_sd_remount() == -1 && !raw_reads);
    assert(dc_rwabs(1, data, 1, 0, 3) == EWRPRO);
    assert(dc_rwabs(1, data, 1, 0, 2) == 0 && ram[0] == 0x12);
    drvsel = 0; assert(dc_storage_sd_prepare() == 0);
    return 0;
}
