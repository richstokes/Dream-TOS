/* Single-producer, single-consumer PCM frame ring. GPL-2.0-or-later.
 * The application thread writes, the audio thread reads. Indices are
 * free-running 32-bit counters, so capacity must be a power of two.
 * No KOS dependency: also built by the host tests. */
#ifndef EMUTOS_DC_AUDIO_RING_H
#define EMUTOS_DC_AUDIO_RING_H
#include <stdint.h>
#include <string.h>

struct dc_ring {
    int16_t *data;
    uint32_t mask, channels;
    volatile uint32_t head, tail; /* head: producer, tail: consumer */
};
static inline int dc_ring_init(struct dc_ring *r, int16_t *data, uint32_t frames, uint32_t channels)
{
    if (!data || !frames || (frames & (frames - 1)) || channels < 1 || channels > 2)
        return 0;
    r->data = data;
    r->mask = frames - 1;
    r->channels = channels;
    r->head = r->tail = 0;
    return 1;
}
static inline uint32_t dc_ring_used(const struct dc_ring *r) { return r->head - r->tail; }
static inline uint32_t dc_ring_space(const struct dc_ring *r) { return r->mask + 1 - (r->head - r->tail); }
/* Producer side. Returns frames stored. */
static inline uint32_t dc_ring_write(struct dc_ring *r, const int16_t *src, uint32_t frames)
{
    uint32_t space = dc_ring_space(r), n = frames < space ? frames : space;
    uint32_t pos = r->head & r->mask, first = r->mask + 1 - pos;
    if (first > n)
        first = n;
    memcpy(r->data + (size_t)pos * r->channels, src, (size_t)first * r->channels * 2);
    memcpy(r->data, src + (size_t)first * r->channels, (size_t)(n - first) * r->channels * 2);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r->head += n;
    return n;
}
/* Consumer side. Returns frames read; the caller pads any shortfall. */
static inline uint32_t dc_ring_read(struct dc_ring *r, int16_t *dst, uint32_t frames)
{
    uint32_t used = dc_ring_used(r), n = frames < used ? frames : used;
    uint32_t pos = r->tail & r->mask, first = r->mask + 1 - pos;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (first > n)
        first = n;
    memcpy(dst, r->data + (size_t)pos * r->channels, (size_t)first * r->channels * 2);
    memcpy(dst + (size_t)first * r->channels, r->data, (size_t)(n - first) * r->channels * 2);
    r->tail += n;
    return n;
}
/* Drop everything queued. The consumer must be excluded (hal holds its lock). */
static inline void dc_ring_clear(struct dc_ring *r) { r->tail = r->head; }
#endif
