/* Cooperative resident GEM utility windows. GPL-2.0-or-later. */
#ifndef DC_ACCESSORY_H
#define DC_ACCESSORY_H
#include "window.h"
#define APP_HAS(member) (dc_os && dc_os->size >= offsetof(struct dc_native_api, member) + sizeof(dc_os->member) && dc_os->member)
typedef struct {
    const char *menu, *title;
    int width, height, interval, menu_id, buttons, pressed, press_x, press_y;
    AppWindow window;
    void (*init)(void), (*draw)(void), (*opened)(void);
    int (*tick)(void), (*key)(int code), (*click)(int x,int y,int press_x,int press_y);
} Accessory;
int accessory_run(Accessory *a);
void accessory_redraw(Accessory *a);
void accessory_message(Accessory *a,const int16_t message[8]);
void accessory_event(Accessory *a,AppEvent *e);
void accessory_text(Accessory *a,int x,int y,const char *text,int colour);
void accessory_box(Accessory *a,int x,int y,int w,int h,int colour);
void accessory_button(Accessory *a,int x,int y,int w,const char *text,int focused);
int accessory_hit(int x,int y,int left,int top,int width,int height);
#endif
