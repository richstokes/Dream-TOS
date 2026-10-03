/* Non-blocking IPv4 TCP service for native foreground apps. GPL-2.0-or-later. */
#ifndef EMUTOS_DC_TCP_H
#define EMUTOS_DC_TCP_H
#include <stdint.h>
#define DC_TCP_AGAIN -8
#define DC_TCP_IO -9
#define DC_TCP_ARGS -64
/* Handles are positive, private to this service (never KOS descriptors).
 * listen(0) selects an ephemeral port; port(handle) returns its host-order port.
 * accept returns a handle and peer IPv4 bytes, or AGAIN if none is waiting.
 * recv/send return bytes (possibly partial), AGAIN, or another negative error.
 * recv == 0 is EOF. Calls never wait for a peer or for buffer space.
 * At most eight handles; close them on exit. The loader also closes all of
 * them at foreground Pterm. This service is not for resident accessories. */
long dc_tcp_listen(uint32_t port);
long dc_tcp_accept(int handle, uint8_t peer[4]);
long dc_tcp_recv(int handle, void *buffer, uint32_t bytes);
long dc_tcp_send(int handle, const void *buffer, uint32_t bytes);
long dc_tcp_port(int handle);
long dc_tcp_close(int handle);
/* Outbound connection: returns a handle immediately. Poll connected() until
 * 0 (connected), AGAIN (pending) or IO (failed); then close on any failure. */
long dc_tcp_connect(const uint8_t ip[4], uint32_t port);
long dc_tcp_connected(int handle);
void dc_tcp_cleanup(void);
#endif
