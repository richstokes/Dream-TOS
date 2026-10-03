/* KOS boundary: SD card on the serial port (jj1odm/DreamShell-style SCIF SPI
 * adapter), through the KOS bit-banged SPI driver. GPL-2.0-or-later. */
#include <kos.h>
#include <dc/sd.h>
#include <kos/dbgio.h>
#include "dreamcast/sd.h"

static int ready;
static uint32_t card_sectors;

int dc_hal_sd_init(uint32_t *sectors)
{
    sd_init_params_t params = {.interface = SD_IF_SCIF, .check_crc = true};
    const char *debug_device;
    uint64_t bytes;
    if (ready) {
        if (sectors) *sectors = card_sectors;
        return 0;
    }
    /* The probe takes over SCIF before it knows whether a card is present.
     * Background KOS/network logging must not touch the serial FIFO while
     * SPI owns the pins, including during initialization and the CSD read.
     * Selecting null preserves the caller's global debug-enabled state. */
    debug_device = dbgio_dev_get();
    if (dbgio_dev_select("null"))
        return -1;
    /* Without an adapter the probe reads idle-high lines and fails quickly. */
    if (sd_init_ex(&params))
        goto failed;
    bytes = sd_get_size();
    if (bytes == (uint64_t)-1) {
        sd_shutdown();
        goto failed;
    }
    /* The serial port is now the SPI bus. KOS's debug console shares it, and
     * anything printed while a card is selected would corrupt the transfer. */
    dbgio_disable();
    ready = 1;
    card_sectors = bytes / 512 > 0xffffffffULL ? 0xffffffffUL : (uint32_t)(bytes / 512);
    if (sectors)
        *sectors = card_sectors;
    return 0;

failed:
    /* KOS releases SPI and restores SCIF on initialization failure or
     * shutdown. Only now can the original console safely be used again. */
    if (debug_device)
        dbgio_dev_select(debug_device);
    return -1;
}
int dc_hal_sd_read(uint32_t sector, uint32_t count, void *buffer)
{
    return ready && sd_read_blocks(sector, count, buffer) == 0 ? 0 : -1;
}
int dc_hal_sd_write(uint32_t sector, uint32_t count, const void *buffer)
{
    return ready && sd_write_blocks(sector, count, buffer) == 0 ? 0 : -1;
}
