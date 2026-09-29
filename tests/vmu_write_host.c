/* Host driver for the VMU file engine against a card image in a file.
 *   vmu_write_host IMAGE info
 *   vmu_write_host IMAGE read NAME OUT
 *   vmu_write_host IMAGE write NAME INPUT [overwrite]
 *   vmu_write_host IMAGE delete NAME
 * Prints "result=N reads=N writes=N" and rewrites IMAGE. Fault injection:
 *   VMU_FAIL_READ=B       reading block B fails
 *   VMU_FAIL_WRITE_N=K    only the K-th block write fails (transient fault)
 *   VMU_FAIL_WRITE_FROM=K the K-th and every later write fails (card pulled)
 *   VMU_FLIP_WRITE_N=K    the K-th write "succeeds" but stores a flipped bit
 * The KOS vmufs entry points come from tests/vmufs_double.c, or from KOS's own
 * vmufs.c when built with the real-KOS shims. */
#include "dreamcast/vmu_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
int vmufs_write(maple_device_t *dev, const char *fn, void *inbuf, int insize, int flags);
int vmufs_delete(maple_device_t *dev, const char *fn);
static unsigned char image[256][512];
static maple_device_t device = {{MAPLE_FUNC_MEMCARD | MAPLE_FUNC_LCD}, 0, 1};
static int reads, writes, fail_read = -1, fail_n, fail_from, flip_n;
static int env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    return value ? atoi(value) : fallback;
}
int vmu_block_read(maple_device_t *dev, uint16_t block, uint8_t *out)
{
    (void)dev;
    reads++;
    if (block > 255 || block == fail_read) return -1;
    memcpy(out, image[block], 512);
    return 0;
}
int vmu_block_write(maple_device_t *dev, uint16_t block, const uint8_t *in)
{
    (void)dev;
    writes++;
    if (block > 255) return -1;
    if (block == 255) { fprintf(stderr, "ROOT BLOCK WRITTEN\n"); exit(99); }
    if (writes == fail_n || (fail_from && writes >= fail_from)) return -1;
    memcpy(image[block], in, 512);
    if (writes == flip_n) image[block][7] ^= 0x10;
    return 0;
}
static int read_block(void *context, unsigned block, unsigned char *out)
{
    return vmu_block_read(context, block, out);
}
static int backend_put(void *context, const char *name, const unsigned char *data, unsigned blocks, int overwrite)
{
    (void)context;
    return vmufs_write(&device, name, (void *)data, blocks * 512, overwrite ? 1 : 0);
}
static int backend_erase(void *context, const char *name)
{
    (void)context;
    return vmufs_delete(&device, name);
}
static unsigned char stage[DC_VMUF_MAX_BYTES], backup[DC_VMUF_MAX_BYTES], buffer[DC_VMUF_MAX_BYTES + 4096];
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(image, 512, 256, f) != 256) { fprintf(stderr, "cannot read image\n"); return 2; }
    fclose(f);
    fail_read = env_int("VMU_FAIL_READ", -1); fail_n = env_int("VMU_FAIL_WRITE_N", 0);
    fail_from = env_int("VMU_FAIL_WRITE_FROM", 0); flip_n = env_int("VMU_FLIP_WRITE_N", 0);
    struct dc_vmu_backend backend = {read_block, backend_put, backend_erase, stage, backup};
    long result = 0;
    const char *cmd = argv[2];
    if (!strcmp(cmd, "info")) {
        static struct dc_vmu_info info;
        int status = dc_vmu_inspect(&info, read_block, &device);
        printf("status=%d free=%u files=%u\n", status, info.free_blocks, info.file_count);
        return 0;
    } else if (!strcmp(cmd, "read") && argc == 5) {
        result = dc_vmuf_read(&backend, &device, argv[3], buffer, DC_VMUF_MAX_BYTES);
        if (result > 0) { FILE *o = fopen(argv[4], "wb"); fwrite(buffer, 1, result, o); fclose(o); }
    } else if (!strcmp(cmd, "readn") && argc == 6) {
        result = dc_vmuf_read(&backend, &device, argv[3], buffer, (uint32_t)atoi(argv[4]));
        if (result > 0) { FILE *o = fopen(argv[5], "wb"); fwrite(buffer, 1, result, o); fclose(o); }
    } else if (!strcmp(cmd, "size") && argc == 4) {
        result = dc_vmuf_read(&backend, &device, argv[3], NULL, 0);
    } else if (!strcmp(cmd, "write") && argc >= 5) {
        FILE *in = fopen(argv[4], "rb");
        size_t n = in ? fread(buffer, 1, sizeof(buffer), in) : 0;
        if (in) fclose(in);
        result = dc_vmuf_write(&backend, &device, argv[3], buffer, n, argc > 5 ? DC_VMUF_OVERWRITE : 0);
    } else if (!strcmp(cmd, "delete") && argc == 4) {
        result = dc_vmuf_delete(&backend, &device, argv[3]);
    } else return 2;
    printf("result=%ld reads=%d writes=%d\n", result, reads, writes);
    f = fopen(argv[1], "wb");
    fwrite(image, 512, 256, f);
    fclose(f);
    return 0;
}
