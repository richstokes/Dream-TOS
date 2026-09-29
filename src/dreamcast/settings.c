#ifdef MACHINE_DREAMCAST
#include "emutos.h"
#endif
/* Versioned, endian-independent settings payload. GPL-2.0-or-later. */
#include "dreamcast/settings.h"
#include <string.h>
static int valid(const struct dc_control_settings *s)
{
    return s->version == DC_SETTINGS_VERSION && s->bytes == sizeof(*s) &&
        dc_input_config_valid(&s->input) && s->desktop_colour < 4;
}
long dc_control_store(int write, uint32_t port, uint32_t unit, void *buffer, uint32_t bytes)
{
    struct dc_control_settings s;
    unsigned char data[DC_SETTINGS_DATA_BYTES];
    uint32_t words[8];
    int result;
    if ((write != 0 && write != 1) || port >= 4 || unit >= 6) return -64;
    if (!buffer) return !write && !bytes ? (long)sizeof(s) : -64;
    if (bytes < sizeof(s)) return -64;
    if (write) {
        memcpy(&s, buffer, sizeof(s));
        if (!valid(&s)) return -64;
        words[0]=s.version; words[1]=s.bytes;
        words[2]=s.input.version; words[3]=s.input.bytes;
        words[4]=s.input.mouse_percent; words[5]=s.input.repeat_delay_ms;
        words[6]=s.input.repeat_interval_ms; words[7]=s.desktop_colour;
        for (unsigned i=0; i<8; i++)
            for (unsigned b=0; b<4; b++) data[4*i+b]=words[i]>>(8*b);
        result=dc_hal_settings_write(port,unit,data);
        if (result) return result;
    } else {
        result=dc_hal_settings_read(port,unit,data);
        if (result) return result;
        for (unsigned i=0; i<8; i++) {
            words[i]=0;
            for (unsigned b=0; b<4; b++) words[i]|=(uint32_t)data[4*i+b]<<(8*b);
        }
        s=(struct dc_control_settings){words[0],words[1],
            {words[2],words[3],words[4],words[5],words[6]},words[7]};
        if (!valid(&s)) return DC_SETTINGS_INVALID;
        memcpy(buffer,&s,sizeof(s));
    }
    return sizeof(s);
}
