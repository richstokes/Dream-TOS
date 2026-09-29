/* VMU-backed Control Panel preferences. GPL-2.0-or-later. */
#ifndef DC_SETTINGS_H
#define DC_SETTINGS_H
#include "dreamcast/control.h"
#define DC_SETTINGS_VERSION 1
#define DC_SETTINGS_FILE "EMUTOS.CFG"
#define DC_SETTINGS_APP "EmuTOS DC"
#define DC_SETTINGS_DATA_BYTES 32
#define DC_SETTINGS_ABSENT -1
#define DC_SETTINGS_MISSING -2
#define DC_SETTINGS_IO -3
#define DC_SETTINGS_INVALID -4
#define DC_SETTINGS_FULL -5
#define DC_SETTINGS_CONFLICT -6
struct dc_control_settings {
    uint32_t version, bytes;
    struct dc_input_config input;
    uint32_t desktop_colour; /* 0=Original, 1=Teal, 2=Slate, 3=Amber */
};
/* Explicit card, load (write=0) or save (write=1). Does not apply settings.
 * Returns sizeof(struct dc_control_settings), or a negative error. Failed loads
 * leave the caller's buffer unchanged. NULL/0 with write=0 queries the size. */
long dc_control_store(int write, uint32_t port, uint32_t unit, void *buffer, uint32_t bytes);
/* Internal KOS boundary: a fixed, little-endian payload, never packed KOS structs. */
int dc_hal_settings_read(uint32_t port, uint32_t unit, unsigned char *data);
int dc_hal_settings_write(uint32_t port, uint32_t unit, const unsigned char *data);
#endif
