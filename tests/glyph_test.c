/* Verify visible glyph geometry independently of GEM and the SH-4 toolchain. */
#include "dreamcast/glyph.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char screen[64][64];
static void pixel(void *unused, int x, int y, int ink)
{
    (void)unused;
    assert(x >= 0 && x < 64 && y >= 0 && y < 64);
    screen[y][x] = ink;
}
static int count(void)
{
    int n = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            n += screen[y][x];
    return n;
}
int main(void)
{
    /* Glyph crosses a 16-bit word boundary; numeric words work on either CPU. */
    static const uint16_t font[] = {0x0001, 0x8000, 0, 0x8000};
    struct dc_glyph base = {.font = font,
                            .stride = 2,
                            .source_x = 15,
                            .width = 2,
                            .height = 2,
                            .x = 16,
                            .y = 16,
                            .light_mask = 0xaaaa};
    const int rotation[] = {0, 900, 1800, 2700};
    const int points[4][6] = {{16, 16, 17, 16, 17, 17},
                              {16, 15, 16, 14, 17, 14},
                              {15, 17, 14, 17, 14, 16},
                              {17, 16, 17, 17, 16, 17}};
    for (int r = 0; r < 4; ++r) {
        memset(screen, 0, sizeof(screen));
        struct dc_glyph g = base;
        g.rotation = rotation[r];
        dc_draw_glyph(&g, pixel, NULL);
        assert(count() == 3);
        for (int i = 0; i < 6; i += 2)
            assert(screen[points[r][i + 1]][points[r][i]]);
        assert(g.x == 16 + (r == 0 ? 2 : r == 2 ? -2 : 0));
        assert(g.y == 16 + (r == 1 ? -2 : r == 3 ? 2 : 0));
    }
    memset(screen, 0, sizeof(screen));
    struct dc_glyph g = base;
    g.scale = 1;
    g.scale_up = 1;
    g.increment = 0xffff;
    dc_draw_glyph(&g, pixel, NULL);
    assert(count() == 12 && g.x == 20 && screen[19][19] && !screen[19][16]);
    memset(screen, 0, sizeof(screen));
    g = base;
    g.scale = 1;
    g.increment = 0x8000;
    dc_draw_glyph(&g, pixel, NULL);
    assert(count() == 1 && g.x == 17 && screen[16][16]);
    memset(screen, 0, sizeof(screen));
    g = base;
    g.style = 2;
    dc_draw_glyph(&g, pixel, NULL);
    assert(count() == 2 && screen[16][16] && screen[17][17]);
    static const uint16_t single[] = {0x8000, 0x8000, 0x8000, 0x8000};
    g = base;
    g.font = single;
    g.stride = 1;
    g.source_x = 0;
    g.width = g.height = 1;
    g.style = 16;
    memset(screen, 0, sizeof(screen));
    dc_draw_glyph(&g, pixel, NULL);
    assert(count() == 8 && !screen[17][17] && g.x == 19);
    g = base;
    g.font = single;
    g.stride = 1;
    g.source_x = 0;
    g.width = g.height = 1;
    g.style = 1;
    g.weight = 1;
    memset(screen, 0, sizeof(screen));
    dc_draw_glyph(&g, pixel, NULL);
    assert(count() == 2 && g.x == 18);
    g = base;
    g.font = single;
    g.stride = 1;
    g.source_x = 0;
    g.width = 1;
    g.height = 4;
    g.style = 4;
    g.skew_width = 2;
    g.skew_mask = 0xffff;
    memset(screen, 0, sizeof(screen));
    dc_draw_glyph(&g, pixel, NULL);
    assert(count() == 4 && screen[16][18] && screen[17][18] && screen[18][17] && screen[19][16]);
    assert(g.x == 17); /* italic overhang does not alter character advance */
    puts("native glyph word order, scaling, spacing, styles and rotation PASS");
}
