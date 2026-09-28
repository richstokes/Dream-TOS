/* KOS boundary: VMU inspection issues block-read commands only. GPL-2.0-or-later. */
#include <kos.h>
#include <dc/maple/vmu.h>
#include "dreamcast/vmu_info.h"
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
