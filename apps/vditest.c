/* Visual native VDI exercise. GPL-2.0-or-later. */
#include "dreamcast/native.h"
static int16_t ac[5], ag[16], ai[16], ao[16];
static intptr_t aa[4], az[4];
static struct {
    int16_t *c, *g, *i, *o;
    intptr_t *a, *z;
} ap = {ac, ag, ai, ao, aa, az};
static int16_t vc[12], vi[128], vo[128], vp[128], vq[128];
static struct {
    int16_t *c, *i, *p, *o, *q;
} vpb = {vc, vi, vp, vo, vq};
static const struct dc_native_api *api;
static void aes(int op, int ni, int no, int na)
{
    ac[0] = op;
    ac[1] = ni;
    ac[2] = no;
    ac[3] = na;
    ac[4] = 0;
    api->aes(&ap);
}
static void vdi(int op, int np, int ni)
{
    vc[0] = op;
    vc[1] = np;
    vc[3] = ni;
    vc[5] = 0;
    api->vdi(&vpb);
}
static void attribute(int op, int value)
{
    vi[0] = value;
    vdi(op, 0, 1);
}
static void text(int x, int y, const char *s)
{
    int n = 0;
    while (*s)
        vi[n++] = (unsigned char)*s++;
    vp[0] = x;
    vp[1] = y;
    vdi(8, 1, n);
}
static void height(int n)
{
    vp[0] = 0;
    vp[1] = n;
    vdi(12, 1, 0);
}
static void bar(int x, int y, int w, int h)
{
    vp[0] = x;
    vp[1] = y;
    vp[2] = x + w - 1;
    vp[3] = y + h - 1;
    vdi(114, 2, 0);
}
static int pixel_color(int x, int y)
{
    vp[0] = x;
    vp[1] = y;
    vdi(105, 1, 0);
    return vo[1];
}
static struct {
    void *address;
    int16_t width, height, word_width, standard, planes, reserved[3];
} screen_form;
static void control_pointer(int index, void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;
    vc[index] = (uint16_t)address;
    vc[index + 1] = (uint16_t)(address >> 16);
}
long dc_app_main(const struct dc_native_api *os, const char *tail, const char *env)
{
    (void)tail;
    (void)env;
    api = os;
    if (os->version != DC_NATIVE_ABI)
        return -32;
    aes(10, 0, 1, 0);
    aes(77, 0, 5, 0);
    vc[6] = ao[0];
    for (int i = 0; i < 10; i++)
        vi[i] = 1;
    vi[10] = 2;
    vdi(100, 0, 11);
    int handle = vc[6];
    if (!handle)
        return -65;
    vi[0] = 0;
    vdi(123, 0, 0); /* hide mouse */
    attribute(23, 1);
    attribute(25, 0);
    bar(0, 20, 640, 460);
    attribute(22, 1);
    attribute(32, 1);
    height(13);
    text(24, 44, "Native SH-4 VDI graphics test");
    for (int i = 0; i < 16; i++) {
        attribute(25, i);
        bar(24 + i * 36, 58, 30, 20);
    }
    height(7);
    text(24, 98, "Small text: 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    height(13);
    text(24, 126, "Normal text");
    height(26);
    text(24, 170, "Scaled text");
    height(13);
    attribute(106, 1);
    text(24, 210, "Bold");
    attribute(106, 2);
    text(144, 210, "Light");
    attribute(106, 4);
    text(264, 210, "Italic");
    attribute(106, 16);
    text(404, 210, "Outline");
    attribute(106, 0);
    attribute(13, 900);
    text(40, 350, "90 deg");
    attribute(13, 1800);
    text(340, 274, "180 degrees");
    attribute(13, 2700);
    text(420, 252, "270 deg");
    attribute(13, 0);
    /* Polyline, solid filled circle and XOR restoration. */
    attribute(17, 2);
    vp[0] = 90;
    vp[1] = 310;
    vp[2] = 170;
    vp[3] = 250;
    vp[4] = 250;
    vp[5] = 310;
    vdi(6, 3, 0);
    attribute(25, 4);
    vp[0] = 330;
    vp[1] = 330;
    vp[2] = vp[3] = 0;
    vp[4] = 28;
    vp[5] = 0;
    vp[6] = 0;
    vp[7] = 0;
    vc[0] = 11;
    vc[1] = 4;
    vc[3] = 0;
    vc[5] = 4;
    api->vdi(&vpb);
    attribute(32, 3);
    attribute(25, 1);
    bar(490, 250, 60, 80);
    bar(490, 250, 60, 80);
    attribute(32, 1);
    int passed = pixel_color(510, 270) == 0 && pixel_color(330, 330) == 4;
    /* Clip a larger fill and copy a non-word-aligned screen rectangle. */
    vi[0] = 1;
    vp[0] = 480;
    vp[1] = 320;
    vp[2] = 495;
    vp[3] = 335;
    vdi(129, 2, 1);
    attribute(25, 3);
    bar(470, 310, 40, 40);
    vi[0] = 0;
    vdi(129, 0, 1);
    passed &= pixel_color(479, 319) == 0 && pixel_color(480, 320) == 3;
    control_pointer(7, &screen_form);
    control_pointer(9, &screen_form);
    vi[0] = 3;
    vp[0] = 480;
    vp[1] = 320;
    vp[2] = 495;
    vp[3] = 335;
    vp[4] = 515;
    vp[5] = 340;
    vp[6] = 530;
    vp[7] = 355;
    vdi(109, 4, 1);
    passed &=
        pixel_color(515, 340) == 3 && pixel_color(530, 355) == 3 && pixel_color(531, 355) == 0;
    text(24, 380,
         passed ? "Circle / XOR / clipping / raster copy: PASS" : "Graphics readback: FAIL");
    text(24, 408, "Return: finish and restore the desktop");
    aes(20, 0, 1, 0); /* evnt_keybd */
    vi[0] = 1;
    vc[6] = handle;
    vdi(122, 0, 1);
    vdi(101, 0, 0);
    aes(19, 0, 1, 0);
    return passed ? 0 : -66;
}
