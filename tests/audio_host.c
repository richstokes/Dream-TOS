/* Native audio ABI shim and ring buffer, without KOS. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio_fake_hal.h"
#include "../src/dreamcast/audio.c"

int main(void)
{
    /* ---- Ring alone: wraparound, full, empty, mono/stereo, index overflow ---- */
    struct dc_ring r;
    int16_t store[16 * 2], in[64], out[64];
    assert(!dc_ring_init(&r, store, 12, 2)); /* not a power of two */
    assert(!dc_ring_init(&r, store, 16, 3));
    assert(dc_ring_init(&r, store, 16, 2));
    for (int i = 0; i < 64; i++)
        in[i] = (int16_t)(i + 1);
    assert(dc_ring_space(&r) == 16 && dc_ring_used(&r) == 0);
    assert(dc_ring_write(&r, in, 10) == 10);
    assert(dc_ring_read(&r, out, 6) == 6 && !memcmp(out, in, 6 * 4));
    assert(dc_ring_write(&r, in + 20, 12) == 12); /* wraps the storage */
    assert(dc_ring_space(&r) == 0 && dc_ring_write(&r, in, 1) == 0);
    assert(dc_ring_read(&r, out, 16) == 16);
    assert(!memcmp(out, in + 12, 4 * 4) && !memcmp(out + 8, in + 20, 12 * 4));
    assert(dc_ring_read(&r, out, 1) == 0);
    r.head = r.tail = 0xfffffff8u; /* free-running counters overflow cleanly */
    assert(dc_ring_write(&r, in, 16) == 16 && dc_ring_used(&r) == 16 && r.head < 16);
    assert(dc_ring_read(&r, out, 16) == 16 && !memcmp(out, in, 16 * 4));
    dc_ring_write(&r, in, 5);
    dc_ring_clear(&r);
    assert(dc_ring_used(&r) == 0);

    /* ---- ABI shim: everything before open is refused, nothing crashes ---- */
    static int16_t pcm[4096 * 2];
    for (int i = 0; i < 4096 * 2; i++)
        pcm[i] = (int16_t)i;
    assert(dc_audio_write(pcm, 10) == DC_AUDIO_ERR_NOTOPEN);
    assert(dc_audio_space() == DC_AUDIO_ERR_NOTOPEN);
    assert(dc_audio_set(DC_AUDIO_VOLUME, 1) == DC_AUDIO_ERR_NOTOPEN);
    assert(dc_audio_close() == DC_AUDIO_OK); /* idempotent */
    assert(fclose_count == 0);

    /* info: size query, too-small buffer, closed snapshot */
    struct dc_audio_info info;
    assert(dc_audio_info(NULL, 0) == (long)sizeof(info));
    assert(dc_audio_info(NULL, 4) == DC_AUDIO_ERR_ARGS);
    assert(dc_audio_info(&info, sizeof(info) - 4) == DC_AUDIO_ERR_ARGS);
    memset(&info, 0xaa, sizeof(info));
    assert(dc_audio_info(&info, sizeof(info)) == (long)sizeof(info));
    assert(info.version == DC_AUDIO_VERSION && info.bytes == sizeof(info) && info.state == DC_AUDIO_CLOSED &&
           info.rate == 0);

    /* open validation */
    assert(dc_audio_open(7999, 2) == DC_AUDIO_ERR_ARGS);
    assert(dc_audio_open(48001, 2) == DC_AUDIO_ERR_ARGS);
    assert(dc_audio_open(44100, 0) == DC_AUDIO_ERR_ARGS);
    assert(dc_audio_open(44100, 3) == DC_AUDIO_ERR_ARGS);
    fail_open = 1;
    assert(dc_audio_open(44100, 2) == DC_AUDIO_ERR_UNAVAILABLE); /* driver failure leaves it closed */
    assert(dc_audio_write(pcm, 1) == DC_AUDIO_ERR_NOTOPEN);
    fail_open = 0;
    assert(dc_audio_open(44100, 2) == DC_AUDIO_OK);
    assert(dc_audio_open(22050, 1) == DC_AUDIO_ERR_BUSY);
    assert(frate == 44100 && fch == 2);

    /* writes never block: fill the ring, see a short write, then zero */
    assert(dc_audio_space() == DC_AUDIO_RING_FRAMES);
    assert(dc_audio_write(NULL, 5) == DC_AUDIO_ERR_ARGS);
    assert(dc_audio_write(pcm, 0) == 0);
    assert(dc_audio_write(pcm, DC_AUDIO_RING_FRAMES + 1) == DC_AUDIO_ERR_ARGS);
    for (int i = 0; i < 15; i++)
        assert(dc_audio_write(pcm, 2048) == 2048);
    assert(dc_audio_space() == DC_AUDIO_RING_FRAMES - 15 * 2048);
    assert(dc_audio_write(pcm, 4096) == 2048); /* only what fits */
    assert(dc_audio_space() == 0);
    assert(dc_audio_write(pcm, 100) == 0);
    assert(dc_audio_info(&info, sizeof(info)) == (long)sizeof(info));
    assert(info.state == DC_AUDIO_PLAYING && info.rate == 44100 && info.channels == 2);
    assert(info.free_frames == 0 && info.queued_frames == DC_AUDIO_RING_FRAMES &&
           info.capacity_frames == DC_AUDIO_RING_FRAMES);

    /* the consumer drains in order; position counts frames handed to the AICA */
    static int16_t got[4096 * 2];
    assert(fake_consume(got, 2048) == 2048 && !memcmp(got, pcm, 2048 * 4));
    assert(dc_audio_info(&info, sizeof(info)) > 0 && info.position_frames == 2048 && info.free_frames == 2048);
    /* flush drops queued audio and zeroes the position; starving is counted, not fatal */
    dc_audio_set(DC_AUDIO_FLUSH, 0);
    assert(dc_audio_info(&info, sizeof(info)) > 0 && info.queued_frames == 0 && info.position_frames == 0);
    assert(fake_consume(got, 10) == 0 && funder == 1);
    dc_audio_info(&info, sizeof(info));
    assert(info.underruns == 1);

    /* pause holds the ring; volume is range checked */
    assert(dc_audio_write(pcm, 100) == 100);
    assert(dc_audio_set(DC_AUDIO_PAUSE, 7) == DC_AUDIO_OK && fpaused == 1); /* any non-zero pauses */
    dc_audio_info(&info, sizeof(info));
    assert(info.state == DC_AUDIO_PAUSED);
    assert(fake_consume(got, 100) == 0);
    assert(dc_audio_set(DC_AUDIO_PAUSE, 0) == DC_AUDIO_OK);
    assert(fake_consume(got, 100) == 100);
    assert(dc_audio_set(DC_AUDIO_VOLUME, 256) == DC_AUDIO_ERR_ARGS && fvol == 255);
    assert(dc_audio_set(DC_AUDIO_VOLUME, 90) == DC_AUDIO_OK && fvol == 90);
    assert(dc_audio_set(99, 0) == DC_AUDIO_ERR_ARGS);

    /* close is idempotent, and a new stream can use a different format */
    assert(dc_audio_close() == DC_AUDIO_OK && fclose_count == 1);
    assert(dc_audio_close() == DC_AUDIO_OK && fclose_count == 1);
    assert(dc_audio_write(pcm, 1) == DC_AUDIO_ERR_NOTOPEN);
    assert(dc_audio_open(22050, 1) == DC_AUDIO_OK && fch == 1 && fopen_count == 2);
    assert(dc_audio_write(pcm, 300) == 300);
    assert(fake_consume(got, 300) == 300 && !memcmp(got, pcm, 300 * 2)); /* mono: 2 bytes per frame */
    dc_audio_close();

    /* All-uint32 struct: no padding, so GEM (2-byte) and KOS packing agree. */
    assert(sizeof(struct dc_audio_info) == 11 * 4);
    puts("audio ok");
    return 0;
}
