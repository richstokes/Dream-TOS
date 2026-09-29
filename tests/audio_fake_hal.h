/* Host stand-in for hal_audio.c: same ring, no KOS. The "AICA" is fake_consume(). */
#ifndef AUDIO_FAKE_HAL_H
#define AUDIO_FAKE_HAL_H
#include <stdlib.h>
#include "dreamcast/audio.h"
#include "dreamcast/audio_ring.h"
static struct dc_ring fring;
static int16_t *fdata;
static uint32_t frate, fch, fpos, fvol = 255, funder, fopen_count, fclose_count, fflush_count;
static int fpaused, fstate_open, fail_open;
long dc_hal_audio_open(uint32_t rate, uint32_t channels)
{
    if (fail_open)
        return DC_AUDIO_ERR_UNAVAILABLE;
    fdata = malloc((size_t)DC_AUDIO_RING_FRAMES * channels * 2);
    if (!fdata)
        return DC_AUDIO_ERR_MEMORY;
    dc_ring_init(&fring, fdata, DC_AUDIO_RING_FRAMES, channels);
    frate = rate;
    fch = channels;
    fpos = funder = 0;
    fpaused = 0;
    fstate_open = 1;
    fopen_count++;
    return 0;
}
void dc_hal_audio_close(void)
{
    free(fdata);
    fdata = NULL;
    fstate_open = 0;
    fclose_count++;
}
long dc_hal_audio_write(const int16_t *pcm, uint32_t frames) { return (long)dc_ring_write(&fring, pcm, frames); }
void dc_hal_audio_set(uint32_t what, uint32_t value)
{
    if (what == DC_AUDIO_VOLUME)
        fvol = value;
    else if (what == DC_AUDIO_PAUSE)
        fpaused = (int)value;
    else if (what == DC_AUDIO_FLUSH) {
        dc_ring_clear(&fring);
        fpos = 0;
        fflush_count++;
    }
}
void dc_hal_audio_info(struct dc_audio_info *info)
{
    info->state = fpaused ? DC_AUDIO_PAUSED : DC_AUDIO_PLAYING;
    info->rate = frate;
    info->channels = fch;
    info->capacity_frames = DC_AUDIO_RING_FRAMES;
    info->free_frames = dc_ring_space(&fring);
    info->queued_frames = dc_ring_used(&fring);
    info->position_frames = fpos;
    info->underruns = funder;
    info->volume = fvol;
}
/* What the audio thread does: take frames unless paused, count starvation. */
static uint32_t fake_consume(int16_t *dst, uint32_t frames)
{
    if (fpaused || !fstate_open)
        return 0;
    uint32_t n = dc_ring_read(&fring, dst, frames);
    fpos += n;
    if (n < frames)
        funder++;
    return n;
}
#endif
