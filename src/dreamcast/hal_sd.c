/* KOS boundary: SD card on the serial port (jj1odm/DreamShell-style SCIF SPI
 * adapter), through the KOS bit-banged SPI driver. GPL-2.0-or-later. */
#include <kos.h>
#include <dc/sd.h>
#include <kos/dbgio.h>
#include "dreamcast/sd.h"

static int ready;

int dc_hal_sd_init(uint32_t *sectors)
{
    sd_init_params_t params = {.interface = SD_IF_SCIF, .check_crc = true};
    uint64_t bytes;
    if (ready)
        return 0;
    /* Without an adapter the probe reads idle-high lines and fails quickly. */
    if (sd_init_ex(&params))
        return -1;
    bytes = sd_get_size();
    if (bytes == (uint64_t)-1) {
        sd_shutdown();
        return -1;
    }
    /* The serial port is now the SPI bus. KOS's debug console shares it, and
     * anything printed while a card is selected would corrupt the transfer. */
    dbgio_disable();
    ready = 1;
    if (sectors)
        *sectors = bytes / 512 > 0xffffffffULL ? 0xffffffffUL : (uint32_t)(bytes / 512);
    return 0;
}
int dc_hal_sd_read(uint32_t sector, uint32_t count, void *buffer)
{
    return ready && sd_read_blocks(sector, count, buffer) == 0 ? 0 : -1;
}
int dc_hal_sd_write(uint32_t sector, uint32_t count, const void *buffer)
{
    return ready && sd_write_blocks(sector, count, buffer) == 0 ? 0 : -1;
}
