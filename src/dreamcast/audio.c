/* Native audio ABI: argument validation and stream state. GPL-2.0-or-later.
 * The KOS-specific mechanism is in hal_audio.c. Nothing here blocks. */
#ifdef MACHINE_DREAMCAST
#include "emutos.h"
#endif
#include <string.h>
#include "dreamcast/audio.h"

static int stream_open;

long dc_audio_open(uint32_t rate, uint32_t channels)
{
    if (rate < DC_AUDIO_MIN_RATE || rate > DC_AUDIO_MAX_RATE || channels < 1 || channels > 2)
        return DC_AUDIO_ERR_ARGS;
    if (stream_open)
        return DC_AUDIO_ERR_BUSY;
    long rc = dc_hal_audio_open(rate, channels);
    if (rc < 0)
        return rc;
    stream_open = 1;
    return DC_AUDIO_OK;
}
/* Idempotent: also called by the loader when a program exits. */
long dc_audio_close(void)
{
    if (stream_open) {
        dc_hal_audio_close();
        stream_open = 0;
    }
    return DC_AUDIO_OK;
}
long dc_audio_write(const int16_t *pcm, uint32_t frames)
{
    if (!stream_open)
        return DC_AUDIO_ERR_NOTOPEN;
    if (!frames)
        return 0;
    if (!pcm || frames > DC_AUDIO_RING_FRAMES)
        return DC_AUDIO_ERR_ARGS;
    return dc_hal_audio_write(pcm, frames);
}
long dc_audio_space(void)
{
    struct dc_audio_info info;
    if (!stream_open)
        return DC_AUDIO_ERR_NOTOPEN;
    memset(&info, 0, sizeof(info));
    dc_hal_audio_info(&info);
    return (long)info.free_frames;
}
long dc_audio_set(uint32_t what, uint32_t value)
{
    if (!stream_open)
        return DC_AUDIO_ERR_NOTOPEN;
    if ((what == DC_AUDIO_VOLUME && value > 255) ||
        (what != DC_AUDIO_VOLUME && what != DC_AUDIO_PAUSE && what != DC_AUDIO_FLUSH))
        return DC_AUDIO_ERR_ARGS;
    dc_hal_audio_set(what, what == DC_AUDIO_PAUSE ? !!value : value);
    return DC_AUDIO_OK;
}
long dc_audio_info(void *buffer, uint32_t bytes)
{
    struct dc_audio_info *info = buffer;
    if (!buffer)
        return bytes ? DC_AUDIO_ERR_ARGS : (long)sizeof(*info);
    if (bytes < sizeof(*info))
        return DC_AUDIO_ERR_ARGS;
    memset(info, 0, sizeof(*info));
    info->version = DC_AUDIO_VERSION;
    info->bytes = sizeof(*info);
    if (stream_open)
        dc_hal_audio_info(info);
    return (long)sizeof(*info);
}
