/* KallistiOS audio boundary: one AICA PCM stream fed from an OS-owned ring.
 * GPL-2.0-or-later. Compiled with KOS structure packing.
 *
 * The sound driver is started lazily on the first open, never at boot, so
 * systems that never play audio are unaffected. A polling thread (higher
 * priority than the GEM thread) moves data from the ring to the AICA, so
 * sound continues while an application decodes or redraws. */
#include <kos.h>
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>
#include <stdlib.h>
#include <string.h>
#include "dreamcast/audio.h"
#include "dreamcast/audio_ring.h"

#define STREAM_BYTES 32768 /* AICA-side buffer; refills happen per half */
#define POLL_MS 5

static struct {
    snd_stream_hnd_t hnd;
    kthread_t *thread;
    struct dc_ring ring;
    int16_t *data;
    mutex_t lock; /* excludes the consumer while the producer flushes */
    volatile int stop, paused;
    uint32_t rate, channels, volume, position, underruns;
} A = {.hnd = SND_STREAM_INVALID, .lock = MUTEX_INITIALIZER, .volume = 255};
static int driver_ready;
static int16_t chunk[STREAM_BYTES / 2] __attribute__((aligned(32)));

/* Called from snd_stream_poll on the audio thread; smp_req is in bytes. */
static void *feed(snd_stream_hnd_t hnd, int smp_req, int *smp_recv)
{
    (void)hnd;
    uint32_t frame_bytes = 2 * A.channels;
    uint32_t want = (uint32_t)smp_req / frame_bytes, got = 0;
    if ((uint32_t)smp_req > sizeof(chunk))
        smp_req = sizeof(chunk), want = smp_req / frame_bytes;
    mutex_lock(&A.lock);
    if (!A.paused) {
        got = dc_ring_read(&A.ring, chunk, want);
        A.position += got;
        if (got < want)
            A.underruns++;
    }
    mutex_unlock(&A.lock);
    if (got < want)
        memset((uint8_t *)chunk + got * frame_bytes, 0, (want - got) * frame_bytes);
    *smp_recv = want * frame_bytes;
    return chunk;
}
static void *audio_thread(void *arg)
{
    (void)arg;
    while (!A.stop) {
        snd_stream_poll(A.hnd);
        thd_sleep(POLL_MS);
    }
    return NULL;
}
long dc_hal_audio_open(uint32_t rate, uint32_t channels)
{
    if (!driver_ready) {
        if (snd_stream_init() < 0)
            return DC_AUDIO_ERR_UNAVAILABLE;
        driver_ready = 1;
    }
    A.data = malloc((size_t)DC_AUDIO_RING_FRAMES * channels * 2);
    if (!A.data)
        return DC_AUDIO_ERR_MEMORY;
    dc_ring_init(&A.ring, A.data, DC_AUDIO_RING_FRAMES, channels);
    A.rate = rate;
    A.channels = channels;
    A.position = A.underruns = 0;
    A.paused = A.stop = 0;
    A.hnd = snd_stream_alloc(feed, STREAM_BYTES);
    if (A.hnd == SND_STREAM_INVALID) {
        free(A.data);
        A.data = NULL;
        return DC_AUDIO_ERR_UNAVAILABLE;
    }
    snd_stream_volume(A.hnd, A.volume);
    snd_stream_start(A.hnd, rate, channels == 2);
    kthread_attr_t attr = {.stack_size = 16384, .prio = PRIO_DEFAULT - 1, .label = "Dream TOS audio"};
    A.thread = thd_create_ex(&attr, audio_thread, NULL);
    if (!A.thread) {
        snd_stream_destroy(A.hnd);
        A.hnd = SND_STREAM_INVALID;
        free(A.data);
        A.data = NULL;
        return DC_AUDIO_ERR_UNAVAILABLE;
    }
    return DC_AUDIO_OK;
}
void dc_hal_audio_close(void)
{
    if (A.hnd == SND_STREAM_INVALID)
        return;
    A.stop = 1;
    if (A.thread)
        thd_join(A.thread, NULL);
    A.thread = NULL;
    snd_stream_stop(A.hnd);
    snd_stream_destroy(A.hnd);
    A.hnd = SND_STREAM_INVALID;
    free(A.data);
    A.data = NULL;
}
long dc_hal_audio_write(const int16_t *pcm, uint32_t frames)
{
    return (long)dc_ring_write(&A.ring, pcm, frames);
}
void dc_hal_audio_set(uint32_t what, uint32_t value)
{
    switch (what) {
    case DC_AUDIO_VOLUME:
        A.volume = value;
        if (!A.paused)
            snd_stream_volume(A.hnd, value);
        break;
    case DC_AUDIO_PAUSE:
        /* Mute at once; audio already in the AICA buffer plays silently. */
        A.paused = value;
        snd_stream_volume(A.hnd, value ? 0 : A.volume);
        break;
    case DC_AUDIO_FLUSH:
        mutex_lock(&A.lock);
        dc_ring_clear(&A.ring);
        A.position = 0;
        mutex_unlock(&A.lock);
        break;
    }
}
void dc_hal_audio_info(struct dc_audio_info *info)
{
    info->state = A.paused ? DC_AUDIO_PAUSED : DC_AUDIO_PLAYING;
    info->rate = A.rate;
    info->channels = A.channels;
    info->capacity_frames = DC_AUDIO_RING_FRAMES;
    info->free_frames = dc_ring_space(&A.ring);
    info->queued_frames = dc_ring_used(&A.ring);
    info->position_frames = A.position;
    info->underruns = A.underruns;
    info->volume = A.volume;
}
