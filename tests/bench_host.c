/* Timing accounting and real file/memory behaviour under sanitizers. */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../apps/ports/bench.c"

static uint32_t fake_time;
static unsigned polls, fail_after, cancel_after, calls;
static int stalled;
static uint32_t fake_clock(void) { return fake_time; }
static int fake_poll(void)
{
    fake_time += 50; /* This must not appear in measured time. */
    return cancel_after && ++polls >= cancel_after;
}
static int fake_work(void *ctx, unsigned count)
{
    (void)ctx;
    if (fail_after && ++calls >= fail_after) return 0;
    if (!stalled) fake_time += count * 2;
    return 1;
}
static void timing(void)
{
    BenchResult r;
    fake_time = UINT32_MAX - 60; /* Cross the native timer wrap. */
    assert(bench_measure(&r, fake_work, NULL, fake_clock, fake_poll) == BENCH_OK);
    for (int i = 0; i < 3; i++) {
        assert(r.rate[i] == 500.0);
        assert(r.ms[i] == 256);
        assert(r.units[i] == 128);
    }
    cancel_after = 6; polls = 0;
    assert(bench_measure(&r, fake_work, NULL, fake_clock, fake_poll) == BENCH_CANCEL);
    cancel_after = 0; fail_after = 2; calls = 0;
    assert(bench_measure(&r, fake_work, NULL, fake_clock, fake_poll) == BENCH_WORK_ERROR);
    fail_after = 0; stalled = 1;
    assert(bench_measure(&r, fake_work, NULL, fake_clock, fake_poll) == BENCH_CLOCK_ERROR);
    stalled = 0;
}
static void kernels_and_files(void)
{
    source = malloc(BLOCK); target = malloc(BLOCK);
    assert(source && target);
    for (unsigned i = 0; i < BLOCK; i++) ((unsigned char *)source)[i] = i & 255;
    assert(integers(NULL, 2) && integer_sink != 0);
    assert(floats(NULL, 2) && doubles(NULL, 2));
    assert(copy_memory(NULL, 2) && !memcmp(source, target, BLOCK));
    assert(read_memory(NULL, 2));
    uint32_t expected = 0;
    for (unsigned i = 0; i < WORDS; i++) expected += source[i];
    assert(integer_sink == expected * 2);
    assert(write_memory(NULL, 2));
    for (unsigned i = 0; i < WORDS; i++) assert(target[i] == 0x5a5a5a5au);
    FILE *f = fopen("C:\\BNCH0000.TMP", "w");
    assert(f); fputs("KEEP", f); fclose(f);
    assert(create_temp());
    assert(!strcmp(temp_path, "C:\\BNCH0001.TMP"));
    assert(write_file(NULL, 2));
    memset(target, 0, BLOCK);
    assert(read_file(NULL, 2) && !memcmp(source, target, BLOCK));
    assert(remove_temp());
    assert(access("C:\\BNCH0001.TMP", F_OK));
    char b[8] = {0};
    f = fopen("C:\\BNCH0000.TMP", "r"); assert(f);
    assert(fread(b, 1, 4, f) == 4 && !strcmp(b, "KEEP")); fclose(f);
    file_fd = open("C:\\BNCH0000.TMP", O_RDONLY);
    assert(file_fd >= 0 && !transfer(0) && !transfer(1)); /* Short EOF/read-only. */
    close_file();
    assert(!transfer(0)); /* Closed descriptor. */
    tests[0].state = DONE;
    tests[0].result = (BenchResult){ {1,2,3}, {4,5,6}, {250,251,252} };
    tests[1].state = CANCELLED;
    tests[11].state = SKIPPED; tests[11].detail = "BENCH.DAT unavailable";
    environment = 2;
    assert(save_report("REPORT.TXT"));
    assert(!save_report("missing/REPORT.TXT"));
    f = fopen("REPORT.TXT", "r"); assert(f);
    char report[8192]; size_t len = fread(report, 1, sizeof(report)-1, f); fclose(f);
    report[len] = 0;
    assert(strstr(report, "Flycast") && strstr(report, "raw units/ms: 4/250 5/251 6/252"));
    assert(strstr(report, "Cancelled") && strstr(report, "BENCH.DAT unavailable"));
    assert(strstr(report, "not a raw GD-ROM"));
    cleanup();
}
int main(void)
{
    timing(); kernels_and_files();
    puts("Benchmark timing, wrap, cancellation, errors, kernels, files and reports: PASS");
    return 0;
}
