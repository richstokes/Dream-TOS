#include "accessory.h"
#include <string.h>
void accessory_text(Accessory *a,int x,int y,const char *text,int colour)
{ app_text(a->window.work.x+x,a->window.work.y+y,text,colour); }
void accessory_box(Accessory *a,int x,int y,int w,int h,int colour)
{ app_box(a->window.work.x+x,a->window.work.y+y,w,h,colour); }
void accessory_button(Accessory *a,int x,int y,int w,const char *text,int focused)
{
    accessory_box(a,x,y,w,24,focused ? 4 : 9);
    accessory_box(a,x+2,y+2,w-4,20,focused ? 4 : 8);
    accessory_text(a,x+(w-(int)strlen(text)*8)/2,y+17,text,focused ? 0 : 1);
}
int accessory_hit(int x,int y,int left,int top,int width,int height)
{ return x>=left && y>=top && x<left+width && y<top+height; }
void accessory_redraw(Accessory *a)
{ if(a->window.handle>=0)app_window_redraw(&a->window,a->window.work,a->draw); }
void accessory_message(Accessory *a,const int16_t m[8])
{
    if(m[0]==40 && m[4]==a->menu_id) {
        if(a->window.handle<0) {
            if(!app_window_open(&a->window,a->title,a->width,a->height,a->width,a->height))return;
            a->pressed=0;
        } else {
            int16_t top[8]={21,0,0,a->window.handle};
            app_window_message(&a->window,top);
        }
        if(a->opened)a->opened();
        accessory_redraw(a);
    } else if(m[0]==41) {
        a->window.handle=-1; a->pressed=0; /* Shell owns teardown. */
    } else if(a->window.handle>=0) {
        int result=app_window_message(&a->window,m);
        if(result==WINDOW_CLOSE) {app_window_close(&a->window);a->pressed=0;}
        else if(result==WINDOW_REDRAW && m[0]==20)
            app_window_redraw(&a->window,(AppRect){m[4],m[5],m[6],m[7]},a->draw);
        else if(result==WINDOW_REDRAW || result==WINDOW_CHANGED) {
            a->pressed=0;accessory_redraw(a);
        }
    }
}
void accessory_event(Accessory *a,AppEvent *e)
{
    if(e->flags&APP_MESSAGE)accessory_message(a,e->message);
    if(a->window.handle<0)return;
    int dirty=0;
    if(e->flags&APP_KEY) {
        if((e->key&255)==27) {app_window_close(&a->window);a->pressed=0;return;}
        int scan=e->key&0xff00;
        if((e->modifiers&4) && (scan==KEY_LEFT || scan==KEY_RIGHT || scan==KEY_UP || scan==KEY_DOWN)) {
            AppRect r=a->window.border;
            int dx=scan==KEY_LEFT ? -8 : scan==KEY_RIGHT ? 8 : 0;
            int dy=scan==KEY_UP ? -8 : scan==KEY_DOWN ? 8 : 0;
            if(e->modifiers&3){r.w+=dx;r.h+=dy;}else{r.x+=dx;r.y+=dy;}
            app_window_bounds(&a->window,r);a->window.full=0;a->pressed=0;dirty=1;
        } else if(a->key) dirty|=a->key(e->key);
    }
    if(e->flags&APP_BUTTON) {
        if(e->buttons&1) {
            a->pressed=app_window_contains(&a->window,e->x,e->y);
            a->press_x=e->x-a->window.work.x;a->press_y=e->y-a->window.work.y;
        } else {
            if(a->pressed && a->click && app_window_contains(&a->window,e->x,e->y))
                dirty|=a->click(e->x-a->window.work.x,e->y-a->window.work.y,a->press_x,a->press_y);
            a->pressed=0;
        }
    }
    if((e->flags&APP_TIMER) && a->tick)dirty|=a->tick();
    if(dirty)accessory_redraw(a);
}
int accessory_run(Accessory *a)
{
    a->window.handle=-1;
    if(!app_begin_windowed())return 1;
    if(a->init)a->init();
    ai[0]=ag[2];aa[0]=(intptr_t)a->menu;
    aes_call(35,1,1,1);a->menu_id=ao[0];
    if(a->menu_id<0)return 1;
    for(;;) {
        AppEvent e={0};
        memset(ai,0,sizeof(ai));
        ai[0]=APP_MESSAGE;
        if(a->window.handle>=0)ai[0]|=APP_KEY|APP_BUTTON|APP_TIMER;
        ai[1]=1;ai[2]=1;ai[3]=!(a->buttons&1);
        ai[14]=a->interval;
        aa[0]=(intptr_t)e.message;
        aes_call(25,16,7,1);
        e.flags=ao[0];e.x=ao[1];e.y=ao[2];e.buttons=ao[3];e.modifiers=ao[4];e.key=(uint16_t)ao[5];
        a->buttons=e.buttons;
        accessory_event(a,&e);
    }
}
