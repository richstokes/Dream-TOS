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
static int end_transfer(FtpSession *c)
{
    int ok = 1;
    if (c->file >= 0) ok = dc_os->gemdos(0x3e, c->file) >= 0;
    c->file = -1;
    close_socket(&c->data);
    close_socket(&c->passive);
    c->transfer = IDLE;
    c->data_len = c->data_pos = 0;
    return ok;
}
static void disconnect(FtpSession *c)
{
    end_transfer(c);
    close_socket(&c->control);
    c->in_len = c->out_len = c->out_pos = 0;
    c->quit = c->malformed = c->binary = 0;
    c->rename[0] = 0;
    strcpy(c->cwd, "/");
}
void ftp_init(FtpServer *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < FTP_SESSIONS; i++) {
        s->sessions[i].file = -1;
        strcpy(s->sessions[i].cwd, "/");
    }
    strcpy(s->status, "Choose a drive or folder to share.");
}
void ftp_stop(FtpServer *s)
{
    for (int i = 0; i < FTP_SESSIONS; i++) disconnect(&s->sessions[i]);
    close_socket(&s->listener);
    strcpy(s->status, "Stopped. Choose a folder, then Start.");
}
int ftp_clients(const FtpServer *s)
{
    int n = 0;
    for (int i = 0; i < FTP_SESSIONS; i++) n += s->sessions[i].control > 0;
    return n;
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
static void reply(FtpServer *s, FtpSession *c, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(c->output + c->out_len, sizeof(c->output) - c->out_len, format, ap);
    va_end(ap);
    if (n < 0 || (unsigned)n >= sizeof(c->output) - c->out_len) {
        disconnect(c);
        strcpy(s->status, "Client sent too many pending commands.");
    } else c->out_len += (unsigned)n;
}
static int resolve(FtpServer *s, FtpSession *c, const char *arg, char *virtual, char *native)
{
    if (!ftp_path(c->cwd, arg, virtual) || !ftp_native(s->root, virtual, native)) {
        reply(s, c, "550 Invalid path; use DOS 8.3 names within this share.\r\n");
        return 0;
    }
    return 1;
}
static void finish(FtpServer *s, FtpSession *c, int code, const char *message)
{
    if (!end_transfer(c) && code == 226) { code = 451; message = "File close failed"; }
    reply(s, c, "%d %s.\r\n", code, message);
    snprintf(s->status, sizeof(s->status), "%s", message);
}
static void command(FtpServer *s, FtpSession *c, char *line, uint32_t now)
{
    char *arg = strchr(line, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; } else arg = line + strlen(line);
    for (char *p = line; *p; p++) *p = (char)toupper((unsigned char)*p);
    int renaming = !strcmp(line, "RNTO");
    if (strcmp(line, "RNFR") && !renaming) c->rename[0] = 0;
    if (!strcmp(line, "QUIT")) { end_transfer(c); reply(s, c, "221 Goodbye.\r\n"); c->quit = 1; return; }
    if (!strcmp(line, "ABOR")) {
        if (c->transfer) reply(s, c, "426 Transfer aborted.\r\n");
        end_transfer(c); reply(s, c, "226 Abort complete.\r\n"); return;
    }
    if (c->transfer) { reply(s, c, "450 Transfer in progress.\r\n"); return; }
    if (!strcmp(line, "USER") || !strcmp(line, "PASS")) reply(s, c, "230 Anonymous access; no password required.\r\n");
    else if (!strcmp(line, "SYST")) reply(s, c, "215 UNIX Type: L8\r\n");
    else if (!strcmp(line, "FEAT")) reply(s, c, "211-Features\r\n EPSV\r\n SIZE\r\n211 End\r\n");
    else if (!strcmp(line, "NOOP")) reply(s, c, "200 OK.\r\n");
    else if (!strcmp(line, "TYPE")) {
        if (!strcmp(arg, "I") || !strcmp(arg, "L 8") || !strcmp(arg, "A")) {
            c->binary = strcmp(arg, "A") != 0; reply(s, c, "200 Type set.\r\n");
        } else reply(s, c, "504 Unsupported type.\r\n");
    } else if ((!strcmp(line, "MODE") && !strcmp(arg, "S")) || (!strcmp(line, "STRU") && !strcmp(arg, "F")))
        reply(s, c, "200 OK.\r\n");
    else if (!strcmp(line, "PWD") || !strcmp(line, "XPWD")) reply(s, c, "257 \"%s\"\r\n", c->cwd);
    else if (!strcmp(line, "PASV") || !strcmp(line, "EPSV")) {
        if (!strcmp(line, "EPSV") && *arg && strcmp(arg, "1")) { reply(s, c, "522 Use IPv4 EPSV.\r\n"); return; }
        end_transfer(c);
        long h = dc_os->tcp_listen(0);
        if (h < 0) { reply(s, c, "425 Cannot open data listener.\r\n"); return; }
        c->passive = (int)h;
        long port = dc_os->tcp_port(c->passive);
        if (port <= 0) { end_transfer(c); reply(s, c, "425 Cannot obtain data port.\r\n"); return; }
        c->last_data = now;
        if (!strcmp(line, "EPSV")) reply(s, c, "229 Entering Extended Passive Mode (|||%ld|).\r\n", port);
        else reply(s, c, "227 Entering Passive Mode (%u,%u,%u,%u,%ld,%ld).\r\n",
                   s->ip[0], s->ip[1], s->ip[2], s->ip[3], port / 256, port % 256);
    } else if (!strcmp(line, "PORT") || !strcmp(line, "EPRT")) reply(s, c, "502 Use passive mode (PASV or EPSV).\r\n");
    else if (!strcmp(line, "CWD") || !strcmp(line, "CDUP") || !strcmp(line, "XCUP")) {
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (strcmp(line, "CWD")) arg = !strcmp(c->cwd, "/") ? "/" : "..";
        if (!*arg) { reply(s, c, "501 Path required.\r\n"); return; }
        if (!resolve(s, c, arg, v, p)) return;
        if (!ftp_fs_stat(p, &e) || !(e.attr & 16)) reply(s, c, "550 Folder unavailable.\r\n");
        else { strcpy(c->cwd, v); reply(s, c, "250 Directory changed.\r\n"); }
    } else if (!strcmp(line, "LIST") || !strcmp(line, "NLST") || !strcmp(line, "MLSD") ||
               !strcmp(line, "RETR") || !strcmp(line, "STOR")) {
        int kind = !strcmp(line, "LIST") ? LIST : !strcmp(line, "NLST") ? NLST :
                   !strcmp(line, "MLSD") ? MLSD : !strcmp(line, "RETR") ? RETR : STOR;
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (kind >= RETR && !*arg) { reply(s, c, "501 Filename required.\r\n"); return; }
        if (kind >= RETR && !c->binary) { reply(s, c, "504 Use TYPE I for file transfers.\r\n"); return; }
        if (kind == STOR && !s->writable) { reply(s, c, "550 This share is read-only.\r\n"); return; }
        /* Common clients send LIST -a or -la. No wildcard expansion. */
        if (kind == LIST && (!strcmp(arg, "-a") || !strcmp(arg, "-la") || !strcmp(arg, "-al"))) arg = "";
        if (!resolve(s, c, arg, v, p)) return;
        int exists = ftp_fs_stat(p, &e);
        if ((kind < RETR && (!exists || !(e.attr & 16))) ||
            (kind == RETR && (!exists || (e.attr & 16))) ||
            (kind == STOR && (!strcmp(v, "/") || (exists && (e.attr & 16))))) {
            reply(s, c, "550 File or folder unavailable.\r\n"); return;
        }
        if (!c->passive && !c->data) { reply(s, c, "425 Use PASV or EPSV first.\r\n"); return; }
        if (kind < RETR && !ftp_fs_first(&c->dir, p)) { reply(s, c, "550 Cannot list folder.\r\n"); return; }
        strcpy(c->target, p);
        c->transfer = kind;
        c->last_data = now;
        reply(s, c, "150 Opening data connection.\r\n");
        snprintf(s->status, sizeof(s->status), "%s %.70s", line, v);
    } else if (!strcmp(line, "SIZE")) {
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (!resolve(s, c, arg, v, p)) return;
        if (ftp_fs_stat(p, &e) && !(e.attr & 16)) reply(s, c, "213 %lu\r\n", (unsigned long)e.size);
        else reply(s, c, "550 File unavailable.\r\n");
    } else if (!strcmp(line, "MKD") || !strcmp(line, "RMD") || !strcmp(line, "DELE") ||
               !strcmp(line, "RNFR") || renaming) {
        char v[FTP_PATH], p[FTP_PATH]; FtpEntry e;
        if (!s->writable) { reply(s, c, "550 This share is read-only.\r\n"); return; }
        if (renaming && !c->rename[0]) { reply(s, c, "503 Send RNFR first.\r\n"); return; }
        if (!*arg) { reply(s, c, "501 Path required.\r\n"); return; }
        if (!resolve(s, c, arg, v, p)) { c->rename[0] = 0; return; }
        if (!strcmp(v, "/")) { reply(s, c, "550 Cannot modify the share root.\r\n"); return; }
        int exists = ftp_fs_stat(p, &e);
        long rc;
        if (!strcmp(line, "RNFR")) {
            c->rename[0] = 0;
            if (!exists) reply(s, c, "550 File unavailable.\r\n");
            else { strcpy(c->rename, p); reply(s, c, "350 Send RNTO.\r\n"); }
            return;
        }
        if (renaming) {
            rc = exists ? -1 : dc_os->gemdos(0x56, 0, c->rename, p);
            c->rename[0] = 0;
        } else if (!strcmp(line, "MKD")) rc = exists ? -1 : dc_os->gemdos(0x39, p);
        else if (!strcmp(line, "RMD")) rc = exists && (e.attr & 16) ? dc_os->gemdos(0x3a, p) : -1;
        else rc = exists && !(e.attr & 16) ? dc_os->gemdos(0x41, p) : -1;
        if (rc < 0) reply(s, c, "550 File operation failed.\r\n");
        else if (!strcmp(line, "MKD")) reply(s, c, "257 \"%s\" created.\r\n", v);
        else reply(s, c, "250 File operation complete.\r\n");
    } else reply(s, c, "502 Command not implemented.\r\n");
}
static void accept_data(FtpServer *s, FtpSession *c, uint32_t now)
{
    if (!c->data && c->passive) {
        uint8_t peer[4];
        long h = dc_os->tcp_accept(c->passive, peer);
        if (h == DC_TCP_AGAIN) return;
        if (h < 0) { finish(s, c, 425, "Data connection failed"); return; }
        /* A passive socket belongs to the peer on the control connection. */
        if (memcmp(peer, c->peer, 4)) { dc_os->tcp_close((int)h); return; }
        c->data = (int)h;
        close_socket(&c->passive);
        c->last_data = now;
    }
}
static void transfer(FtpServer *s, FtpSession *c, uint32_t now, int steps)
{
    if (!c->data) return;
    if (c->transfer >= RETR && c->file < 0) {
        long f = c->transfer == RETR ? dc_os->gemdos(0x3d, c->target, 0) : dc_os->gemdos(0x3c, c->target, 0);
        if (f < 0) { finish(s, c, 550, "Cannot open file"); return; }
        c->file = (int)f;
    }
    /* Bounded batch; the caller shares 16 KiB per tick between transfers. */
    for (int step = 0; step < steps && c->transfer; step++) {
        if (c->transfer == STOR) {
            long n = dc_os->tcp_recv(c->data, c->block, sizeof(c->block));
            if (n == DC_TCP_AGAIN) return;
            if (n < 0) { finish(s, c, 426, "Upload interrupted; partial file retained"); return; }
            if (!n) { finish(s, c, 226, "Transfer complete"); return; }
            if (dc_os->gemdos(0x40, c->file, n, c->block) != n) { finish(s, c, 452, "Write failed; check free space"); return; }
            s->transferred += (uint32_t)n;
            c->last_data = now;
            continue;
        }
        if (c->data_pos == c->data_len) {
            c->data_pos = c->data_len = 0;
            if (c->transfer == RETR) {
                long n = dc_os->gemdos(0x3f, c->file, (long)sizeof(c->block), c->block);
                if (n < 0) { finish(s, c, 451, "Read failed"); return; }
                c->data_len = (unsigned)n;
            } else {
                FtpEntry e;
                int rc = ftp_fs_next(&c->dir, &e);
                if (rc < 0) { finish(s, c, 451, "Directory read failed"); return; }
                if (rc) {
                    if (c->transfer == NLST) c->data_len = snprintf(c->block, sizeof(c->block), "%s\r\n", e.name);
                    else if (c->transfer == MLSD) c->data_len = snprintf(c->block, sizeof(c->block),
                        "type=%s;size=%lu; %s\r\n", e.attr & 16 ? "dir" : "file", (unsigned long)e.size, e.name);
                    else c->data_len = snprintf(c->block, sizeof(c->block), "%cr%cxr-xr-x 1 ftp ftp %10lu Jan 01  1980 %s\r\n",
                        e.attr & 16 ? 'd' : '-', s->writable && !(e.attr & 1) ? 'w' : '-', (unsigned long)e.size, e.name);
                }
            }
            if (!c->data_len) { finish(s, c, 226, "Transfer complete"); return; }
        }
        long n = dc_os->tcp_send(c->data, c->block + c->data_pos, c->data_len - c->data_pos);
        if (n == DC_TCP_AGAIN) return;
        if (n <= 0) { finish(s, c, 426, "Data connection lost"); return; }
        c->data_pos += (unsigned)n;
        s->transferred += (uint32_t)n;
        c->last_data = now;
    }
}
static void poll_session(FtpServer *s, FtpSession *c, uint32_t now, int steps)
{
    if (now - c->last_control > 300000 && !c->transfer) {
        disconnect(c); strcpy(s->status, "Idle client disconnected."); return;
    }
    if (c->out_pos < c->out_len) {
        long n = dc_os->tcp_send(c->control, c->output + c->out_pos, c->out_len - c->out_pos);
        if (n == DC_TCP_AGAIN) return;
        if (n <= 0) { disconnect(c); strcpy(s->status, "Client disconnected."); return; }
        c->out_pos += (unsigned)n;
        if (c->out_pos < c->out_len) return;
        c->out_pos = c->out_len = 0;
    }
    if (c->quit) {
        disconnect(c);
        strcpy(s->status, ftp_clients(s) ? "Client disconnected." : "Waiting for an FTP client.");
        return;
    }
    /* Read at most one command per tick; retain fragmented lines without ever
     * treating a truncated/embedded-NUL command as a valid filesystem request. */
    for (unsigned i = 0; i < 512; i++) {
        unsigned char ch;
        long n = dc_os->tcp_recv(c->control, &ch, 1);
        if (n == DC_TCP_AGAIN) break;
        if (n <= 0) { disconnect(c); strcpy(s->status, "Client disconnected."); return; }
        c->last_control = now;
        if (ch == '\n') {
            if (c->in_len && c->input[c->in_len - 1] == '\r') c->in_len--;
            c->input[c->in_len] = 0;
            if (c->malformed) reply(s, c, "500 Malformed or overlong command.\r\n");
            else command(s, c, c->input, now);
            c->in_len = c->malformed = 0;
            break;
        }
        if (ch == 0 || ch >= 127 || (ch < 32 && ch != '\r')) c->malformed = 1;
        if (c->in_len + 1 >= sizeof(c->input)) c->malformed = 1;
        else c->input[c->in_len++] = (char)ch;
    }
    if (!c->control) return;
    /* KOS completes passive handshakes in accept(), so accept even before a
     * transfer command: clients commonly connect data before sending LIST. */
    accept_data(s, c, now);
    if (c->transfer) {
        /* A long SD transfer is activity even without another control command. */
        c->last_control = now;
        if (now - c->last_data > 30000) finish(s, c, 426, "Data connection timed out; partial upload may remain");
        else transfer(s, c, now, steps);
    } else if ((c->passive || c->data) && now - c->last_data > 30000) end_transfer(c);
}
void ftp_poll(FtpServer *s, uint32_t now)
{
    if (!s->listener) return;
    /* Keep accepting while clients are connected: FileZilla and others open a
     * second login for transfers, which an unserviced backlog would refuse. */
    uint8_t peer[4];
    long h = dc_os->tcp_accept(s->listener, peer);
    if (h != DC_TCP_AGAIN) {
        if (h < 0) { ftp_stop(s); strcpy(s->status, "Listener failed. Check network, then Start."); return; }
        FtpSession *c = NULL;
        for (int i = 0; i < FTP_SESSIONS && !c; i++) if (!s->sessions[i].control) c = &s->sessions[i];
        if (!c) {
            static const char busy[] = "421 Too many connections; try again later.\r\n";
            dc_os->tcp_send((int)h, busy, sizeof(busy) - 1); /* best effort */
            dc_os->tcp_close((int)h);
        } else {
            disconnect(c);
            c->control = (int)h;
            memcpy(c->peer, peer, 4);
            c->last_control = now;
            reply(s, c, "220 Dream TOS anonymous FTP ready.\r\n");
            snprintf(s->status, sizeof(s->status), "Client %u.%u.%u.%u connected.", peer[0], peer[1], peer[2], peer[3]);
        }
    }
    /* Concurrent transfers split the 4 x 4 KiB per-tick budget so the UI stays responsive. */
    int active = 0;
    for (int i = 0; i < FTP_SESSIONS; i++) active += s->sessions[i].transfer != IDLE;
    int steps = active > 1 ? 4 / active : 4;
    for (int i = 0; i < FTP_SESSIONS; i++)
        if (s->sessions[i].control) poll_session(s, &s->sessions[i], now, steps);
}
