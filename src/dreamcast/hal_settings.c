/* Only EMUTOS.CFG is writable through this service. GPL-2.0-or-later. */
#include <kos.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>
#include <dc/vmu_pkg.h>
#include "dreamcast/settings.h"
#include "dreamcast/vmu_info.h"

/* Inspect bounded metadata before handing a card to the KOS writer. */
static int locate(uint32_t port, uint32_t unit, struct dc_vmu_file *file, uint32_t *free_blocks)
{
    struct dc_vmu_info info;
    long result=dc_vmu_info(port,unit,&info,sizeof(info));
    if (result != sizeof(info)) return DC_SETTINGS_IO;
    if (info.status == DC_VMU_ABSENT) return DC_SETTINGS_ABSENT;
    if (info.status == DC_VMU_IO) return DC_SETTINGS_IO;
    if (info.status != DC_VMU_OK) return DC_SETTINGS_INVALID;
    *free_blocks=info.free_blocks;
    for (uint32_t i=0; i<info.file_count; i++) {
        if (!strcmp(info.files[i].name,DC_SETTINGS_FILE)) {
            *file=info.files[i];
            if (file->blocks!=1 || file->type!=0x33 || file->header_block || file->protected_file)
                return DC_SETTINGS_CONFLICT;
            return 0;
        }
    }
    return DC_SETTINGS_MISSING;
}
static maple_device_t *card(uint32_t port, uint32_t unit)
{
    maple_device_t *dev=maple_enum_dev(port,unit);
    return dev && (dev->info.functions&MAPLE_FUNC_MEMCARD) ? dev : NULL;
}
static int read_file(uint32_t port, uint32_t unit, const struct dc_vmu_file *file,
                     unsigned char *data)
{
    /* Aligned for KOS's vmu_hdr_t. Only the fixed single-block format is accepted. */
    uint32_t block[128];
    vmu_pkg_t pkg;
    maple_device_t *dev=card(port,unit);
    if (!dev) return DC_SETTINGS_ABSENT;
    if (vmu_block_read(dev,file->first_block,(uint8_t *)block)) return DC_SETTINGS_IO;
    const vmu_hdr_t *hdr=(const vmu_hdr_t *)block;
    static const char app_id[16]=DC_SETTINGS_APP;
    if (memcmp(hdr->app_id,app_id,sizeof(app_id))) return DC_SETTINGS_CONFLICT;
    if (hdr->icon_cnt || hdr->eyecatch_type || hdr->data_len!=DC_SETTINGS_DATA_BYTES)
        return DC_SETTINGS_INVALID;
    if (vmu_pkg_parse((uint8_t *)block,sizeof(block),&pkg)) return DC_SETTINGS_INVALID;
    memcpy(data,pkg.data,DC_SETTINGS_DATA_BYTES);
    return 0;
}
int dc_hal_settings_read(uint32_t port, uint32_t unit, unsigned char *data)
{
    struct dc_vmu_file file;
    uint32_t free_blocks;
    int result=locate(port,unit,&file,&free_blocks);
    return result ? result : read_file(port,unit,&file,data);
}
int dc_hal_settings_write(uint32_t port, uint32_t unit, const unsigned char *data)
{
    struct dc_vmu_file file;
    uint32_t free_blocks;
    unsigned char previous[DC_SETTINGS_DATA_BYTES];
    int result=locate(port,unit,&file,&free_blocks);
    if (!result) {
        /* Refuse to overwrite a foreign/corrupt file sharing our filename. */
        result=read_file(port,unit,&file,previous);
        if (result) return result;
        if (!memcmp(previous,data,sizeof(previous))) return 0;
    } else if (result!=DC_SETTINGS_MISSING) return result;
    else if (!free_blocks) return DC_SETTINGS_FULL;

    vmu_pkg_t pkg={0};
    uint8_t *built=NULL;
    int size=0;
    strcpy(pkg.desc_short,"EmuTOS settings");
    strcpy(pkg.desc_long,"Dreamcast Control Panel");
    strcpy(pkg.app_id,DC_SETTINGS_APP);
    pkg.data_len=DC_SETTINGS_DATA_BYTES; pkg.data=data;
    /* Non-null sources even for the package builder's zero-length copies. */
    pkg.icon_data=pkg.eyecatch_data=(uint8_t *)data;
    if (vmu_pkg_build(&pkg,&built,&size)) return DC_SETTINGS_IO;
    /* vmufs_write reads whole blocks, so provide actual initialized padding. */
    uint32_t block[128]={0};
    if (size<0 || (unsigned)size>sizeof(block)) {free(built);return DC_SETTINGS_IO;}
    memcpy(block,built,size); free(built);
    maple_device_t *dev=card(port,unit);
    if (!dev) return DC_SETTINGS_ABSENT;
    if (vmufs_write(dev,DC_SETTINGS_FILE,block,sizeof(block),VMUFS_OVERWRITE))
        return DC_SETTINGS_IO;
    result=dc_hal_settings_read(port,unit,previous);
    /* A verification error follows a write attempt: do not report that nothing
     * was saved or that an existing file was left untouched. */
    if (result) return DC_SETTINGS_IO;
    return memcmp(previous,data,sizeof(previous)) ? DC_SETTINGS_IO : 0;
}
