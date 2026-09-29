/* KOS boundary. dc_vmu_info issues block-read commands only; the file
 * service below reads and, after validation by vmu_file.c, writes whole files
 * through KOS vmufs. There is no formatting path. GPL-2.0-or-later. */
#include <kos.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>
#include "dreamcast/vmu_info.h"
#include "dreamcast/vmu_file.h"
struct card_address { unsigned port, unit; };
static int read_block(void *context, unsigned block, unsigned char *out)
{
    struct card_address *address=context;
    maple_device_t *dev=maple_enum_dev(address->port,address->unit);
    if (block>255 || !dev || !(dev->info.functions&MAPLE_FUNC_MEMCARD)) return -1;
    return vmu_block_read(dev,block,out);
}
long dc_vmu_info(uint32_t port, uint32_t unit, void *buffer, uint32_t bytes)
{
    if (port>=MAPLE_PORT_COUNT || unit>=MAPLE_UNIT_COUNT) return -64;
    if (!buffer) return bytes ? -64 : (long)sizeof(struct dc_vmu_info);
    if (bytes<sizeof(struct dc_vmu_info)) return -64;
    struct dc_vmu_info *out=buffer;
    memset(out,0,sizeof(*out));
    out->version=DC_VMU_VERSION; out->bytes=sizeof(*out); out->port=port; out->unit=unit;
    maple_device_t *dev=maple_enum_dev(port,unit);
    if (!dev || !(dev->info.functions&MAPLE_FUNC_MEMCARD)) out->status=DC_VMU_ABSENT;
    else {
        struct card_address address={port,unit};
        out->status=dc_vmu_inspect(out,read_block,&address);
    }
    if (out->status!=DC_VMU_OK) out->file_count=out->free_blocks=out->total_blocks=0;
    return sizeof(*out);
}

/* --- VMU file service and LCD ------------------------------------------- */
static uint8_t stage_buffer[DC_VMUF_MAX_BYTES] __attribute__((aligned(32)));
static uint8_t backup_buffer[DC_VMUF_MAX_BYTES] __attribute__((aligned(32)));
static maple_device_t *memory_card(unsigned port, unsigned unit)
{
    maple_device_t *dev=maple_enum_dev(port,unit);
    return dev && (dev->info.functions&MAPLE_FUNC_MEMCARD) ? dev : NULL;
}
static int backend_put(void *context, const char *name, const unsigned char *data,
                       unsigned blocks, int overwrite)
{
    struct card_address *address=context;
    maple_device_t *dev=memory_card(address->port,address->unit);
    /* vmufs_write checks space before writing and re-reads the FAT itself. */
    return dev ? vmufs_write(dev,name,(void *)data,(int)(blocks*512),overwrite ? VMUFS_OVERWRITE : 0) : -1;
}
static int backend_erase(void *context, const char *name)
{
    struct card_address *address=context;
    maple_device_t *dev=memory_card(address->port,address->unit);
    return dev ? vmufs_delete(dev,name) : -1;
}
static long with_card(uint32_t port, uint32_t unit, int mode, const char *name, void *buffer,
                      const void *data, uint32_t bytes, uint32_t flags)
{
    if (port>=MAPLE_PORT_COUNT || unit>=MAPLE_UNIT_COUNT) return DC_VMUF_BADARG;
    if (!memory_card(port,unit)) return DC_VMUF_ABSENT;
    struct card_address address={port,unit};
    struct dc_vmu_backend backend={read_block,backend_put,backend_erase,stage_buffer,backup_buffer};
    if (mode==0) return dc_vmuf_read(&backend,&address,name,buffer,bytes);
    if (mode==1) return dc_vmuf_write(&backend,&address,name,data,bytes,flags);
    return dc_vmuf_delete(&backend,&address,name);
}
long dc_vmu_file_read(uint32_t port, uint32_t unit, const char *name, void *buffer, uint32_t bytes)
{
    return with_card(port,unit,0,name,buffer,NULL,bytes,0);
}
long dc_vmu_file_write(uint32_t port, uint32_t unit, const char *name, const void *data,
                       uint32_t bytes, uint32_t flags)
{
    return with_card(port,unit,1,name,NULL,data,bytes,flags);
}
long dc_vmu_file_delete(uint32_t port, uint32_t unit, const char *name)
{
    return with_card(port,unit,2,name,NULL,NULL,0,0);
}
long dc_vmu_screen(uint32_t port, uint32_t unit, const void *bitmap, uint32_t bytes)
{
    if (port>=MAPLE_PORT_COUNT || unit>=MAPLE_UNIT_COUNT || !bitmap || bytes!=DC_VMU_LCD_BYTES)
        return DC_VMUF_BADARG;
    maple_device_t *dev=maple_enum_dev(port,unit);
    if (!dev) return DC_VMUF_ABSENT;
    if (!(dev->info.functions&MAPLE_FUNC_LCD)) return DC_VMUF_UNSUPPORTED;
    /* vmu_draw_lcd_rotated reads 32 bits at a time. Natural orientation in,
     * rotated to the LCD's native order by KOS; nothing retains the buffer. */
    uint32_t aligned[DC_VMU_LCD_BYTES/4];
    memcpy(aligned,bitmap,sizeof(aligned));
    int result=vmu_draw_lcd_rotated(dev,aligned);
    if (result==MAPLE_EOK) return 0;
    return result==MAPLE_EAGAIN ? DC_VMUF_BUSY : DC_VMUF_UNSUPPORTED;
}
