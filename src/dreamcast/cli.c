/* Native entry and BIOS bridge for the upstream EmuCON2 C sources.
 * GPL-2.0-or-later. No Atari basepage startup or low-memory accesses. */
#include "emutos.h"
#include "fs.h"
#include "bdosstub.h"
#include "lineavars.h"
#include "biosbind.h"
#include "xbiosbind.h"
#include "gemerror.h"
#include "string.h"
#include "../../upstream/emutos/cli/clistub.h"
#include <stdarg.h>

extern int cmdmain(void);
extern void exit_cmdedit(void);
extern char *ad_envrn;
extern LONG trap1(int, ...);
extern void panic(const char *, ...);
char *environment;
static const char *test_input;
static char test_output[4096];
static unsigned test_output_len;

void coma_start(void)
{
    DTA console_dta;
    DTA *saved_dta = run->p_xdta;
    char *saved_env = run->p_env;
    static char empty_env[2];
    environment = ad_envrn ? ad_envrn : empty_env;
    run->p_env = environment;
    run->p_xdta = &console_dta;
    if (!test_input) kprintf("EmuCON: enter\n");
    cmdmain();
    exit_cmdedit();
    run->p_xdta = saved_dta;
    run->p_env = saved_env;
    if (!test_input) kprintf("EmuCON: return to desktop\n");
}

ULONG getwh(void)
{
    return ((ULONG)(UWORD)v_cel_mx << 16) | (UWORD)v_cel_my;
}
WORD getht(void)
{
    return v_cel_ht;
}

/* Only the calls used by EmuCON belong here; all GEMDOS calls go directly
 * through trap1, with native C default argument promotions. */
LONG jmp_bios(WORD op, ...)
{
    va_list args;
    va_start(args, op);
    int device = va_arg(args, int);
    LONG result;
    if (test_input) {
        result = 0;
        if (op == 2) {
            if (!*test_input) panic("SELFTEST: EmuCON exhausted input\n");
            result = (UBYTE)*test_input++;
            if (result == 1) result = 0x00480000L; /* Up: command history */
            if (result == '\t') result = 0x000f0009L;
        } else if (op == 3) {
            int ch = va_arg(args, int);
            if (test_output_len < sizeof(test_output)-1)
                test_output[test_output_len++] = ch;
        }
        va_end(args);
        return result;
    }
    if (op == 3)
        result = bios_l_ww(op, device, va_arg(args, int));
    else
        result = bios_l_w(op, device);
    va_end(args);
    return result;
}
LONG jmp_xbios(WORD op, ...)
{
    va_list args;
    va_start(args, op);
    LONG result = EINVFN;
    if (op == 4)
        result = xbios_w_v(op);
    else if (op == 38)
        result = xbios_l_l(op, va_arg(args, long));
    else if (op == 7 || op == 21 || op == 35) {
        int a = va_arg(args, int);
        result = xbios_w_ww(op, a, va_arg(args, int));
    }
    va_end(args);
    return result;
}

static void check_file(const char *path, const char *expected)
{
    char buffer[256];
    LONG h = trap1(0x3d, path, 0);
    LONG n = h < 0 ? h : trap1(0x3f, (int)h, (LONG)sizeof(buffer), buffer);
    if (h >= 0) trap1(0x3e, (int)h);
    if (n != (LONG)strlen(expected) || memcmp(buffer, expected, n)) {
        test_output[test_output_len] = 0;
        panic("SELFTEST: EmuCON file %s mismatch (%ld)\n%s\n", path, n, test_output);
    }
    trap1(0x41, path);
}
void dc_cli_selftest(void)
{
    LONG h = trap1(0x3d, "D:\\UTILS\\WC.TTP", 0);
    if (h < 0) return; /* direct ELF boot has no optional utility bundle */
    trap1(0x3e, (int)h);
    const char *script =
        "echo alpha beta > C:\\CLISRC.TXT\r"
        "copy C:\\CLISRC.TXT C:\\CLICOPY.TXT\r"
        "rm C:\\CLICOPY.TXT > D:\\FORBID.TXT\r" /* failed redirect must not run rm */
        "wc -l C:\\CLICOPY.TXT > C:\\CLIWC.TXT\r"
        "grep -n beta C:\\CLISRC.TXT > C:\\CLIGREP.TXT\r"
        "expr 6*7 > C:\\CLIEXPR.TXT\r"
        "echo history > C:\\CLIHIST.TXT\r\001\r" /* recall and rerun */
        "echo completed > C:\\CLICOM.TXT\r"
        "type C:\\CLICOM\t > C:\\CLITAB.TXT\r" /* filename tab completion */
        "exit\r";
    LONG memory = trap1(0x48, -1L);
    DTA *saved_dta = run->p_xdta;
    for (int round = 0; round < 3; round++) {
        int handles = 0, refs = 0;
        for (int i = 0; i < OPNFILES; i++) {
            handles += sft[i].f_own != NULL;
            refs += sft[i].f_use;
        }
        test_input = script;
        test_output_len = 0;
        coma_start();
        if (*test_input) panic("SELFTEST: EmuCON exited before script end\n");
        test_input = NULL;
        check_file("C:\\CLISRC.TXT", "alpha beta\r\n");
        check_file("C:\\CLICOPY.TXT", "alpha beta\r\n");
        check_file("C:\\CLIWC.TXT", "1 C:\\CLICOPY.TXT\n");
        check_file("C:\\CLIGREP.TXT", "1:alpha beta\n");
        check_file("C:\\CLIEXPR.TXT", "42\n");
        check_file("C:\\CLIHIST.TXT", "history\r\n");
        check_file("C:\\CLICOM.TXT", "completed\r\n");
        check_file("C:\\CLITAB.TXT", "completed\r\n");
        for (int i = 0; i < OPNFILES; i++) {
            handles -= sft[i].f_own != NULL;
            refs -= sft[i].f_use;
        }
        if (trap1(0x48, -1L) != memory || handles || refs || run->p_xdta != saved_dta ||
            run->p_uft[1] != -1)
            panic("SELFTEST: EmuCON leaked memory/handles/DTA (%d, %d)\n", handles, refs);
    }
    kprintf("SELFTEST: EmuCON commands/PATH/native tools/redirection/history/TAB/reentry/cleanup PASS\n");
}
