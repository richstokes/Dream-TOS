/* Minimal source bindings required by GEM Worm's original VDI renderer.
 * GEM data is packed on 16-bit boundaries; portable C engine data is not. */
#ifndef DC_GEM_H
#define DC_GEM_H
#include "app.h"
#ifdef WORD
#undef WORD
#endif
#define WORD int16_t
#define WHITE 0
#define BLACK 1
#define RED 2
#define GREEN 3
#pragma pack(push, 2)
typedef struct {
    int16_t g_x, g_y, g_w, g_h;
} GRECT;
typedef struct {
    int16_t ob_next, ob_head, ob_tail;
    uint16_t ob_type, ob_flags, ob_state;
    union {
        intptr_t index;
        void *pointer;
    } ob_spec;
    int16_t ob_x, ob_y, ob_width, ob_height;
} OBJECT;
#pragma pack(pop)
static inline void vsf_color(WORD h, WORD c)
{
    (void)h;
    vi[0] = c;
    vdi_call(25, 0, 1);
}
static inline void vsf_interior(WORD h, WORD c)
{
    (void)h;
    vi[0] = c;
    vdi_call(23, 0, 1);
}
static inline void v_bar(WORD h, const WORD *p)
{
    (void)h;
    for (int i = 0; i < 4; i++)
        vp[i] = p[i];
    vdi_call(114, 2, 0);
}
static inline void v_ellipse(WORD h, WORD x, WORD y, WORD rx, WORD ry)
{
    (void)h;
    vp[0] = x;
    vp[1] = y;
    vp[2] = rx;
    vp[3] = ry;
    vc[0] = 11;
    vc[1] = 2;
    vc[3] = 0;
    vc[5] = 5;
    dc_os->vdi(&(struct { int16_t *c, *i, *p, *o, *q; }){vc, vi, vp, vo, vq});
}
static inline void v_pieslice(WORD h, WORD x, WORD y, WORD r, WORD a, WORD b)
{
    (void)h;
    vp[0] = x;
    vp[1] = y;
    vp[2] = vp[3] = vp[4] = vp[5] = vp[7] = 0;
    vp[6] = r;
    vi[0] = a;
    vi[1] = b;
    vc[0] = 11;
    vc[1] = 4;
    vc[3] = 2;
    vc[5] = 3;
    dc_os->vdi(&(struct { int16_t *c, *i, *p, *o, *q; }){vc, vi, vp, vo, vq});
}
#endif
