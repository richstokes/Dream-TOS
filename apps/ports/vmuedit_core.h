/* VMU Editor core: pure data handling with no GEM or OS calls, shared by
 * VMUEDIT.PRG and its host tests. GPL-2.0-or-later. */
#ifndef VMUEDIT_CORE_H
#define VMUEDIT_CORE_H
#include <stddef.h>
#include <stdint.h>

/* ---- 48x32 LCD bitmap -------------------------------------------------------
 * Same layout as the OS vmu_screen call: 6 bytes per row, top row first, the
 * most significant bit is the leftmost pixel, 1 = dark. */
#define LCD_W 48
#define LCD_H 32
#define LCD_BYTES 192
#define LCD_BMP_BYTES 318 /* 14 + 40 + 8 palette + 32 rows of 8 bytes */
int lcd_get(const uint8_t *lcd, int x, int y);
void lcd_put(uint8_t *lcd, int x, int y, int dark);
void lcd_invert(uint8_t *lcd);
void lcd_clear(uint8_t *lcd);
void lcd_flip_h(uint8_t *lcd);
void lcd_flip_v(uint8_t *lcd);
void lcd_shift(uint8_t *lcd, int dx, int dy); /* wraps around */

/* 1-bit, bottom-up BMP with palette white, black. Returns LCD_BMP_BYTES. */
size_t lcd_to_bmp(const uint8_t *lcd, uint8_t *out);
enum { BMP_OK = 0, BMP_TRUNCATED = -1, BMP_NOT_BMP = -2, BMP_UNSUPPORTED = -3, BMP_TOO_LARGE = -4 };
/* Accepts uncompressed 1/4/8/24/32-bit BMPs of any size up to 4096x4096. Pixels
 * darker than mid-grey become dark. Larger images are cropped to the top-left
 * 48x32, smaller ones padded with light pixels (*adjusted is set). */
int lcd_from_bmp(const uint8_t *data, size_t bytes, uint8_t *lcd, int *adjusted);
const char *bmp_error_text(int error);

/* ---- VMS save-file header (icon viewer/editor) ------------------------------
 * Layout as written by KOS vmu_pkg_build: 128-byte header (16+32+16 bytes of
 * text, icon count, animation speed, eyecatch type, CRC16, data length, 20
 * reserved bytes, 16 ARGB4444 palette entries), then 512 bytes per 32x32 4-bit
 * icon frame (left pixel in the high nibble, top row first), then the optional
 * eyecatch, then the data. The CRC covers header+icons+eyecatch+data with the
 * CRC field zeroed, and does not cover any block padding after the data. */
#define VMS_HEADER 128
#define VMS_ICON_BYTES 512
#define VMS_MAX_FRAMES 3
enum { VMS_OK = 0, VMS_SHORT = -1, VMS_ICONS = -2, VMS_EYECATCH = -3, VMS_LENGTH = -4, VMS_CRC = -5 };
struct vms_info {
    unsigned icons, speed, eyecatch, data_len, total;
    char short_text[17], long_text[33], app_id[17];
};
/* Validates the header, sizes and CRC. On success fills info. */
int vms_parse(const uint8_t *file, size_t bytes, struct vms_info *info);
const char *vms_error_text(int error);
uint16_t vms_crc16(const uint8_t *data, size_t bytes); /* CRC-16/CCITT, init 0 (XMODEM) */
int vms_pixel(const uint8_t *file, int frame, int x, int y);
void vms_set_pixel(uint8_t *file, int frame, int x, int y, int index);
uint16_t vms_palette(const uint8_t *file, int index);
void vms_set_palette(uint8_t *file, int index, uint16_t argb4444);
void vms_flip_h(uint8_t *file, int frame);
void vms_flip_v(uint8_t *file, int frame);
void vms_fill(uint8_t *file, int frame, int index);
/* Recomputes and stores the CRC after edits (info from vms_parse of the original). */
void vms_seal(uint8_t *file, const struct vms_info *info);
/* ARGB4444 to 0..255 RGB blended over white; returns 1 if (nearly) transparent. */
int argb4444_rgb(uint16_t value, int *r, int *g, int *b);

/* ---- names -------------------------------------------------------------------- */
int vmu_name_ok(const char *name); /* 1..12 printable ASCII, no leading space */
/* Suggested GEMDOS 8.3 name for a VMU file (keeps a short extension, else .VMS). */
void dos_name_for_vmu(const char *vmu_name, char out[13]);
/* Suggested VMU name for a GEMDOS file name (drops a .VMS extension). */
void vmu_name_for_dos(const char *dos_name, char out[13]);
#endif
