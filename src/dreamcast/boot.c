/* Direct boot/crash display. No allocation, BIOS calls, locks or GEM state.
 * Keep the EmuTOS font access on the GEM side of the packing boundary.
 * GPL-2.0-or-later. */
#include "emutos.h"
#include "fonthdr.h"
#include "dreamcast/hal.h"

extern const Fonthead fnt_st_8x16;

static void text(volatile unsigned short *pixels, unsigned y, const char *s)
{
    unsigned x = 40;
    for (; *s && y <= 440; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch == '\n' || x > 592) {
            x = 40;
            y += 20;
            if (ch == '\n') continue;
            if (y > 440) break;
        }
        if (ch < 32 || ch > 126) ch = '?';
        for (unsigned row = 0; row < 16; row++) {
            unsigned bits = fnt_st_8x16.dat_table[row * 128 + ch / 2];
            bits = ch & 1 ? bits & 255 : bits >> 8;
            for (unsigned col = 0; col < 8; col++)
                if (bits & (0x80 >> col))
                    pixels[(y + row) * 640 + x + col] = 0xffff;
        }
        x += 8;
    }
}

void dc_boot_draw(volatile unsigned short *pixels, const char *stage, const char *failure)
{
    unsigned short background = failure ? 0x6000 : 0x0848;
    for (unsigned i = 0; i < 640 * 480; i++) pixels[i] = background;
    text(pixels, 48, "EmuTOS for Dreamcast");
    text(pixels, 96, failure ? "Startup/runtime failure" : "Starting up...");
    text(pixels, 144, stage);
    if (failure) text(pixels, 192, failure);
    text(pixels, 384, "If this screen stays here, report its text.");
}
