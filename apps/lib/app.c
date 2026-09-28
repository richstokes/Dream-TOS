/* AES/VDI bindings use WORD arrays, never a Motorola stack layout. */
#include "app.h"
#include <string.h>
int16_t ai[16], ao[16], ag[16], vi[512], vo[512], vp[512], vq[512], vc[12];
intptr_t aa[8], az[8];
static int16_t ac[5];
static struct {
    int16_t *c, *g, *i, *o;
    intptr_t *a, *z;
} ap = {ac, ag, ai, ao, aa, az};
static struct {
    int16_t *c, *i, *p, *o, *q;
} vpb = {vc, vi, vp, vo, vq};
static int active, fullscreen;
static int16_t old_palette[16][3];
void aes_call(int op, int ni, int no, int na)
{
    ac[0] = op;
    ac[1] = ni;
    ac[2] = no;
    ac[3] = na;
    ac[4] = 0;
    dc_os->aes(&ap);
}
void vdi_call(int op, int np, int ni)
{
    vc[0] = op;
    vc[1] = np;
    vc[3] = ni;
    vc[5] = 0;
    dc_os->vdi(&vpb);
}
static void attr(int op, int value)
{
    vi[0] = value;
    vdi_call(op, 0, 1);
}
void app_mouse(int show)
{
    ai[0] = show ? 257 : 256;
    aa[0] = 0;
    aes_call(78, 1, 1, 1);
}
void app_palette(int index, int r, int g, int b)
{
    vi[0] = index;
    vi[1] = r;
    vi[2] = g;
    vi[3] = b;
    vdi_call(14, 0, 4);
}
static int begin(int full)
{
    aes_call(10, 0, 1, 0);
    if (ao[0] < 0)
        return 0;
    aes_call(77, 0, 5, 0);
    vc[6] = ao[0];
    for (int i = 0; i < 10; i++)
        vi[i] = 1;
    vi[10] = 2;
    vdi_call(100, 0, 11);
    if (!vc[6]) {
        aes_call(19, 0, 1, 0);
        return 0;
    }
    fullscreen = full;
    for (int i = 0; full && i < 16; i++) {
        vi[0] = i;
        vi[1] = 0;
        vdi_call(26, 0, 2);
        memcpy(old_palette[i], vo + 1, 6);
    }
    if (full) {
        /* Fullscreen apps own display and mouse until app_end. */
        ai[0] = 1;
        aes_call(107, 1, 1, 0);
        ai[0] = 3;
        aes_call(107, 1, 1, 0);
    }
    ai[0] = 0;
    aa[0] = 0;
    aes_call(78, 1, 1, 1); /* arrow, not inherited busy cursor */
    if (full)
        app_mouse(0);
    active = 1;
    attr(23, 1);
    attr(104, 0);
    attr(32, 1);
    vp[0] = 0;
    vp[1] = 13;
    vdi_call(12, 1, 0);
    return 1;
}
int app_begin_windowed(void)
{
    return begin(0);
}
int app_begin(const char *title)
{
    if (!begin(1))
        return 0;
    app_clear(0);
    app_box(0, 0, 640, 24, 1);
    app_text(8, 17, title, 0);
    return 1;
}
void app_end(void)
{
    if (!active)
        return;
    active = 0;
    app_unclip();
    if (fullscreen) {
        for (int i = 0; i < 16; i++)
            app_palette(i, old_palette[i][0], old_palette[i][1], old_palette[i][2]);
        app_mouse(1);
        ai[0] = 2;
        aes_call(107, 1, 1, 0);
        ai[0] = 0;
        aes_call(107, 1, 1, 0);
    }
    vdi_call(101, 0, 0);
    aes_call(19, 0, 1, 0);
}
void app_clear(int colour)
{
    app_box(0, 24, 640, 456, colour);
}
void app_box(int x, int y, int w, int h, int c)
{
    if (w <= 0 || h <= 0)
        return;
    attr(25, c);
    vp[0] = x;
    vp[1] = y;
    vp[2] = x + w - 1;
    vp[3] = y + h - 1;
    vdi_call(114, 2, 0);
}
void app_text(int x, int y, const char *s, int c)
{
    attr(22, c);
    attr(32, 2);
    int n = 0;
    while (*s && n < 511)
        vi[n++] = (unsigned char)*s++;
    vp[0] = x;
    vp[1] = y;
    vdi_call(8, 1, n);
    attr(32, 1);
}
void app_line(int x, int y, int x2, int y2, int c)
{
    attr(17, c);
    vp[0] = x;
    vp[1] = y;
    vp[2] = x2;
    vp[3] = y2;
    vdi_call(6, 2, 0);
}
void app_clip(int x, int y, int w, int h)
{
    vi[0] = 1;
    vp[0] = x;
    vp[1] = y;
    vp[2] = x + w - 1;
    vp[3] = y + h - 1;
    vdi_call(129, 2, 1);
}
void app_unclip(void)
{
    vi[0] = 0;
    vdi_call(129, 0, 1);
}
void app_status(const char *s)
{
    app_unclip();
    app_box(0, 454, 640, 26, 1);
    app_text(8, 473, s, 0);
}
void app_alert(const char *s)
{
    if (!active) {
        dc_os->gemdos(9, "Native application failed.\r\n");
        return;
    }
    char b[240];
    snprintf(b, sizeof(b), "[1][%.180s][OK]", s);
    ai[0] = 1;
    aa[0] = (intptr_t)b;
    aes_call(52, 1, 1, 1);
}
int app_event(int ms, int *mx, int *my, int *buttons)
{
    int16_t msg[8];
    memset(ai, 0, sizeof(ai));
    ai[0] = 1 | 32;
    ai[14] = ms;
    ai[15] = (unsigned)ms >> 16;
    aa[0] = (intptr_t)msg;
    aes_call(25, 16, 7, 1);
    if (mx)
        *mx = ao[1];
    if (my)
        *my = ao[2];
    if (buttons)
        *buttons = ao[3];
    return (ao[0] & 1) ? (uint16_t)ao[5] : 0;
}
int app_key(void)
{
    aes_call(20, 0, 1, 0);
    return (uint16_t)ao[0];
}
int app_prompt(const char *label, char *buffer, size_t cap)
{
    char edit[128];
    snprintf(edit, sizeof(edit), "%s", buffer);
    size_t len = strlen(edit);
    for (;;) {
        app_box(24, 178, 592, 104, 0);
        app_box(24, 178, 592, 2, 1);
        app_box(24, 280, 592, 2, 1);
        app_text(32, 201, label, 1);
        app_text(32, 230, edit, 1);
        app_line(32 + 8 * (int)len, 234, 38 + 8 * (int)len, 234, 1);
        app_text(32, 265, "Return: accept   Esc: cancel   Ctrl+U: clear", 1);
        int key = app_key() & 255;
        if (key == 27)
            return 0;
        if (key == 13) {
            snprintf(buffer, cap, "%s", edit);
            return 1;
        }
        if (key == 21)
            len = 0;
        else if (key == 8 || key == 127) {
            if (len)
                len--;
        } else if (key >= 32 && key < 127 && len + 1 < cap && len < 70)
            edit[len++] = key;
        edit[len] = 0;
    }
}
#pragma pack(push, 2)
struct mfdb {
    void *data;
    int16_t w, h, ww, standard, planes, reserved[3];
};
#pragma pack(pop)
void app_blit(void *src, int w, int h, int x, int y)
{
    struct mfdb from = {src, w, h, (w + 15) / 16, 0, 4, {0}}, to = {0};
    uintptr_t p = (uintptr_t)&from;
    vc[7] = p;
    vc[8] = p >> 16;
    p = (uintptr_t)&to;
    vc[9] = p;
    vc[10] = p >> 16;
    vp[0] = vp[1] = 0;
    vp[2] = w - 1;
    vp[3] = h - 1;
    vp[4] = x;
    vp[5] = y;
    vp[6] = x + w - 1;
    vp[7] = y + h - 1;
    vi[0] = 3;
    vdi_call(109, 4, 1);
}
