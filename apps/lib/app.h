/* Small native GEM application toolkit. GPL-2.0-or-later. */
#ifndef DC_APP_H
#define DC_APP_H
#include "dreamcast/native.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
extern const struct dc_native_api *dc_os;
extern int16_t ai[16], ao[16], ag[16], vi[512], vo[512], vp[512], vq[512], vc[12];
extern intptr_t aa[8], az[8];
void aes_call(int op, int ni, int no, int na);
void vdi_call(int op, int np, int ni);
unsigned long app_millis(void);
int app_begin(const char *title);
void app_end(void);
void app_clear(int colour);
void app_box(int x, int y, int w, int h, int colour);
void app_text(int x, int y, const char *s, int colour);
void app_line(int x, int y, int x2, int y2, int colour);
void app_clip(int x, int y, int w, int h);
void app_unclip(void);
void app_status(const char *s);
void app_alert(const char *s);
int app_prompt(const char *label, char *buffer, size_t capacity);
int app_event(int ms, int *mx, int *my, int *buttons);
int app_key(void);
void app_palette(int index, int r, int g, int b);
void app_mouse(int show);
void app_blit(void *src, int width, int height, int x, int y);
#define KEY_UP 0x4800
#define KEY_DOWN 0x5000
#define KEY_LEFT 0x4b00
#define KEY_RIGHT 0x4d00
#define KEY_HOME 0x4700
#define KEY_END 0x4f00
#define KEY_DELETE 0x537f
#endif
