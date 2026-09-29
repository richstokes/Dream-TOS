/* Minimal KOS boundary for testing the actual settings HAL on the host. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define MAPLE_FUNC_MEMCARD 0x02000000
#define VMUFS_OVERWRITE 1
typedef struct { struct { uint32_t functions; } info; int port,unit; } maple_device_t;
typedef struct {
    char desc_short[20],desc_long[36],app_id[20];
    int icon_cnt,icon_anim_speed,eyecatch_type,data_len;
    uint16_t icon_pal[16];
    uint8_t *icon_data,*eyecatch_data;
    const uint8_t *data;
} vmu_pkg_t;
typedef struct {
    char desc_short[16],desc_long[32],app_id[16];
    uint16_t icon_cnt,icon_anim_speed,eyecatch_type,crc;
    uint32_t data_len;
    uint8_t reserved[20];
    uint16_t icon_pal[16];
} vmu_hdr_t;
maple_device_t *maple_enum_dev(unsigned port,unsigned unit);
int vmu_block_read(maple_device_t *,unsigned,uint8_t *);
int vmufs_write(maple_device_t *,const char *,void *,int,int);
int vmu_pkg_build(vmu_pkg_t *,uint8_t **,int *);
int vmu_pkg_parse(uint8_t *,size_t,vmu_pkg_t *);
