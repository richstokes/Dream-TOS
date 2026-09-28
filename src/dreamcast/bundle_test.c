/* Optional bundled-app runtime smoke test. GPL-2.0-or-later. */
#include "emutos.h"
#include "bdosstub.h"
extern void panic(const char *, ...);
extern long trap1(int, ...);
extern long trap1_pexec(short, const char *, const char *, const char *);
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
}
