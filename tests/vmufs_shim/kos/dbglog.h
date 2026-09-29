#pragma once
#include <stdio.h>
#include <stdlib.h>
#define DBG_ERROR 1
#define DBG_WARNING 2
#define DBG_INFO 3
#define dbglog(level, ...) do { if (getenv("VMU_TRACE")) fprintf(stderr, __VA_ARGS__); } while (0)
