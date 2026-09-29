/* MP3 player: pure logic. GPL-2.0-or-later. See mp3_core.h. */
#include "mp3_core.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Folder listing / playlist ---------------- */
int pl_kind(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || strlen(dot) != 4)
        return PL_NONE;
    char e[4] = {(char)toupper((unsigned char)dot[1]), (char)toupper((unsigned char)dot[2]),
                 (char)toupper((unsigned char)dot[3]), 0};
    if (!strcmp(e, "MP3"))
        return PL_MP3;
    if (!strcmp(e, "WAV"))
        return PL_WAV;
    return PL_NONE;
}
/* Order: "..", other folders, then files; each alphabetical. */
static int rank(const PlEntry *e) { return !strcmp(e->name, "..") ? 0 : e->dir ? 1 : 2; }
static int before(const PlEntry *a, const PlEntry *b)
{
    int ra = rank(a), rb = rank(b);
    return ra != rb ? ra < rb : strcmp(a->name, b->name) < 0;
}
int pl_add(Playlist *pl, const char *name, uint32_t size, int dir)
{
    if (!name || !*name || strlen(name) > 12 || pl->count >= PL_MAX)
        return -1;
    PlEntry e;
    memset(&e, 0, sizeof(e));
    for (int i = 0; name[i]; i++)
        e.name[i] = (char)toupper((unsigned char)name[i]);
    e.size = size;
    e.dir = dir ? 1 : 0;
    if (!e.dir && pl_kind(e.name) == PL_NONE)
        return -1;
    int at = 0;
    while (at < pl->count && before(&pl->e[at], &e))
        at++;
    if (at < pl->count && !strcmp(pl->e[at].name, e.name) && pl->e[at].dir == e.dir)
        return -1;
    memmove(&pl->e[at + 1], &pl->e[at], (size_t)(pl->count - at) * sizeof(PlEntry));
    pl->e[at] = e;
    pl->count++;
    return at;
}
int pl_file_count(const Playlist *pl)
{
    int n = 0;
    for (int i = 0; i < pl->count; i++)
        n += !pl->e[i].dir;
    return n;
}
int pl_first_file(const Playlist *pl)
{
    for (int i = 0; i < pl->count; i++)
        if (!pl->e[i].dir)
            return i;
    return -1;
}
int pl_step(const Playlist *pl, int cur, int dir, int wrap)
{
    if (!pl_file_count(pl))
        return -1;
    int i = cur;
    for (int n = 0; n < pl->count; n++) {
        i += dir < 0 ? -1 : 1;
        if (i < 0 || i >= pl->count) {
            if (!wrap)
                return -1;
            i = i < 0 ? pl->count - 1 : 0;
        }
        if (!pl->e[i].dir)
            return i;
    }
    return -1;
}
void pl_join(char *out, size_t n, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    snprintf(out, n, "%s%s%s", dir, len && dir[len - 1] == '\\' ? "" : "\\", name);
}
int pl_parent(char *dir)
{
    char *slash = strrchr(dir, '\\');
    if (!slash)
        return 0;
    if (slash - dir <= 2) { /* "E:\" or "E:\NAME" */
        if (slash[1]) {
            slash[1] = 0;
            return 1;
        }
        return 0;
    }
    *slash = 0;
    return 1;
}

/* ---------------- ID3 ---------------- */
static uint32_t syncsafe(const uint8_t *p)
{
    return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 | (uint32_t)(p[2] & 127) << 7 | (p[3] & 127);
}
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
size_t id3v2_total(const uint8_t *h, size_t len)
{
    if (len < 10 || memcmp(h, "ID3", 3) || h[3] < 2 || h[3] > 4 || h[4] == 0xff ||
        ((h[6] | h[7] | h[8] | h[9]) & 0x80))
        return 0;
    return 10 + syncsafe(h + 6) + ((h[3] == 4 && (h[5] & 0x10)) ? 10 : 0);
}
/* Reduce text in any ID3 encoding to printable ASCII, stopping at the first NUL. */
static void put_text(char *dst, size_t cap, const uint8_t *p, size_t n, int enc)
{
    size_t o = 0;
    int be = enc == 2;
    if (enc == 1 && n >= 2 && ((p[0] == 0xff && p[1] == 0xfe) || (p[0] == 0xfe && p[1] == 0xff))) {
        be = p[0] == 0xfe;
        p += 2;
        n -= 2;
    }
    for (size_t i = 0; o + 1 < cap;) {
        unsigned c;
        if (enc == 1 || enc == 2) {
            if (i + 1 >= n)
                break;
            c = be ? p[i] << 8 | p[i + 1] : p[i + 1] << 8 | p[i];
            i += 2;
            if (c >= 0xd800 && c < 0xdc00 && i + 1 < n) /* surrogate pair: one glyph */
                i += 2, c = '?';
        } else {
            if (i >= n)
                break;
            c = p[i++];
            if (enc == 3 && c >= 0xc0) { /* UTF-8 sequence -> one placeholder */
                while (i < n && (p[i] & 0xc0) == 0x80)
                    i++;
                c = '?';
            }
        }
        if (!c)
            break;
        dst[o++] = c >= 32 && c < 127 ? (char)c : c >= 128 ? '?' : ' ';
    }
    dst[o] = 0;
    while (o && dst[o - 1] == ' ')
        dst[--o] = 0;
}
static size_t unsync(uint8_t *p, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        p[o++] = p[i];
        if (p[i] == 0xff && i + 1 < n && p[i + 1] == 0)
            i++;
    }
    return o;
}
static char *wanted(Tags *t, const char *id, int v22)
{
    if (v22)
        return !memcmp(id, "TT2", 3) ? t->title : !memcmp(id, "TP1", 3) ? t->artist : !memcmp(id, "TAL", 3) ? t->album : NULL;
    return !memcmp(id, "TIT2", 4) ? t->title : !memcmp(id, "TPE1", 4) ? t->artist : !memcmp(id, "TALB", 4) ? t->album : NULL;
}
int id3v2_parse(uint8_t *buf, size_t len, Tags *t)
{
    size_t total = id3v2_total(buf, len);
    if (!total)
        return 0;
    int ver = buf[3], flags = buf[5];
    size_t end = total - (ver == 4 && (flags & 0x10) ? 10 : 0);
    if (end > len)
        end = len; /* truncated: use what we have */
    uint8_t *p = buf + 10;
    size_t n = end - 10;
    if (ver < 4 && (flags & 0x80))
        n = unsync(p, n);
    if (ver >= 3 && (flags & 0x40)) { /* extended header */
        size_t skip = n >= 4 ? (ver == 4 ? syncsafe(p) : be32(p) + 4) : n;
        if (skip > n)
            return 0;
        p += skip;
        n -= skip;
    }
    size_t hdr = ver == 2 ? 6 : 10;
    int found = 0;
    while (n >= hdr && p[0]) {
        size_t id_len = ver == 2 ? 3 : 4;
        size_t size = ver == 2 ? (size_t)p[3] << 16 | p[4] << 8 | p[5] : ver == 3 ? be32(p + 4) : syncsafe(p + 4);
        int fflags = ver == 2 ? 0 : p[9];
        if (size > n - hdr)
            break;
        char *dst = wanted(t, (const char *)p, ver == 2);
        if (dst && size >= 2 && !((ver == 3 && (p[9] & 0xc0)) || (ver == 4 && (p[9] & 0x0c)))) {
            /* Compressed or encrypted frames are skipped. */
            const uint8_t *d = p + hdr;
            size_t dn = size;
            uint8_t tmp[256];
            if (ver == 4 && (fflags & 2) && dn <= sizeof(tmp)) { /* per-frame unsync */
                memcpy(tmp, d, dn);
                dn = unsync(tmp, dn);
                d = tmp;
            }
            if (ver == 4 && (fflags & 1) && dn > 4) /* data length indicator */
                d += 4, dn -= 4;
            if (dn >= 1 && d[0] <= 3) {
                put_text(dst, 64, d + 1, dn - 1, d[0]);
                found += dst[0] != 0;
            }
        }
        (void)id_len;
        p += hdr + size;
        n -= hdr + size;
    }
    return found;
}
int id3v1_parse(const uint8_t b[128], Tags *t)
{
    if (memcmp(b, "TAG", 3))
        return 0;
    static const int at[3] = {3, 33, 63};
    char *dst[3] = {t->title, t->artist, t->album};
    int found = 0;
    for (int f = 0; f < 3; f++) {
        char tmp[64];
        size_t n = 0;
        for (int i = 0; i < 30 && b[at[f] + i]; i++)
            n++;
        put_text(tmp, sizeof(tmp), b + at[f], n, 0);
        if (!dst[f][0] && tmp[0]) {
            strcpy(dst[f], tmp);
            found = 1;
        }
    }
    return found;
}

/* ---------------- MPEG headers ---------------- */
int mp3_head(const uint8_t *p, Mp3Head *h)
{
    static const int br1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const int br2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const int sr[3] = {44100, 48000, 32000};
    if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0 || ((p[1] >> 1) & 3) != 1)
        return 0;
    int v = (p[1] >> 3) & 3, bi = p[2] >> 4, si = (p[2] >> 2) & 3;
    if (v == 1 || !bi || bi == 15 || si == 3)
        return 0;
    h->version = v == 3 ? 1 : v == 2 ? 2 : 25;
    h->hz = sr[si] >> (v == 3 ? 0 : v == 2 ? 1 : 2);
    h->bitrate_kbps = (v == 3 ? br1 : br2)[bi];
    h->channels = (p[3] >> 6) == 3 ? 1 : 2;
    h->samples = v == 3 ? 1152 : 576;
    h->frame_bytes = (v == 3 ? 144 : 72) * h->bitrate_kbps * 1000 / h->hz + ((p[2] >> 1) & 1);
    return 1;
}
uint32_t mp3_vbr_frames(const uint8_t *f, size_t len, const Mp3Head *h)
{
    size_t side = h->version == 1 ? (h->channels == 1 ? 17 : 32) : (h->channels == 1 ? 9 : 17);
    size_t pos = 4 + side + ((f[1] & 1) ? 0 : 2);
    if (len >= pos + 12 && (!memcmp(f + pos, "Xing", 4) || !memcmp(f + pos, "Info", 4)) && (be32(f + pos + 4) & 1))
        return be32(f + pos + 8);
    pos = 36 + ((f[1] & 1) ? 0 : 2);
    if (len >= pos + 18 && !memcmp(f + pos, "VBRI", 4))
        return be32(f + pos + 14);
    return 0;
}

/* ---------------- WAV ---------------- */
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
int wav_parse(const uint8_t *b, size_t len, WavInfo *w)
{
    if (len < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4))
        return 0;
    int have_fmt = 0;
    memset(w, 0, sizeof(*w));
    for (size_t pos = 12; pos + 8 <= len;) {
        uint32_t size = le32(b + pos + 4);
        const uint8_t *d = b + pos + 8;
        if (!memcmp(b + pos, "fmt ", 4)) {
            if (size < 16 || pos + 8 + 16 > len)
                return 0;
            unsigned tag = le16(d);
            if (tag == 0xfffe && size >= 26 && pos + 8 + 26 <= len)
                tag = le16(d + 24);
            w->channels = (int)le16(d + 2);
            w->rate = le32(d + 4);
            w->bits = (int)le16(d + 14);
            if (tag != 1 || w->channels < 1 || w->channels > 2 || (w->bits != 8 && w->bits != 16) ||
                w->rate < 8000 || w->rate > 48000)
                return 0;
            have_fmt = 1;
        } else if (!memcmp(b + pos, "data", 4)) {
            if (!have_fmt)
                return 0;
            w->data_off = (uint32_t)(pos + 8);
            w->data_bytes = size;
            return 1;
        }
        if (size > len) /* also guards the addition below against wrap */
            return 0;
        pos += 8 + size + (size & 1);
    }
    return 0;
}

/* ---------------- Presentation ---------------- */
void fmt_time(char *out, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}
int vol_to_hw(int step)
{
    if (step <= 0)
        return 0;
    if (step >= VOL_STEPS)
        return 255;
    return (step * step * 255 + VOL_STEPS * VOL_STEPS / 2) / (VOL_STEPS * VOL_STEPS);
}
uint32_t seek_offset(uint32_t start, uint32_t end, int permille)
{
    if (end <= start)
        return start;
    if (permille < 0)
        permille = 0;
    if (permille > 1000)
        permille = 1000;
    return start + (uint32_t)((uint64_t)(end - start) * (unsigned)permille / 1000);
}

/* ---------------- Visualiser: 512-point FFT, 24 log-spaced bands ---------------- */
static float win[VIS_N], tw_re[VIS_N / 2], tw_im[VIS_N / 2];
static uint16_t rev[VIS_N], edge[VIS_BANDS + 1];
static int vis_ready;
static void vis_init(void)
{
    const float pi = 3.14159265f;
    for (int i = 0; i < VIS_N; i++) {
        win[i] = 0.5f - 0.5f * cosf(2 * pi * (float)i / (VIS_N - 1));
        int r = 0;
        for (int b = 0; b < 9; b++) /* log2(VIS_N) */
            if (i & (1 << b))
                r |= 1 << (8 - b);
        rev[i] = (uint16_t)r;
    }
    for (int i = 0; i < VIS_N / 2; i++) {
        tw_re[i] = cosf(2 * pi * (float)i / VIS_N);
        tw_im[i] = -sinf(2 * pi * (float)i / VIS_N);
    }
    for (int b = 0; b <= VIS_BANDS; b++) {
        int e = (int)(powf(VIS_N / 2, (float)b / VIS_BANDS));
        edge[b] = (uint16_t)(e > VIS_N / 2 ? VIS_N / 2 : e < 1 ? 1 : e);
        if (b && edge[b] <= edge[b - 1])
            edge[b] = edge[b - 1] + 1;
    }
    edge[VIS_BANDS] = VIS_N / 2;
    vis_ready = 1;
}
int vis_analyse(const int16_t *m, uint8_t bands[VIS_BANDS])
{
    if (!vis_ready)
        vis_init();
    float re[VIS_N], im[VIS_N];
    int peak = 0;
    for (int i = 0; i < VIS_N; i++) {
        int v = m[i] < 0 ? -m[i] : m[i];
        if (v > peak)
            peak = v;
        re[rev[i]] = (float)m[i] * win[i];
        im[rev[i]] = 0;
    }
    for (int len = 2; len <= VIS_N; len <<= 1)
        for (int i = 0; i < VIS_N; i += len)
            for (int k = 0; k < len / 2; k++) {
                float wr = tw_re[k * (VIS_N / len)], wi = tw_im[k * (VIS_N / len)];
                int a = i + k, b = a + len / 2;
                float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
    for (int b = 0; b < VIS_BANDS; b++) {
        float best = 0;
        for (int k = edge[b]; k < edge[b + 1] && k < VIS_N / 2; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            if (p > best)
                best = p;
        }
        /* Full-scale sine with a Hann window peaks near 32768*VIS_N/4. */
        const float full = 32768.0f * (VIS_N / 4);
        float db = 10.0f * log10f(best / (full * full) + 1e-6f);
        int level = (int)((db + 60.0f) * (255.0f / 60.0f));
        bands[b] = (uint8_t)(level < 0 ? 0 : level > 255 ? 255 : level);
    }
    return peak > 32767 ? 255 : peak >> 7;
}
