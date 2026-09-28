/* Calibrate to >=16 ms batches, then collect three >=250 ms samples.
 * OS servicing is outside measured intervals; interrupts remain enabled.
 * Explicit uint32_t subtraction handles the native millisecond clock wrapping.
 * GPL-2.0-or-later.
 */
#include "bench_core.h"
#include <string.h>
int bench_measure(BenchResult *r, BenchWork work, void *ctx,
                  BenchClock clock, BenchPoll poll)
{
    unsigned batch = 1;
    uint32_t elapsed;
    memset(r, 0, sizeof(*r));
    for (;;) {
        if (poll()) return BENCH_CANCEL;
        uint32_t start = clock();
        if (!work(ctx, batch)) return BENCH_WORK_ERROR;
        elapsed = clock() - start;
        if (elapsed >= 16 && elapsed < 0x80000000u) break;
        if (elapsed >= 0x80000000u || batch == 8192) return BENCH_CLOCK_ERROR;
        batch *= 2;
    }
    for (unsigned sample = 0; sample < BENCH_SAMPLES; sample++) {
        unsigned stalled = 0;
        while (r->ms[sample] < BENCH_SAMPLE_MS) {
            if (poll()) return BENCH_CANCEL;
            uint32_t start = clock();
            if (!work(ctx, batch)) return BENCH_WORK_ERROR;
            elapsed = clock() - start;
            if (elapsed >= 0x80000000u || (!elapsed && ++stalled == 32))
                return BENCH_CLOCK_ERROR;
            if (elapsed) stalled = 0;
            r->ms[sample] += elapsed;
            r->units[sample] += batch;
        }
        r->rate[sample] = (double)r->units[sample] * 1000.0 / r->ms[sample];
    }
    for (unsigned i = 0; i < BENCH_SAMPLES; i++)
        for (unsigned j = i + 1; j < BENCH_SAMPLES; j++)
            if (r->rate[j] < r->rate[i]) {
                double swap = r->rate[i]; r->rate[i] = r->rate[j]; r->rate[j] = swap;
            }
    return BENCH_OK;
}
