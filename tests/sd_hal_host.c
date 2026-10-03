/* Exercise serial-console ownership during the optional boot-time SD probe. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dc/sd.h>
#include <kos/dbgio.h>
#include "dreamcast/sd.h"

static const char *debug_device = "scif";
static int debug_enabled = 1, null_unavailable;
static int spi_owned, init_result, init_calls, shutdown_calls, log_attempts;
static uint64_t card_bytes = 8192 * 512;

static void background_log(void)
{
    ++log_attempts;
    /* Model a network/KOS message at each point where the SD driver owns
     * SCIF. Sending it to the serial FIFO would conflict with SPI. */
    assert(!spi_owned || !debug_enabled || !strcmp(debug_device, "null"));
}

const char *dbgio_dev_get(void) { return debug_device; }
int dbgio_dev_select(const char *name)
{
    if (!strcmp(name, "null") && null_unavailable)
        return -1;
    if (strcmp(name, "null"))
        assert(!spi_owned); /* Restore only after KOS releases the pins. */
    debug_device = name;
    return 0;
}
void dbgio_disable(void) { debug_enabled = 0; }

int sd_init_ex(const sd_init_params_t *params)
{
    assert(params->interface == SD_IF_SCIF && params->check_crc);
    ++init_calls;
    spi_owned = 1;
    background_log();
    /* KOS shuts SPI down and restores SCIF on probe failure. */
    if (init_result)
        spi_owned = 0;
    return init_result;
}
uint64_t sd_get_size(void)
{
    assert(spi_owned);
    background_log();
    return card_bytes;
}
int sd_shutdown(void)
{
    assert(spi_owned);
    ++shutdown_calls;
    background_log();
    spi_owned = 0;
    return 0;
}
int sd_read_blocks(uint32_t sector, size_t count, uint8_t *buffer)
{
    (void)sector; (void)count; (void)buffer;
    assert(spi_owned);
    background_log();
    return 0;
}
int sd_write_blocks(uint32_t sector, size_t count, const uint8_t *buffer)
{
    (void)sector; (void)count; (void)buffer;
    assert(spi_owned);
    background_log();
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t sectors = 123;
    unsigned char buffer[512] = {0};
    assert(argc == 2);
    assert(dc_hal_sd_read(0, 1, buffer) == -1);
    assert(dc_hal_sd_write(0, 1, buffer) == -1);

    if (!strcmp(argv[1], "no-card") || !strcmp(argv[1], "disabled") ||
        !strcmp(argv[1], "retry")) {
        int was_enabled = debug_enabled = strcmp(argv[1], "disabled") != 0;
        init_result = -1;
        assert(dc_hal_sd_init(&sectors) == -1);
        assert(!strcmp(debug_device, "scif"));
        assert(debug_enabled == was_enabled && !spi_owned);
        assert(init_calls == 1 && shutdown_calls == 0 && log_attempts == 1);
        assert(sectors == 123);
        if (strcmp(argv[1], "retry"))
            return 0;
        init_result = 0;
    } else if (!strcmp(argv[1], "size-failure")) {
        card_bytes = (uint64_t)-1;
        assert(dc_hal_sd_init(&sectors) == -1);
        assert(!strcmp(debug_device, "scif") && debug_enabled && !spi_owned);
        assert(init_calls == 1 && shutdown_calls == 1 && log_attempts == 3);
        assert(sectors == 123);
        assert(dc_hal_sd_read(0, 1, buffer) == -1);
        return 0;
    } else if (!strcmp(argv[1], "no-null-handler")) {
        null_unavailable = 1;
        assert(dc_hal_sd_init(&sectors) == -1);
        assert(!strcmp(debug_device, "scif") && debug_enabled);
        assert(!init_calls && !spi_owned && sectors == 123);
        return 0;
    } else {
        assert(!strcmp(argv[1], "success"));
    }

    assert(dc_hal_sd_init(&sectors) == 0);
    assert(sectors == 8192 && spi_owned && !debug_enabled);
    assert(dc_hal_sd_read(0, 1, buffer) == 0);
    assert(dc_hal_sd_write(0, 1, buffer) == 0);
    int calls = init_calls;
    assert(dc_hal_sd_init(NULL) == 0 && init_calls == calls);
    sectors = 0;
    assert(dc_hal_sd_init(&sectors) == 0 && sectors == 8192 && init_calls == calls);
    return 0;
}
