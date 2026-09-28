/* Native ABI snapshot assembly. GPL-2.0-or-later. */
#include "emutos.h"
#include "string.h"
#include "dreamcast/system_info.h"

long dc_system_info(void *buffer, uint32_t bytes)
{
    struct dc_system_info *info = buffer;
    if (!buffer)
        return bytes ? -64 : (long)sizeof(*info);
    if (bytes < sizeof(*info))
        return -64;
    memset(info, 0, sizeof(*info));
    info->version = DC_SYSTEM_INFO_VERSION;
    info->bytes = sizeof(*info);
    strlcpy(info->os_version, EMU_VERSION, sizeof(info->os_version));
    dc_hal_system_info(info);
    dc_memory_system_info(info);
    dc_storage_system_info(info);
    return sizeof(*info);
}
