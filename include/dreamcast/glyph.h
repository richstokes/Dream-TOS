/* Portable glyph transforms used by the native SH-4 renderer. GPL-2.0-or-later. */
#ifndef DC_GLYPH_H
#define DC_GLYPH_H
#include <stdint.h>
struct dc_glyph {
    const uint16_t *font;
    int stride, source_x, source_y, width, height;
    int x, y, rotation, style, weight, skew_width, mono;
    int scale, scale_up;
    uint16_t increment, accumulator, light_mask, skew_mask;
};
/* Coordinates are already rotated. The receiver implements clipping and mode. */
typedef void (*dc_glyph_pixel)(void *, int x, int y, int ink);
void dc_draw_glyph(struct dc_glyph *, dc_glyph_pixel, void *);
#endif
