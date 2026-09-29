#pragma once
#include <dc/maple.h>
int vmu_block_read(maple_device_t *dev, uint16_t blocknum, uint8_t *buffer);
int vmu_block_write(maple_device_t *dev, uint16_t blocknum, const uint8_t *buffer);
