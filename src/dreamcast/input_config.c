#ifdef MACHINE_DREAMCAST
#include "emutos.h"
#endif
/* Validated session-only settings. No filesystem writes. GPL-2.0-or-later. */
#include "dreamcast/control.h"
#include <string.h>
static struct dc_input_config config = {DC_CONTROL_VERSION, sizeof(config), 100, 300, 40};
int dc_input_config_valid(const struct dc_input_config *c)
{
    return c->version == DC_CONTROL_VERSION && c->bytes == sizeof(*c) &&
        c->mouse_percent >= 25 && c->mouse_percent <= 400 &&
        c->repeat_delay_ms >= 100 && c->repeat_delay_ms <= 1000 &&
        c->repeat_interval_ms >= 20 && c->repeat_interval_ms <= 200;
}
long dc_input_config(int write, void *buffer, uint32_t bytes)
{
    if (write != 0 && write != 1) return -64;
    if (!buffer) return (!write && !bytes) ? (long)sizeof(config) : -64;
    if (bytes < sizeof(config)) return -64;
    if (write) {
        struct dc_input_config next;
        memcpy(&next, buffer, sizeof(next));
        if (!dc_input_config_valid(&next)) return -64;
        config = next;
        dc_hal_input_config_changed(config.mouse_percent, config.repeat_delay_ms,
                                    config.repeat_interval_ms);
    } else memcpy(buffer, &config, sizeof(config));
    return sizeof(config);
}
int dc_scale_motion(int delta, unsigned percent, int *remainder)
{
    /* Maple deltas are bounded; retain fractions for slow, one-count movement. */
    int scaled = delta * (int)percent + *remainder;
    int result = scaled / 100;
    *remainder = scaled - result * 100;
    return result;
}
