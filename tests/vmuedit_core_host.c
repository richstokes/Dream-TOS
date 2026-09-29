/* VMU Editor core: LCD bitmap operations, BMP I/O, VMS header/icon codec, names. */
#include "../apps/ports/vmuedit_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The CRC as written in KallistiOS net_crc16ccitt (kernel/net/net_crc.c), for cross-checking. */
static uint16_t kos_crc(const uint8_t *data, int size)
{
    uint16_t rv = 0, tmp;
    while (size--) {
        tmp = (rv >> 8) ^ *data++;
        tmp ^= tmp >> 4;
        rv = (rv << 8) ^ (tmp << 12) ^ (tmp << 5) ^ tmp;
    }
    return rv;
}
static void put16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, unsigned v) { put16(p, v); put16(p + 2, v >> 16); }

static void test_lcd(void)
{
    uint8_t a[LCD_BYTES], b[LCD_BYTES];
    lcd_clear(a);
    lcd_put(a, 0, 0, 1);
    lcd_put(a, 47, 31, 1);
    lcd_put(a, 9, 5, 1);
    assert(a[0] == 0x80 && a[191] == 0x01 && a[5 * 6 + 1] == 0x40);
    assert(lcd_get(a, 0, 0) && lcd_get(a, 47, 31) && lcd_get(a, 9, 5) && !lcd_get(a, 8, 5));
    assert(!lcd_get(a, -1, 0) && !lcd_get(a, 48, 0) && !lcd_get(a, 0, 32));
    lcd_put(a, 99, 99, 1); /* out of range is ignored */
    lcd_put(a, 9, 5, 0);
    assert(!lcd_get(a, 9, 5));
    memcpy(b, a, sizeof(a));
    lcd_invert(b);
    assert(lcd_get(b, 1, 0) && !lcd_get(b, 0, 0));
    lcd_invert(b);
    assert(!memcmp(a, b, sizeof(a)));
    lcd_flip_h(b);
    assert(lcd_get(b, 47, 0) && lcd_get(b, 0, 31) && !lcd_get(b, 0, 0));
    lcd_flip_h(b);
    assert(!memcmp(a, b, sizeof(a)));
    lcd_flip_v(b);
    assert(lcd_get(b, 0, 31) && lcd_get(b, 47, 0));
    lcd_flip_v(b);
    assert(!memcmp(a, b, sizeof(a)));
    lcd_shift(b, 1, 1);
    assert(lcd_get(b, 1, 1) && lcd_get(b, 0, 0)); /* (47,31) wraps to (0,0) */
    lcd_shift(b, -1, -1);
    assert(!memcmp(a, b, sizeof(a)));
    lcd_clear(b);
    assert(!memcmp(b, (uint8_t[LCD_BYTES]){0}, LCD_BYTES));
}

static void test_bmp(void)
{
    uint8_t lcd[LCD_BYTES], back[LCD_BYTES], bmp[LCD_BMP_BYTES];
    for (int i = 0; i < LCD_BYTES; i++)
        lcd[i] = (uint8_t)(i * 37 + 11);
    assert(lcd_to_bmp(lcd, bmp) == LCD_BMP_BYTES);
    assert(bmp[0] == 'B' && bmp[1] == 'M' && bmp[10] == 62 && bmp[18] == 48 && bmp[22] == 32 && bmp[28] == 1);
    assert(!memcmp(bmp + 62 + 8 * 31, lcd, 6)); /* file's last row is the top of the picture */
    int adjusted = 9;
    assert(lcd_from_bmp(bmp, sizeof(bmp), back, &adjusted) == BMP_OK && adjusted == 0);
    assert(!memcmp(lcd, back, LCD_BYTES));
    /* Errors leave the bitmap alone and return codes. */
    memset(back, 0xaa, sizeof(back));
    assert(lcd_from_bmp((const uint8_t *)"PNG", 3, back, NULL) == BMP_NOT_BMP);
    assert(lcd_from_bmp(bmp, 40, back, NULL) == BMP_TRUNCATED);
    assert(lcd_from_bmp(bmp, sizeof(bmp) - 1, back, NULL) == BMP_TRUNCATED);
    assert(back[0] == 0xaa);
    uint8_t bad[LCD_BMP_BYTES];
    memcpy(bad, bmp, sizeof(bad)); put32(bad + 30, 1); /* RLE8 */
    assert(lcd_from_bmp(bad, sizeof(bad), back, NULL) == BMP_UNSUPPORTED);
    memcpy(bad, bmp, sizeof(bad)); put32(bad + 18, 5000);
    assert(lcd_from_bmp(bad, sizeof(bad), back, NULL) == BMP_TOO_LARGE);
    memcpy(bad, bmp, sizeof(bad)); put16(bad + 28, 2);
    assert(lcd_from_bmp(bad, sizeof(bad), back, NULL) == BMP_UNSUPPORTED);
    memcpy(bad, bmp, sizeof(bad)); put32(bad + 10, 100000);
    assert(lcd_from_bmp(bad, sizeof(bad), back, NULL) == BMP_TRUNCATED);
    memcpy(bad, bmp, sizeof(bad)); put32(bad + 14, 12); /* OS/2 header */
    assert(lcd_from_bmp(bad, sizeof(bad), back, NULL) == BMP_UNSUPPORTED);
    /* Inverted palette (index 0 black) flips polarity. */
    memcpy(bad, bmp, sizeof(bad));
    memset(bad + 54, 0, 3); memset(bad + 58, 0xff, 3);
    assert(lcd_from_bmp(bad, sizeof(bad), back, NULL) == BMP_OK);
    lcd_invert(back);
    assert(!memcmp(back, lcd, LCD_BYTES));

    /* 24-bit, 100x50, top-down and bottom-up: dark pixel where x==y; cropped to 48x32. */
    for (int top_down = 0; top_down < 2; top_down++) {
        int w = 100, h = 50, stride = (w * 3 + 3) & ~3;
        uint8_t *big = calloc(1, 54 + stride * h);
        memcpy(big, "BM", 2);
        put32(big + 2, 54 + stride * h); put32(big + 10, 54); put32(big + 14, 40);
        put32(big + 18, w); put32(big + 22, top_down ? (unsigned)-h : (unsigned)h);
        put16(big + 26, 1); put16(big + 28, 24);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t *p = big + 54 + stride * (top_down ? y : h - 1 - y) + 3 * x;
                p[0] = p[1] = p[2] = x == y ? 0 : 255;
            }
        assert(lcd_from_bmp(big, 54 + stride * h, back, &adjusted) == BMP_OK && adjusted == 1);
        for (int y = 0; y < LCD_H; y++)
            for (int x = 0; x < LCD_W; x++)
                assert(lcd_get(back, x, y) == (x == y));
        free(big);
    }
    /* 8-bit palettised and 4-bit, smaller than the LCD: padded with light pixels. */
    uint8_t small[54 + 16 * 4 + 8 * 4];
    memset(small, 0, sizeof(small));
    memcpy(small, "BM", 2);
    put32(small + 10, 54 + 64); put32(small + 14, 40); put32(small + 18, 4); put32(small + 22, 2);
    put16(small + 26, 1); put16(small + 28, 4); put32(small + 46, 2);
    memset(small + 54 + 4, 0xff, 3); /* index 1 white; index 0 black */
    /* rows are bottom-up: row 0 of the file is the picture's bottom row. 4 pixels: 0,1,1,0 */
    small[54 + 64] = 0x01; small[54 + 65] = 0x10;
    small[54 + 68] = 0x10; small[54 + 69] = 0x01; /* top row of the picture: 1,0,0,1 */
    assert(lcd_from_bmp(small, sizeof(small), back, &adjusted) == BMP_OK && adjusted == 1);
    assert(!lcd_get(back, 0, 0) && lcd_get(back, 1, 0) && lcd_get(back, 2, 0) && !lcd_get(back, 3, 0));
    assert(lcd_get(back, 0, 1) && !lcd_get(back, 1, 1) && !lcd_get(back, 2, 1) && lcd_get(back, 3, 1));
    assert(!lcd_get(back, 4, 0) && !lcd_get(back, 0, 2) && !lcd_get(back, 47, 31));
    /* Palette index beyond the declared colours is rejected, not read out of bounds. */
    small[54 + 68] = 0x50;
    assert(lcd_from_bmp(small, sizeof(small), back, NULL) == BMP_TRUNCATED);
    /* A file too short for its declared pixels never over-reads. */
    put32(small + 22, 200);
    assert(lcd_from_bmp(small, sizeof(small), back, NULL) == BMP_TRUNCATED);
}

static size_t build_vms(uint8_t *f, unsigned icons, unsigned eyecatch, unsigned data_len)
{
    static const unsigned ec[4] = {0, 8064, 4544, 2048};
    size_t total = VMS_HEADER + 512u * icons + ec[eyecatch] + data_len;
    memset(f, 0, 16384);
    memcpy(f, "Test save       ", 16);
    memcpy(f + 16, "A long description for the save ", 32);
    memcpy(f + 48, "TestApp\0\0\0\0\0\0\0\0\0", 16);
    put16(f + 64, icons); put16(f + 66, 8); put16(f + 68, eyecatch); put32(f + 72, data_len);
    for (int i = 0; i < 16; i++)
        put16(f + 96 + 2 * i, 0xf000 | (i * 0x111));
    for (size_t i = VMS_HEADER; i < total; i++)
        f[i] = (uint8_t)(i * 13);
    struct vms_info info = {.total = total};
    vms_seal(f, &info);
    return total;
}

static void test_vms(void)
{
    /* CRC-16/CCITT (XMODEM) check value, and agreement with the KOS routine. */
    assert(vms_crc16((const uint8_t *)"123456789", 9) == 0x31c3);
    uint8_t noise[1000];
    for (int i = 0; i < 1000; i++)
        noise[i] = (uint8_t)(i * i + 3 * i);
    for (int n = 0; n < 1000; n += 37)
        assert(vms_crc16(noise, n) == kos_crc(noise, n));

    uint8_t file[16384], copy[16384];
    struct vms_info info;
    size_t total = build_vms(file, 1, 0, 100);
    assert(total == 128 + 512 + 100);
    assert(vms_parse(file, 2 * 512, &info) == VMS_OK);
    assert(info.icons == 1 && info.data_len == 100 && info.total == total && info.speed == 8);
    assert(!strcmp(info.short_text, "Test save") && !strcmp(info.app_id, "TestApp"));
    assert(!strcmp(info.long_text, "A long description for the save"));
    /* The CRC is what KOS computes: over the whole VMS with the CRC field zeroed. */
    memcpy(copy, file, total);
    unsigned stored = copy[70] | copy[71] << 8;
    copy[70] = copy[71] = 0;
    assert(kos_crc(copy, (int)total) == stored);
    /* The padding after the data is outside the CRC. */
    memcpy(copy, file, sizeof(copy));
    copy[total + 1] ^= 0xff;
    assert(vms_parse(copy, 2 * 512, &info) == VMS_OK);
    /* Any change inside is caught; malformed headers are refused. */
    for (size_t at = 0; at < total; at += 41) {
        memcpy(copy, file, sizeof(copy));
        copy[at] ^= 1;
        int r = vms_parse(copy, 2 * 512, &info);
        assert(r != VMS_OK);
    }
    assert(vms_parse(file, 100, &info) == VMS_SHORT);
    memcpy(copy, file, sizeof(copy)); put16(copy + 64, 0);
    assert(vms_parse(copy, 1024, &info) == VMS_ICONS);
    put16(copy + 64, 4);
    assert(vms_parse(copy, 1024, &info) == VMS_ICONS);
    memcpy(copy, file, sizeof(copy)); put16(copy + 68, 4);
    assert(vms_parse(copy, 1024, &info) == VMS_EYECATCH);
    memcpy(copy, file, sizeof(copy)); put32(copy + 72, 0xffffffffu);
    assert(vms_parse(copy, 1024, &info) == VMS_LENGTH);
    put32(copy + 72, 1024);
    assert(vms_parse(copy, 1024, &info) == VMS_LENGTH);
    assert(*vms_error_text(VMS_CRC) && *bmp_error_text(BMP_NOT_BMP));

    /* Pixel layout: left pixel in the high nibble, 16 bytes per row, top row first. */
    memset(file + VMS_HEADER, 0, 512);
    vms_set_pixel(file, 0, 0, 0, 0xa);
    vms_set_pixel(file, 0, 1, 0, 0x5);
    vms_set_pixel(file, 0, 31, 31, 0xf);
    vms_set_pixel(file, 0, 2, 3, 0x7);
    assert(file[VMS_HEADER] == 0xa5 && file[VMS_HEADER + 511] == 0x0f && file[VMS_HEADER + 3 * 16 + 1] == 0x70);
    assert(vms_pixel(file, 0, 0, 0) == 0xa && vms_pixel(file, 0, 1, 0) == 5 && vms_pixel(file, 0, 31, 31) == 15);
    vms_set_pixel(file, 0, 1, 0, 0x2);
    assert(file[VMS_HEADER] == 0xa2);
    /* The same addressing as the settings save icon (src/dreamcast/settings_icon.h). */
    uint8_t icon[512] = {0};
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 32; col++) {
            unsigned colour = (row * 3 + col) & 15, offset = row * 16 + col / 2, shift = (col & 1) ? 0 : 4;
            icon[offset] = (icon[offset] & ~(15u << shift)) | (colour << shift);
            assert(1);
        }
    memcpy(file + VMS_HEADER, icon, 512);
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 32; col++)
            assert(vms_pixel(file, 0, col, row) == ((row * 3 + col) & 15));
    /* Edits keep everything else byte for byte, and reseal validly. */
    total = build_vms(file, 2, 1, 300);
    memcpy(copy, file, total);
    assert(vms_parse(file, total, &info) == VMS_OK && info.icons == 2 && info.eyecatch == 1);
    vms_set_pixel(file, 1, 5, 6, 3);
    vms_set_palette(file, 3, 0xf123);
    assert(vms_palette(file, 3) == 0xf123 && vms_parse(file, total, &info) == VMS_CRC);
    vms_seal(file, &info);
    assert(vms_parse(file, total, &info) == VMS_OK);
    int differing = 0;
    for (size_t i = 0; i < total; i++)
        differing += file[i] != copy[i];
    assert(differing <= 1 + 2 + 2); /* pixel byte, palette entry, CRC */
    assert(!memcmp(file, copy, 70) && !memcmp(file + 72, copy + 72, 96 - 72));
    /* Flips and fills. */
    vms_fill(file, 0, 9);
    assert(vms_pixel(file, 0, 0, 0) == 9 && vms_pixel(file, 0, 31, 31) == 9 && file[VMS_HEADER + 100] == 0x99);
    vms_set_pixel(file, 0, 0, 0, 1);
    vms_set_pixel(file, 0, 5, 2, 2);
    vms_flip_h(file, 0);
    assert(vms_pixel(file, 0, 31, 0) == 1 && vms_pixel(file, 0, 26, 2) == 2 && vms_pixel(file, 0, 0, 0) == 9);
    vms_flip_v(file, 0);
    assert(vms_pixel(file, 0, 31, 31) == 1 && vms_pixel(file, 0, 26, 29) == 2);
    /* Colour conversion. */
    int r, g, b;
    assert(!argb4444_rgb(0xff00, &r, &g, &b) && r == 255 && g == 0 && b == 0);
    assert(argb4444_rgb(0x0f00, &r, &g, &b) && r == g && g == b && b == 255);
    assert(!argb4444_rgb(0xf000, &r, &g, &b) && r == 0 && g == 0 && b == 0);
    argb4444_rgb(0x8000, &r, &g, &b);
    assert(r > 100 && r < 160);
}

static void test_names(void)
{
    char out[13];
    assert(vmu_name_ok("NOTES.TXT") && vmu_name_ok("A") && vmu_name_ok("TWELVE_CHARS") && vmu_name_ok("a b"));
    assert(!vmu_name_ok("") && !vmu_name_ok("THIRTEEN_CHAR") && !vmu_name_ok(" X") && !vmu_name_ok("   ") &&
           !vmu_name_ok("bad\tname") && !vmu_name_ok("caf\xc3\xa9"));
    assert(vmu_name_ok("TRAILING  "));
    dos_name_for_vmu("SONICADV_SYS", out); assert(!strcmp(out, "SONICADV.VMS"));
    dos_name_for_vmu("EMUTOS.CFG", out); assert(!strcmp(out, "EMUTOS.CFG"));
    dos_name_for_vmu("my note.text", out); assert(!strcmp(out, "MY_NOTE.TEX"));
    dos_name_for_vmu(".hidden", out); assert(!strcmp(out, "V.HID"));
    dos_name_for_vmu("a.", out); assert(!strcmp(out, "A.VMS"));
    vmu_name_for_dos("SAVE.VMS", out); assert(!strcmp(out, "SAVE"));
    vmu_name_for_dos("NOTES.TXT", out); assert(!strcmp(out, "NOTES.TXT"));
    vmu_name_for_dos("LONGNAME.EXT", out); assert(!strcmp(out, "LONGNAME.EXT"));
}

/* A VMS save built by tools/vmu_fixture.py (independent implementation) must parse here. */
static void test_python_vms(const char *path)
{
    static uint8_t file[4096];
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(file, 1, sizeof(file), f);
    fclose(f);
    struct vms_info info;
    assert(vms_parse(file, n, &info) == VMS_OK);
    assert(info.icons == 1 && info.data_len == 200 && !strcmp(info.short_text, "Icon test") && !strcmp(info.app_id, "VMUFIX"));
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int d = (x - 16) * (x - 16) + (y - 16) * (y - 16);
            int expect = (d > 100 && d < 150) ? 1 : d <= 100 ? 2 + x / 11 : 0;
            assert(vms_pixel(file, 0, x, y) == (expect & 15));
        }
    assert(vms_palette(file, 2) == 0xfd22);
    /* Re-sealing an untouched file must reproduce the stored CRC exactly. */
    uint8_t copy[4096];
    memcpy(copy, file, n);
    vms_seal(copy, &info);
    assert(!memcmp(copy, file, n));
}

int main(int argc, char **argv)
{
    test_lcd();
    test_bmp();
    test_vms();
    test_names();
    if (argc > 1)
        test_python_vms(argv[1]);
    puts("VMU editor core (LCD, BMP, VMS icon, names): PASS");
    return 0;
}
