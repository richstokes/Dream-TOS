/* Native application ABI v1. GPL-2.0-or-later. SH-4 little endian, GCC m4-single.
 * GEM WORD is int16_t; pointers/long/int are 32 bits. GEM structs use pack(2).
 * No traps or Motorola register conventions cross this interface. */
#ifndef EMUTOS_DC_NATIVE_H
#define EMUTOS_DC_NATIVE_H
#include <stdint.h>
#define DC_NATIVE_ABI 1
/* The OS is built with -fpack-struct=2, while native applications use the
 * normal SH-4 ABI. Preserve four-byte alignment when exporting this table: an
 * application reads its fields with 32-bit loads, which fault at 2 mod 4. */
struct __attribute__((aligned(4))) dc_native_api {
    uint32_t version;
    uint32_t size;
    long (*gemdos)(int opcode, ...);
    void (*aes)(void *parameter_block);
    void (*vdi)(void *parameter_block);
    void (*yield)(void);
    /* Optional extension; check size before accessing. Monotonic milliseconds. */
    unsigned long (*millis)(void);
    /* Optional extension; check size. See dreamcast/system_info.h. */
    long (*system_info)(void *buffer, uint32_t bytes);
    /* Optional extensions; check size. See control.h and vmu_info.h. */
    long (*input_config)(int write, void *buffer, uint32_t bytes);
    long (*input_snapshot)(void *buffer, uint32_t bytes);
    long (*vmu_info)(uint32_t port, uint32_t unit, void *buffer, uint32_t bytes);
    /* Optional named Control Panel save; see settings.h. */
    long (*control_store)(int write, uint32_t port, uint32_t unit, void *buffer, uint32_t bytes);
    /* Optional networking (Broadband Adapter); see net.h. */
    long (*net_info)(void *buffer, uint32_t bytes);
    long (*net_ping)(const uint8_t ip[4], uint32_t seq, uint32_t size, uint32_t timeout_ms, void *result, uint32_t bytes);
    long (*net_resolve)(const char *host, uint32_t timeout_ms, uint8_t ip[4]);

    /* Optional audio output (AICA stream, non-blocking PCM ring); see audio.h. */
    long (*audio_open)(uint32_t rate, uint32_t channels);
    long (*audio_close)(void);
    long (*audio_write)(const int16_t *pcm, uint32_t frames);
    long (*audio_space)(void);
    long (*audio_set)(uint32_t what, uint32_t value);
    long (*audio_info)(void *buffer, uint32_t bytes);
    /* --- Optional VMU file service and LCD; see vmu_file.h. Check size. --- */
    long (*vmu_file_read)(uint32_t port, uint32_t unit, const char *name, void *buffer, uint32_t bytes);
    long (*vmu_file_write)(uint32_t port, uint32_t unit, const char *name, const void *data, uint32_t bytes, uint32_t flags);
    long (*vmu_file_delete)(uint32_t port, uint32_t unit, const char *name);
    long (*vmu_screen)(uint32_t port, uint32_t unit, const void *bitmap, uint32_t bytes);
    /* --- End of VMU file service. --- */
    /* Optional foreground TCP sockets; see tcp.h. Check size and pointer. */
    long (*tcp_listen)(uint32_t port);
    long (*tcp_accept)(int handle, uint8_t peer[4]);
    long (*tcp_recv)(int handle, void *buffer, uint32_t bytes);
    long (*tcp_send)(int handle, const void *buffer, uint32_t bytes);
    long (*tcp_port)(int handle);
    long (*tcp_close)(int handle);
    /* Optional serial SD card formatter; see sd_format.h. Check size/pointer. */
    long (*sd_card_info)(void *buffer, uint32_t bytes);
    long (*sd_card_format)(uint32_t target, uint32_t action);
};
/* Return a GEMDOS exit status. tail is the standard length-prefixed command
 * line; env is a double-NUL-terminated environment, owned by the caller. */
typedef long (*dc_native_entry)(const struct dc_native_api *, const char *tail, const char *env);
#endif
