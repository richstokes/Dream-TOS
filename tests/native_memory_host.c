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

/* GCC ASan retains native.c's API table even with --gc-sections. Supply its
 * service dependencies without linking the console hardware into this test.
 * None belongs to memory ownership: fail immediately if one is ever called. */
#define UNUSED_SERVICE(result, name, args) \
    result name args { fputs("Unexpected native service: " #name "\n", stderr); abort(); }
UNUSED_SERVICE(long, trap1, (int opcode, ...))
UNUSED_SERVICE(LONG, super, (WORD opcode, void *pb))
UNUSED_SERVICE(void, dc_vdi, (void *pb))
UNUSED_SERVICE(void, dc_poll, (void))
UNUSED_SERVICE(unsigned long, dc_millis, (void))
UNUSED_SERVICE(long, dc_system_info, (void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_input_config, (int write, void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_input_snapshot, (void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_vmu_info, (uint32_t port, uint32_t unit, void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_control_store, (int write, uint32_t port, uint32_t unit, void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_net_info, (void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_net_ping, (const uint8_t ip[4], uint32_t seq, uint32_t size,
                                  uint32_t timeout_ms, void *result, uint32_t bytes))
UNUSED_SERVICE(long, dc_net_resolve, (const char *host, uint32_t timeout_ms, uint8_t ip[4]))
UNUSED_SERVICE(long, dc_audio_open, (uint32_t rate, uint32_t channels))
UNUSED_SERVICE(long, dc_audio_close, (void))
UNUSED_SERVICE(long, dc_audio_write, (const int16_t *pcm, uint32_t frames))
UNUSED_SERVICE(long, dc_audio_space, (void))
UNUSED_SERVICE(long, dc_audio_set, (uint32_t what, uint32_t value))
UNUSED_SERVICE(long, dc_audio_info, (void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_vmu_file_read, (uint32_t port, uint32_t unit, const char *name,
                                      void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_vmu_file_write, (uint32_t port, uint32_t unit, const char *name,
                                       const void *data, uint32_t bytes, uint32_t flags))
UNUSED_SERVICE(long, dc_vmu_file_delete, (uint32_t port, uint32_t unit, const char *name))
UNUSED_SERVICE(long, dc_vmu_screen, (uint32_t port, uint32_t unit, const void *bitmap, uint32_t bytes))
UNUSED_SERVICE(long, dc_tcp_listen, (uint32_t port))
UNUSED_SERVICE(long, dc_tcp_accept, (int handle, uint8_t peer[4]))
UNUSED_SERVICE(long, dc_tcp_recv, (int handle, void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_tcp_send, (int handle, const void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_tcp_port, (int handle))
UNUSED_SERVICE(long, dc_tcp_close, (int handle))
UNUSED_SERVICE(long, dc_tcp_connect, (const uint8_t ip[4], uint32_t port))
UNUSED_SERVICE(long, dc_tcp_connected, (int handle))
UNUSED_SERVICE(long, dc_sd_card_info, (void *buffer, uint32_t bytes))
UNUSED_SERVICE(long, dc_sd_card_format, (uint32_t target, uint32_t action))
#undef UNUSED_SERVICE

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
