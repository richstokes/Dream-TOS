/* Real FTP engine, GEMDOS adapter and TCP boundary against host files/sockets. */
#include "app.h"
#include "ftp_core.h"
#include "dreamcast/tcp.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static unsigned char default_dta[44], *dta = default_dta;
/* Search position lives in each emulated DTA, just as it does in GEMDOS. */
static char searches[32][512];
static unsigned search_id;
static int fail_write, fail_read, fail_close, foreign_peer;
static unsigned calls;
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping = 1; }
static void native_path(char *out, const char *in)
{
    assert(in[0] >= 'C' && in[0] <= 'H' && in[1] == ':' && in[2] == '\\');
    snprintf(out, 512, "%c/%s", in[0], in + 3);
    for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
}
static long dos_error(void)
{
    return errno == ENOENT ? -33 : errno == ENOTDIR ? -34 : -36;
}
static void fill(const char *name, const struct stat *st)
{
    memset(dta + 21, 0, 23);
    dta[21] = S_ISDIR(st->st_mode) ? 16 : 0;
    uint32_t size = (uint32_t)st->st_size;
    memcpy(dta + 26, &size, 4);
    snprintf((char *)dta + 30, 14, "%.12s", name);
}
static long next(void)
{
    unsigned id, index;
    memcpy(&id, dta, 4); memcpy(&index, dta + 4, 4);
    assert(id < 32);
    DIR *dir = opendir(searches[id]);
    if (!dir) return dos_error();
    struct dirent *e;
    unsigned i = 0;
    long result = -49;
    while ((e = readdir(dir))) {
        if (i++ < index) continue;
        char path[1024]; struct stat st;
        snprintf(path, sizeof(path), "%s/%s", searches[id], e->d_name);
        if (!stat(path, &st)) {
            fill(e->d_name, &st);
            memcpy(dta + 4, &i, 4);
            result = 0;
        }
        break;
    }
    closedir(dir);
    return result;
}
static long gemdos(int op, ...)
{
    va_list args;
    va_start(args, op);
    char path[512]; long result = 0;
    switch (op) {
    case 0x2f: result = (long)dta; break;
    case 0x1a: dta = va_arg(args, unsigned char *); break;
    case 0x36: {
        uint32_t *space = va_arg(args, uint32_t *); int drive = va_arg(args, int);
        snprintf(path, sizeof(path), "%c", 'A' + drive - 1);
        struct stat st;
        result = stat(path, &st) ? -46 : 0;
        memset(space, 0, 16);
        break;
    }
    case 0x4e: {
        native_path(path, va_arg(args, const char *));
        (void)va_arg(args, int);
        char *wild = strstr(path, "*.*");
        if (wild) {
            assert(wild > path && wild[-1] == '/'); wild[-1] = 0;
            unsigned id = search_id++ % 32, index = 0;
            strcpy(searches[id], path);
            memset(dta, 0, 44);
            memcpy(dta, &id, 4); memcpy(dta + 4, &index, 4);
            result = next();
            if (result == -49) result = -33;
        } else {
            struct stat st;
            if (stat(path, &st)) result = dos_error();
            else fill(strrchr(path, '/') + 1, &st);
        }
        break;
    }
    case 0x4f: result = next(); break;
    case 0x3d: case 0x3c:
        native_path(path, va_arg(args, const char *));
        (void)va_arg(args, int);
        result = open(path, op == 0x3d ? O_RDONLY : O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (result < 0) result = dos_error();
        break;
    case 0x3e:
        result = close(va_arg(args, int));
        if (fail_close) result = -36;
        break;
    case 0x3f: case 0x40: {
        int h = va_arg(args, int); long bytes = va_arg(args, long); void *buf = va_arg(args, void *);
        if ((op == 0x3f && fail_read) || (op == 0x40 && fail_write)) result = -36;
        else result = op == 0x3f ? read(h, buf, bytes) : write(h, buf, bytes);
        break;
    }
    case 0x39: case 0x3a: case 0x41:
        native_path(path, va_arg(args, const char *));
        result = op == 0x39 ? mkdir(path, 0700) : op == 0x3a ? rmdir(path) : unlink(path);
        break;
    case 0x56: {
        (void)va_arg(args, int);
        native_path(path, va_arg(args, const char *));
        char other[512]; native_path(other, va_arg(args, const char *));
        result = rename(path, other);
        break;
    }
    default: assert(!"Unexpected GEMDOS opcode");
    }
    va_end(args);
    return result;
}
static long partial_send(int h, const void *b, uint32_t n)
{
    if (++calls % 5 == 0) return DC_TCP_AGAIN;
    return dc_tcp_send(h, b, n > 73 ? 73 : n);
}
static long accept_peer(int h, uint8_t peer[4])
{
    long result = dc_tcp_accept(h, peer);
    /* Portable peer-mismatch fixture: macOS may not route 127.0.0.2. */
    if (result > 0 && foreign_peer) { peer[3] ^= 1; foreign_peer = 0; }
    return result;
}
static const struct dc_native_api api = {
    .size = sizeof(api), .gemdos = gemdos, .tcp_listen = dc_tcp_listen, .tcp_accept = accept_peer,
    .tcp_recv = dc_tcp_recv, .tcp_send = partial_send, .tcp_port = dc_tcp_port, .tcp_close = dc_tcp_close
};
const struct dc_native_api *dc_os = &api;
static uint32_t millis(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void selftest(void)
{
    char p[FTP_PATH], n[FTP_PATH];
    assert(ftp_path("/MUSIC", "../SAVE/a.bin", p) && !strcmp(p, "/SAVE/A.BIN"));
    assert(ftp_path("/MUSIC", "/one/./two/../a", p) && !strcmp(p, "/ONE/A"));
    assert(ftp_native("E:\\SHARED", p, n) && !strcmp(n, "E:\\SHARED\\ONE\\A"));
    const char *bad[] = {"..", "../../A", "E:/FILE", "..\\FILE", "A:B", "A*", "A?", "FOO.",
        "TOOLONGGG.TXT", "A.LONG", ".HIDDEN", "A..B", "A B", "A\rB", "A\nB", "A;B", NULL};
    for (int i = 0; bad[i]; i++) assert(!ftp_path("/", bad[i], p));
    memset(n, 'A', 127); n[127] = 0; assert(!ftp_native(n, "/X", p));
    assert(dc_tcp_listen(65536) == DC_TCP_ARGS);
    assert(dc_tcp_close(0) == DC_TCP_ARGS && dc_tcp_close(99) == DC_TCP_ARGS);
    int h[8]; uint8_t peer[4]; char b;
    for (int i = 0; i < 8; i++) { h[i] = dc_tcp_listen(0); assert(h[i] > 0); }
    assert(dc_tcp_listen(0) == DC_TCP_IO);
    assert(dc_tcp_accept(h[0], peer) == DC_TCP_AGAIN);
    assert(dc_tcp_recv(-1, &b, 1) == DC_TCP_ARGS);
    dc_tcp_cleanup();
    for (int i = 0; i < 8; i++) assert(dc_tcp_close(h[i]) == DC_TCP_ARGS);
    puts("ftp unit checks passed");
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--selftest")) { selftest(); return 0; }
    signal(SIGTERM, stop);
    FtpServer server; ftp_init(&server);
    uint8_t ip[] = {127, 0, 0, 1};
    assert(ftp_start(&server, argc > 1 ? argv[1] : "C:\\SHARE", argc > 2 ? atoi(argv[2]) : 1, ip, 0));
    printf("%ld\n", dc_tcp_port(server.listener)); fflush(stdout);
    uint32_t advance = 0;
    while (!stopping) {
        struct pollfd in = {.fd = 0, .events = POLLIN};
        if (poll(&in, 1, 0) > 0) {
            char command;
            if (read(0, &command, 1) <= 0 || command == 'S') break;
            if (command == 'T') advance += 31000;
            if (command == 'I') advance += 301000;
            if (command == 'L') advance += 20000;
            if (command == 'P') foreign_peer = 1;
            if (command == 'W') fail_write = 1;
            if (command == 'R') fail_read = 1;
            if (command == 'C') fail_close = 1;
        }
        ftp_poll(&server, millis() + advance);
        assert(dta == default_dta);
        usleep(1000);
    }
    ftp_stop(&server);
    dc_tcp_cleanup();
    assert(server.file == -1 && !server.listener && !server.control && !server.data && !server.passive);
    return 0;
}
