/* MP3 player: pure logic (playlist, tags, headers, WAV, visualiser).
 * GPL-2.0-or-later. No OS calls, so the host tests exercise it directly. */
#ifndef DC_MP3_CORE_H
#define DC_MP3_CORE_H
#include <stddef.h>
#include <stdint.h>

/* ---- Folder listing / playlist ---- */
#define PL_MAX 256
enum { PL_NONE = 0, PL_MP3 = 1, PL_WAV = 2 };
typedef struct {
    char name[16]; /* 8.3 name, upper case */
    uint32_t size;
    uint8_t dir;
} PlEntry;
typedef struct {
    PlEntry e[PL_MAX];
    int count;
} Playlist;
int pl_kind(const char *name);                            /* by extension */
int pl_add(Playlist *, const char *name, uint32_t size, int dir); /* sorted; index or -1 */
int pl_step(const Playlist *, int cur, int dir, int wrap); /* next (+1) / previous (-1) file; -1 if none */
int pl_first_file(const Playlist *);
int pl_file_count(const Playlist *);
void pl_join(char *out, size_t n, const char *dir, const char *name); /* "E:\MUSIC" + "A.MP3" */
int pl_parent(char *dir);                                 /* "E:\A\B" -> "E:\A"; 0 at root */

/* ---- Tags ---- */
typedef struct {
    char title[64], artist[64], album[64];
} Tags;
/* Size of an ID3v2 tag (header, body and footer) from its 10-byte header; 0 if none. */
size_t id3v2_total(const uint8_t *header, size_t len);
/* Parse a tag held in buf[0..len) (may be truncated; the buffer is modified). */
int id3v2_parse(uint8_t *buf, size_t len, Tags *);
int id3v1_parse(const uint8_t b[128], Tags *);

/* ---- MPEG audio frame headers ---- */
typedef struct {
    int version;  /* 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5 */
    int hz, channels, bitrate_kbps, frame_bytes, samples;
} Mp3Head;
int mp3_head(const uint8_t *p, Mp3Head *); /* layer III only; 0 if not a valid header */
/* Frame count from a Xing/Info/VBRI header in the first frame, or 0. */
uint32_t mp3_vbr_frames(const uint8_t *frame, size_t len, const Mp3Head *);

/* ---- WAV (PCM 8 or 16 bit, mono or stereo) ---- */
typedef struct {
    uint32_t rate, data_off, data_bytes;
    int channels, bits;
} WavInfo;
int wav_parse(const uint8_t *buf, size_t len, WavInfo *);

/* ---- Presentation helpers ---- */
void fmt_time(char *out, size_t n, uint32_t ms);
int vol_to_hw(int step);                                 /* 0..VOL_STEPS -> 0..255 */
#define VOL_STEPS 16
uint32_t seek_offset(uint32_t start, uint32_t end, int permille);

/* ---- Visualiser ---- */
#define VIS_N 512
#define VIS_BANDS 24
/* mono: VIS_N samples. bands: 0..255 per log-spaced band. Returns peak 0..255. */
int vis_analyse(const int16_t *mono, uint8_t bands[VIS_BANDS]);
#endif
