/* Host-only display stubs: tests exercise engine and file behavior, not VDI. */
#include "app.h"
#include <stdlib.h>
#include <string.h>
const struct dc_native_api *dc_os;
int16_t ai[16], ao[16], ag[16], vi[512], vo[512], vp[512], vq[512], vc[12];
intptr_t aa[8], az[8];
void aes_call(int a, int b, int c, int d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    ao[0] = 1;
}
void vdi_call(int a, int b, int c)
{
    (void)a;
    (void)b;
    (void)c;
}
unsigned long app_millis(void)
{
    return 123456;
}
int app_begin(const char *s)
{
    (void)s;
    return 1;
}
void app_end(void) {}
void app_clear(int c)
{
    (void)c;
}
void app_box(int a, int b, int c, int d, int e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
}
void app_text(int a, int b, const char *s, int c)
{
    (void)a;
    (void)b;
    (void)s;
    (void)c;
}
void app_line(int a, int b, int c, int d, int e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
}
void app_clip(int a, int b, int c, int d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
void app_unclip(void) {}
void app_status(const char *s)
{
    (void)s;
}
void app_alert(const char *s)
{
    (void)s;
}
int app_prompt(const char *s, char *b, size_t n)
{
    (void)s;
    (void)b;
    (void)n;
    return 1;
}
int app_event(int ms, int *x, int *y, int *b)
{
    (void)ms;
    (void)x;
    (void)y;
    (void)b;
    return 27;
}
int app_key(void)
{
    return 27;
}
void app_palette(int a, int b, int c, int d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
void app_mouse(int v)
{
    (void)v;
}
void app_blit(void *p, int w, int h, int x, int y)
{
    (void)p;
    (void)w;
    (void)h;
    (void)x;
    (void)y;
}
