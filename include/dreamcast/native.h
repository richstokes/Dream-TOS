/* Native application ABI v1. GPL-2.0-or-later. SH-4 little endian, GCC m4-single.
 * GEM WORD is int16_t; pointers/long/int are 32 bits. GEM structs use pack(2).
 * No traps or Motorola register conventions cross this interface. */
#ifndef EMUTOS_DC_NATIVE_H
#define EMUTOS_DC_NATIVE_H
#include <stdint.h>
#define DC_NATIVE_ABI 1
struct dc_native_api {
    uint32_t version;
    uint32_t size;
    long (*gemdos)(int opcode, ...);
    void (*aes)(void *parameter_block);
    void (*vdi)(void *parameter_block);
    void (*yield)(void);
    /* Optional extension; check size before accessing. Monotonic milliseconds. */
    unsigned long (*millis)(void);
};
/* Return a GEMDOS exit status. tail is the standard length-prefixed command
 * line; env is a double-NUL-terminated environment, owned by the caller. */
typedef long (*dc_native_entry)(const struct dc_native_api *, const char *tail, const char *env);
#endif
