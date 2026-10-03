/* Passive FTP over the native TCP API, with GEMDOS file I/O on the AES thread.
 * Each poll does bounded socket/disk work. No worker ever enters GEMDOS.
 * GPL-2.0-or-later. */
#include "app.h"
#include "ftp_core.h"
#include "dreamcast/tcp.h"
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

enum { IDLE, LIST, NLST, MLSD, RETR, STOR };
#define HAS(m) (dc_os->size >= offsetof(struct dc_native_api, m) + sizeof(dc_os->m) && dc_os->m)
int ftp_available(void)
{
    return HAS(tcp_listen) && HAS(tcp_accept) && HAS(tcp_recv) && HAS(tcp_send) && HAS(tcp_port) && HAS(tcp_close);
}
/* Reject names GEMDOS might silently shorten or treat as wildcard/device paths. */
static int component(const char *p, unsigned len)
{
    unsigned base = 0, ext = 0;
    int dot = 0;
    for (unsigned i = 0; i < len; i++) {
        unsigned char c = p[i];
        if (c == '.') { if (dot || !base) return 0; dot = 1; continue; }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || strchr("!#$%&'()-@^_`{}~", c))) return 0;
        if (dot) ext++; else base++;
    }
    return base && base <= 8 && ext <= 3 && (!dot || ext);
}
int ftp_path(const char *cwd, const char *input, char out[FTP_PATH])
{
    if (!cwd || !input || cwd[0] != '/') return 0;
    size_t n = input[0] == '/' ? 1 : strlen(cwd);
    if (n >= FTP_PATH) return 0;
    if (input[0] == '/') strcpy(out, "/"); else memcpy(out, cwd, n + 1);
    const char *p = input;
    while (*p) {
        while (*p == '/') p++;
        const char *start = p;
        while (*p && *p != '/') p++;
        unsigned len = (unsigned)(p - start);
        if (!len || (len == 1 && *start == '.')) continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (n == 1) return 0;
            while (n > 1 && out[n - 1] != '/') n--;
            if (n > 1) n--;
            out[n] = 0;
            continue;
        }
        if (!component(start, len) || n + (n > 1) + len >= FTP_PATH) return 0;
        if (n > 1) out[n++] = '/';
        for (unsigned i = 0; i < len; i++) out[n++] = (char)toupper((unsigned char)start[i]);
        out[n] = 0;
    }
    return 1;
}
int ftp_native(const char *root, const char *path, char out[FTP_PATH])
{
    size_t n = strlen(root), tail = strlen(path + 1);
    int slash = n && root[n - 1] != '\\' && tail;
    if (path[0] != '/' || n + slash + tail >= FTP_PATH) return 0;
    memcpy(out, root, n);
    if (slash) out[n++] = '\\';
    for (unsigned i = 1; path[i]; i++) out[n++] = path[i] == '/' ? '\\' : path[i];
    out[n] = 0;
    return 1;
}
static void close_socket(int *h)
{
    if (*h > 0) dc_os->tcp_close(*h);
    *h = 0;
}
static int end_transfer(FtpServer *s)
{
    int ok = 1;
    if (s->file >= 0) ok = dc_os->gemdos(0x3e, s->file) >= 0;
    s->file = -1;
    close_socket(&s->data);
    close_socket(&s->passive);
    s->transfer = IDLE;
    s->data_len = s->data_pos = 0;
    return ok;
}
static void disconnect(FtpServer *s)
{
    end_transfer(s);
    close_socket(&s->control);
    s->in_len = s->out_len = s->out_pos = 0;
    s->quit = s->malformed = s->binary = 0;
    s->rename[0] = 0;
    strcpy(s->cwd, "/");
}
void ftp_init(FtpServer *s)
{
    memset(s, 0, sizeof(*s));
    s->file = -1;
    strcpy(s->status, "Choose a drive or folder to share.");
}
void ftp_stop(FtpServer *s)
{
    disconnect(s);
    close_socket(&s->listener);
    strcpy(s->status, "Stopped. Choose a folder, then Start.");
}
int ftp_start(FtpServer *s, const char *root, int writable, const uint8_t ip[4], unsigned port)
{
    char relative[FTP_PATH], normalized[FTP_PATH], native[FTP_PATH];
    size_t n = strlen(root);
    if (n < 3 || n >= FTP_PATH || root[1] != ':' || root[2] != '\\' ||
        toupper((unsigned char)root[0]) < 'C' || toupper((unsigned char)root[0]) > 'Z') return 0;
    strcpy(relative, root + 2);
    for (char *p = relative; *p; p++) if (*p == '\\') *p = '/';
    char drive[] = {(char)toupper((unsigned char)root[0]), ':', '\\', 0};
    FtpEntry e;
    if (!ftp_path("/", relative, normalized) || !ftp_native(drive, normalized, native) ||
        !ftp_fs_stat(native, &e) || !(e.attr & 16)) {
        strcpy(s->status, "Cannot open the selected folder.");
        return 0;
    }
    ftp_stop(s);
    if (!ftp_available()) { strcpy(s->status, "This OS does not provide TCP networking."); return 0; }
    long h = dc_os->tcp_listen(port);
    if (h < 0) { strcpy(s->status, "Cannot listen: network unavailable or port in use."); return 0; }
    s->listener = (int)h;
    strcpy(s->root, native);
    s->writable = writable;
    memcpy(s->ip, ip, 4);
    s->transferred = 0;
    strcpy(s->status, "Listening. Waiting for an FTP client.");
    return 1;
}
static void reply(FtpServer *s, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(s->output + s->out_len, sizeof(s->output) - s->out_len, format, ap);
    va_end(ap);
    if (n < 0 || (unsigned)n >= sizeof(s->output) - s->out_len) {
        disconnect(s);
        strcpy(s->status, "Client sent too many pending commands.");
    } else s->out_len += (unsigned)n;
}
static int resolve(FtpServer *s, const char *arg, char *virtual, char *native)
{
    if (!ftp_path(s->cwd, arg, virtual) || !ftp_native(s->root, virtual, native)) {
        reply(s, "550 Invalid path; use DOS 8.3 names within this share.\r\n");
        return 0;
    }
    return 1;
}
static void finish(FtpServer *s, int code, const char *message)
{
    if (!end_transfer(s) && code == 226) { code = 451; message = "File close failed"; }
    reply(s, "%d %s.\r\n", code, message);
    snprintf(s->status, sizeof(s->status), "%s", message);
}
static void command(FtpServer *s, char *line, uint32_t now)
{
    char *arg = strchr(line, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; } else arg = line + strlen(line);
    for (char *p = line; *p; p++) *p = (char)toupper((unsigned char)*p);
    int renaming = !strcmp(line, "RNTO");
    if (strcmp(line, "RNFR") && !renaming) s->rename[0] = 0;
    if (!strcmp(line, "QUIT")) { end_transfer(s); reply(s, "221 Goodbye.\r\n"); s->quit = 1; return; }
    if (!strcmp(line, "ABOR")) {
        if (s->transfer) reply(s, "426 Transfer aborted.\r\n");
        end_transfer(s); reply(s, "226 Abort complete.\r\n"); return;
    }
    if (s->transfer) { reply(s, "450 Transfer in progress.\r\n"); return; }
    if (!strcmp(line, "USER") || !strcmp(line, "PASS")) reply(s, "230 Anonymous access; no password required.\r\n");
    else if (!strcmp(line, "SYST")) reply(s, "215 UNIX Type: L8\r\n");
    else if (!strcmp(line, "FEAT")) reply(s, "211-Features\r\n EPSV\r\n SIZE\r\n211 End\r\n");
    else if (!strcmp(line, "NOOP")) reply(s, "200 OK.\r\n");
    else if (!strcmp(line, "TYPE")) {
        if (!strcmp(arg, "I") || !strcmp(arg, "L 8") || !strcmp(arg, "A")) {
            s->binary = strcmp(arg, "A") != 0; reply(s, "200 Type set.\r\n");
        } else reply(s, "504 Unsupported type.\r\n");
    } else if ((!strcmp(line, "MODE") && !strcmp(arg, "S")) || (!strcmp(line, "STRU") && !strcmp(arg, "F")))
        reply(s, "200 OK.\r\n");
    else if (!strcmp(line, "PWD") || !strcmp(line, "XPWD")) reply(s, "257 \"%s\"\r\n", s->cwd);
    else if (!strcmp(line, "PASV") || !strcmp(line, "EPSV")) {
        if (!strcmp(line, "EPSV") && *arg && strcmp(arg, "1")) { reply(s, "522 Use IPv4 EPSV.\r\n"); return; }
        end_transfer(s);
        long h = dc_os->tcp_listen(0);
        if (h < 0) { reply(s, "425 Cannot open data listener.\r\n"); return; }
        s->passive = (int)h;
        long port = dc_os->tcp_port(s->passive);
        if (port <= 0) { end_transfer(s); reply(s, "425 Cannot obtain data port.\r\n"); return; }
        s->last_data = now;
        if (!strcmp(line, "EPSV")) reply(s, "229 Entering Extended Passive Mode (|||%ld|).\r\n", port);
        else reply(s, "227 Entering Passive Mode (%u,%u,%u,%u,%ld,%ld).\r\n",
                   s->ip[0], s->ip[1], s->ip[2], s->ip[3], port / 256, port % 256);
    } else if (!strcmp(line, "PORT") || !strcmp(line, "EPRT")) reply(s, "502 Use passive mode (PASV or EPSV).\r\n");
    else if (!strcmp(line, "CWD") || !strcmp(line, "CDUP") || !strcmp(line, "XCUP")) {
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (strcmp(line, "CWD")) arg = !strcmp(s->cwd, "/") ? "/" : "..";
        if (!*arg) { reply(s, "501 Path required.\r\n"); return; }
        if (!resolve(s, arg, v, p)) return;
        if (!ftp_fs_stat(p, &e) || !(e.attr & 16)) reply(s, "550 Folder unavailable.\r\n");
        else { strcpy(s->cwd, v); reply(s, "250 Directory changed.\r\n"); }
    } else if (!strcmp(line, "LIST") || !strcmp(line, "NLST") || !strcmp(line, "MLSD") ||
               !strcmp(line, "RETR") || !strcmp(line, "STOR")) {
        int kind = !strcmp(line, "LIST") ? LIST : !strcmp(line, "NLST") ? NLST :
                   !strcmp(line, "MLSD") ? MLSD : !strcmp(line, "RETR") ? RETR : STOR;
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (kind >= RETR && !*arg) { reply(s, "501 Filename required.\r\n"); return; }
        if (kind >= RETR && !s->binary) { reply(s, "504 Use TYPE I for file transfers.\r\n"); return; }
        if (kind == STOR && !s->writable) { reply(s, "550 This share is read-only.\r\n"); return; }
        /* Common clients send LIST -a or -la. No wildcard expansion. */
        if (kind == LIST && (!strcmp(arg, "-a") || !strcmp(arg, "-la") || !strcmp(arg, "-al"))) arg = "";
        if (!resolve(s, arg, v, p)) return;
        int exists = ftp_fs_stat(p, &e);
        if ((kind < RETR && (!exists || !(e.attr & 16))) ||
            (kind == RETR && (!exists || (e.attr & 16))) ||
            (kind == STOR && (!strcmp(v, "/") || (exists && (e.attr & 16))))) {
            reply(s, "550 File or folder unavailable.\r\n"); return;
        }
        if (!s->passive && !s->data) { reply(s, "425 Use PASV or EPSV first.\r\n"); return; }
        if (kind < RETR && !ftp_fs_first(&s->dir, p)) { reply(s, "550 Cannot list folder.\r\n"); return; }
        strcpy(s->target, p);
        s->transfer = kind;
        s->last_data = now;
        reply(s, "150 Opening data connection.\r\n");
        snprintf(s->status, sizeof(s->status), "%s %.70s", line, v);
    } else if (!strcmp(line, "SIZE")) {
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (!resolve(s, arg, v, p)) return;
        if (ftp_fs_stat(p, &e) && !(e.attr & 16)) reply(s, "213 %lu\r\n", (unsigned long)e.size);
        else reply(s, "550 File unavailable.\r\n");
    } else if (!strcmp(line, "MKD") || !strcmp(line, "RMD") || !strcmp(line, "DELE") ||
               !strcmp(line, "RNFR") || renaming) {
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (!s->writable) { reply(s, "550 This share is read-only.\r\n"); return; }
        if (renaming && !s->rename[0]) { reply(s, "503 Send RNFR first.\r\n"); return; }
        if (!*arg) { reply(s, "501 Path required.\r\n"); return; }
        if (!resolve(s, arg, v, p)) { s->rename[0] = 0; return; }
        if (!strcmp(v, "/")) { reply(s, "550 Cannot modify the share root.\r\n"); return; }
        int exists = ftp_fs_stat(p, &e);
        long rc;
        if (!strcmp(line, "RNFR")) {
            s->rename[0] = 0;
            if (!exists) reply(s, "550 File unavailable.\r\n");
            else { strcpy(s->rename, p); reply(s, "350 Send RNTO.\r\n"); }
            return;
        }
        if (renaming) {
            rc = exists ? -1 : dc_os->gemdos(0x56, 0, s->rename, p);
            s->rename[0] = 0;
        } else if (!strcmp(line, "MKD")) rc = exists ? -1 : dc_os->gemdos(0x39, p);
        else if (!strcmp(line, "RMD")) rc = exists && (e.attr & 16) ? dc_os->gemdos(0x3a, p) : -1;
        else rc = exists && !(e.attr & 16) ? dc_os->gemdos(0x41, p) : -1;
        if (rc < 0) reply(s, "550 File operation failed.\r\n");
        else if (!strcmp(line, "MKD")) reply(s, "257 \"%s\" created.\r\n", v);
        else reply(s, "250 File operation complete.\r\n");
    } else reply(s, "502 Command not implemented.\r\n");
}
static void accept_data(FtpServer *s, uint32_t now)
{
    if (!s->data && s->passive) {
        uint8_t peer[4];
        long h = dc_os->tcp_accept(s->passive, peer);
        if (h == DC_TCP_AGAIN) return;
        if (h < 0) { finish(s, 425, "Data connection failed"); return; }
        /* A passive socket belongs to the peer on the control connection. */
        if (memcmp(peer, s->peer, 4)) { dc_os->tcp_close((int)h); return; }
        s->data = (int)h;
        close_socket(&s->passive);
        s->last_data = now;
    }
}
static void transfer(FtpServer *s, uint32_t now)
{
    if (!s->data) return;
    if (s->transfer >= RETR && s->file < 0) {
        long f = s->transfer == RETR ? dc_os->gemdos(0x3d, s->target, 0) : dc_os->gemdos(0x3c, s->target, 0);
        if (f < 0) { finish(s, 550, "Cannot open file"); return; }
        s->file = (int)f;
    }
    /* Bounded batch; UI is serviced again after at most 16 KiB. */
    for (int step = 0; step < 4 && s->transfer; step++) {
        if (s->transfer == STOR) {
            long n = dc_os->tcp_recv(s->data, s->block, sizeof(s->block));
            if (n == DC_TCP_AGAIN) return;
            if (n < 0) { finish(s, 426, "Upload interrupted; partial file retained"); return; }
            if (!n) { finish(s, 226, "Transfer complete"); return; }
            if (dc_os->gemdos(0x40, s->file, n, s->block) != n) { finish(s, 452, "Write failed; check free space"); return; }
            s->transferred += (uint32_t)n;
            s->last_data = now;
            continue;
        }
        if (s->data_pos == s->data_len) {
            s->data_pos = s->data_len = 0;
            if (s->transfer == RETR) {
                long n = dc_os->gemdos(0x3f, s->file, (long)sizeof(s->block), s->block);
                if (n < 0) { finish(s, 451, "Read failed"); return; }
                s->data_len = (unsigned)n;
            } else {
                FtpEntry e;
                int rc = ftp_fs_next(&s->dir, &e);
                if (rc < 0) { finish(s, 451, "Directory read failed"); return; }
                if (rc) {
                    if (s->transfer == NLST) s->data_len = snprintf(s->block, sizeof(s->block), "%s\r\n", e.name);
                    else if (s->transfer == MLSD) s->data_len = snprintf(s->block, sizeof(s->block),
                        "type=%s;size=%lu; %s\r\n", e.attr & 16 ? "dir" : "file", (unsigned long)e.size, e.name);
                    else s->data_len = snprintf(s->block, sizeof(s->block), "%cr%cxr-xr-x 1 ftp ftp %10lu Jan 01  1980 %s\r\n",
                        e.attr & 16 ? 'd' : '-', s->writable && !(e.attr & 1) ? 'w' : '-', (unsigned long)e.size, e.name);
                }
            }
            if (!s->data_len) { finish(s, 226, "Transfer complete"); return; }
        }
        long n = dc_os->tcp_send(s->data, s->block + s->data_pos, s->data_len - s->data_pos);
        if (n == DC_TCP_AGAIN) return;
        if (n <= 0) { finish(s, 426, "Data connection lost"); return; }
        s->data_pos += (unsigned)n;
        s->transferred += (uint32_t)n;
        s->last_data = now;
    }
}
void ftp_poll(FtpServer *s, uint32_t now)
{
    if (!s->listener) return;
    if (!s->control) {
        long h = dc_os->tcp_accept(s->listener, s->peer);
        if (h == DC_TCP_AGAIN) return;
        if (h < 0) { ftp_stop(s); strcpy(s->status, "Listener failed. Check network, then Start."); return; }
        disconnect(s);
        s->control = (int)h;
        s->last_control = now;
        reply(s, "220 Dream TOS anonymous FTP ready.\r\n");
        snprintf(s->status, sizeof(s->status), "Client %u.%u.%u.%u connected.", s->peer[0], s->peer[1], s->peer[2], s->peer[3]);
    }
    if (now - s->last_control > 300000 && !s->transfer) {
        disconnect(s); strcpy(s->status, "Idle client disconnected."); return;
    }
    if (s->out_pos < s->out_len) {
        long n = dc_os->tcp_send(s->control, s->output + s->out_pos, s->out_len - s->out_pos);
        if (n == DC_TCP_AGAIN) return;
        if (n <= 0) { disconnect(s); strcpy(s->status, "Client disconnected."); return; }
        s->out_pos += (unsigned)n;
        if (s->out_pos < s->out_len) return;
        s->out_pos = s->out_len = 0;
    }
    if (s->quit) { disconnect(s); strcpy(s->status, "Waiting for an FTP client."); return; }
    /* Read at most one command per tick; retain fragmented lines without ever
     * treating a truncated/embedded-NUL command as a valid filesystem request. */
    for (unsigned i = 0; i < 512; i++) {
        unsigned char c;
        long n = dc_os->tcp_recv(s->control, &c, 1);
        if (n == DC_TCP_AGAIN) break;
        if (n <= 0) { disconnect(s); strcpy(s->status, "Client disconnected."); return; }
        s->last_control = now;
        if (c == '\n') {
            if (s->in_len && s->input[s->in_len - 1] == '\r') s->in_len--;
            s->input[s->in_len] = 0;
            if (s->malformed) reply(s, "500 Malformed or overlong command.\r\n");
            else command(s, s->input, now);
            s->in_len = s->malformed = 0;
            break;
        }
        if (c == 0 || c >= 127 || (c < 32 && c != '\r')) s->malformed = 1;
        if (s->in_len + 1 >= sizeof(s->input)) s->malformed = 1;
        else s->input[s->in_len++] = (char)c;
    }
    /* KOS completes passive handshakes in accept(), so accept even before a
     * transfer command: clients commonly connect data before sending LIST. */
    accept_data(s, now);
    if (s->transfer) {
        /* A long SD transfer is activity even without another control command. */
        s->last_control = now;
        if (now - s->last_data > 30000) finish(s, 426, "Data connection timed out; partial upload may remain");
        else transfer(s, now);
    } else if ((s->passive || s->data) && now - s->last_data > 30000) end_transfer(s);
}
