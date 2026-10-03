/* Compatibility shim for the unchanged OpenBSD bcrypt code in libssh2. */
#ifndef DC_BCRYPT_COMPAT_H
#define DC_BCRYPT_COMPAT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wolfssl/wolfcrypt/sha512.h>
#define SHA512_DIGEST_LENGTH 64
#define LIBSSH2_MIN(a,b) ((a)<(b)?(a):(b))
typedef wc_Sha512 libssh2_sha512_ctx;
#define libssh2_sha512_init(c) (wc_InitSha512(c) == 0)
#define libssh2_sha512_update(c,p,n) (wc_Sha512Update(&(c),(const byte *)(p),(word32)(n)) == 0)
#define libssh2_sha512_final(c,p) (wc_Sha512Final(&(c),(p)) == 0)
static inline void _libssh2_explicit_zero(void *p, size_t n)
{ volatile unsigned char *v = p; while(n--) *v++ = 0; }
#endif
