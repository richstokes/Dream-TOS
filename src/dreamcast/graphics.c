/* Native entry and glyph rasterizer for EmuTOS planar VDI. GPL-2.0-or-later. */
#include "emutos.h"
#include "string.h"
#include "lineavars.h"
#include "vdi_defs.h"
#include "vdistub.h"
#include "gsx2.h"
#include "tosvars.h"
#include "fonthdr.h"
#include "dreamcast/hal.h"
#include "dreamcast/glyph.h"

static UWORD framebuffer[480 * 160] __attribute__((aligned(32)));
UBYTE *v_bas_ad = (UBYTE *)framebuffer;
static UWORD palette[16] = {0xffff, 0xf800, 0x07e0, 0xffe0, 0x001f, 0xf81f, 0x07ff, 0xbdf7,
                            0x7bef, 0xfbef, 0x7fef, 0xffef, 0x7bff, 0xfbff, 0x7fff, 0};
extern void screen(void);
void dc_vdi(VDIPB *pb)
{
    WORD n = pb->contrl[1];
    CONTRL = pb->contrl;
    INTIN = pb->intin;
    INTOUT = pb->intout;
    PTSOUT = pb->ptsout;
    if (n > MAX_VERTICES)
        pb->contrl[1] = MAX_VERTICES;
    if (n < 0)
        pb->contrl[1] = 0;
    PTSIN = vdishare.main.local_ptsin;
    memcpy(PTSIN, pb->ptsin, pb->contrl[1] * 2 * sizeof(WORD));
    screen();
    pb->contrl[1] = n;
}
void screen_get_current_mode_info(UWORD *planes, UWORD *width, UWORD *height)
{
    *planes = 4;
    *width = 640;
    *height = 480;
}
WORD get_monitor_type(void)
{
    return MON_VGA;
}
WORD get_palette(void)
{
    return 4096;
}
void get_pixel_size(WORD *w, WORD *h)
{
    *w = *h = 278;
}
WORD check_moderez(WORD m)
{
    (void)m;
    return 0;
}
WORD dc_setcolor(WORD i, WORD rgb)
{
    static WORD st[16];
    if (i < 0 || i > 15)
        return 0;
    WORD old = st[i];
    if (rgb >= 0) {
        st[i] = rgb;
        unsigned r = (rgb >> 8) & 7, g = (rgb >> 4) & 7, b = rgb & 7;
        palette[i] = ((r * 31 / 7) << 11) | ((g * 63 / 7) << 5) | (b * 31 / 7);
    }
    return old;
}
void dc_graphics_present(void)
{
    dc_present(framebuffer, palette);
}
extern const Fonthead fnt_st_6x6, fnt_st_8x8, fnt_st_8x16;
void get_fonts(const Fonthead **a, const Fonthead **b, const Fonthead **c)
{
    *a = &fnt_st_6x6;
    *b = &fnt_st_8x8;
    *c = &fnt_st_8x16;
}
static void putpixel(int x, int y, int bit, int mode, int color)
{
    if (x < 0 || x >= 640 || y < 0 || y >= 480)
        return;
    if (CLIP && (x < XMINCL || x > XMAXCL || y < YMINCL || y > YMAXCL))
        return;
    UWORD *p = framebuffer + y * 160 + (x >> 4) * 4, m = 0x8000 >> (x & 15);
    for (int i = 0; i < 4; i++) {
        int c = (color >> i) & 1;
        if (mode == 0) {
            if (bit && c)
                p[i] |= m;
            else
                p[i] &= ~m;
        } else if (mode == 1 && bit) {
            if (c)
                p[i] |= m;
            else
                p[i] &= ~m;
        } else if (mode == 2 && bit)
            p[i] ^= m;
        else if (mode == 3 && !bit) {
            if (c)
                p[i] |= m;
            else
                p[i] &= ~m;
        }
    }
}
static void glyph_pixel(void *unused, int x, int y, int ink)
{
    (void)unused;
    putpixel(x, y, ink, WRT_MODE, TEXTFG);
}
void text_blt(void)
{
    struct dc_glyph glyph = {.font = FBASE,
                             .stride = FWIDTH / 2,
                             .source_x = SOURCEX,
                             .source_y = SOURCEY,
                             .width = DELX,
                             .height = DELY,
                             .x = DESTX,
                             .y = DESTY,
                             .rotation = CHUP,
                             .style = STYLE,
                             .weight = WEIGHT,
                             .skew_width = LOFF + ROFF,
                             .mono = MONO,
                             .scale = SCALE,
                             .scale_up = SCALDIR,
                             .increment = DDAINC,
                             .accumulator = XDDA,
                             .light_mask = LITEMASK,
                             .skew_mask = SKEWMASK};
    dc_draw_glyph(&glyph, glyph_pixel, NULL);
    DESTX = glyph.x;
    DESTY = glyph.y;
    XDDA = glyph.accumulator;
}
