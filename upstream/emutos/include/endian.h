/* FAT fields are little endian; byte-built values still use swpw(). */
#ifndef ETOS_ENDIAN_H
#define ETOS_ENDIAN_H
#include "asm.h"
#ifdef MACHINE_DREAMCAST
#define dc_le16(x) ((void)(x))
#define dc_le32(x) ((void)(x))
static inline void dc_le16copy(const UWORD *s,UWORD *d) { *d=*s; }
#else
#define dc_le16 swpw
#define dc_le32 swpl
#define dc_le16copy swpcopyw
#endif
#endif
