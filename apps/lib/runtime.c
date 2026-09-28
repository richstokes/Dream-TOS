/* Native SH-4/newlib bridge. GPL-2.0-or-later. No KOS or 68k traps in apps. */
#include "app.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
const struct dc_native_api *dc_os;
/* GEMDOS owns every allocation and reclaims it even after Pterm. Keep a
 * 32-byte header so returned addresses retain the allocator's alignment. */
typedef struct {
    size_t size;
    unsigned char pad[28];
} MemHeader;
void *malloc(size_t n)
{
    if (n > 3 * 1024 * 1024 - sizeof(MemHeader)) {
        errno = ENOMEM;
        return NULL;
    }
    MemHeader *h = (void *)dc_os->gemdos(0x48, (long)(n + sizeof(*h)));
    if (!h) {
        errno = ENOMEM;
        return NULL;
    }
    h->size = n;
    return h + 1;
}
void free(void *p)
{
    if (p)
        dc_os->gemdos(0x49, (MemHeader *)p - 1);
}
void *calloc(size_t n, size_t size)
{
    if (size && n > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    void *p = malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}
void *realloc(void *p, size_t size)
{
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return NULL;
    }
    size_t old = ((MemHeader *)p - 1)->size;
    if (size <= old) {
        ((MemHeader *)p - 1)->size = size;
        return p;
    }
    void *q = malloc(size);
    if (q) {
        memcpy(q, p, old);
        free(p);
    }
    return q;
}
struct _reent;
void *_malloc_r(struct _reent *r, size_t n)
{
    (void)r;
    return malloc(n);
}
void *_calloc_r(struct _reent *r, size_t n, size_t s)
{
    (void)r;
    return calloc(n, s);
}
void *_realloc_r(struct _reent *r, void *p, size_t n)
{
    (void)r;
    return realloc(p, n);
}
void _free_r(struct _reent *r, void *p)
{
    (void)r;
    free(p);
}
/* Apps are single-threaded; AES yields never re-enter this app's libc. */
void __newlib_lock_init(__newlib_lock_t *p)
{
    (void)p;
}
void __newlib_lock_close(__newlib_lock_t *p)
{
    (void)p;
}
void __newlib_lock_acquire(__newlib_lock_t *p)
{
    (void)p;
}
void __newlib_lock_release(__newlib_lock_t *p)
{
    (void)p;
}
void __newlib_lock_init_recursive(__newlib_recursive_lock_t *p)
{
    (void)p;
}
void __newlib_lock_close_recursive(__newlib_recursive_lock_t *p)
{
    (void)p;
}
void __newlib_lock_acquire_recursive(__newlib_recursive_lock_t *p)
{
    (void)p;
}
void __newlib_lock_release_recursive(__newlib_recursive_lock_t *p)
{
    (void)p;
}
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)file;
    (void)line;
    (void)func;
    app_alert(expr);
    exit(1);
}
static int error(long r)
{
    if (r >= 0)
        return r;
    switch (r) {
    case -13:
        errno = EROFS;
        break;
    case -33:
    case -34:
        errno = ENOENT;
        break;
    case -35:
        errno = EMFILE;
        break;
    case -36:
        errno = EACCES;
        break;
    case -37:
        errno = EBADF;
        break;
    case -39:
        errno = ENOMEM;
        break;
    default:
        errno = EIO;
        break;
    }
    return -1;
}
int _open(const char *path, int flags, ...)
{
    int mode = (flags & O_ACCMODE) == O_RDWR ? 2 : (flags & O_ACCMODE) == O_WRONLY ? 1 : 0;
    long h = dc_os->gemdos(0x3d, path, mode);
    if (h >= 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
        dc_os->gemdos(0x3e, (int)h);
        errno = EEXIST;
        return -1;
    }
    if (h >= 0 && (flags & O_TRUNC)) {
        dc_os->gemdos(0x3e, (int)h);
        h = dc_os->gemdos(0x3c, path, 0);
    } else if (h == -33 && (flags & O_CREAT))
        h = dc_os->gemdos(0x3c, path, 0);
    if (h >= 0 && (flags & O_APPEND))
        dc_os->gemdos(0x42, 0L, (int)h, 2);
    return error(h);
}
int _close(int fd)
{
    return fd < 3 ? 0 : error(dc_os->gemdos(0x3e, fd));
}
int _read(int fd, void *buf, size_t n)
{
    return error(dc_os->gemdos(0x3f, fd, (long)n, buf));
}
int _write(int fd, const void *buf, size_t n)
{
    if (fd == 1 || fd == 2) {
        for (size_t i = 0; i < n; i++)
            dc_os->gemdos(2, ((const unsigned char *)buf)[i]);
        return n;
    }
    return error(dc_os->gemdos(0x40, fd, (long)n, buf));
}
off_t _lseek(int fd, off_t off, int whence)
{
    return error(dc_os->gemdos(0x42, (long)off, fd, whence));
}
int _fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    if (fd < 3) {
        st->st_mode = S_IFCHR;
        return 0;
    }
    long pos = dc_os->gemdos(0x42, 0L, fd, 1);
    if (pos < 0)
        return error(pos);
    long len = dc_os->gemdos(0x42, 0L, fd, 2);
    dc_os->gemdos(0x42, pos, fd, 0);
    if (len < 0)
        return error(len);
    st->st_mode = S_IFREG;
    st->st_size = len;
    return 0;
}
int _isatty(int fd)
{
    return fd < 3;
}
int _unlink(const char *p)
{
    return error(dc_os->gemdos(0x41, p));
}
int _getpid(void)
{
    return 1;
}
int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}
void _exit(int status)
{
    dc_os->gemdos(0x4c, status);
    for (;;) {
    }
}
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    unsigned long ms = app_millis();
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
    return 0;
}
unsigned long app_millis(void)
{
    return dc_os->size >= offsetof(struct dc_native_api, millis) + sizeof(dc_os->millis)
               ? dc_os->millis()
               : 0;
}
static char *empty_env[] = {NULL};
char **environ = empty_env;
int app_main(int argc, char **argv);
long dc_app_main(const struct dc_native_api *os, const char *tail, const char *env)
{
    (void)env;
    dc_os = os;
    if (os->version != DC_NATIVE_ABI)
        return -32;
    char argbuf[128];
    char *argv[18] = {(char *)"NATIVE.PRG"};
    int argc = 1;
    unsigned int n = tail ? (unsigned char)tail[0] : 0;
    if (n > 126)
        n = 126;
    if (n)
        memcpy(argbuf, tail + 1, n);
    argbuf[n] = 0;
    char *p = argbuf;
    while (*p && argc < 17) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"')
                p++;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ')
                p++;
        }
        if (*p)
            *p++ = 0;
    }
    argv[argc] = NULL;
    int result = app_main(argc, argv);
    /* newlib's exit flushes stdio and executes registered app cleanup. */
    exit(result);
}
/* KOS newlib deliberately leaves the reentrant syscall glue to its host. */
int _open_r(struct _reent *r, const char *p, int f, int mode)
{
    (void)r;
    (void)mode;
    return _open(p, f);
}
int _close_r(struct _reent *r, int fd)
{
    (void)r;
    return _close(fd);
}
int _unlink_r(struct _reent *r, const char *path)
{
    (void)r;
    return _unlink(path);
}
_ssize_t _read_r(struct _reent *r, int fd, void *p, size_t n)
{
    (void)r;
    return _read(fd, p, n);
}
_ssize_t _write_r(struct _reent *r, int fd, const void *p, size_t n)
{
    (void)r;
    return _write(fd, p, n);
}
_off_t _lseek_r(struct _reent *r, int fd, _off_t off, int whence)
{
    (void)r;
    return _lseek(fd, off, whence);
}
int _fstat_r(struct _reent *r, int fd, struct stat *st)
{
    (void)r;
    return _fstat(fd, st);
}
int _isatty_r(struct _reent *r, int fd)
{
    (void)r;
    return _isatty(fd);
}
void __assert(const char *file, int line, const char *expr)
{
    __assert_func(file, line, "", expr);
}

int _gettimeofday_r(struct _reent *r, struct timeval *tv, void *tz)
{
    (void)r;
    return _gettimeofday(tv, tz);
}

/* KOS's stdlib header leaves process abort to the host, like its syscalls. */
void abort(void)
{
    app_alert("The application aborted");
    exit(1);
}
