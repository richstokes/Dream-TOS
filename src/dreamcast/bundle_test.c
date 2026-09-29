/* Optional bundled-app runtime smoke test. GPL-2.0-or-later. */
#include "emutos.h"
#include "bdosstub.h"
#include "string.h"
extern void panic(const char *, ...);
extern long trap1(int, ...);
extern long trap1_pexec(short, const char *, const char *, const char *);
static void stream_test(void)
{
    const char *paths[] = {"C:\\CLIIN.TXT", "C:\\CLIOUT.TXT", "C:\\CLIERR.TXT"};
    int saved[3], handles[3];
    long memory = trap1(0x48, -1L);
    for (int i = 0; i < 3; i++) {
        saved[i] = trap1(0x45, i);
        handles[i] = trap1(0x3c, paths[i], 0);
        if (saved[i] < 0 || handles[i] < 0) panic("SELFTEST: stream setup failed\n");
        if (!i) {
            if (trap1(0x40, handles[i], 11L, "alpha beta\n") != 11 ||
                trap1(0x42, 0L, handles[i], 0)) panic("SELFTEST: stdin fixture failed\n");
        }
        if (trap1(0x46, i, handles[i])) panic("SELFTEST: Fforce failed\n");
    }
    const char tail[] = {6, 'S', 'T', 'R', 'E', 'A', 'M', 0};
    long result = trap1_pexec(0, "D:\\RUNTIME.PRG", tail, NULL);
    for (int i = 0; i < 3; i++) {
        if (trap1(0x3e, i) || trap1(0x46, i, saved[i]) || trap1(0x3e, saved[i]) ||
            trap1(0x3e, handles[i])) panic("SELFTEST: stream restoration failed\n");
    }
    const char *expected[] = {"alpha beta\n", "OUT:alpha beta\n", "ERR:separate\n"};
    for (int i = 0; i < 3; i++) {
        char buf[64];
        long h = trap1(0x3d, paths[i], 0);
        long n = h < 0 ? h : trap1(0x3f, (int)h, (long)sizeof(buf), buf);
        if (h >= 0) trap1(0x3e, (int)h);
        if (n != (long)strlen(expected[i]) || memcmp(buf, expected[i], n))
            panic("SELFTEST: redirected stream %d mismatch (%ld)\n", i, n);
        trap1(0x41, paths[i]);
    }
    if (result || trap1(0x48, -1L) != memory)
        panic("SELFTEST: stdio inheritance/cleanup failed: %ld\n", result);
    kprintf("SELFTEST: native stdin/stdout/stderr inheritance/redirection/flush PASS\n");
}
void dc_bundle_selftest(void)
{
    long h = trap1(0x3d, "D:\\RUNTIME.PRG", 0);
    if (h < 0)
        return; /* Direct ELF boot, or a disc without the optional bundle. */
    trap1(0x3e, (int)h);
    static const char tail[] = {4, 'T', 'E', 'S', 'T', 0};
    long before = trap1(0x48, -1L);
    long result = trap1_pexec(0, "D:\\RUNTIME.PRG", tail, NULL);
    if (result || trap1(0x48, -1L) != before)
        panic("Native app runtime selftest failed: %ld\n", result);
    kprintf("SELFTEST: native newlib allocation/stdio/math/read-only/cleanup PASS\n");
    stream_test();
}
