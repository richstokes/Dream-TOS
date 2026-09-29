/* Host driver for dc_sd_scan: scans a raw card image, prints one line per volume. */
#include <stdio.h>
#include <stdlib.h>
#include "dreamcast/sd.h"

static FILE *image;
static unsigned long reads;
static long fail_at = -1;
static int read_image(void *ctx, uint32_t sector, uint32_t count, void *buf)
{
    (void)ctx;
    reads++;
    if (fail_at >= 0 && (long)sector == fail_at) return -1;
    if (fseek(image, (long)sector * 512, SEEK_SET)) return -1;
    return fread(buf, 512, count, image) == count ? 0 : -1;
}
int main(int argc, char **argv)
{
    if (argc < 2 || !(image = fopen(argv[1], "rb"))) return 2;
    if (argc > 2) fail_at = atol(argv[2]);
    fseek(image, 0, SEEK_END);
    uint32_t sectors = ftell(image) / 512, skipped = 99;
    struct dc_sd_volume v[DC_SD_MAX_VOLUMES];
    int n = dc_sd_scan(read_image, NULL, sectors, v, DC_SD_MAX_VOLUMES, &skipped);
    printf("count %d skipped %u\n", n, skipped);
    for (int i = 0; i < n; i++)
        printf("vol %d start=%u sectors=%u clsiz=%u rdlen=%u fsiz=%u fatrec=%u datrec=%u numcl=%u fat16=%u nfats=%u\n",
               i, v[i].start, v[i].sectors, v[i].clsiz, v[i].rdlen, v[i].fsiz, v[i].fatrec, v[i].datrec,
               v[i].numcl, v[i].fat16, v[i].nfats);
    return 0;
}
