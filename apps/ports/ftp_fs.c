/* Keep our DTA private across AES yields/accessory file operations. GPL-2.0-or-later. */
#include "app.h"
#include "ftp_fs.h"
#include <string.h>

static void entry_from_dta(const uint8_t *dta, FtpEntry *entry)
{
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, dta + 30, 13);
    entry->attr = dta[21];
    entry->size = (uint32_t)dta[26] | (uint32_t)dta[27] << 8 |
                  (uint32_t)dta[28] << 16 | (uint32_t)dta[29] << 24;
}
int ftp_fs_stat(const char *path, FtpEntry *entry)
{
    if (strlen(path) == 3 && path[1] == ':' && path[2] == '\\') {
        uint32_t space[4];
        if (dc_os->gemdos(0x36, space, path[0] - 'A' + 1) < 0) return 0;
        memset(entry, 0, sizeof(*entry));
        entry->attr = 0x10;
        return 1;
    }
    uint8_t dta[44] __attribute__((aligned(4)));
    long old = dc_os->gemdos(0x2f);
    dc_os->gemdos(0x1a, dta);
    long rc = dc_os->gemdos(0x4e, path, 0x16);
    if (!rc) entry_from_dta(dta, entry);
    dc_os->gemdos(0x1a, (void *)old);
    return !rc;
}
int ftp_fs_first(FtpDir *dir, const char *path)
{
    char pattern[FTP_PATH + 5];
    snprintf(pattern, sizeof(pattern), "%s%s*.*", path, path[strlen(path) - 1] == '\\' ? "" : "\\");
    long old = dc_os->gemdos(0x2f);
    dc_os->gemdos(0x1a, dir->dta);
    dir->result = dc_os->gemdos(0x4e, pattern, 0x16);
    dc_os->gemdos(0x1a, (void *)old);
    return dir->result == 0 || dir->result == -33 || dir->result == -49;
}
int ftp_fs_next(FtpDir *dir, FtpEntry *entry)
{
    while (!dir->result) {
        entry_from_dta(dir->dta, entry);
        long old = dc_os->gemdos(0x2f);
        dc_os->gemdos(0x1a, dir->dta);
        dir->result = dc_os->gemdos(0x4f);
        dc_os->gemdos(0x1a, (void *)old);
        if (!(entry->attr & 8) && strcmp(entry->name, ".") && strcmp(entry->name, "..")) return 1;
    }
    return dir->result == -33 || dir->result == -49 ? 0 : -1;
}
