/* Exact-sized command tails, including overflow followed by another argument. */
#define STANDALONE_CONSOLE
#include "../upstream/emutos/cli/cmdexec.c"
#include <assert.h>
int main(void)
{
    char tail[CMDLINELEN], a[130], b[] = "b";
    char *args[] = {"test", a, b};
    memset(a, 'x', sizeof(a));
    a[MAXCMDLINE] = 0;
    assert(build_cmdline(tail,2,args) == 0 && tail[0] == MAXCMDLINE);
    assert(build_cmdline(tail,3,args) < 0);
    a[MAXCMDLINE] = 'x'; a[MAXCMDLINE+1] = 0;
    assert(build_cmdline(tail,3,args) < 0);
    a[MAXCMDLINE-2] = 0;
    assert(build_cmdline(tail,3,args) == 0 && tail[0] == MAXCMDLINE);
    return 0;
}
