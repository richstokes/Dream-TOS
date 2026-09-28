/* Native font scaling, effects and rotation, independent of CPU byte order.
 * Built-in EmuTOS fonts are at most 8x16, scaled at most 2x by VDI.
 * GPL-2.0-or-later. */
#ifdef MACHINE_DREAMCAST
#include "emutos.h"
#endif
#include "dreamcast/glyph.h"
#include <string.h>

#define LIMIT 64
#define THICKEN 1
#define LIGHT 2
#define SKEW 4
#define OUTLINE 16

/* Map destination pixels to source pixels using GEM's fractional accumulator.
 * The horizontal accumulator continues across glyphs for proportional spacing. */
static int axis_map(int *map, int count, const struct dc_glyph *g, uint16_t *acc)
{
    int n = 0;
    for (int i = 0; i < count; ++i) {
        int copies = 1;
        if (g->scale) {
            uint16_t next = *acc + g->increment;
            copies = !!g->scale_up + (next < *acc);
            *acc = next;
            if (g->increment == 0xffff)
                copies = 2;
        }
        while (copies-- && n < LIMIT)
            map[n++] = i;
    }
    if (!n && count)
        map[n++] = count - 1;
    return n;
}

void dc_draw_glyph(struct dc_glyph *g, dc_glyph_pixel pixel, void *target)
{
    int xm[LIMIT], ym[LIMIT], shift[LIMIT] = {0};
    /* Only the active AES process renders, and this routine never yields. */
    static unsigned char ink[LIMIT][LIMIT];
    if (g->width <= 0 || g->height <= 0 || g->width > LIMIT / 2 || g->height > LIMIT / 2)
        return;
    uint16_t ya = 0x7fff;
    int w = axis_map(xm, g->width, g, &g->accumulator);
    int h = axis_map(ym, g->height, g, &ya);
    int thick = (g->style & THICKEN) ? g->weight : 0;
    int edge = !!(g->style & OUTLINE);
    int skew = (g->style & SKEW) ? g->skew_width : 0;
    int advance = w + 2 * edge + (g->mono ? 0 : thick);
    int width = w + (g->mono ? 0 : thick) + skew + 2 * edge;
    int height = h + 2 * edge;
    if (thick < 0 || skew < 0 || width > LIMIT || height > LIMIT)
        return;
    memset(ink, 0, height * LIMIT);
    uint16_t sm = g->skew_mask;
    int displacement = 0;
    for (int y = h - 1; y >= 0; --y) {
        shift[y] = displacement;
        if (skew) {
            sm = (uint16_t)((sm << 1) | (sm >> 15));
            if ((sm & 0x8000) && displacement < skew)
                ++displacement;
        }
        for (int x = 0; x < w; ++x) {
            int sx = g->source_x + xm[x];
            uint16_t word = g->font[(g->source_y + ym[y]) * g->stride + (sx >> 4)];
            if (!(word & (0x8000u >> (sx & 15))))
                continue;
            for (int t = 0; t <= thick; ++t) {
                if (x + t < w + (g->mono ? 0 : thick))
                    ink[y + edge][x + t + shift[y] + edge] = 1;
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        uint16_t lm = g->light_mask;
        unsigned turn = y & 15;
        if (turn)
            lm = (uint16_t)((lm << turn) | (lm >> (16 - turn)));
        for (int x = 0; x < width; ++x) {
            /* Preserve neighbouring italic cells in replace mode. */
            if (skew && !edge && (x < shift[y] || x >= shift[y] + width - skew))
                continue;
            int bit = ink[y][x];
            if (edge) {
                int adjacent = 0;
                for (int yy = y - 1; yy <= y + 1; ++yy)
                    for (int xx = x - 1; xx <= x + 1; ++xx)
                        if (yy >= 0 && yy < height && xx >= 0 && xx < width)
                            adjacent |= ink[yy][xx];
                bit = adjacent && !bit;
            }
            if ((g->style & LIGHT) && !(lm & (0x8000u >> (x & 15))))
                bit = 0;
            int dx = g->x + x, dy = g->y + y;
            switch (g->rotation) {
            case 900:
                dx = g->x + y;
                dy = g->y - x - 1;
                break;
            case 1800:
                dx = g->x - x - 1;
                dy = g->y + height - y - 1;
                break;
            case 2700:
                dx = g->x + height - y - 1;
                dy = g->y + x;
                break;
            }
            pixel(target, dx, dy, bit);
        }
    }
    switch (g->rotation) {
    case 900:
        g->y -= advance;
        break;
    case 1800:
        g->x -= advance;
        break;
    case 2700:
        g->y += advance;
        break;
    default:
        g->x += advance;
        break;
    }
}
