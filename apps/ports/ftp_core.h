/* Single-client, bounded-memory FTP engine. GPL-2.0-or-later. */
#ifndef DC_FTP_CORE_H
#define DC_FTP_CORE_H
#include "ftp_fs.h"
#include <stdint.h>
typedef struct {
    int listener, control, passive, data, file;
    int transfer, quit, binary, malformed;
    unsigned in_len, out_len, out_pos, data_len, data_pos;
    uint32_t last_control, last_data, transferred;
    uint8_t ip[4], peer[4];
    char root[FTP_PATH], cwd[FTP_PATH], target[FTP_PATH], rename[FTP_PATH];
    char input[512], output[1024], block[4096], status[96];
    int writable;
    FtpDir dir;
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
#endif
