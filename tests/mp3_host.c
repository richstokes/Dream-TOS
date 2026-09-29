/* MP3 player: pure logic, the real minimp3 decoder against a generated MP3
 * fixture, and the whole UI/decode/audio pipeline against a fake AICA.
 * argv[1] is a scratch directory. */
#define MP3_NO_MAIN
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include "audio_fake_hal.h"
#include "../src/dreamcast/audio.c"
#include "../apps/ports/mp3.c"

/* ---------------- Fixture: a minimal but real MPEG-1 Layer III stream ----------------
 * Each frame is mono, 32 kbps, one non-zero spectral line (line 20, about 785 Hz),
 * built with the Huffman table 1 pair (1,0) plus a sign bit. No encoder is needed
 * and the output is bit-exact on every host. */
static void put_bits(uint8_t *b, int *pos, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--, (*pos)++)
        if ((v >> i) & 1)
            b[*pos >> 3] |= (uint8_t)(0x80 >> (*pos & 7));
}
static int frame_bytes_for(int sr_idx) { return 144 * 32000 / (int[]){44100, 48000, 32000}[sr_idx]; }
static int make_frame(uint8_t *f, int sr_idx, int gain)
{
    int n = frame_bytes_for(sr_idx), pos = 32;
    memset(f, 0, (size_t)n);
    f[0] = 0xff;
    f[1] = 0xfb; /* MPEG-1, Layer III, no CRC */
    f[2] = (uint8_t)(1 << 4 | sr_idx << 2);
    f[3] = 0xc0; /* mono */
    put_bits(f, &pos, 0, 9);
    put_bits(f, &pos, 0, 5);
    put_bits(f, &pos, 0, 4);
    for (int gr = 0; gr < 2; gr++) {
        put_bits(f, &pos, 13, 12); /* part2_3_length */
        put_bits(f, &pos, 11, 9);  /* big_values */
        put_bits(f, &pos, (uint32_t)gain, 8);
        put_bits(f, &pos, 0, 4);
        put_bits(f, &pos, 0, 1);
        for (int t = 0; t < 3; t++)
            put_bits(f, &pos, 1, 5); /* table 1 */
        put_bits(f, &pos, 0, 4 + 3 + 1 + 1 + 1);
    }
    pos = 21 * 8;
    for (int gr = 0; gr < 2; gr++) {
        for (int i = 0; i < 10; i++)
            put_bits(f, &pos, 1, 1); /* (0,0) */
        put_bits(f, &pos, 1, 2);     /* (1,0) */
        put_bits(f, &pos, 0, 1);     /* sign */
    }
    return n;
}
static size_t make_stream(uint8_t *out, int frames, int sr_idx, int gain)
{
    size_t n = 0;
    for (int i = 0; i < frames; i++)
        n += (size_t)make_frame(out + n, sr_idx, gain);
    return n;
}
static void put_syncsafe(uint8_t *p, uint32_t v)
{
    p[0] = v >> 21 & 127;
    p[1] = v >> 14 & 127;
    p[2] = v >> 7 & 127;
    p[3] = v & 127;
}
static size_t id3_frame(uint8_t *out, int ver, const char *id, const uint8_t *data, size_t n)
{
    size_t o = 0;
    memcpy(out, id, ver == 2 ? 3 : 4);
    o = ver == 2 ? 3 : 4;
    if (ver == 2) {
        out[o++] = (uint8_t)(n >> 16);
        out[o++] = (uint8_t)(n >> 8);
        out[o++] = (uint8_t)n;
    } else {
        if (ver == 3) {
            out[o++] = 0;
            out[o++] = 0;
            out[o++] = (uint8_t)(n >> 8);
            out[o++] = (uint8_t)n;
        } else {
            put_syncsafe(out + o, (uint32_t)n);
            o += 4;
        }
        out[o++] = 0;
        out[o++] = 0;
    }
    memcpy(out + o, data, n);
    return o + n;
}
static size_t id3_tag(uint8_t *out, int ver, size_t body)
{
    memcpy(out, "ID3", 3);
    out[3] = (uint8_t)ver;
    out[4] = 0;
    out[5] = 0;
    put_syncsafe(out + 6, (uint32_t)body);
    return 10;
}
static void write_file(const char *path, const uint8_t *a, size_t an, const uint8_t *b, size_t bn, const uint8_t *c, size_t cn)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    if (an)
        assert(fwrite(a, 1, an, f) == an);
    if (bn)
        assert(fwrite(b, 1, bn, f) == bn);
    if (cn)
        assert(fwrite(c, 1, cn, f) == cn);
    fclose(f);
}
static double goertzel(const int16_t *x, int n, double hz, double rate)
{
    double w = 2 * M_PI * hz / rate, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

/* ---------------- Pure logic ---------------- */
static void test_playlist(void)
{
    static Playlist pl;
    assert(pl_kind("A.MP3") == PL_MP3 && pl_kind("song.mp3") == PL_MP3 && pl_kind("X.WAV") == PL_WAV);
    assert(pl_kind("NOEXT") == PL_NONE && pl_kind("A.MP33") == PL_NONE && pl_kind("A.TXT") == PL_NONE && pl_kind("A.MP") == PL_NONE);
    assert(pl_add(&pl, "B.MP3", 10, 0) == 0);
    assert(pl_add(&pl, "a.mp3", 20, 0) == 0); /* upper-cased, sorted first */
    assert(pl_add(&pl, "C.WAV", 30, 0) == 2);
    assert(pl_add(&pl, "README.TXT", 5, 0) == -1); /* not audio */
    assert(pl_add(&pl, "A.MP3", 1, 0) == -1);      /* duplicate */
    assert(pl_add(&pl, "TOOLONGNAME.MP3", 1, 0) == -1);
    assert(pl_add(&pl, "ZED", 0, 1) == 0);  /* folders first */
    assert(pl_add(&pl, "..", 0, 1) == 0);   /* parent before other folders */
    assert(pl.count == 5 && !strcmp(pl.e[0].name, "..") && !strcmp(pl.e[1].name, "ZED") && !strcmp(pl.e[2].name, "A.MP3"));
    assert(pl_file_count(&pl) == 3 && pl_first_file(&pl) == 2);
    /* Stepping skips folders; wrapping is optional. */
    assert(pl_step(&pl, 2, 1, 0) == 3 && pl_step(&pl, 4, 1, 0) == -1 && pl_step(&pl, 4, 1, 1) == 2);
    assert(pl_step(&pl, 2, -1, 0) == -1 && pl_step(&pl, 2, -1, 1) == 4 && pl_step(&pl, 3, -1, 0) == 2);
    static Playlist empty;
    assert(pl_step(&empty, 0, 1, 1) == -1 && pl_first_file(&empty) == -1);
    static Playlist dirs;
    pl_add(&dirs, "X", 0, 1);
    assert(pl_step(&dirs, 0, 1, 1) == -1); /* only folders */
    for (int i = 0; i < PL_MAX + 5; i++) { /* bounded */
        char n[16];
        snprintf(n, sizeof(n), "T%d.MP3", i);
        pl_add(&empty, n, 1, 0);
    }
    assert(empty.count == PL_MAX);

    char p[64];
    pl_join(p, sizeof(p), "E:\\", "A.MP3");
    assert(!strcmp(p, "E:\\A.MP3"));
    pl_join(p, sizeof(p), "E:\\MUSIC", "A.MP3");
    assert(!strcmp(p, "E:\\MUSIC\\A.MP3"));
    pl_join(p, sizeof(p), "E:\\MUSIC\\ROCK", "A.MP3");
    assert(!strcmp(p, "E:\\MUSIC\\ROCK\\A.MP3"));
    pl_join(p, 6, "E:\\MUSIC", "A.MP3"); /* truncates, never overruns */
    assert(strlen(p) == 5);
    char d[32] = "E:\\A\\B";
    assert(pl_parent(d) && !strcmp(d, "E:\\A"));
    assert(pl_parent(d) && !strcmp(d, "E:\\"));
    assert(!pl_parent(d) && !strcmp(d, "E:\\"));
    char bad[8] = "E:";
    assert(!pl_parent(bad));
}
static void test_tags(void)
{
    Tags t;
    uint8_t tag[512], body[256];
    size_t o;
    /* v2.3: Latin-1 title, UTF-16 (BOM) artist, padding after the frames */
    memset(&t, 0, sizeof(t));
    memset(tag, 0, sizeof(tag));
    o = 10;
    memcpy(body, "\x00Tone A", 7);
    o += id3_frame(tag + o, 3, "TIT2", body, 7);
    memcpy(body, "\x01\xff\xfeT\0e\0s\0t\0e\0r\0", 15);
    o += id3_frame(tag + o, 3, "TPE1", body, 15);
    memcpy(body, "\x00" "Caf\xe9", 5);
    o += id3_frame(tag + o, 3, "TALB", body, 5);
    o += 20; /* padding */
    id3_tag(tag, 3, o - 10);
    assert(id3v2_total(tag, o) == o);
    assert(id3v2_parse(tag, o, &t) == 3);
    assert(!strcmp(t.title, "Tone A") && !strcmp(t.artist, "Tester") && !strcmp(t.album, "Caf?"));

    /* v2.4: UTF-8 with a multi-byte character, syncsafe frame sizes, footer flag */
    memset(&t, 0, sizeof(t));
    memset(tag, 0, sizeof(tag));
    o = 10;
    memcpy(body, "\x03Na\xc3\xafve \xe2\x82\xac", 11);
    o += id3_frame(tag + o, 4, "TIT2", body, 11);
    memcpy(body, "\x03" "Band\0Other", 11);
    o += id3_frame(tag + o, 4, "TPE1", body, 11); /* first of several values only */
    id3_tag(tag, 4, o - 10);
    assert(id3v2_parse(tag, o, &t) == 2);
    assert(!strcmp(t.title, "Na?ve ?") && !strcmp(t.artist, "Band"));
    tag[5] = 0x10; /* footer present: total grows by ten */
    assert(id3v2_total(tag, o) == o + 10);

    /* v2.2: three-letter ids and 24-bit sizes */
    memset(&t, 0, sizeof(t));
    memset(tag, 0, sizeof(tag));
    o = 10;
    memcpy(body, "\x00Old", 4);
    o += id3_frame(tag + o, 2, "TT2", body, 4);
    memcpy(body, "\x00Timer", 6);
    o += id3_frame(tag + o, 2, "TP1", body, 6);
    id3_tag(tag, 2, o - 10);
    assert(id3v2_parse(tag, o, &t) == 2 && !strcmp(t.title, "Old") && !strcmp(t.artist, "Timer"));

    /* Truncated, wrongly sized and hostile tags never read out of bounds. */
    memset(&t, 0, sizeof(t));
    memset(tag, 0, sizeof(tag));
    o = 10;
    memcpy(body, "\x00Tone A", 7);
    o += id3_frame(tag + o, 3, "TIT2", body, 7);
    id3_tag(tag, 3, 400); /* claims more than we hold */
    assert(id3v2_parse(tag, o, &t) == 1 && !strcmp(t.title, "Tone A"));
    id3v2_parse(tag, o - 3, &t); /* frame cut short: ignored, no overread */
    assert(id3v2_total((const uint8_t *)"ID3\x09\0\0\0\0\0\0", 10) == 0);
    assert(id3v2_total((const uint8_t *)"ID3\x03\0\0\x80\0\0\0", 10) == 0); /* non-syncsafe size */
    assert(id3v2_total((const uint8_t *)"ID3", 3) == 0);
    unsigned seed = 12345;
    for (int iter = 0; iter < 4000; iter++) {
        uint8_t junk[300];
        for (size_t i = 0; i < sizeof(junk); i++) {
            seed = seed * 1103515245u + 12345u;
            junk[i] = (uint8_t)(seed >> 16);
        }
        memcpy(junk, "ID3", 3);
        junk[3] = (uint8_t)(2 + iter % 3);
        junk[4] = 0;
        junk[5] = (uint8_t)(iter % 5 ? 0 : 0xff & seed);
        put_syncsafe(junk + 6, (uint32_t)(iter % 400));
        memset(&t, 0, sizeof(t));
        id3v2_parse(junk, sizeof(junk) - (size_t)(iter % 40), &t);
        assert(strlen(t.title) < 64 && strlen(t.artist) < 64 && strlen(t.album) < 64);
    }

    /* v1 fills only what v2 left empty and never trusts unterminated fields */
    uint8_t v1[128];
    memset(v1, 0, sizeof(v1));
    memcpy(v1, "TAG", 3);
    memcpy(v1 + 3, "Old Title                     ", 30);
    memcpy(v1 + 33, "Some Artist\xe9", 12);
    memset(v1 + 63, 'A', 30); /* album fills its field with no terminator */
    memset(&t, 0, sizeof(t));
    strcpy(t.title, "Keep");
    assert(id3v1_parse(v1, &t) == 1);
    assert(!strcmp(t.title, "Keep") && !strcmp(t.artist, "Some Artist?") && strlen(t.album) == 30);
    v1[0] = 'X';
    assert(!id3v1_parse(v1, &t));
}
static void test_headers_wav_misc(void)
{
    Mp3Head h;
    uint8_t f[128];
    make_frame(f, 0, 200);
    assert(mp3_head(f, &h) && h.version == 1 && h.hz == 44100 && h.channels == 1 && h.bitrate_kbps == 32 &&
           h.samples == 1152 && h.frame_bytes == 104);
    make_frame(f, 1, 200);
    assert(mp3_head(f, &h) && h.hz == 48000 && h.frame_bytes == 96);
    uint8_t bad[4] = {0xff, 0xfb, 0xf0, 0xc0}; /* bitrate index 15 */
    assert(!mp3_head(bad, &h));
    bad[2] = 0x1c; /* sample-rate index 3 */
    assert(!mp3_head(bad, &h));
    uint8_t l2[4] = {0xff, 0xfd, 0x10, 0xc0}; /* layer II is not supported here */
    assert(!mp3_head(l2, &h));
    uint8_t v2[4] = {0xff, 0xf3, 0x60, 0x00}; /* MPEG-2 22.05 kHz stereo, 48 kbps */
    assert(mp3_head(v2, &h) && h.version == 2 && h.hz == 22050 && h.samples == 576 && h.channels == 2 &&
           h.bitrate_kbps == 48 && h.frame_bytes == 72 * 48000 / 22050);
    assert(!mp3_head((const uint8_t *)"ID3\x03", &h));

    /* Xing and VBRI frame counts */
    uint8_t x[256] = {0};
    make_frame(x, 0, 200);
    mp3_head(x, &h);
    memset(x + 21, 0, 60);
    memcpy(x + 21, "Xing", 4);
    x[28] = 1;    /* flags: frame count present */
    x[31] = 0x03;
    x[32] = 0xe8; /* 1000 frames */
    assert(mp3_vbr_frames(x, sizeof(x), &h) == 1000);
    assert(mp3_vbr_frames(x, 30, &h) == 0); /* truncated */
    x[28] = 0;
    assert(mp3_vbr_frames(x, sizeof(x), &h) == 0); /* frames flag clear */
    memset(x + 21, 0, 60);
    memcpy(x + 36, "VBRI", 4);
    x[36 + 17] = 0x02;
    x[36 + 16] = 0x01; /* frames are bytes 14..17 after the tag, big-endian: 258 */
    assert(mp3_vbr_frames(x, sizeof(x), &h) == 0x0102);

    /* WAV */
    uint8_t w[64] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x22\x56\0\0\x88\x58\x01\0\x04\0\x10\0data\x00\x10\0\0";
    WavInfo wi;
    assert(wav_parse(w, 44, &wi) && wi.channels == 2 && wi.rate == 22050 && wi.bits == 16 && wi.data_off == 44 &&
           wi.data_bytes == 4096);
    w[20] = 3; /* float */
    assert(!wav_parse(w, 44, &wi));
    w[20] = 1;
    w[22] = 6; /* six channels */
    assert(!wav_parse(w, 44, &wi));
    w[22] = 2;
    assert(!wav_parse(w, 30, &wi) && !wav_parse(w, 8, &wi));
    w[16] = 0xff;
    w[17] = 0xff;
    w[18] = 0xff;
    w[19] = 0x7f; /* absurd fmt chunk size must not overflow the walk */
    assert(!wav_parse(w, 44, &wi));

    /* helpers */
    char s[16];
    fmt_time(s, sizeof(s), 0);
    assert(!strcmp(s, "0:00"));
    fmt_time(s, sizeof(s), 61999);
    assert(!strcmp(s, "1:01"));
    fmt_time(s, sizeof(s), 3600 * 1000);
    assert(!strcmp(s, "60:00"));
    assert(vol_to_hw(0) == 0 && vol_to_hw(-3) == 0 && vol_to_hw(VOL_STEPS) == 255 && vol_to_hw(99) == 255);
    for (int i = 1; i <= VOL_STEPS; i++)
        assert(vol_to_hw(i) > vol_to_hw(i - 1));
    assert(seek_offset(100, 1100, 0) == 100 && seek_offset(100, 1100, 500) == 600 && seek_offset(100, 1100, 1000) == 1100);
    assert(seek_offset(100, 1100, 5000) == 1100 && seek_offset(100, 1100, -5) == 100 && seek_offset(50, 40, 500) == 50);
    assert(seek_offset(0, 0xffffff00u, 999) < 0xffffff00u);
}
static void test_visualiser(void)
{
    int16_t x[VIS_N] = {0};
    uint8_t b[VIS_BANDS];
    assert(vis_analyse(x, b) == 0);
    for (int i = 0; i < VIS_BANDS; i++)
        assert(b[i] == 0);
    for (int i = 0; i < VIS_N; i++)
        x[i] = (int16_t)(20000 * sin(2 * M_PI * 1000.0 * i / 44100.0));
    int peak = vis_analyse(x, b);
    assert(peak > 140 && peak <= 255);
    int best = 0;
    for (int i = 1; i < VIS_BANDS; i++)
        if (b[i] > b[best])
            best = i;
    assert(best >= 9 && best <= 12); /* 1 kHz is bin 11.6 of 86 Hz */
    assert(b[best] > 200 && b[VIS_BANDS - 1] < 60 && b[0] < 60);
    for (int i = 0; i < VIS_N; i++)
        x[i] = i & 1 ? 32767 : -32768; /* full-scale Nyquist: top band, no overflow */
    assert(vis_analyse(x, b) == 255 && b[VIS_BANDS - 1] > 200);
}

/* ---------------- Decoder against the generated stream ---------------- */
#define NFRAMES 400
static uint8_t stream44[NFRAMES * 104], stream48[NFRAMES * 96];
static size_t stream44_bytes, stream48_bytes;
static int16_t ref44[NFRAMES * 1152];
static void test_decoder(const char *root)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/dec.mp3", root);
    static uint8_t tag[256];
    size_t o = 10;
    uint8_t body[16];
    memcpy(body, "\x00Tone A", 7);
    o += id3_frame(tag + o, 3, "TIT2", body, 7);
    id3_tag(tag, 3, o - 10);
    uint8_t v1[128] = {0};
    memcpy(v1, "TAGV1 title", 11);
    write_file(path, tag, o, stream44, stream44_bytes, v1, 128);

    /* Reference decode straight from memory, independent of the streaming reader. */
    mp3dec_t ref;
    mp3dec_init(&ref);
    size_t used = 0, total = 0;
    while (used < stream44_bytes) {
        mp3dec_frame_info_t info;
        int n = mp3dec_decode_frame(&ref, stream44 + used, (int)(stream44_bytes - used), ref44 + total, &info);
        if (!info.frame_bytes)
            break;
        used += (size_t)info.frame_bytes;
        total += (size_t)n;
    }
    assert(total == (size_t)NFRAMES * 1152);
    /* The tone is really there: about 785 Hz, and far stronger than 3 kHz or 200 Hz. */
    int peak = 0;
    for (size_t i = 0; i < total; i++)
        if (abs(ref44[i]) > peak)
            peak = abs(ref44[i]);
    assert(peak > 500 && peak < 32768);
    double e785 = 0, e785b = 0;
    for (double hz = 700; hz <= 870; hz += 10) {
        double e = goertzel(ref44 + 44100, 4096, hz, 44100);
        if (e > e785)
            e785 = e, e785b = hz;
    }
    assert(e785 > 25 * goertzel(ref44 + 44100, 4096, 3000, 44100) && e785 > 25 * goertzel(ref44 + 44100, 4096, 200, 44100));
    assert(e785b >= 730 && e785b <= 840);

    /* The streaming reader (refills, ID3 skip, ID3v1 trim) yields the very same PCM. */
    S.message[0] = 0;
    static Track t;
    assert(track_open(&t, path));
    assert(t.kind == PL_MP3 && t.hz == 44100 && t.channels == 1 && t.kbps == 32);
    assert(!strcmp(t.tags.title, "Tone A")); /* v2 wins over v1 */
    assert(t.start == o && t.end == o + stream44_bytes);
    assert(t.total_ms >= 10400 && t.total_ms <= 10500); /* CBR estimate for 400 frames */
    size_t got = 0;
    memcpy(ref44 + 0, ref44, 0);
    int16_t pcm[1152 * 2];
    int n = t.pend_n;
    memcpy(pcm, t.pend, (size_t)n * 2);
    for (;;) {
        assert(got + (size_t)n <= total);
        assert(!memcmp(pcm, ref44 + got, (size_t)n * 2));
        got += (size_t)n;
        n = track_read(&t, pcm);
        if (n <= 0)
            break;
    }
    assert(got == total);
    /* Seeking: half way, then the remaining audio is half of it (within a frame). */
    track_seek(&t, 500);
    size_t rest = 0;
    while ((n = track_read(&t, pcm)) > 0)
        rest += (size_t)n;
    assert(rest > total * 45 / 100 && rest < total * 55 / 100);
    track_close(&t);

    /* Xing header: total length comes from the frame count. */
    static uint8_t xing[104 + NFRAMES * 104];
    make_frame(xing, 0, 200);
    memset(xing + 4, 0, 100);
    memcpy(xing + 21, "Xing", 4);
    xing[28] = 1;
    xing[32] = 100; /* claims 100 frames although more exist */
    memcpy(xing + 104, stream44, stream44_bytes);
    snprintf(path, sizeof(path), "%s/xing.mp3", root);
    write_file(path, xing, 104 + stream44_bytes, NULL, 0, NULL, 0);
    assert(track_open(&t, path) && t.vbr && t.total_ms == (uint32_t)(100ull * 1152 * 1000 / 44100));
    track_close(&t);

    /* Junk, empty and truncated inputs are rejected with a message, never a hang. */
    static uint8_t junk[20000];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(i * 7 + 3);
    snprintf(path, sizeof(path), "%s/junk.mp3", root);
    write_file(path, junk, sizeof(junk), NULL, 0, NULL, 0);
    S.message[0] = 0;
    assert(!track_open(&t, path) && S.message[0] && !t.f);
    snprintf(path, sizeof(path), "%s/empty.mp3", root);
    write_file(path, NULL, 0, NULL, 0, NULL, 0);
    assert(!track_open(&t, path));
    assert(!track_open(&t, "/nonexistent/none.mp3"));
    snprintf(path, sizeof(path), "%s/cut.mp3", root);
    write_file(path, stream44, 150, NULL, 0, NULL, 0); /* one frame and a half */
    assert(track_open(&t, path));
    int frames = 1;
    while (track_read(&t, pcm) > 0)
        frames++;
    assert(frames == 1); /* the incomplete second frame is dropped */
    track_close(&t);

    /* WAV: 8-bit mono is widened, data chunk length is honoured. */
    uint8_t wav[44 + 400] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x40\x1f\0\0\x40\x1f\0\0\x01\0\x08\0data\x90\x01\0\0";
    for (int i = 0; i < 400; i++)
        wav[44 + i] = (uint8_t)(128 + (i % 2 ? 100 : -100));
    snprintf(path, sizeof(path), "%s/eight.wav", root);
    write_file(path, wav, sizeof(wav), NULL, 0, NULL, 0);
    assert(track_open(&t, path) && t.hz == 8000 && t.channels == 1 && t.bits == 8 && t.total_ms == 50);
    n = track_read(&t, pcm);
    assert(n == 400 && pcm[0] == -25600 && pcm[1] == 25600 && track_read(&t, pcm) == 0);
    track_close(&t);
}

/* ---------------- Whole pipeline: keyboard/mouse -> decode -> OS ring ---------------- */
static const char *g_root;
static unsigned long fake_now = 1000;
static unsigned long fake_clock(void) { return fake_now; }
static void host_path(char *out, size_t n, const char *gem)
{
    /* "D:\MUSIC\A.MP3" -> root/D/MUSIC/A.MP3 */
    snprintf(out, n, "%s/%c", g_root, gem[0]);
    for (const char *p = gem + 2; *p && strlen(out) + 2 < n; p++)
        strncat(out, *p == '\\' ? "/" : (char[]){*p, 0}, 1);
}
static FILE *host_open(const char *gem)
{
    char p[600];
    host_path(p, sizeof(p), gem);
    return fopen(p, "rb");
}
static int host_list(const char *gem, Playlist *pl)
{
    char p[600];
    host_path(p, sizeof(p), gem);
    DIR *d = opendir(p);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char full[900];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", p, e->d_name);
        if (!stat(full, &st))
            pl_add(pl, e->d_name, (uint32_t)st.st_size, S_ISDIR(st.st_mode));
    }
    closedir(d);
    return 1;
}
static void noop_yield(void) {}
static struct dc_native_api api;
static int16_t captured[NFRAMES * 1152 * 2];
static size_t cap_n;
/* One tick of the real main loop with a fake AICA taking audio at real-time pace. */
static void run_loop(int iterations, uint32_t consume_per_iter)
{
    for (int i = 0; i < iterations; i++) {
        pump();
        cap_n += fake_consume(captured + cap_n, consume_per_iter) * (fch ? fch : 1);
        fake_now += 40;
        tick();
    }
}
static int row_of(const char *name)
{
    for (int i = 0; i < S.pl.count; i++)
        if (!strcmp(S.pl.e[i].name, name))
            return i;
    return -1;
}
static void key(int ch, int scan) { assert(event((scan << 8) | ch, -1, -1, 0) == 0); }
static void click(Rect r)
{
    int x = r.x + r.w / 2, y = r.y + r.h / 2;
    event(0, x, y, 1);
    event(0, x, y, 0);
}
static void test_ui(const char *root)
{
    g_root = root;
    char dir[600], path[700];
    snprintf(dir, sizeof(dir), "%s/D", root);
    mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/D/MUSIC", root);
    mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/D/MUSIC/SUB", root);
    mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/D/MUSIC/README.TXT", root);
    write_file(dir, (const uint8_t *)"hi", 2, NULL, 0, NULL, 0);
    static uint8_t tag[128];
    size_t o = 10;
    uint8_t body[32];
    memcpy(body, "\x00Tone A", 7);
    o += id3_frame(tag + o, 3, "TIT2", body, 7);
    memcpy(body, "\x00Tester", 7);
    o += id3_frame(tag + o, 3, "TPE1", body, 7);
    id3_tag(tag, 3, o - 10);
    snprintf(path, sizeof(path), "%s/D/MUSIC/A.MP3", root);
    write_file(path, tag, o, stream44, stream44_bytes, NULL, 0);
    snprintf(path, sizeof(path), "%s/D/MUSIC/B.MP3", root);
    write_file(path, stream48, stream48_bytes, NULL, 0, NULL, 0); /* different rate: stream reopens */
    static uint8_t wav[44 + 8000 * 4];
    memcpy(wav, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x40\x1f\0\0\0\x7d\0\0\x04\0\x10\0data\x00\x7d\0\0", 44);
    for (int i = 0; i < 8000; i++) { /* 1 s stereo, 8 kHz: L ramp, R negated */
        int16_t l = (int16_t)(i * 3 - 12000);
        wav[44 + 4 * i] = (uint8_t)l;
        wav[45 + 4 * i] = (uint8_t)(l >> 8);
        wav[46 + 4 * i] = (uint8_t)-l;
        wav[47 + 4 * i] = (uint8_t)(-l >> 8);
    }
    snprintf(path, sizeof(path), "%s/D/MUSIC/C.WAV", root);
    write_file(path, wav, sizeof(wav), NULL, 0, NULL, 0);
    static uint8_t junk[3000];
    for (size_t i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(i * 13 + 1);
    snprintf(path, sizeof(path), "%s/D/MUSIC/SUB/BAD.MP3", root);
    write_file(path, junk, sizeof(junk), NULL, 0, NULL, 0);

    memset(&api, 0, sizeof(api));
    api.version = 1;
    api.size = sizeof(api);
    api.yield = noop_yield;
    api.audio_open = dc_audio_open;
    api.audio_close = dc_audio_close;
    api.audio_write = dc_audio_write;
    api.audio_space = dc_audio_space;
    api.audio_set = dc_audio_set;
    api.audio_info = dc_audio_info;
    dc_os = &api;
    open_file = host_open;
    list_dir = host_list;
    clock_ms = fake_clock;
    window.work = (AppRect){0, 0, 600, 440};

    /* Start: the SD/D: default drive and its folders. */
    init_state();
    compute_layout();
    assert(S.have_audio && !strcmp(S.dir, "D:\\") && S.ndrives == 2 && S.drives[0] == 'C' && S.drives[1] == 'D');
    assert(row_of("MUSIC") >= 0 && S.state == STOPPED);
    S.sel = row_of("MUSIC");
    key(13, 0); /* open the folder */
    assert(!strcmp(S.dir, "D:\\MUSIC") && row_of("..") == 0 && row_of("SUB") == 1);
    assert(row_of("README.TXT") < 0);
    assert(row_of("A.MP3") == 2 && row_of("B.MP3") == 3 && row_of("C.WAV") == 4 && S.sel == row_of("A.MP3"));

    /* Space starts the selected file and the OS stream matches its format. */
    key(' ', 0x39);
    assert(S.state == PLAYING && S.cur == row_of("A.MP3") && fstate_open && frate == 44100 && fch == 1);
    assert(!strcmp(tk.tags.title, "Tone A") && !strcmp(tk.tags.artist, "Tester") && fvol == (uint32_t)vol_to_hw(S.volume));
    /* Pump with an audio thread that takes 1152 frames per 40 ms tick (faster than real time
     * is fine: the ring is what limits decoding). Everything decoded arrives, in order. */
    cap_n = 0;
    run_loop(3, 1152);
    assert(cap_n > 0);
    assert(dc_audio_space() >= 0);
    struct dc_audio_info info;
    assert(dc_audio_info(&info, sizeof(info)) > 0 && info.queued_frames <= (uint32_t)44100 * 45 / 100 + 1152);
    for (int i = 0; i < 400 && S.cur == row_of("A.MP3"); i++)
        run_loop(1, 1152 * 4);
    /* A ended; only complete, bit-identical PCM was delivered before the switch. */
    assert(S.cur == row_of("B.MP3") && S.state == PLAYING);
    assert(cap_n <= (size_t)NFRAMES * 1152 && cap_n >= (size_t)NFRAMES * 1152 - 1152 * 8);
    assert(!memcmp(captured, ref44, (cap_n < 1152 * 64 ? cap_n : 1152 * 64) * 2));
    assert(frate == 48000 && fclose_count == 1 && fopen_count == 2); /* format change reopened the stream */
    assert(!strcmp(tk.tags.title, "B.MP3")); /* no tag: file name */

    /* Visualiser follows what is being heard: energy near 785 Hz of a 48 kHz stream. */
    cap_n = 0;
    run_loop(12, 1152);
    int best = 0;
    for (int i = 1; i < VIS_BANDS; i++)
        if (S.bands[i] > S.bands[best])
            best = i;
    assert(S.bands[best] > 100 && best >= 6 && best <= 10);
    assert((dirty & D_VIS) && S.load_pct >= 0 && S.load_pct <= 100);

    /* Pause holds the ring and mutes; resume continues. */
    key(' ', 0x39);
    assert(S.state == PAUSED && fpaused);
    size_t held = dc_ring_used(&fring);
    run_loop(3, 1152);
    assert(dc_ring_used(&fring) == held);
    key(' ', 0x39);
    assert(S.state == PLAYING && !fpaused);

    /* Volume keys and clicks stay within range and reach the OS. */
    int v = S.volume;
    key('+', 0);
    assert(S.volume == v + 1 && fvol == (uint32_t)vol_to_hw(v + 1));
    key('-', 0);
    key('-', 0);
    assert(S.volume == v - 1);
    for (int i = 0; i < 40; i++)
        key('-', 0);
    assert(S.volume == 0 && fvol == 0);
    for (int i = 0; i < 40; i++)
        key('=', 0);
    assert(S.volume == VOL_STEPS && fvol == 255);
    event(0, r_vol.x + 3, r_vol.y + 4, 1);
    event(0, r_vol.x + 3, r_vol.y + 4, 0);
    assert(S.volume == 1);
    S.volume = 12;
    set_volume(12);

    /* Seeking: arrows move by ten seconds, the progress bar by position, and both
     * flush the OS ring so the new position is heard promptly. */
    key('p', 0); /* within 3 s of the start: back to the previous file */
    assert(S.cur == row_of("A.MP3"));
    unsigned flushes = fflush_count;
    run_loop(4, 1152);
    key(0, 0x4d);
    assert(fflush_count == flushes + 1 && S.base_ms >= 9900 && S.base_ms <= 10300 && tk.total_ms > 10300);
    key(0, 0x4b);
    assert(S.base_ms < 300); /* about 10.1 s back to about 0.1 s */
    event(0, r_prog.x + r_prog.w / 2, r_prog.y + 3, 1);
    event(0, r_prog.x + r_prog.w / 2, r_prog.y + 3, 0);
    assert(S.base_ms > tk.total_ms * 45 / 100 && S.base_ms < tk.total_ms * 55 / 100);
    seek_to_ms(0);

    /* Previous after three seconds restarts the track instead. */
    fake_now += 10;
    run_loop(130, 1152);
    assert(position_ms() > 3000 && S.cur == row_of("A.MP3"));
    key('p', 0);
    assert(S.cur == row_of("A.MP3") && S.base_ms == 0 && position_ms() < 500);
    key('p', 0); /* first track, near the start, nothing before it: restarts, does not stop */
    assert(S.state == PLAYING);

    /* Next, and Stop closes the stream. Repeat wraps past the last file. */
    key('n', 0);
    assert(S.cur == row_of("B.MP3"));
    click(r_btn[B_NEXT]);
    assert(S.cur == row_of("C.WAV") && frate == 8000 && fch == 2);
    run_loop(4, 1152);
    click(r_btn[B_NEXT]);
    assert(S.state == STOPPED && !fstate_open && S.cur == -1); /* end of list without repeat */
    S.sel = row_of("C.WAV");
    key('r', 0);
    assert(S.repeat);
    key(' ', 0x39);
    assert(S.state == PLAYING && S.cur == row_of("C.WAV"));
    click(r_btn[B_NEXT]);
    assert(S.cur == row_of("A.MP3") && S.state == PLAYING && frate == 44100); /* wrapped round */
    key('r', 0);
    assert(!S.repeat);

    /* Bad file: message, no crash, stream closed. */
    stop();
    S.sel = row_of("SUB");
    key(13, 0);
    S.sel = row_of("BAD.MP3");
    key(13, 0);
    assert(S.state == STOPPED && S.message[0] && !fstate_open);
    key(8, 0);
    assert(!strcmp(S.dir, "D:\\MUSIC"));
    /* WAV content check: play it and compare the PCM the OS received. */
    S.sel = row_of("C.WAV");
    key(13, 0);
    assert(S.state == PLAYING && frate == 8000);
    cap_n = 0;
    static int16_t wavcap[8000 * 2];
    size_t wn = 0;
    for (int i = 0; i < 40 && wn < 8000 * 2; i++) {
        pump();
        wn += fake_consume(wavcap + wn / 2 * 2, 2048) * 2;
        fake_now += 40;
    }
    assert(wn == 8000 * 2);
    for (int i = 0; i < 8000; i++)
        assert(wavcap[2 * i] == (int16_t)(i * 3 - 12000) && wavcap[2 * i + 1] == (int16_t)(-(i * 3 - 12000)));
    /* It ends by itself and the (repeat-off) list stops. */
    for (int i = 0; i < 30 && S.state == PLAYING; i++) {
        pump();
        fake_now += 100;
    }
    assert(S.state == STOPPED && !fstate_open);

    /* Mouse in the list: click selects, second click opens; folders and drives navigate. */
    S.sel = 0;
    event(0, r_list.x + 30, r_list.y + 16 * row_of("SUB") + 4, 1);
    event(0, r_list.x + 30, r_list.y + 16 * row_of("SUB") + 4, 0);
    assert(S.sel == row_of("SUB") && !strcmp(S.dir, "D:\\MUSIC"));
    fake_now += 100;
    event(0, r_list.x + 30, r_list.y + 16 * row_of("SUB") + 4, 1);
    event(0, r_list.x + 30, r_list.y + 16 * row_of("SUB") + 4, 0);
    assert(!strcmp(S.dir, "D:\\MUSIC\\SUB") && S.pl.count == 2); /* ".." and BAD.MP3 */
    key(8, 0); /* Backspace: up */
    assert(!strcmp(S.dir, "D:\\MUSIC"));
    key('d', 0); /* next drive wraps C: */
    assert(!strcmp(S.dir, "C:\\"));
    click(r_drv[1]);
    assert(!strcmp(S.dir, "D:\\"));
    key(0, 0x50);
    key(0, 0x48);
    key(0, 0x4f);
    key(0, 0x47);
    key(0, 0x51);
    key(0, 0x49);
    assert(S.sel >= 0 && S.sel < S.pl.count);

    /* Escape quits; leaving stops the audio. */
    S.sel = row_of("MUSIC");
    key(13, 0);
    S.sel = row_of("A.MP3");
    key(13, 0);
    assert(S.state == PLAYING && fstate_open);
    assert(event(27, -1, -1, 0) == -1);
    stop();
    assert(!fstate_open && !tk.f);

    /* No audio in the OS: the player says so instead of crashing. */
    api.size = (uint32_t)offsetof(struct dc_native_api, net_resolve) + sizeof(api.net_resolve);
    init_state();
    compute_layout();
    assert(!S.have_audio);
    S.sel = row_of("MUSIC");
    key(13, 0);
    key(' ', 0x39);
    assert(S.state == STOPPED && strstr(S.message, "no audio"));
    key('+', 0);
    key(0, 0x4d);
    key('s', 0);
    /* An OS that refuses to open the stream is reported too. */
    api.size = sizeof(api);
    init_state();
    compute_layout();
    S.sel = row_of("MUSIC");
    key(13, 0);
    fail_open = 1;
    key(' ', 0x39);
    assert(S.state == STOPPED && strstr(S.message, "Audio unavailable"));
    fail_open = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    stream44_bytes = make_stream(stream44, NFRAMES, 0, 200);
    stream48_bytes = make_stream(stream48, NFRAMES, 1, 200);
    assert(stream44_bytes == NFRAMES * 104 && stream48_bytes == NFRAMES * 96);
    test_playlist();
    test_tags();
    test_headers_wav_misc();
    test_visualiser();
    test_decoder(argv[1]);
    test_ui(argv[1]);
    puts("mp3 ok");
    return 0;
}
