/* Same-directory staged saves for GEMDOS, which cannot rename over a file.
 * Keep the original backup until the verified new file has its final name.
 * GPL-2.0-or-later. */
#include "app.h"
#include "editor_core.h"
#include "editor_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef ED_RENAME
static int editor_rename(const char *from, const char *to)
{
#ifdef APP_HOST_TEST
    return rename(from, to);
#else
    long r = dc_os->gemdos(0x56, 0, from, to);
    if (r >= 0) return 0;
    errno = r == -33 || r == -34 ? ENOENT : EIO;
    return -1;
#endif
}
#define ED_RENAME editor_rename
#endif
#ifndef ED_WRITE
#define ED_WRITE write
#endif
int editor_read_file(const char *path, char **text, size_t *length, char *error, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(error, cap, "Cannot open: %s", strerror(errno)); return 0; }
    int ok = !fseek(f, 0, SEEK_END);
    long size = ok ? ftell(f) : -1;
    if (size < 0 || size > ED_MAX_BYTES || fseek(f, 0, SEEK_SET)) {
        fclose(f); snprintf(error, cap, "File unreadable or larger than 1 MiB"); return 0;
    }
    char *s = malloc((size_t)size + 1);
    if (!s) { fclose(f); snprintf(error, cap, "Not enough memory; document unchanged"); return 0; }
    ok = fread(s, 1, (size_t)size, f) == (size_t)size && !ferror(f);
    /* Detect a file growing between length check and read. */
    if (ok && fgetc(f) != EOF) ok = 0;
    if (ferror(f)) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok) { free(s); snprintf(error, cap, "Read failed; document unchanged"); return 0; }
    s[size] = 0; *text = s; *length = (size_t)size; return 1;
}
static int unused(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 0; }
    return errno == ENOENT;
}
int editor_write_file(const char *path, const char *text, size_t length, char *message, size_t cap)
{
    char temp[128], backup[128];
    const char *base = strrchr(path, '\\');
    if (!base) base = strrchr(path, '/');
    size_t prefix = base ? (size_t)(base - path + 1) : (path[0] && path[1] == ':' ? 2 : 0);
    if (prefix + 13 >= sizeof(temp)) { snprintf(message, cap, "Path too long"); return 0; }
    memcpy(temp, path, prefix); memcpy(backup, path, prefix);
    int fd = -1;
    for (unsigned i = 0; i < 1000; ++i) {
        snprintf(temp + prefix, sizeof(temp) - prefix, "ED%06u.TMP", i);
        snprintf(backup + prefix, sizeof(backup) - prefix, "ED%06u.BAK", i);
        if (!strcmp(temp, path) || !strcmp(backup, path) || !unused(backup)) continue;
        fd = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { snprintf(message, cap, "Cannot stage save: %s", strerror(errno)); return 0; }
    int ok = 1;
    size_t written = 0;
    while (written < length) {
        ssize_t n = ED_WRITE(fd, text + written, length - written);
        if (n <= 0) { ok = 0; break; }
        written += (size_t)n;
    }
    if (close(fd)) ok = 0;
    if (!ok) { remove(temp); snprintf(message, cap, "Write failed; original kept"); return 0; }
    /* Verify without allocating another document-sized buffer. */
    FILE *f = fopen(temp, "rb");
    ok = f != NULL;
    size_t pos = 0; char block[2048];
    while (ok && pos < length) {
        size_t n = length - pos; if (n > sizeof(block)) n = sizeof(block);
        if (fread(block, 1, n, f) != n || memcmp(block, text + pos, n)) ok = 0;
        pos += n;
    }
    if (f) { if (ferror(f)) ok = 0; if (fclose(f)) ok = 0; }
    if (!ok) { remove(temp); snprintf(message, cap, "Verification failed; original kept"); return 0; }
    int exists = !unused(path);
    if (exists && ED_RENAME(path, backup)) {
        snprintf(message, cap, "Original kept. New copy: %s", temp); return 0;
    }
    if (ED_RENAME(temp, path)) {
        if (exists && ED_RENAME(backup, path))
            snprintf(message, cap, "Recovery files: %s | %s", backup, temp);
        else snprintf(message, cap, "Original kept. New copy: %s", temp);
        return 0;
    }
    if (exists && remove(backup)) snprintf(message, cap, "Saved; old copy retained: %s", backup);
    else snprintf(message, cap, "Saved %lu bytes", (unsigned long)length);
    return 1;
}
