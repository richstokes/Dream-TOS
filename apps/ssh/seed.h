#ifndef DC_SSH_SEED_H
#define DC_SSH_SEED_H
#include <stdint.h>
int ssh_seed_open(const char *path);
/* Only call after the user explicitly accepts weak randomness for this run. */
int ssh_seed_insecure(uint32_t prompt_started);
void ssh_seed_close(void);
#endif
