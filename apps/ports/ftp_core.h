/* Small fixed-session, bounded-memory FTP engine. GPL-2.0-or-later. */
#ifndef DC_FTP_CORE_H
#define DC_FTP_CORE_H
#include "ftp_fs.h"
#include <stdint.h>
/* Clients such as FileZilla browse on one login and transfer on others.
 * Listener + 3 x (control + passive/data) fits the 8 TCP handles. */
#define FTP_SESSIONS 3
typedef struct {
    int control, passive, data, file;
    int transfer, quit, binary, malformed;
    unsigned in_len, out_len, out_pos, data_len, data_pos;
    uint32_t last_control, last_data;
    uint8_t peer[4];
    char cwd[FTP_PATH], target[FTP_PATH], rename[FTP_PATH];
    char input[512], output[1024], block[4096];
    FtpDir dir;
} FtpSession;
typedef struct {
    int listener, writable;
    uint32_t transferred;
    uint8_t ip[4];
    char root[FTP_PATH], status[96];
    FtpSession sessions[FTP_SESSIONS];
} FtpServer;
/* Normalize an FTP path rooted at /; reject escape, malformed/long DOS names.
 * Both input and cwd use / separators. Native drive syntax is never accepted. */
int ftp_path(const char *cwd, const char *input, char out[FTP_PATH]);
int ftp_native(const char *root, const char *path, char out[FTP_PATH]);
int ftp_available(void);
void ftp_init(FtpServer *s);
int ftp_start(FtpServer *s, const char *root, int writable, const uint8_t ip[4], unsigned port);
void ftp_stop(FtpServer *s);
void ftp_poll(FtpServer *s, uint32_t now);
int ftp_clients(const FtpServer *s);
#endif
