/* BENCH.PRG: original native SH-4 benchmark, GPL-2.0-or-later. */
#include "app.h"
#include "bench_core.h"
#include "dreamcast/system_info.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLOCK (256u * 1024u)
#define WORDS (BLOCK / sizeof(uint32_t))
#define STEPS 1024u
#define ROWS 12
static uint32_t *source, *target;
static volatile uint32_t integer_sink;
static volatile float float_sink;
static volatile double double_sink;
static struct dc_system_info info;
static int have_info, cancelled, environment; /* 0 unknown, 1 hardware, 2 Flycast */
static int file_fd = -1, temp_owned;
static char temp_path[32];
static const char *notice = "R/Return: run   S: save   E: environment   Esc: exit";
static const char *environments[] = {"Unspecified", "Real hardware", "Flycast"};

enum { PENDING, DONE, SKIPPED, FAILED, CANCELLED };
typedef struct {
    const char *name, *unit;
    double scale;
    BenchWork work;
    BenchResult result;
    int state;
    const char *detail;
} Test;

static uint32_t ticks(void) { return (uint32_t)dc_os->millis(); }
static int poll_run(void)
{
    dc_os->yield();
    if ((app_event(0, NULL, NULL, NULL) & 255) == 27) cancelled = 1;
    return cancelled;
}

/* Dependent arithmetic chains, with outputs consumed through volatile sinks.
 * No fast-math: each FP update is a multiply and an add, not a FLOPS claim. */
static __attribute__((noinline)) int integers(void *ctx, unsigned repeats)
{
    (void)ctx;
    uint32_t x = integer_sink + 1;
    for (unsigned n = 0; n < repeats; n++)
        for (unsigned i = 0; i < STEPS; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        }
    integer_sink = x;
    return 1;
}
static __attribute__((noinline)) int floats(void *ctx, unsigned repeats)
{
    (void)ctx;
    float x = 0.5f;
    for (unsigned n = 0; n < repeats; n++)
        for (unsigned i = 0; i < STEPS; i++) x = x * 0.9999f + 0.00001f;
    float_sink = x;
    return x > 0.0f && x <= 0.5f;
}
static __attribute__((noinline)) int doubles(void *ctx, unsigned repeats)
{
    (void)ctx;
    double x = 0.5;
    for (unsigned n = 0; n < repeats; n++)
        for (unsigned i = 0; i < STEPS; i++) x = x * 0.9999 + 0.00001;
    double_sink = x;
    return x > 0.0 && x <= 0.5;
}
static int copy_memory(void *ctx, unsigned repeats)
{
    (void)ctx;
    for (unsigned n = 0; n < repeats; n++) {
        memcpy(target, source, BLOCK);
        /* Also acts as a compiler memory barrier for each completed copy. */
        __asm__ volatile ("" : : "r"(target) : "memory");
    }
    return 1;
}
static int read_memory(void *ctx, unsigned repeats)
{
    (void)ctx;
    const volatile uint32_t *p = source;
    uint32_t sum = 0;
    for (unsigned n = 0; n < repeats; n++)
        for (unsigned i = 0; i < WORDS; i++) sum += p[i];
    integer_sink = sum;
    return 1;
}
static int write_memory(void *ctx, unsigned repeats)
{
    (void)ctx;
    volatile uint32_t *p = target;
    for (unsigned n = 0; n < repeats; n++)
        for (unsigned i = 0; i < WORDS; i++) p[i] = 0x5a5a5a5au;
    return 1;
}
/* Each graphics unit includes the toolkit's VDI attribute calls. Drawing is
 * confined to a 608x112 area, independent of the results above it. */
static int lines(void *ctx, unsigned repeats)
{
    (void)ctx;
    for (unsigned n = 0; n < repeats; n++)
        app_line(16, 326 + n % 112, 623, 437 - n % 112, 1 + n % 15);
    return 1;
}
static int fills(void *ctx, unsigned repeats)
{
    (void)ctx;
    for (unsigned n = 0; n < repeats; n++)
        app_box(16 + n % 520, 326 + n % 80, 80, 32, n % 16);
    return 1;
}
static int text(void *ctx, unsigned repeats)
{
    (void)ctx;
    for (unsigned n = 0; n < repeats; n++)
        app_text(16 + n % 320, 340 + n % 90, "Native SH-4 GEM benchmark", 1 + n % 15);
    return 1;
}
static int transfer(int writing)
{
    unsigned char *p = (unsigned char *)(writing ? source : target);
    size_t left = BLOCK;
    if (lseek(file_fd, 0, SEEK_SET) != 0) return 0;
    while (left) {
        /* Fixed 16 KiB requests; include syscall/seek overhead in file rates. */
        size_t chunk = left < 16384 ? left : 16384;
        ssize_t n = writing ? write(file_fd, p, chunk) : read(file_fd, p, chunk);
        if (n <= 0 || (size_t)n > chunk) return 0;
        p += n; left -= n;
    }
    return 1;
}
static int read_file(void *ctx, unsigned repeats)
{
    (void)ctx;
    for (unsigned n = 0; n < repeats; n++) if (!transfer(0)) return 0;
    return 1;
}
static int write_file(void *ctx, unsigned repeats)
{
    (void)ctx;
    for (unsigned n = 0; n < repeats; n++) if (!transfer(1)) return 0;
    return 1;
}
static Test tests[ROWS] = {
    {"Integer xorshift32", "M iter/s", STEPS / 1e6, integers},
    {"Float multiply/add", "M upd/s", STEPS / 1e6, floats},
    {"Double multiply/add", "M upd/s", STEPS / 1e6, doubles},
    {"Memory copy 256 KiB", "MiB/s", 0.25, copy_memory},
    {"Memory read 256 KiB", "MiB/s", 0.25, read_memory},
    {"Memory write 256 KiB", "MiB/s", 0.25, write_memory},
    {"VDI lines", "lines/s", 1, lines},
    {"VDI fills 80x32", "fills/s", 1, fills},
    {"VDI text 24 chars", "strings/s", 1, text},
    {"C: RAM file write", "MiB/s", 0.25, write_file},
    {"C: RAM file read", "MiB/s", 0.25, read_file},
    {"D: repeated file read", "MiB/s", 0.25, read_file},
};
static const char *state_name(int state)
{
    switch (state) {
    case DONE: return "OK";
    case SKIPPED: return "Skipped";
    case FAILED: return "Failed";
    case CANCELLED: return "Cancelled";
    default: return "--";
    }
}
static void draw(void)
{
    char b[100];
    app_clear(0);
    snprintf(b, sizeof(b), "Native benchmark v1    Run environment: %s", environments[environment]);
    app_text(16, 44, b, 1);
    app_text(16, 65, "Test                         Median       Range (3 samples)", 1);
    app_line(16, 71, 624, 71, 1);
    for (int i = 0; i < ROWS; i++) {
        Test *t = &tests[i];
        app_text(16, 90 + i * 18, t->name, 1);
        if (t->state == DONE)
            snprintf(b, sizeof(b), "%9.2f  %-9s %8.2f-%8.2f",
                     t->result.rate[1] * t->scale, t->unit,
                     t->result.rate[0] * t->scale, t->result.rate[2] * t->scale);
        else snprintf(b, sizeof(b), "%s%s%s", state_name(t->state),
                      t->detail ? ": " : "", t->detail ? t->detail : "");
        app_text(224, 90 + i * 18, b, 1);
    }
    app_text(16, 314, "Graphics test area / higher rates mean faster work", 1);
    app_box(16, 326, 608, 112, 8);
    app_status(notice);
}
static void close_file(void)
{
    if (file_fd >= 0) close(file_fd);
    file_fd = -1;
}
static int remove_temp(void)
{
    close_file();
    if (temp_owned && unlink(temp_path)) return 0;
    temp_owned = 0;
    return 1;
}
static void cleanup(void)
{
    remove_temp();
    free(source); free(target);
    source = target = NULL;
    app_end();
}
static int create_temp(void)
{
    if (!remove_temp()) return 0;
    for (int n = 0; n < 100; n++) {
        snprintf(temp_path, sizeof(temp_path), "C:\\BNCH%04d.TMP", n);
        file_fd = open(temp_path, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (file_fd >= 0) { temp_owned = 1; return transfer(1); }
        if (errno != EEXIST) break;
    }
    return 0;
}
static void get_info(void)
{
    memset(&info, 0, sizeof(info));
    have_info = dc_os->size >= offsetof(struct dc_native_api, system_info) + sizeof(dc_os->system_info)
        && dc_os->system_info && dc_os->system_info(&info, sizeof(info)) == sizeof(info)
        && info.version == DC_SYSTEM_INFO_VERSION && info.bytes == sizeof(info);
    info.os_version[sizeof(info.os_version)-1] = 0;
    info.kos_version[sizeof(info.kos_version)-1] = 0;
}
static void run_all(void)
{
    cancelled = 0;
    for (int i = 0; i < ROWS; i++) { tests[i].state = PENDING; tests[i].detail = NULL; }
    get_info();
    for (int i = 0; i < ROWS; i++) {
        Test *t = &tests[i];
        char progress[80];
        snprintf(progress, sizeof(progress), "Test %d/%d: %s   Esc: cancel", i + 1, ROWS, t->name);
        notice = progress;
        draw();
        notice = "R/Return: run   S: save   E: environment   Esc: exit";
        if (cancelled) { t->state = CANCELLED; continue; }
        if (i == 9 && (!have_info || !(info.volatile_mask & (1u << 2)) ||
                      !(info.drive_mask & (1u << 2)) || (info.readonly_mask & (1u << 2)))) {
            t->state = SKIPPED; t->detail = "C: is not a RAM disk"; continue;
        }
        if (i == 9 && !create_temp()) {
            t->state = FAILED; t->detail = "Cannot create test file"; remove_temp(); continue;
        }
        if (i == 10 && file_fd < 0) {
            t->state = SKIPPED; t->detail = "No RAM test file"; continue;
        }
        if (i == 11) {
            close_file();
            file_fd = open("D:\\BENCH.DAT", O_RDONLY);
            if (file_fd < 0) {
                t->state = SKIPPED; t->detail = "BENCH.DAT unavailable"; continue;
            }
        }
        int r = bench_measure(&t->result, t->work, NULL, ticks, poll_run);
        t->state = r == BENCH_OK ? DONE : r == BENCH_CANCEL ? CANCELLED : FAILED;
        if (r == BENCH_WORK_ERROR) t->detail = "Work/I/O error";
        if (r == BENCH_CLOCK_ERROR) t->detail = "Timer did not advance";
        /* Verify outside timing; never publish rates for corrupt transfers. */
        int valid = 1;
        if (r == BENCH_OK && (i == 3 || i == 10 || i == 11)) valid = !memcmp(source, target, BLOCK);
        if (r == BENCH_OK && i == 5)
            for (unsigned n = 0; n < WORDS; n++) if (target[n] != 0x5a5a5a5au) valid = 0;
        if (r == BENCH_OK && i == 9) valid = transfer(0) && !memcmp(source, target, BLOCK);
        if (!valid) { t->state = FAILED; t->detail = "Data verification failed"; }
        if (i == 10 || i == 11) close_file();
    }
    notice = cancelled ? "Cancelled. S: save partial results   R: rerun   Esc: exit" :
                          "Done. S: save   R: rerun   E: environment   Esc: exit";
    if (!remove_temp()) notice = "Cannot remove C:\\BNCH*.TMP test file. S: save   Esc: exit";
    draw();
}
static int save_report(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "Native Dreamcast benchmark v1\nRun environment (user selected): %s\n", environments[environment]);
    if (have_info) fprintf(f, "OS: %s; KOS: %s; uptime at run: %lu seconds\n",
                          info.os_version, info.kos_version, (unsigned long)info.uptime_seconds);
    fprintf(f, "Build: SH-4, GCC %s, -O2 -m4-single -ffp-contract=off; no fast-math\n"
        "Display: 640x480, 16 colours, software planar VDI\n"
        "Timer: monotonic milliseconds; warmup/calibration >=16ms per batch.\n"
        "Three samples, each >=250ms measured work; median and min/max rates.\n"
        "Event polling/presentation between batches excluded; IRQs enabled.\n"
        "CPU: dependent xorshift32 or FP multiply+add updates; not MIPS/FLOPS.\n"
        "Memory: 256 KiB per buffer, normal cached RAM. Copy counts payload once.\n"
        "Read sums volatile words; write stores volatile words; copy uses memcpy.\n"
        "VDI: toolkit attribute calls included; RGB565 presentation excluded.\n"
        "Lines span 608 pixels; fills 80x32; text 24 characters, transparent.\n"
        "Files: repeat 256 KiB, 16 KiB requests; seek included, open/close excluded.\n"
        "C: RAM disk only. D: BENCH.DAT repeated reads may use filesystem/emulator\n"
        "caches; this is not a raw GD-ROM speed measurement.\n"
        "Compare the same benchmark/build/settings. Flycast rates are not\n"
        "measurements of physical Dreamcast performance.\n\n", __VERSION__);
    for (int i = 0; i < ROWS; i++) {
        Test *t = &tests[i];
        fprintf(f, "%s: %s", t->name, state_name(t->state));
        if (t->state == DONE) {
            fprintf(f, " %.3f %s (min %.3f, max %.3f)\n  raw units/ms:",
                    t->result.rate[1]*t->scale, t->unit,
                    t->result.rate[0]*t->scale, t->result.rate[2]*t->scale);
            for (int n = 0; n < BENCH_SAMPLES; n++)
                fprintf(f, " %llu/%lu", (unsigned long long)t->result.units[n], (unsigned long)t->result.ms[n]);
        }
        if (t->detail) fprintf(f, " (%s)", t->detail);
        fputc('\n', f);
    }
    fputs("\nC:\\BENCH.TXT disappears at reset or power-off.\n", f);
    int ok = !ferror(f);
    if (fclose(f)) ok = 0;
    return ok;
}
int app_main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!app_begin("Native Dreamcast benchmark")) return 1;
    atexit(cleanup);
    if (dc_os->size < offsetof(struct dc_native_api, millis) + sizeof(dc_os->millis) || !dc_os->millis) {
        app_alert("This OS has no benchmark timer. Rebuild the OS and BENCH.PRG together.");
        return 1;
    }
    source = malloc(BLOCK); target = malloc(BLOCK);
    if (!source || !target) { app_alert("Not enough memory for two 256 KiB buffers."); return 1; }
    for (unsigned i = 0; i < BLOCK; i++) ((unsigned char *)source)[i] = i & 255;
    get_info();
    draw();
    for (;;) {
        int key = app_key() & 255;
        if (key == 27 || key == 'q' || key == 'Q') break;
        if (key == 'r' || key == 'R' || key == 13) run_all();
        if (key == 'e' || key == 'E') { environment = (environment + 1) % 3; draw(); }
        if (key == 's' || key == 'S') {
            notice = save_report("C:\\BENCH.TXT") ? "Saved C:\\BENCH.TXT (RAM disk; lost at reset). Esc: exit" :
                                                   "Could not save C:\\BENCH.TXT. Esc: exit";
            draw();
        }
    }
    return 0;
}
