#ifndef DC_SSH_KEYS_H
#define DC_SSH_KEYS_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned char *private_key, *public_key;
    unsigned private_size, public_size;
    char type[48];
} SshKey;
int ssh_key_load(const char *,SshKey *);
void ssh_key_free(SshKey *);
#endif
