/* Sparse card fixture: exercise the real formatter and native service. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dreamcast/sd_format.h"
static struct { uint32_t sector; unsigned char data[512]; } blocks[2048];
static unsigned used, writes, reads, prepared, remounted;
static uint32_t capacity, mounted = 0x30;
static int cached, busy, mount_error, fault, fail_at;
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24; }
int dc_hal_sd_read(uint32_t s, uint32_t n, void *b)
{
    assert(n == 1 && s < capacity); reads++;
    if (fault == 1 && (int)writes == fail_at) return -1;
    memset(b, 0xa5, 512);
    for (unsigned i = 0; i < used; i++) if (blocks[i].sector == s) memcpy(b, blocks[i].data, 512);
    if (fault == 3 && (int)writes == fail_at) ((unsigned char *)b)[7] ^= 1;
    return 0;
}
int dc_hal_sd_write(uint32_t s, uint32_t n, const void *b)
{
    assert(prepared && !mounted && n == 1 && s < capacity);
    writes++;
    if (fault == 2 && (int)writes == fail_at) return -1;
    unsigned i;
    for (i = 0; i < used && blocks[i].sector != s; i++);
    if (i == used) { assert(used < 2048); used++; }
    blocks[i].sector = s; memcpy(blocks[i].data, b, 512);
    return 0;
}
void dc_storage_sd_status(uint32_t *s, uint32_t *m) { *s = capacity; *m = mounted; }
int dc_storage_sd_prepare(void)
{
    if (busy) return DC_SD_BUSY;
    prepared++; mounted = 0; return cached;
}
static int read_card(void *ctx, uint32_t s, uint32_t n, void *b)
{ (void)ctx; return dc_hal_sd_read(s, n, b); }
int dc_storage_sd_remount(void)
{
    struct dc_sd_volume v[4];
    remounted++;
    if (mount_error) return -1;
    assert(dc_sd_scan(read_card, NULL, capacity, v, 4, NULL) == 1);
    assert(v[0].fat16 && v[0].nfats == 2 && v[0].clsiz <= 64 && v[0].datrec <= 65535);
    mounted = 16; return 0;
}
int main(int argc, char **argv)
{
    assert(argc >= 3); capacity = (uint32_t)strtoul(argv[1], NULL, 10);
    const char *mode = argv[2];
    cached = !strcmp(mode, "cached"); busy = !strcmp(mode, "busy"); mount_error = !strcmp(mode, "mount-error");
    if (sscanf(mode, "write:%d", &fail_at) == 1) fault = 2;
    if (sscanf(mode, "read:%d", &fail_at) == 1) fault = 1;
    if (sscanf(mode, "corrupt:%d", &fail_at) == 1) fault = 3;
    /* Every GEMDOS drive number and letter, particularly C:/D:, is forbidden. */
    for (uint32_t d = 0; d < 256; d++) {
        assert(dc_sd_card_format(d, DC_SD_FORMAT_START) == DC_SD_INVALID);
        assert(dc_sd_card_format(d, DC_SD_FORMAT_STEP) == DC_SD_INVALID);
    }
    assert(!writes && !reads && !prepared);
    assert(dc_sd_card_format(DC_SD_TARGET_SERIAL, 99) == DC_SD_INVALID);
    assert(dc_sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_STEP) == DC_SD_INVALID);
    struct dc_sd_format_info info;
    assert(dc_sd_card_info(NULL, sizeof(info)) == DC_SD_INVALID);
    assert(dc_sd_card_info(&info, sizeof(info)-1) == DC_SD_INVALID);
    assert(dc_sd_card_info(&info, sizeof(info)) == sizeof(info));
    assert(info.card_sectors == capacity && !info.state);
    long r = dc_sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_START);
    if (!capacity || capacity < 8192 || busy || (fault == 1 && !fail_at)) {
        assert(r == (!capacity ? DC_SD_ABSENT : capacity < 8192 ? DC_SD_TOO_SMALL : busy ? DC_SD_BUSY : DC_SD_IO));
        assert(!writes && !prepared && mounted == 0x30); return 0;
    }
    assert(r == DC_SD_FORMATTING && !writes && prepared == 1);
    assert(dc_sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_START) == DC_SD_BUSY);
    do {
        unsigned before = writes;
        r = dc_sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_STEP);
        assert(writes - before <= 8);
        assert(dc_sd_card_info(&info, sizeof(info)) == sizeof(info));
        assert(info.completed <= info.total);
    } while (r == DC_SD_FORMATTING);
    if (fault) {
        assert(r == DC_SD_IO && info.state == DC_SD_FAILED && !mounted && !remounted);
        assert(dc_sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_START) == DC_SD_NEEDS_REBOOT);
        return 0;
    }
    assert(r == (cached || mount_error ? DC_SD_REBOOT : DC_SD_MOUNTED));
    assert(remounted == !cached && info.completed == info.total);
    if (cached || mount_error) {
        assert(!mounted);
        assert(dc_sd_card_format(DC_SD_TARGET_SERIAL, DC_SD_FORMAT_START) == DC_SD_NEEDS_REBOOT);
    } else assert(mounted == 16);
    unsigned char mbr[512], boot[512], a[512], b[512];
    dc_hal_sd_read(0, 1, mbr);
    assert(mbr[510] == 0x55 && mbr[511] == 0xaa && mbr[450] == 0x0e);
    assert(rd32(mbr + 454) == 2048 && rd32(mbr + 458) == info.volume_sectors);
    for (int i = 462; i < 510; i++) assert(!mbr[i]);
    dc_hal_sd_read(2048, 1, boot);
    unsigned fat = boot[22] | boot[23]<<8;
    assert(boot[11] == 0 && boot[12] == 2 && boot[13] <= 64);
    assert(rd32(boot + 28) == 2048);
    for (unsigned i = 0; i < fat; i++) {
        dc_hal_sd_read(2049 + i, 1, a); dc_hal_sd_read(2049 + fat + i, 1, b);
        assert(!memcmp(a, b, 512));
        for (unsigned j = i ? 0 : 4; j < 512; j++) assert(!a[j]);
    }
    dc_hal_sd_read(2049, 1, a); assert(a[0] == 0xf8 && a[1] == 0xff && a[2] == 0xff && a[3] == 0xff);
    for (unsigned i = 0; i < 32; i++) {
        dc_hal_sd_read(2049 + 2 * fat + i, 1, a);
        if (!i) assert(!memcmp(a, "DREAM SD   ", 11) && a[11] == 8);
        for (unsigned j = i ? 0 : 12; j < 512; j++) assert(!a[j]);
    }
    for (unsigned i = 1; i < 34; i++) {
        dc_hal_sd_read(i, 1, a); dc_hal_sd_read(capacity - i, 1, b);
        for (int j = 0; j < 512; j++) assert(!a[j] && !b[j]);
    }
    /* Optional sparse partition image for an independent host fsck. */
    if (argc > 3) {
        FILE *f = fopen(argv[3], "wb"); assert(f);
        assert(!fseek(f, (long)info.volume_sectors * 512 - 1, SEEK_SET)); fputc(0, f);
        for (unsigned i = 0; i < used; i++) if (blocks[i].sector >= 2048 && blocks[i].sector < 2048 + info.volume_sectors) {
            assert(!fseek(f, (long)(blocks[i].sector - 2048) * 512, SEEK_SET));
            assert(fwrite(blocks[i].data, 1, 512, f) == 512);
        }
        fclose(f);
    }
    printf("FAT16 %u sectors, %u sectors/cluster, %u verified writes\n", info.volume_sectors, info.cluster_sectors, writes);
    return 0;
}
