/* Read-only VMU metadata. All public members use a stable scalar layout.
 * Writing files is a separate, validated service: see vmu_file.h. */
#ifndef DC_VMU_INFO_H
#define DC_VMU_INFO_H
#include <stdint.h>
#define DC_VMU_VERSION 1
#define DC_VMU_FILES 256
#define DC_VMU_OK 0
#define DC_VMU_ABSENT -1
#define DC_VMU_IO -2
#define DC_VMU_UNFORMATTED -3
#define DC_VMU_UNSUPPORTED -4
#define DC_VMU_CORRUPT -5
#define DC_VMU_NOT_READ -6
struct dc_vmu_file {
    char name[16], modified[20];
    uint32_t blocks, type, protected_file, first_block, header_block;
};
struct dc_vmu_info {
    uint32_t version, bytes, port, unit;
    int32_t status;
    uint32_t total_blocks, free_blocks, file_count;
    struct dc_vmu_file files[DC_VMU_FILES];
};
/* Returns snapshot bytes even for a card error (see status). This call never
 * writes. -64 rejects bad port/unit/buffers without modifying the buffer. */
long dc_vmu_info(uint32_t port, uint32_t unit, void *buffer, uint32_t bytes);
/* Portable metadata parser used by the HAL and host fixture tests. Reads only
 * root/FAT/directory blocks. Each callback is one bounded 512-byte block read. */
typedef int (*dc_vmu_reader)(void *context, unsigned block, unsigned char *out);
int dc_vmu_inspect(struct dc_vmu_info *out, dc_vmu_reader read, void *context);

/* Same parser, additionally returning each file's raw 12-byte directory name
 * (raw_names[DC_VMU_FILES], may be NULL) and the validated layout (may be
 * NULL). The file service uses these to match names exactly as KOS does. */
struct dc_vmu_layout {
    uint32_t fat_loc, dir_loc, dir_size, blocks, dir_entries;
};
int dc_vmu_inspect_ex(struct dc_vmu_info *out, unsigned char (*raw_names)[12],
                      struct dc_vmu_layout *layout, dc_vmu_reader read, void *context);
#endif
