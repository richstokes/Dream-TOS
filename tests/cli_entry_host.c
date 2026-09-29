/* The native entry must return: NORETURN lets GCC discard the AES continuation. */
#define MACHINE_DREAMCAST
#include "../upstream/emutos/include/portab.h"
#include "../upstream/emutos/cli/clistub.h"
#include <assert.h>
static volatile int calls;
void coma_start(void) { calls++; }
int main(void)
{
    coma_start(); coma_start();
    assert(calls == 2);
    return 0;
}
