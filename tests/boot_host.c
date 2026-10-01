/* The crash renderer must remain usable when GEM and BIOS services are not.
 * Check the actual font's visible glyphs and write bounds under ASan/UBSan. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dreamcast/hal.h"

enum { WIDTH = 640, HEIGHT = 480, PIXELS = WIDTH * HEIGHT };
static uint16_t *pixels;

/* Fixed raster expectations, independent of the renderer's word extraction.
 * Adjacent even/odd character codes catch swapping the numeric font bytes on
 * little-endian hosts and SH-4, even when both glyphs remain recognizable. */
static const uint8_t zero[16] = {
    0, 0, 0x3c, 0x7e, 0x66, 0x66, 0x66, 0x6e,
    0x76, 0x66, 0x66, 0x66, 0x7e, 0x3c, 0, 0
};
static const uint8_t one[16] = {
    0, 0, 0x18, 0x18, 0x38, 0x38, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x7e, 0x7e, 0, 0
};
static const uint8_t letter_a[16] = {
    0, 0, 0x18, 0x3c, 0x7e, 0x66, 0x66, 0x66,
    0x7e, 0x7e, 0x66, 0x66, 0x66, 0x66, 0, 0
};
static const uint8_t letter_b[16] = {
    0, 0, 0x7c, 0x7e, 0x66, 0x66, 0x7e, 0x7c,
    0x66, 0x66, 0x66, 0x66, 0x7e, 0x7c, 0, 0
};
static const uint8_t question[16] = {
    0, 0, 0x3c, 0x7e, 0x66, 0x66, 0x0c, 0x0c,
    0x18, 0x18, 0x18, 0, 0x18, 0x18, 0, 0
};

static void glyph(unsigned x, unsigned y, const uint8_t expected[16], uint16_t bg)
{
    for (unsigned row = 0; row < 16; row++)
        for (unsigned col = 0; col < 8; col++)
            assert(pixels[(y + row) * WIDTH + x + col] ==
                   ((expected[row] & (0x80u >> col)) ? 0xffff : bg));
}

static void margins(uint16_t bg)
{
    unsigned lit = 0;
    for (unsigned y = 0; y < HEIGHT; y++) {
        for (unsigned x = 0; x < WIDTH; x++) {
            uint16_t color = pixels[y * WIDTH + x];
            assert(color == bg || color == 0xffff);
            if (x < 40 || x >= 600 || y < 48 || y >= 456)
                assert(color == bg);
            lit += color == 0xffff;
        }
    }
    assert(lit > 100); /* A blank or all-background renderer is not success. */
}

int main(void)
{
    /* Exact-sized allocation gives ASan redzones immediately outside VRAM. */
    pixels = malloc(PIXELS * sizeof(*pixels));
    assert(pixels);
    memset(pixels, 0xff, PIXELS * sizeof(*pixels));
    dc_boot_draw(pixels, "01AB\nAB01", NULL);
    glyph(40, 144, zero, 0x0848);
    glyph(48, 144, one, 0x0848);
    glyph(56, 144, letter_a, 0x0848);
    glyph(64, 144, letter_b, 0x0848);
    glyph(40, 164, letter_a, 0x0848);
    glyph(48, 164, letter_b, 0x0848);
    glyph(56, 164, zero, 0x0848);
    glyph(64, 164, one, 0x0848);
    margins(0x0848);

    dc_boot_draw(pixels, "", "\x01\x7f\x80\xff\n01");
    for (unsigned i = 0; i < 4; i++) glyph(40 + i * 8, 192, question, 0x6000);
    glyph(40, 212, zero, 0x6000);
    glyph(48, 212, one, 0x6000);
    for (unsigned y = 144; y < 180; y++)
        for (unsigned x = 40; x < 100; x++)
            assert(pixels[y * WIDTH + x] == 0x6000); /* No stale stage text. */
    margins(0x6000);

    char wrapped[72];
    memset(wrapped, 'A', 70);
    wrapped[70] = 'B';
    wrapped[71] = 0;
    dc_boot_draw(pixels, wrapped, NULL);
    glyph(592, 144, letter_a, 0x0848); /* Last complete cell in the row. */
    glyph(40, 164, letter_b, 0x0848); /* Following glyph wraps cleanly. */
    margins(0x0848);

    char *long_text = malloc(20001);
    assert(long_text);
    memset(long_text, 'B', 20000);
    long_text[20000] = 0;
    dc_boot_draw(pixels, long_text, long_text);
    margins(0x6000);
    /* Many line breaks must stop at the bottom just as automatic wrapping
     * does; high-bit and control bytes must never index outside the font. */
    for (unsigned i = 0; i < 20000; i++)
        long_text[i] = i % 4 == 0 ? '\n' : (char)(0x80 + i % 128);
    dc_boot_draw(pixels, long_text, long_text);
    margins(0x6000);
    dc_boot_draw(pixels, "", "");
    margins(0x6000);
    dc_boot_draw(pixels, "", NULL);
    margins(0x0848);
    free(long_text);
    free(pixels);
    puts("Boot display: real font byte order, newline/wrap, crash text and bounds PASS");
    return 0;
}
