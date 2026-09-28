/* Native benchmark timing, GPL-2.0-or-later. */
#ifndef DC_BENCH_CORE_H
#define DC_BENCH_CORE_H
#include <stdint.h>
#define BENCH_SAMPLES 3
#define BENCH_SAMPLE_MS 250
/* A work unit is defined by the caller. Return zero on an I/O/work error. */
typedef int (*BenchWork)(void *, unsigned);
typedef uint32_t (*BenchClock)(void);
typedef int (*BenchPoll)(void); /* nonzero cancels; called outside timing */
typedef struct {
    double rate[BENCH_SAMPLES]; /* work units per second, sorted ascending */
    uint64_t units[BENCH_SAMPLES]; /* raw samples, in acquisition order */
    uint32_t ms[BENCH_SAMPLES];
} BenchResult;
enum { BENCH_OK, BENCH_CANCEL, BENCH_WORK_ERROR, BENCH_CLOCK_ERROR };
int bench_measure(BenchResult *, BenchWork, void *, BenchClock, BenchPoll);
#endif
