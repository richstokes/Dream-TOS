/* Graphical MP3 / WAV player for the native GEM desktop. GPL-2.0-or-later.
 *
 * The application decodes (minimp3, CC0) and pushes PCM through the optional
 * audio_* native API; the OS feeds the AICA from a ring buffer on its own
 * thread, so sound keeps playing while this program decodes or redraws.
 * Files are read through GEMDOS, so any mounted drive works: E:-H: (SD card),
 * C: (RAM disk) or D: (disc). */
#include "app.h"
#include "window.h"
#include "drives.h"
#include "accessory.h" /* APP_HAS */
#include "mp3_core.h"
#include "dreamcast/audio.h"
#include "dreamcast/system_info.h"
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN_SIZE (16 * 1024)
#define IN_LOW (6 * 1024)   /* refill below this many unread bytes */
#define IN_CHUNK (8 * 1024) /* one SD read; small so the UI and pump stay lively */
#define FRAME_MAX 1152
#define HIST 64
#define TAG_WINDOW (32 * 1024)

/* ---- Overridable environment (the host tests replace these) ---- */
static FILE *(*open_file)(const char *path) = NULL;
static int (*list_dir)(const char *dir, Playlist *pl) = NULL;
static unsigned long (*clock_ms)(void) = NULL;
static unsigned long now_ms(void) { return clock_ms ? clock_ms() : app_millis(); }
static FILE *do_open(const char *path) { return open_file ? open_file(path) : fopen(path, "rb"); }

/* ---- Track: one open file plus its decoder ---- */
typedef struct {
    FILE *f;
    int kind; /* PL_MP3 / PL_WAV / 0 */
    mp3dec_t dec;
    uint8_t in[IN_SIZE];
    size_t in_fill, in_pos;
    uint32_t left;        /* file bytes not yet read, before audio_end */
    uint32_t start, end;  /* audio byte range in the file */
    int eof;
    int hz, channels, kbps, vbr, bits;
    uint32_t total_ms;    /* 0 if unknown */
    Tags tags;
    int16_t pend[FRAME_MAX * 2];
    int pend_n, pend_off; /* decoded frames not yet accepted by the OS */
    int done;             /* decoder is exhausted */
} Track;
static Track tk;

typedef struct {
    uint32_t end_frame;
    int16_t mono[VIS_N];
} Hist;
static Hist hist[HIST];
static unsigned hist_n;

enum { STOPPED, PLAYING, PAUSED };
static struct {
    Playlist pl;
    char dir[96];
    int sel, top, cur;      /* selected row, first visible row, playing row */
    int state, repeat, volume;
    int have_audio, stream_hz, stream_ch;
    char message[80];
    uint32_t base_ms;       /* track time at the last flush */
    uint32_t pushed;        /* frames given to the OS since the last flush */
    uint32_t last_pos_ms;
    unsigned long empty_since, last_ui, load_t0;
    unsigned long dec_ms, audio_ms; /* measurement window */
    int load_pct;
    uint32_t underruns;
    uint8_t bands[VIS_BANDS], hold[VIS_BANDS];
    int peak;
    int drives[8], ndrives;
    int buttons, pressed;
    unsigned long last_click;
    int last_click_row;
} S;

/* ---------------- Decoder ---------------- */
static void track_close(Track *t)
{
    if (t->f)
        fclose(t->f);
    memset(t, 0, sizeof(*t));
}
static void fill_input(Track *t, int force)
{
    size_t rem = t->in_fill - t->in_pos;
    if (t->eof || (!force && rem >= IN_LOW))
        return;
    if (t->in_pos) {
        memmove(t->in, t->in + t->in_pos, rem);
        t->in_pos = 0;
        t->in_fill = rem;
    }
    size_t want = IN_CHUNK;
    if (want > IN_SIZE - t->in_fill)
        want = IN_SIZE - t->in_fill;
    if (want > t->left)
        want = t->left;
    size_t n = want ? fread(t->in + t->in_fill, 1, want, t->f) : 0;
    t->in_fill += n;
    t->left -= (uint32_t)n;
    if (!n || !t->left)
        t->eof = 1;
}
/* Decode the next chunk into pcm (interleaved). Returns frames, 0 at end. */
static int track_read(Track *t, int16_t *pcm)
{
    if (t->kind == PL_WAV) {
        int blk = t->channels * t->bits / 8;
        size_t want = (size_t)FRAME_MAX * blk;
        if (want > t->left)
            want = t->left;
        uint8_t raw[FRAME_MAX * 4];
        size_t got = want ? fread(raw, 1, want, t->f) : 0;
        got -= got % blk;
        t->left -= (uint32_t)got;
        int frames = (int)(got / blk);
        for (int i = 0; i < frames * t->channels; i++)
            pcm[i] = t->bits == 8 ? (int16_t)((raw[i] - 128) * 256) : (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8);
        return frames;
    }
    size_t scanned = 0;
    for (;;) {
        fill_input(t, 0);
        size_t avail = t->in_fill - t->in_pos;
        if (!avail)
            return 0;
        mp3dec_frame_info_t info;
        unsigned long t0 = now_ms();
        int n = mp3dec_decode_frame(&t->dec, t->in + t->in_pos, (int)avail, pcm, &info);
        S.dec_ms += now_ms() - t0;
        if (!info.frame_bytes) {
            if (t->eof)
                return 0;
            if (avail >= IN_SIZE - 16) /* buffer full of junk */
                t->in_pos += avail - 4;
            else
                fill_input(t, 1);
            continue;
        }
        t->in_pos += info.frame_bytes;
        if (n > 0) {
            t->hz = info.hz;
            t->channels = info.channels;
            t->kbps = info.bitrate_kbps;
            return n;
        }
        scanned += info.frame_bytes;
        if (scanned > 256 * 1024)
            return 0; /* not audio */
    }
}
static void track_seek(Track *t, int permille)
{
    uint32_t off = seek_offset(t->start, t->end, permille);
    if (t->kind == PL_WAV) {
        int blk = t->channels * t->bits / 8;
        off = t->start + (off - t->start) / blk * blk;
    }
    if (fseek(t->f, (long)off, SEEK_SET))
        return;
    t->left = t->end - off;
    t->in_fill = t->in_pos = 0;
    t->eof = t->done = 0;
    t->pend_n = t->pend_off = 0;
    mp3dec_init(&t->dec);
}
static void set_message(const char *s) { snprintf(S.message, sizeof(S.message), "%s", s); }
static int track_open(Track *t, const char *path)
{
    memset(t, 0, sizeof(*t));
    t->f = do_open(path);
    if (!t->f) {
        set_message("Cannot open file");
        return 0;
    }
    fseek(t->f, 0, SEEK_END);
    long size = ftell(t->f);
    fseek(t->f, 0, SEEK_SET);
    if (size < 128 || size > 0x7fffff00L) {
        set_message("File is empty or too small");
        goto fail;
    }
    t->kind = pl_kind(path);
    if (t->kind == PL_WAV) {
        uint8_t head[4096];
        size_t n = fread(head, 1, sizeof(head), t->f);
        WavInfo w;
        if (!wav_parse(head, n, &w)) {
            set_message("Unsupported WAV (need 8 or 16 bit PCM, 8-48 kHz)");
            goto fail;
        }
        t->hz = (int)w.rate;
        t->channels = w.channels;
        t->bits = w.bits;
        t->start = w.data_off;
        t->end = w.data_off + w.data_bytes;
        if (t->end > (uint32_t)size || t->end < t->start)
            t->end = (uint32_t)size; /* streamed or truncated files */
        t->total_ms = (uint32_t)((uint64_t)(t->end - t->start) * 1000 / ((uint64_t)t->hz * t->channels * t->bits / 8));
        fseek(t->f, (long)t->start, SEEK_SET);
        t->left = t->end - t->start;
        return 1;
    }
    /* MP3: ID3v2 at the start, ID3v1 in the last 128 bytes. */
    static uint8_t tagbuf[TAG_WINDOW];
    uint8_t h10[10];
    t->start = 0;
    t->end = (uint32_t)size;
    if (fread(h10, 1, 10, t->f) == 10) {
        size_t total = id3v2_total(h10, 10);
        if (total && total < (size_t)size) {
            size_t take = total < TAG_WINDOW ? total : TAG_WINDOW;
            memcpy(tagbuf, h10, 10);
            size_t got = take > 10 ? fread(tagbuf + 10, 1, take - 10, t->f) + 10 : 10;
            id3v2_parse(tagbuf, got, &t->tags);
            t->start = (uint32_t)total;
        }
    }
    uint8_t v1[128];
    if ((uint32_t)size > t->start + 128 && fseek(t->f, size - 128, SEEK_SET) == 0 && fread(v1, 1, 128, t->f) == 128 &&
        !memcmp(v1, "TAG", 3)) {
        id3v1_parse(v1, &t->tags); /* fills only what ID3v2 left empty */
        t->end = (uint32_t)size - 128;
    }
    if (t->end <= t->start || fseek(t->f, (long)t->start, SEEK_SET)) {
        set_message("No audio data");
        goto fail;
    }
    t->left = t->end - t->start;
    mp3dec_init(&t->dec);
    /* Duration: Xing/Info/VBRI frame count, else constant-bitrate estimate. */
    fill_input(t, 1);
    for (size_t i = 0; i + 8 < t->in_fill && i < 4096; i++) {
        Mp3Head mh, next;
        if (!mp3_head(t->in + i, &mh))
            continue;
        if (i + mh.frame_bytes + 4 <= t->in_fill && !mp3_head(t->in + i + mh.frame_bytes, &next))
            continue;
        uint32_t frames = mp3_vbr_frames(t->in + i, t->in_fill - i, &mh);
        if (frames) {
            t->vbr = 1;
            t->total_ms = (uint32_t)((uint64_t)frames * mh.samples * 1000 / mh.hz);
        } else if (mh.bitrate_kbps) {
            t->total_ms = (uint32_t)((uint64_t)(t->end - t->start - i) * 8 / mh.bitrate_kbps);
        }
        break;
    }
    /* Decode the first frame now: it tells us the true rate and channels. */
    t->pend_n = track_read(t, t->pend);
    if (t->pend_n <= 0 || t->hz < DC_AUDIO_MIN_RATE || t->hz > DC_AUDIO_MAX_RATE || t->channels < 1 || t->channels > 2) {
        set_message("Not a playable MP3 file");
        goto fail;
    }
    return 1;
fail:
    track_close(t);
    return 0;
}

/* ---------------- Audio control ---------------- */
static long audio_info_get(struct dc_audio_info *info)
{
    memset(info, 0, sizeof(*info));
    if (!S.have_audio || !dc_os->audio_info)
        return -1;
    long r = dc_os->audio_info(info, sizeof(*info));
    return r == (long)sizeof(*info) && info->version == DC_AUDIO_VERSION ? 0 : -1;
}
static void audio_stop_stream(void)
{
    if (S.have_audio && S.stream_hz)
        dc_os->audio_close();
    S.stream_hz = S.stream_ch = 0;
}
static void reset_progress(uint32_t base_ms)
{
    S.base_ms = base_ms;
    S.pushed = 0;
    hist_n = 0;
    S.empty_since = 0;
    S.dec_ms = S.audio_ms = 0;
    S.load_t0 = now_ms();
    memset(S.bands, 0, sizeof(S.bands));
    memset(S.hold, 0, sizeof(S.hold));
    S.peak = 0;
}
static void stop(void)
{
    audio_stop_stream();
    track_close(&tk);
    S.state = STOPPED;
    S.cur = -1;
    reset_progress(0);
}
static int play_row(int row)
{
    if (row < 0 || row >= S.pl.count || S.pl.e[row].dir)
        return 0;
    if (!S.have_audio) {
        set_message("This OS build has no audio support");
        return 0;
    }
    char path[128];
    pl_join(path, sizeof(path), S.dir, S.pl.e[row].name);
    S.message[0] = 0;
    track_close(&tk);
    if (!track_open(&tk, path)) {
        audio_stop_stream();
        S.state = STOPPED;
        S.cur = -1;
        S.sel = row;
        return 0;
    }
    if (!tk.tags.title[0])
        snprintf(tk.tags.title, sizeof(tk.tags.title), "%s", S.pl.e[row].name);
    /* Reuse the stream when the format is unchanged (gapless enough). */
    if (S.stream_hz != tk.hz || S.stream_ch != tk.channels) {
        audio_stop_stream();
        long rc = dc_os->audio_open((uint32_t)tk.hz, (uint32_t)tk.channels);
        if (rc < 0) {
            char m[80];
            snprintf(m, sizeof(m), "Audio unavailable (error %ld)", rc);
            set_message(m);
            track_close(&tk);
            S.state = STOPPED;
            S.cur = -1;
            return 0;
        }
        S.stream_hz = tk.hz;
        S.stream_ch = tk.channels;
    } else {
        dc_os->audio_set(DC_AUDIO_FLUSH, 0);
    }
    dc_os->audio_set(DC_AUDIO_VOLUME, (uint32_t)vol_to_hw(S.volume));
    dc_os->audio_set(DC_AUDIO_PAUSE, 0);
    S.cur = S.sel = row;
    S.state = PLAYING;
    reset_progress(0);
    return 1;
}
static void seek_to_ms(uint32_t ms);
static int advance(int dir)
{
    int next = S.cur >= 0 ? pl_step(&S.pl, S.cur, dir, S.repeat) : pl_step(&S.pl, S.sel, dir, 0);
    if (next < 0) {
        if (dir < 0 && S.state != STOPPED) { /* nothing before the first track: restart it */
            seek_to_ms(0);
            return 1;
        }
        stop();
        return 0;
    }
    return play_row(next);
}
static void set_pause(int pause)
{
    if (S.state == STOPPED)
        return;
    dc_os->audio_set(DC_AUDIO_PAUSE, pause);
    S.state = pause ? PAUSED : PLAYING;
}
static void set_volume(int v)
{
    S.volume = v < 0 ? 0 : v > VOL_STEPS ? VOL_STEPS : v;
    if (S.have_audio && S.stream_hz)
        dc_os->audio_set(DC_AUDIO_VOLUME, (uint32_t)vol_to_hw(S.volume));
}
static uint32_t position_ms(void)
{
    struct dc_audio_info info;
    if (S.state == STOPPED || audio_info_get(&info) || !tk.hz)
        return S.base_ms;
    return S.base_ms + (uint32_t)((uint64_t)info.position_frames * 1000 / (uint32_t)tk.hz);
}
static void seek_to_ms(uint32_t ms)
{
    if (S.state == STOPPED || !tk.total_ms)
        return;
    if (ms > tk.total_ms)
        ms = tk.total_ms;
    int permille = (int)((uint64_t)ms * 1000 / tk.total_ms);
    track_seek(&tk, permille);
    dc_os->audio_set(DC_AUDIO_FLUSH, 0);
    reset_progress((uint32_t)((uint64_t)permille * tk.total_ms / 1000));
}

/* Decode ahead of the OS ring; call often. Returns 1 if the track ended. */
static void pump(void)
{
    struct dc_audio_info info;
    if (S.state != PLAYING || !tk.f || audio_info_get(&info))
        return;
    S.underruns = info.underruns;
    uint32_t target = (uint32_t)tk.hz * 45 / 100, low = (uint32_t)tk.hz * 15 / 100;
    unsigned long t0 = now_ms();
    while (!tk.done || tk.pend_n) {
        if (tk.pend_n) {
            long n = dc_os->audio_write(tk.pend + (size_t)tk.pend_off * tk.channels, (uint32_t)tk.pend_n);
            if (n < 0) {
                set_message("Audio write failed");
                stop();
                return;
            }
            tk.pend_off += (int)n;
            tk.pend_n -= (int)n;
            S.pushed += (uint32_t)n;
            S.audio_ms += (unsigned long)n * 1000 / (unsigned)tk.hz;
            if (tk.pend_n)
                break;
        }
        if (audio_info_get(&info) || info.queued_frames >= target || info.free_frames < FRAME_MAX)
            break;
        if (tk.done)
            break;
        int frames = track_read(&tk, tk.pend);
        if (frames <= 0) {
            tk.done = 1;
            break;
        }
        tk.pend_n = frames;
        tk.pend_off = 0;
        /* Keep a mono snapshot to draw when this audio is actually heard. */
        Hist *h = &hist[hist_n++ % HIST];
        h->end_frame = S.pushed + (uint32_t)frames;
        for (int i = 0; i < VIS_N; i++) {
            int k = frames - VIS_N + i;
            if (k < 0)
                h->mono[i] = 0;
            else if (tk.channels == 2)
                h->mono[i] = (int16_t)((tk.pend[2 * k] + tk.pend[2 * k + 1]) / 2);
            else
                h->mono[i] = tk.pend[k];
        }
        if (dc_os->yield)
            dc_os->yield();
        if (now_ms() - t0 > (info.queued_frames < low ? 150u : 25u))
            break;
    }
    /* Track finished when everything is queued and the ring has drained. */
    if (tk.done && !tk.pend_n) {
        if (audio_info_get(&info))
            return;
        if (info.queued_frames == 0) {
            if (!S.empty_since)
                S.empty_since = now_ms() ? now_ms() : 1;
            else if (now_ms() - S.empty_since > 300) /* let the AICA buffer play out */
                advance(1);
        } else {
            S.empty_since = 0;
        }
    }
}

/* ---------------- Browsing ---------------- */
static int gem_list(const char *dir, Playlist *pl)
{
    uint8_t dta[44] __attribute__((aligned(4)));
    char pattern[128];
    pl_join(pattern, sizeof(pattern), dir, "*.*");
    long old = dc_os->gemdos(0x2f); /* Fgetdta */
    dc_os->gemdos(0x1a, dta);       /* Fsetdta */
    long r = dc_os->gemdos(0x4e, pattern, 0x10);
    int ok = r == 0 || r == -33; /* found, or no files */
    while (r == 0) {
        const char *name = (const char *)dta + 30;
        uint32_t size = dta[26] | (uint32_t)dta[27] << 8 | (uint32_t)dta[28] << 16 | (uint32_t)dta[29] << 24;
        if (strcmp(name, ".") && (dta[21] & 0x10 || pl_kind(name)))
            pl_add(pl, name, (dta[21] & 0x10) ? 0 : size, (dta[21] & 0x10) != 0);
        r = dc_os->gemdos(0x4f); /* Fsnext */
    }
    dc_os->gemdos(0x1a, (void *)old);
    return ok;
}
static void scan(void)
{
    memset(&S.pl, 0, sizeof(S.pl));
    int (*lister)(const char *, Playlist *) = list_dir ? list_dir : gem_list;
    if (!lister(S.dir, &S.pl))
        set_message("Cannot read this drive");
    char up[96];
    snprintf(up, sizeof(up), "%s", S.dir);
    if (pl_parent(up))
        pl_add(&S.pl, "..", 0, 1);
    S.sel = pl_first_file(&S.pl);
    if (S.sel < 0)
        S.sel = 0;
    S.top = 0;
    S.cur = -1;
}
static void enter_dir(const char *dir)
{
    snprintf(S.dir, sizeof(S.dir), "%s", dir);
    S.message[0] = 0;
    scan();
    if (S.state != STOPPED) /* browsing elsewhere does not interrupt playback */
        S.cur = -1;
}
static void find_drives(void)
{
    static struct dc_system_info info;
    uint32_t mask = (1u << 2) | (1u << 3); /* C: and D: without system_info */
    if (APP_HAS(system_info) && dc_os->system_info(&info, sizeof(info)) == (long)sizeof(info) &&
        info.version == DC_SYSTEM_INFO_VERSION)
        mask = info.drive_mask;
    S.ndrives = 0;
    for (int d = 2; d < 26 && S.ndrives < 8; d++)
        if (mask & (1u << d))
            S.drives[S.ndrives++] = 'A' + d;
}
static void select_drive(int letter)
{
    char dir[4] = {(char)letter, ':', '\\', 0};
    /* Playback continues across drive changes; the list is what changes. */
    enter_dir(dir);
}
static int default_drive(void)
{
    char sd = dc_storage_drive();
    if (sd != 'C')
        return sd;
    for (int i = 0; i < S.ndrives; i++)
        if (S.drives[i] == 'D')
            return 'D';
    return S.ndrives ? S.drives[0] : 'D';
}
static int cur_drive_index(void)
{
    for (int i = 0; i < S.ndrives; i++)
        if (S.drives[i] == S.dir[0])
            return i;
    return -1;
}
static void activate(int row)
{
    if (row < 0 || row >= S.pl.count)
        return;
    if (S.pl.e[row].dir) {
        if (!strcmp(S.pl.e[row].name, "..")) {
            char up[96];
            snprintf(up, sizeof(up), "%s", S.dir);
            if (pl_parent(up))
                enter_dir(up);
        } else {
            char sub[128];
            pl_join(sub, sizeof(sub), S.dir, S.pl.e[row].name);
            enter_dir(sub);
        }
    } else {
        play_row(row);
    }
}

/* ---------------- Layout and drawing ---------------- */
enum { PAPER = 0, INK = 1, RED = 2, LIGHTGREEN = 3, BLUE = 4, YELLOW = 6, FACE = 8, EDGE = 9, GREEN = 11 };
typedef struct { int x, y, w, h; } Rect;
enum { B_PREV, B_PLAY, B_STOP, B_NEXT, B_VOLD, B_VOLU, B_REPEAT, B_UP, B_DOWN, B_COUNT };
static struct { int x, y, w, h; } L = {0, 0, 600, 440};
static Rect r_vis, r_prog, r_time, r_btn[B_COUNT], r_vol, r_drv[8], r_list, r_path;
static int list_rows;
static AppWindow window;
enum { D_HEAD = 1, D_VIS = 2, D_PROG = 4, D_BTN = 8, D_DRIVE = 16, D_LIST = 32, D_ALL = 63 };
static unsigned dirty, draw_mask = D_ALL;

static void compute_layout(void)
{
    L.x = window.work.x;
    L.y = window.work.y;
    L.w = window.work.w;
    L.h = window.work.h;
    int W = L.w, H = L.h;
    r_vis = (Rect){10, 60, W - 20, 48};
    r_prog = (Rect){10, 114, W - 20, 10};
    r_time = (Rect){10, 126, W - 20, 16};
    r_btn[B_PREV] = (Rect){10, 148, 56, 24};
    r_btn[B_PLAY] = (Rect){70, 148, 72, 24};
    r_btn[B_STOP] = (Rect){146, 148, 56, 24};
    r_btn[B_NEXT] = (Rect){206, 148, 56, 24};
    r_btn[B_VOLU] = (Rect){W - 36, 148, 26, 24};
    r_vol = (Rect){W - 36 - 4 - VOL_STEPS * 7, 148, VOL_STEPS * 7, 24};
    r_btn[B_VOLD] = (Rect){r_vol.x - 4 - 26, 148, 26, 24};
    r_btn[B_REPEAT] = (Rect){W - 92, 178, 82, 18};
    for (int i = 0; i < 8; i++)
        r_drv[i] = (Rect){52 + i * 28, 178, 24, 18};
    r_path = (Rect){52 + S.ndrives * 28 + 4, 178, W - 92 - (52 + S.ndrives * 28 + 4) - 4, 18};
    list_rows = (H - 22 - 200) / 16;
    if (list_rows < 1)
        list_rows = 1;
    r_list = (Rect){10, 200, W - 38, list_rows * 16};
    r_btn[B_UP] = (Rect){W - 26, 200, 16, 16};
    r_btn[B_DOWN] = (Rect){W - 26, 200 + list_rows * 16 - 16, 16, 16};
    if (S.sel < S.top)
        S.top = S.sel;
    if (S.sel >= S.top + list_rows)
        S.top = S.sel - list_rows + 1;
    if (S.top < 0)
        S.top = 0;
}
static void box(int x, int y, int w, int h, int c) { app_box(L.x + x, L.y + y, w, h, c); }
static void text(int x, int y, const char *s, int c) { app_text(L.x + x, L.y + y, s, c); }
static void line(int x, int y, int x2, int y2, int c) { app_line(L.x + x, L.y + y, L.x + x2, L.y + y2, c); }
static void border(int x, int y, int w, int h, int c)
{
    line(x, y, x + w - 1, y, c);
    line(x, y, x, y + h - 1, c);
    line(x + w - 1, y, x + w - 1, y + h - 1, c);
    line(x, y + h - 1, x + w - 1, y + h - 1, c);
}
static void fit(char *dst, size_t n, const char *src, int chars)
{
    if (chars < 1)
        chars = 1;
    if ((int)strlen(src) <= chars) {
        snprintf(dst, n, "%s", src);
    } else {
        snprintf(dst, n, "%.*s", chars > 3 ? chars - 3 : chars, src);
        if (chars > 3)
            strcat(dst, "...");
    }
}
static void button(Rect r, const char *label, int down, int face)
{
    box(r.x + 2, r.y + 2, r.w, r.h, EDGE);
    box(r.x, r.y, r.w, r.h, face);
    border(r.x, r.y, r.w, r.h, INK);
    line(r.x + 1, r.y + 1, r.x + r.w - 2, r.y + 1, down ? EDGE : PAPER);
    line(r.x + 1, r.y + 1, r.x + 1, r.y + r.h - 2, down ? EDGE : PAPER);
    int tw = (int)strlen(label) * 8;
    text(r.x + (r.w - tw) / 2 + down, r.y + (r.h + 13) / 2 - 1 + down, label, INK);
}
static int in_rect(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static const char *state_name(void) { return S.state == PLAYING ? "Playing" : S.state == PAUSED ? "Paused" : "Stopped"; }
static void draw_vis(void)
{
    int n = VIS_BANDS, bw = r_vis.w / n, bh = r_vis.h - 4;
    int ox = r_vis.x + (r_vis.w - bw * n) / 2;
    box(r_vis.x, r_vis.y, r_vis.w, r_vis.h, INK);
    /* Each column is drawn as level + background, never cleared first: no flicker. */
    for (int i = 0; i < n; i++) {
        int h = S.bands[i] * bh / 255, hp = S.hold[i] * bh / 255;
        int x = ox + i * bw, y0 = r_vis.y + 2 + bh;
        box(x, r_vis.y + 2, bw - 1, bh - h, INK);
        if (h > 0)
            box(x, y0 - h, bw - 1, h, S.bands[i] > 215 ? RED : S.bands[i] > 150 ? YELLOW : LIGHTGREEN);
        if (hp > h)
            box(x, y0 - hp - 1, bw - 1, 2, PAPER);
    }
}
static void draw_progress(void)
{
    uint32_t pos = position_ms();
    int fill = tk.total_ms ? (int)((uint64_t)(pos > tk.total_ms ? tk.total_ms : pos) * (r_prog.w - 2) / tk.total_ms) : 0;
    box(r_prog.x, r_prog.y, r_prog.w, r_prog.h, PAPER);
    border(r_prog.x, r_prog.y, r_prog.w, r_prog.h, INK);
    box(r_prog.x + 1, r_prog.y + 1, fill, r_prog.h - 2, BLUE);
    box(r_time.x, r_time.y, r_time.w, r_time.h, FACE);
    char a[16], b[16], s[96];
    fmt_time(a, sizeof(a), S.state == STOPPED ? 0 : pos);
    fmt_time(b, sizeof(b), tk.total_ms);
    snprintf(s, sizeof(s), "%s / %s", a, tk.total_ms ? b : "?:??");
    text(r_time.x, r_time.y + 13, s, INK);
    if (S.state != STOPPED) {
        snprintf(s, sizeof(s), "decode load %d%%   underruns %u", S.load_pct, (unsigned)S.underruns);
        text(r_time.x + r_time.w - (int)strlen(s) * 8, r_time.y + 13, s, S.load_pct > 85 ? RED : EDGE);
    }
}
static void draw(void)
{
    unsigned m = draw_mask;
    int chars = (L.w - 20) / 8;
    char buf[160];
    if (m == D_ALL)
        box(0, 0, L.w, L.h, FACE);
    if (m & D_HEAD) {
        box(0, 0, L.w, 58, FACE);
        if (S.state == STOPPED && !tk.f) {
            text(10, 18, "MP3 Player", INK);
            text(10, 36, S.message[0] ? S.message : "Choose a file below, then press Return or Space", S.message[0] ? RED : EDGE);
            text(10, 54, S.have_audio ? "Reads MP3 and WAV from the SD card (E: to H:), C: or D:" : "This OS build has no audio support", EDGE);
        } else {
            fit(buf, sizeof(buf), tk.tags.title, chars);
            text(10, 18, buf, INK);
            snprintf(buf, sizeof(buf), "%s%s%s", tk.tags.artist, tk.tags.artist[0] && tk.tags.album[0] ? "  -  " : "", tk.tags.album);
            char f[160];
            fit(f, sizeof(f), buf, chars);
            text(10, 36, f, EDGE);
            if (S.message[0]) {
                text(10, 54, S.message, RED);
            } else {
                snprintf(buf, sizeof(buf), "%s %s  %d Hz  %s  %s", state_name(), tk.kind == PL_WAV ? "WAV" : "MP3", tk.hz,
                         tk.channels == 2 ? "stereo" : "mono",
                         tk.kind == PL_WAV ? "" : tk.vbr ? "VBR" : "");
                if (tk.kind == PL_MP3 && !tk.vbr && tk.kbps)
                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%d kbps", tk.kbps);
                text(10, 54, buf, BLUE);
            }
        }
    }
    if (m & D_VIS)
        draw_vis();
    if (m & D_PROG)
        draw_progress();
    if (m & D_BTN) {
        box(0, 146, L.w, 30, FACE);
        button(r_btn[B_PREV], "|<", S.pressed == B_PREV, FACE);
        button(r_btn[B_PLAY], S.state == PLAYING ? "Pause" : "Play", S.pressed == B_PLAY, GREEN);
        button(r_btn[B_STOP], "Stop", S.pressed == B_STOP, FACE);
        button(r_btn[B_NEXT], ">|", S.pressed == B_NEXT, FACE);
        button(r_btn[B_VOLD], "-", S.pressed == B_VOLD, FACE);
        button(r_btn[B_VOLU], "+", S.pressed == B_VOLU, FACE);
        box(r_vol.x, r_vol.y + 4, r_vol.w, r_vol.h - 8, PAPER);
        border(r_vol.x, r_vol.y + 4, r_vol.w, r_vol.h - 8, INK);
        box(r_vol.x + 1, r_vol.y + 5, S.volume * 7 - 2 > 0 ? S.volume * 7 - 2 : 0, r_vol.h - 10, BLUE);
    }
    if (m & D_DRIVE) {
        box(0, 176, L.w, 22, FACE);
        text(10, 191, "Drive", INK);
        for (int i = 0; i < S.ndrives; i++) {
            char l[3] = {(char)S.drives[i], 0, 0};
            int on = S.dir[0] == S.drives[i];
            box(r_drv[i].x, r_drv[i].y, r_drv[i].w, r_drv[i].h, on ? BLUE : FACE);
            border(r_drv[i].x, r_drv[i].y, r_drv[i].w, r_drv[i].h, INK);
            text(r_drv[i].x + 8, r_drv[i].y + 14, l, on ? PAPER : INK);
        }
        fit(buf, sizeof(buf), S.dir, r_path.w / 8);
        text(r_path.x, r_path.y + 14, buf, INK);
        char rep[16];
        snprintf(rep, sizeof(rep), "Repeat %s", S.repeat ? "on" : "off");
        button(r_btn[B_REPEAT], rep, 0, S.repeat ? GREEN : FACE);
    }
    if (m & D_LIST) {
        box(r_list.x, r_list.y, r_list.w, r_list.h, PAPER);
        border(r_list.x - 1, r_list.y - 1, r_list.w + 2, r_list.h + 2, INK);
        for (int r = 0; r < list_rows; r++) {
            int i = S.top + r, y = r_list.y + r * 16;
            if (i >= S.pl.count) {
                if (!S.pl.count && r == 0)
                    text(r_list.x + 6, y + 13, "(no MP3 or WAV files here)", EDGE);
                continue;
            }
            int sel = i == S.sel, playing = i == S.cur;
            if (sel)
                box(r_list.x, y, r_list.w, 16, BLUE);
            char row[64], name[24];
            snprintf(name, sizeof(name), "%s%s", S.pl.e[i].name, S.pl.e[i].dir && strcmp(S.pl.e[i].name, "..") ? "\\" : "");
            if (S.pl.e[i].dir)
                snprintf(row, sizeof(row), "%s", name);
            else
                snprintf(row, sizeof(row), "%-13s %6u KB", name, (unsigned)((S.pl.e[i].size + 1023) / 1024));
            text(r_list.x + 20, y + 13, row, sel ? PAPER : INK);
            if (playing)
                text(r_list.x + 4, y + 13, ">", sel ? PAPER : RED);
        }
        Rect u = r_btn[B_UP], d = r_btn[B_DOWN];
        button(u, "^", S.pressed == B_UP, FACE);
        button(d, "v", S.pressed == B_DOWN, FACE);
        text(10, L.h - 6, "Space play/pause  Enter open  Left/Right seek  N/P track  +/- volume  S stop  R repeat  D drive  Esc quit", EDGE);
    }
}
static Rect dirty_rect(unsigned m)
{
    if (m == D_ALL || (m & (D_HEAD | D_LIST)))
        return (Rect){0, 0, L.w, L.h};
    int y0 = 1 << 20, y1 = 0;
    if (m & D_VIS) { y0 = r_vis.y; y1 = r_vis.y + r_vis.h; }
    if (m & D_PROG) { y0 = y0 < r_prog.y ? y0 : r_prog.y; y1 = r_time.y + r_time.h; }
    if (m & D_BTN) { y0 = y0 < 146 ? y0 : 146; y1 = y1 > 176 ? y1 : 176; }
    if (m & D_DRIVE) { y0 = y0 < 176 ? y0 : 176; y1 = y1 > 198 ? y1 : 198; }
    return (Rect){0, y0, L.w, y1 - y0};
}
#ifndef MP3_NO_MAIN
static void redraw(unsigned m)
{
    if (!m)
        return;
    Rect r = dirty_rect(m);
    draw_mask = m;
    app_window_redraw(&window, (AppRect){(int16_t)(L.x + r.x), (int16_t)(L.y + r.y), (int16_t)r.w, (int16_t)r.h}, draw);
    draw_mask = D_ALL;
}
#endif

/* ---------------- Input ---------------- */
static void move_sel(int delta)
{
    int n = S.sel + delta;
    if (n < 0)
        n = 0;
    if (n >= S.pl.count)
        n = S.pl.count - 1;
    if (n < 0)
        n = 0;
    S.sel = n;
    if (S.sel < S.top)
        S.top = S.sel;
    if (S.sel >= S.top + list_rows)
        S.top = S.sel - list_rows + 1;
    dirty |= D_LIST;
}
static void toggle_play(void)
{
    if (S.state == STOPPED)
        play_row(S.sel < S.pl.count && !S.pl.e[S.sel].dir ? S.sel : pl_first_file(&S.pl));
    else
        set_pause(S.state == PLAYING);
}
static void do_button(int id)
{
    switch (id) {
    case B_PREV:
        /* Past the first 3 seconds, Previous restarts the track like most players. */
        if (S.state != STOPPED && position_ms() > 3000)
            seek_to_ms(0);
        else
            advance(-1);
        break;
    case B_PLAY: toggle_play(); break;
    case B_STOP: stop(); break;
    case B_NEXT: advance(1); break;
    case B_VOLD: set_volume(S.volume - 1); break;
    case B_VOLU: set_volume(S.volume + 1); break;
    case B_REPEAT: S.repeat = !S.repeat; break;
    case B_UP: move_sel(-list_rows); break;
    case B_DOWN: move_sel(list_rows); break;
    }
    dirty |= D_ALL;
}
static int hit_button(int x, int y)
{
    for (int i = 0; i < B_COUNT; i++)
        if (in_rect(r_btn[i], x, y))
            return i;
    return -1;
}
/* One event path for the real GEM loop and the host tests.
 * Coordinates are window-relative; returns -1 to quit. */
static int event(int key, int x, int y, int buttons)
{
    int old = S.buttons;
    S.buttons = buttons;
    if ((buttons & 1) && !(old & 1)) {
        int b = hit_button(x, y);
        S.pressed = b;
        if (b >= 0) {
            dirty |= D_BTN | D_LIST | D_DRIVE;
        } else if (in_rect(r_prog, x, y) && tk.total_ms && S.state != STOPPED) {
            seek_to_ms((uint32_t)((uint64_t)(x - r_prog.x) * tk.total_ms / (uint32_t)r_prog.w));
            dirty |= D_PROG;
        } else if (in_rect(r_vol, x, y)) {
            set_volume((x - r_vol.x) / 7 + 1);
            dirty |= D_BTN;
        } else if (in_rect(r_list, x, y)) {
            int row = S.top + (y - r_list.y) / 16;
            if (row < S.pl.count) {
                int again = row == S.last_click_row && now_ms() - S.last_click < 600;
                S.sel = row;
                S.last_click_row = again ? -1 : row;
                S.last_click = now_ms();
                dirty |= D_LIST;
                if (again)
                    activate(row);
                dirty |= D_ALL;
            }
        } else {
            for (int i = 0; i < S.ndrives; i++)
                if (in_rect(r_drv[i], x, y)) {
                    select_drive(S.drives[i]);
                    dirty |= D_ALL;
                }
        }
    }
    if (!(buttons & 1) && (old & 1)) {
        int b = S.pressed;
        S.pressed = -1;
        if (b >= 0 && b == hit_button(x, y))
            do_button(b);
        dirty |= D_BTN | D_LIST;
    }
    if (!key)
        return 0;
    int ch = key & 255, scan = (key >> 8) & 255;
    if (ch == 27)
        return -1;
    if (ch == ' ') {
        toggle_play();
        dirty |= D_ALL;
    } else if (ch == 13) {
        activate(S.sel);
        dirty |= D_ALL;
    } else if (scan == 0x48 && ch == 0) move_sel(-1);
    else if (scan == 0x50 && ch == 0) move_sel(1);
    else if (scan == 0x49) move_sel(-list_rows);
    else if (scan == 0x51) move_sel(list_rows);
    else if (scan == 0x47 && ch == 0) move_sel(-S.pl.count);
    else if (scan == 0x4f && ch == 0) move_sel(S.pl.count);
    else if (scan == 0x4b && ch == 0) {
        uint32_t p = position_ms();
        seek_to_ms(p > 10000 ? p - 10000 : 0);
        dirty |= D_PROG;
    } else if (scan == 0x4d && ch == 0) {
        seek_to_ms(position_ms() + 10000);
        dirty |= D_PROG;
    } else if (ch == 'n' || ch == 'N') do_button(B_NEXT);
    else if (ch == 'p' || ch == 'P') do_button(B_PREV);
    else if (ch == 's' || ch == 'S') do_button(B_STOP);
    else if (ch == '+' || ch == '=') do_button(B_VOLU);
    else if (ch == '-' || ch == '_') do_button(B_VOLD);
    else if (ch == 'r' || ch == 'R') do_button(B_REPEAT);
    else if (ch == 'd' || ch == 'D') {
        if (S.ndrives) {
            select_drive(S.drives[(cur_drive_index() + 1) % S.ndrives]);
            dirty |= D_ALL;
        }
    } else if (ch == 8 || ch == 127) {
        char up[96];
        snprintf(up, sizeof(up), "%s", S.dir);
        if (pl_parent(up))
            enter_dir(up);
        dirty |= D_ALL;
    }
    return 0;
}
/* Time-driven work: measured decode load, visualiser and progress. */
static void tick(void)
{
    unsigned long now = now_ms();
    if (S.state == PLAYING && now - S.load_t0 >= 2000) {
        S.load_pct = S.audio_ms ? (int)(S.dec_ms * 100 / S.audio_ms) : 0;
        S.dec_ms = S.audio_ms = 0;
        S.load_t0 = now;
    }
    if (S.state == STOPPED || now - S.last_ui < 90)
        return;
    S.last_ui = now;
    uint32_t pos = position_ms();
    if (S.state == PLAYING && hist_n && tk.hz) {
        struct dc_audio_info info;
        uint32_t heard = 0;
        if (!audio_info_get(&info))
            heard = info.position_frames > (uint32_t)tk.hz / 10 ? info.position_frames - (uint32_t)tk.hz / 10 : 0;
        const Hist *best = NULL;
        unsigned n = hist_n < HIST ? hist_n : HIST;
        for (unsigned i = 0; i < n; i++) {
            const Hist *h = &hist[(hist_n - 1 - i) % HIST];
            if (h->end_frame <= heard + 1152 && (!best || h->end_frame > best->end_frame))
                best = h;
        }
        if (best) {
            S.peak = vis_analyse(best->mono, S.bands);
            for (int i = 0; i < VIS_BANDS; i++) {
                if (S.bands[i] >= S.hold[i])
                    S.hold[i] = S.bands[i];
                else
                    S.hold[i] = S.hold[i] > 12 ? S.hold[i] - 12 : 0;
            }
        }
    } else if (S.state == PAUSED) {
        for (int i = 0; i < VIS_BANDS; i++)
            S.bands[i] = S.bands[i] > 30 ? S.bands[i] - 30 : 0;
    }
    if (pos / 1000 != S.last_pos_ms / 1000 || S.state == PLAYING)
        dirty |= D_VIS | D_PROG;
    S.last_pos_ms = pos;
}

#ifndef MP3_NO_MAIN
static void finish(void)
{
    stop();
    app_window_close(&window);
    app_end();
}
static int window_key(int key, int modifiers)
{
    int scan = (key >> 8) & 255;
    if (scan == 0x3f) {
        app_window_full(&window);
    } else if ((modifiers & 4) && (scan == 72 || scan == 80 || scan == 75 || scan == 77)) {
        AppRect bounds = window.border;
        int dx = scan == 75 ? -16 : scan == 77 ? 16 : 0;
        int dy = scan == 72 ? -16 : scan == 80 ? 16 : 0;
        if (modifiers & 3) {
            bounds.w += dx;
            bounds.h += dy;
        } else {
            bounds.x += dx;
            bounds.y += dy;
        }
        app_window_bounds(&window, bounds);
        window.full = 0;
    } else {
        return 0;
    }
    return 1;
}
#endif
static void init_state(void)
{
    memset(&S, 0, sizeof(S));
    S.cur = -1;
    S.pressed = -1;
    S.last_click_row = -1;
    S.volume = 12;
    S.have_audio = APP_HAS(audio_open) && APP_HAS(audio_close) && APP_HAS(audio_write) && APP_HAS(audio_set) && APP_HAS(audio_info);
    find_drives();
    select_drive(default_drive());
}

#ifndef MP3_NO_MAIN
int app_main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "TEST"))
        return 0;
    if (!app_begin_windowed())
        return 1;
    if (!app_window_open(&window, "MP3 Player", 600, 440, 480, 340)) {
        app_alert("Unable to open the player window.");
        app_end();
        return 1;
    }
    atexit(finish);
    init_state();
    if (!S.have_audio)
        set_message("This OS build has no audio support");
    compute_layout();
    redraw(D_ALL);
    for (;;) {
        pump();
        AppEvent e;
        app_window_event(&e, S.state == PLAYING ? 4 : 100, S.buttons);
        if (e.flags & APP_MESSAGE) {
            int result = app_window_message(&window, e.message);
            if (result == WINDOW_CLOSE)
                break;
            if (result == WINDOW_CHANGED) {
                compute_layout();
                dirty |= D_ALL;
            } else if (result == WINDOW_REDRAW) {
                AppRect damage = window.work;
                if (e.message[0] == 20)
                    damage = (AppRect){e.message[4], e.message[5], e.message[6], e.message[7]};
                draw_mask = D_ALL;
                app_window_redraw(&window, damage, draw);
            }
        }
        if (window_key(e.key, e.modifiers)) {
            compute_layout();
            dirty |= D_ALL;
        } else {
            int inside = app_window_contains(&window, e.x, e.y);
            if (event(e.key, inside ? e.x - L.x : -1, inside ? e.y - L.y : -1, e.buttons) < 0)
                break;
        }
        tick();
        compute_layout();
        if (dirty) {
            unsigned m = dirty;
            dirty = 0;
            redraw(m);
        }
    }
    return 0;
}
#endif
