/* KOS socket boundary; compile with normal SDK alignment. GPL-2.0-or-later.
 * Uses POSIX socket calls so the same implementation is exercised on the host. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include "dreamcast/tcp.h"

static int descriptors[8]; /* fd + 1; zero means unused */
static int connecting[8];
static int descriptor(int h)
{
    return h > 0 && h <= 8 && descriptors[h - 1] ? descriptors[h - 1] - 1 : -1;
}
static long failure(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? DC_TCP_AGAIN : DC_TCP_IO;
}
static long remember(int fd)
{
    if (fd < 0) return failure();
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return DC_TCP_IO; }
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    for (int i = 0; i < 8; i++) {
        if (!descriptors[i]) { descriptors[i] = fd + 1; connecting[i] = 0; return i + 1; }
    }
    close(fd);
    return DC_TCP_IO;
}
long dc_tcp_listen(uint32_t port)
{
    if (port > 65535) return DC_TCP_ARGS;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return failure();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    int one = 1;
    /* KOS may not implement this option; binding still reports port conflicts. */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 2) < 0) {
        close(fd);
        return DC_TCP_IO;
    }
    return remember(fd);
}
long dc_tcp_accept(int handle, uint8_t peer[4])
{
    int fd = descriptor(handle);
    if (fd < 0 || !peer) return DC_TCP_ARGS;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int accepted = accept(fd, (struct sockaddr *)&addr, &len);
    if (accepted < 0) return failure();
    memcpy(peer, &addr.sin_addr.s_addr, 4);
    return remember(accepted);
}
long dc_tcp_port(int handle)
{
    int fd = descriptor(handle);
    if (fd < 0) return DC_TCP_ARGS;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    return getsockname(fd, (struct sockaddr *)&addr, &len) < 0 ? DC_TCP_IO : ntohs(addr.sin_port);
}
long dc_tcp_recv(int handle, void *buffer, uint32_t bytes)
{
    int fd = descriptor(handle);
    if (fd < 0 || !buffer || !bytes || bytes > 65536) return DC_TCP_ARGS;
    long rc = recv(fd, buffer, bytes, 0);
    return rc < 0 ? failure() : rc;
}
long dc_tcp_send(int handle, const void *buffer, uint32_t bytes)
{
    int fd = descriptor(handle);
    if (fd < 0 || !buffer || !bytes || bytes > 65536) return DC_TCP_ARGS;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    long rc = send(fd, buffer, bytes, flags);
    return rc < 0 ? failure() : rc;
}
long dc_tcp_close(int handle)
{
    int fd = descriptor(handle);
    if (fd < 0) return DC_TCP_ARGS;
    descriptors[handle - 1] = 0;
    connecting[handle - 1] = 0;
    return close(fd) < 0 ? DC_TCP_IO : 0;
}
long dc_tcp_connect(const uint8_t ip[4], uint32_t port)
{
    if (!ip || !port || port > 65535) return DC_TCP_ARGS;
    long h = remember(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (h < 0) return h;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr.s_addr, ip, 4);
    if (connect(descriptor(h), (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EALREADY) {
            dc_tcp_close(h); return DC_TCP_IO;
        }
        connecting[h - 1] = 1;
    }
    return h;
}
long dc_tcp_connected(int handle)
{
    int fd = descriptor(handle);
    if (fd < 0) return DC_TCP_ARGS;
    if (!connecting[handle - 1]) return 0;
    struct pollfd p = {fd, POLLOUT, 0};
    int rc = poll(&p, 1, 0);
    if (rc < 0) return failure();
    if (!rc) return DC_TCP_AGAIN;
    int error = 0;
    socklen_t len = sizeof(error);
    if ((p.revents & (POLLERR | POLLHUP | POLLNVAL)) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error)
        return DC_TCP_IO;
    if (!(p.revents & POLLOUT)) return DC_TCP_AGAIN;
    connecting[handle - 1] = 0;
    return 0;
}
void dc_tcp_cleanup(void)
{
    for (int i = 1; i <= 8; i++) if (descriptor(i) >= 0) dc_tcp_close(i);
}
