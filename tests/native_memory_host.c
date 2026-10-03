/* Accessory heaps survive foreground Pterm and are reclaimed on unload. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/dreamcast/native.c"
#include "../src/dreamcast/memory.c"

static PD foreground;
PD *run = &foreground;
AESPD *rlr;
void *dc_alloc(size_t bytes) { return calloc(1, bytes); }
void dc_free(void *p) { free(p); }

int main(void)
{
    assert(dc_native_memory_owner() == run);
    void *p = xmalloc(128);
    assert(p);
    dc_free_process_memory(run);
    long initial = (long)xmalloc(-1);
    AESPD process = {0};
    rlr = &process;
    assert(dc_native_memory_owner() == run);
    for (int pid = 2; pid < NUM_PDS; pid++) {
        struct native_image *image = dc_alloc(sizeof(*image));
        image->code = dc_alloc(32);
        process.p_pid = pid;
        process.p_ldaddr = (LONG)image;
        assert(dc_native_memory_owner() == run); /* Not started yet. */
        acc_active[pid - 2] = 1;
        assert(dc_native_memory_owner() == (PD *)image);
        unsigned char *resident = xmalloc(256);
        assert(resident);
        memset(resident, 0x5a, 256);
        process.p_pid = 0;
        void *app = xmalloc(1024);
        assert(app && dc_native_memory_owner() == run);
        dc_free_process_memory(run);
        assert(xmfree(app) == EIMBA);
        for (int i = 0; i < 256; i++) assert(resident[i] == 0x5a);
        assert((long)xmalloc(-1) < initial);
        process.p_pid = pid;
        /* A second calculation after foreground exit still uses its own heap. */
        void *temporary = xmalloc(512);
        assert(temporary && !xmfree(temporary));
        dc_native_acc_free((LONG)image);
        acc_active[pid - 2] = 0;
        assert((long)xmalloc(-1) == initial);
    }
    rlr = NULL;
    free(arena);
    puts("Native accessory memory survives foreground cleanup and is freed on unload: PASS");
    return 0;
}
