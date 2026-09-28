/* Read-only native system snapshot. GPL-2.0-or-later.
 * Fixed-width scalars only: identical layout at GEM and KOS packing boundaries.
 * Values describe the running OS, not a specification of the console.
 */
#ifndef EMUTOS_DC_SYSTEM_INFO_H
#define EMUTOS_DC_SYSTEM_INFO_H
#include <stdint.h>

#define DC_SYSTEM_INFO_VERSION 1
#define DC_SYSTEM_INFO_DEVICES 24
#define DC_INFO_VIDEO 1u

struct dc_device_info {
    uint32_t port;      /* 0=A, 1=B, ... */
    uint32_t unit;      /* 0=main peripheral */
    uint32_t functions; /* Maple device-reported function mask */
    char name[32];      /* Sanitized, NUL-terminated product name */
};

struct dc_system_info {
    uint32_t version, bytes, flags;
    uint32_t system_type;     /* KOS hardware_sys_mode() result */
    uint32_t ram_bytes;       /* KOS startup RAM detection (HW_MEMSIZE) */
    uint32_t heap_used_bytes; /* KOS mallinfo().uordblks */
    uint32_t uptime_seconds;  /* KOS monotonic timer, seconds since boot */
    uint32_t video_width, video_height, video_pixel_mode, video_flags;
    int32_t video_cable;        /* KOS vid_check_cable(), -1 if unknown */
    uint32_t gem_pool_bytes;    /* Configured GEMDOS allocation arena */
    uint32_t gem_free_bytes;    /* Sum of currently free payloads */
    uint32_t gem_largest_bytes; /* Largest allocatable contiguous payload */
    uint32_t drive_mask;        /* Mounted GEMDOS drives: bit 0=A */
    uint32_t readonly_mask;     /* Driver-enforced write protection */
    uint32_t volatile_mask;     /* RAM-backed drives */
    char os_version[32];
    char kos_version[16]; /* Linked KOS runtime version */
    uint32_t device_count;
    struct dc_device_info devices[DC_SYSTEM_INFO_DEVICES];
};

/* OS-side entry points. Applications use dc_native_api.system_info instead.
 * NULL/0 returns the required byte count. Too-small buffers return -64 and are
 * untouched. Success fills a v1 snapshot and returns its byte count.
 */
long dc_system_info(void *buffer, uint32_t bytes);
void dc_hal_system_info(struct dc_system_info *info);
void dc_memory_system_info(struct dc_system_info *info);
void dc_storage_system_info(struct dc_system_info *info);
#endif
