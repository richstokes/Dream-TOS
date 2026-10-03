/* GEMDOS filesystem adapter for the FTP server and folder picker. */
#ifndef DC_FTP_FS_H
#define DC_FTP_FS_H
#include <stdint.h>
#define FTP_PATH 128
typedef struct {
    char name[14];
    uint32_t size;
    unsigned attr;
} FtpEntry;
typedef struct {
    uint8_t dta[44] __attribute__((aligned(4)));
    long result;
} FtpDir;
/* All paths here are absolute native paths, validated by the caller. */
int ftp_fs_stat(const char *path, FtpEntry *entry);
int ftp_fs_first(FtpDir *dir, const char *path);
int ftp_fs_next(FtpDir *dir, FtpEntry *entry); /* 1 entry, 0 end, -1 error */
#endif
