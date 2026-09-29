#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#define MAPLE_FUNC_MEMCARD 0x02000000
#define MAPLE_FUNC_LCD 0x04000000
typedef struct maple_device { struct { uint32_t functions; } info; int port, unit; } maple_device_t;
