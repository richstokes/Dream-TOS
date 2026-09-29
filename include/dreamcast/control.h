/* Native input preferences and non-consuming diagnostics. GPL-2.0-or-later. */
#ifndef DC_CONTROL_H
#define DC_CONTROL_H
#include <stdint.h>
#define DC_CONTROL_VERSION 1
#define DC_INPUT_MOUSE 1u
#define DC_INPUT_KEYBOARD 2u
#define DC_INPUT_CONTROLLER 4u
struct dc_input_config {
    uint32_t version, bytes;
    uint32_t mouse_percent;       /* 25..400; keyboard pointer unaffected */
    uint32_t repeat_delay_ms;     /* 100..1000 */
    uint32_t repeat_interval_ms;  /* 20..200 */
};
struct dc_input_snapshot {
    uint32_t version, bytes, present;
    uint32_t mouse_port, keyboard_port, controller_port;
    int32_t mouse_dx, mouse_dy; /* last captured raw packet, before scaling */
    uint32_t mouse_buttons, mouse_packets;
    uint32_t key_raw, key_tos, key_events, modifiers, keys[6];
    uint32_t controller_buttons;
    int32_t joy_x, joy_y;
    uint32_t trigger_left, trigger_right;
};
/* Query with write=0; validate and apply atomically with write=1. NULL/0 queries
 * the required size. -64 means bad buffer/config; buffers are then untouched. */
long dc_input_config(int write, void *buffer, uint32_t bytes);
int dc_input_config_valid(const struct dc_input_config *config);
long dc_input_snapshot(void *buffer, uint32_t bytes);
void dc_hal_input_config_changed(uint32_t speed, uint32_t delay, uint32_t interval);
int dc_scale_motion(int delta, unsigned percent, int *remainder);
#endif
