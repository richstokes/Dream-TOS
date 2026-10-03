/* Platform boundary for the command-line SSH client. GPL-3.0-or-later. */
#ifndef DC_SSH_PLATFORM_H
#define DC_SSH_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
uint32_t ssh_now(void);
uint32_t ssh_wallclock(void); /* Public RTC value, not cryptographic entropy. */
void ssh_make_parent(const char *path); /* Best-effort single parent directory. */
void ssh_idle(void);
int ssh_key(void); /* TOS scan code in bits 16..23, ASCII in bits 0..7; 0 if none */
unsigned ssh_modifiers(void); /* libvterm: Shift=1, Alt=2, Ctrl=4 */
void ssh_output(const char *, size_t);
int ssh_network_ready(void);
int ssh_resolve_target(const char *, uint8_t[4]);
int ssh_resolve(const char *, uint8_t[4]);
long ssh_tcp_connect(const uint8_t[4], unsigned);
long ssh_tcp_connected(int);
long ssh_tcp_read(int, void *, unsigned);
long ssh_tcp_write(int, const void *, unsigned);
void ssh_tcp_close(int);
char ssh_default_drive(void);
void ssh_wipe(void *, size_t);
void ssh_print(const char *);
void ssh_safe_text(const unsigned char *, size_t);
int ssh_prompt(const char *, char *, size_t, int echo);
int ssh_main(int, char **);
#endif
