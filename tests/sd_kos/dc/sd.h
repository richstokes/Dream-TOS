#ifndef TEST_SD_KOS_SD_H
#define TEST_SD_KOS_SD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { SD_IF_SCIF } sd_interface_t;
typedef struct {
    sd_interface_t interface;
    bool check_crc;
} sd_init_params_t;
int sd_init_ex(const sd_init_params_t *params);
uint64_t sd_get_size(void);
int sd_shutdown(void);
int sd_read_blocks(uint32_t sector, size_t count, uint8_t *buffer);
int sd_write_blocks(uint32_t sector, size_t count, const uint8_t *buffer);
#endif
