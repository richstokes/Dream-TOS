/* Native audio output (AICA stream). GPL-2.0-or-later.
 * Fixed-width scalars only: identical layout at GEM and KOS packing boundaries.
 * Applications decode; the OS feeds the AICA. Applications never call KOS.
 *
 * Model: signed 16-bit interleaved PCM (mono or stereo) is pushed into an
 * OS-owned ring buffer with audio_write, which never blocks. A background
 * KOS thread moves it to the AICA, so audio continues while the application
 * is busy. An empty ring plays silence and is counted as an underrun.
 * The stream is closed automatically when the program exits.
 */
#ifndef EMUTOS_DC_AUDIO_H
#define EMUTOS_DC_AUDIO_H
#include <stdint.h>

#define DC_AUDIO_VERSION 1
#define DC_AUDIO_MIN_RATE 8000
#define DC_AUDIO_MAX_RATE 48000
#define DC_AUDIO_RING_FRAMES 32768 /* power of two; about 0.74 s at 44.1 kHz */

/* Return values. Non-negative is success (audio_write: frames accepted). */
#define DC_AUDIO_OK 0
#define DC_AUDIO_ERR_ARGS -64
#define DC_AUDIO_ERR_UNAVAILABLE -1 /* no sound hardware or driver failure */
#define DC_AUDIO_ERR_BUSY -2        /* a stream is already open */
#define DC_AUDIO_ERR_NOTOPEN -3
#define DC_AUDIO_ERR_MEMORY -4

/* audio_set(what, value) */
#define DC_AUDIO_VOLUME 1 /* 0 (mute) to 255 (full) */
#define DC_AUDIO_PAUSE 2  /* 1 pause (muted, ring not consumed), 0 resume */
#define DC_AUDIO_FLUSH 3  /* drop queued audio and reset position to zero */

/* dc_audio_info.state */
#define DC_AUDIO_CLOSED 0
#define DC_AUDIO_PLAYING 1
#define DC_AUDIO_PAUSED 2

struct dc_audio_info {
    uint32_t version, bytes;
    uint32_t state;           /* DC_AUDIO_* */
    uint32_t rate, channels;
    uint32_t capacity_frames; /* ring size */
    uint32_t free_frames;     /* audio_write would accept this many now */
    uint32_t queued_frames;   /* waiting in the ring */
    /* Frames handed to the AICA since open or the last flush. Sound leaves the
     * speakers up to one AICA stream buffer (about 0.2 s) after this. */
    uint32_t position_frames;
    uint32_t underruns;       /* refills that found the ring empty */
    uint32_t volume;
};

/* OS-side entry points. Applications use dc_native_api.audio_* instead.
 * open: rate DC_AUDIO_MIN_RATE..MAX_RATE, channels 1 or 2.
 * write: copies up to frames (channels samples each); returns frames taken,
 *   possibly fewer or zero when the ring is full. Poll audio_space or retry.
 * info: NULL/0 returns the byte count; otherwise fills a v1 snapshot. */
long dc_audio_open(uint32_t rate, uint32_t channels);
long dc_audio_close(void);
long dc_audio_write(const int16_t *pcm, uint32_t frames);
long dc_audio_space(void);
long dc_audio_set(uint32_t what, uint32_t value);
long dc_audio_info(void *buffer, uint32_t bytes);

/* KOS boundary (hal_audio.c). Argument checks are done by audio.c. */
long dc_hal_audio_open(uint32_t rate, uint32_t channels);
void dc_hal_audio_close(void);
long dc_hal_audio_write(const int16_t *pcm, uint32_t frames);
void dc_hal_audio_set(uint32_t what, uint32_t value);
void dc_hal_audio_info(struct dc_audio_info *info); /* fills state onward */
#endif
