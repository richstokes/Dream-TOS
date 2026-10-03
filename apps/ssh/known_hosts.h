#ifndef DC_SSH_KNOWN_HOSTS_H
#define DC_SSH_KNOWN_HOSTS_H
#include <stddef.h>
int ssh_host_check(const char *path,const char *host,unsigned port,const unsigned char *key,size_t size);
#endif
