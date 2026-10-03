/* Fixed portable crypto/SSH configuration. GPL-3.0-or-later. */
#ifndef DC_SSH_USER_SETTINGS_H
#define DC_SSH_USER_SETTINGS_H
#include <strings.h>
#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_ASM
#define WOLFSSL_SP_MATH_ALL
#define WOLFSSL_SP_SMALL
#define WOLFSSL_SP_NO_64BIT
#define SP_INT_BITS 4096
#define NO_DH
#define NO_DSA
#define NO_MD4
#define NO_MD5
#define NO_SHA
#define NO_RC4
#define NO_DES3
#define NO_PSK
#define NO_PWDBASED
#define NO_PKCS12
#define NO_PKCS8
#define NO_ASN_TIME
#define WOLFSSL_PEM_TO_DER
#define WOLFSSL_KEY_GEN
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING
#define HAVE_ECC256
#define HAVE_ECC384
#define HAVE_ECC521
#define HAVE_CURVE25519
#define CURVE25519_SMALL
#define HAVE_ED25519
#define ED25519_SMALL
#define WOLFSSL_ED25519_STREAMING_VERIFY
#define WOLFSSL_SHA512
#define WOLFSSL_SHA384
#define WOLFSSL_AES_COUNTER
#define HAVE_AESGCM
#define GCM_SMALL
#define WOLFSSL_BASE64_ENCODE
#define HAVE_HASHDRBG
#define WC_NO_CONSTRUCTORS
int ssh_entropy(unsigned char *out, unsigned int size);
#define CUSTOM_RAND_GENERATE_SEED ssh_entropy
unsigned int ssh_choose_auth(void *, unsigned int, int);
#define WOLFSSH_CLIENT_AUTH_SELECT ssh_choose_auth
#define NO_WOLFSSH_SERVER
#define WOLFSSH_USER_IO
#define WOLFSSH_TERM
#define NO_TERMIOS
#define WOLFSSH_KEYBOARD_INTERACTIVE
#define WOLFSSH_NO_AES_CBC
#define WOLFSSH_NO_DH
#define DEFAULT_WINDOW_SZ 32768
#define DEFAULT_MAX_PACKET_SZ 16384
#define WOLFSSH_DC_TERMINAL
#endif
