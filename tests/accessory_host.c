#include "gem_model.h"
#include "accessory.h"
static int opened_calls,ticks,keys,clicks;
static void draw_accessory(void){app_box(0,0,640,480,0);}
static void open_accessory(void){opened_calls++;}
static int tick_accessory(void){ticks++;return 1;}
static int key_accessory(int code){keys++;return code=='r';}
static int click_accessory(int x,int y,int px,int py){assert(x==px && y==py);clicks++;return 1;}
static const struct dc_native_api api={.aes=mock_aes,.vdi=mock_vdi};
const struct dc_native_api *dc_os=&api;
int main(void)
{
    assert(app_begin_windowed());visible[0]=desktop;inspect_drawing=1;
    Accessory a={.menu_id=3,.title="Utility",.width=480,.height=368,.window={.handle=-1},
        .draw=draw_accessory,.opened=open_accessory,.tick=tick_accessory,.key=key_accessory,.click=click_accessory};
    int16_t m[8]={40,0,0,0,99};accessory_message(&a,m);assert(!created);
    m[4]=3;accessory_message(&a,m);assert(created==1 && opened_calls==1);
    accessory_message(&a,m);assert(created==1 && opened_calls==2);
    AppEvent e={.flags=APP_TIMER};accessory_event(&a,&e);assert(ticks==1);
    e=(AppEvent){.flags=APP_BUTTON,.buttons=1,.x=work.x+20,.y=work.y+20};
    accessory_event(&a,&e);e.buttons=0;accessory_event(&a,&e);assert(clicks==1);
    find_handle=9;e.buttons=1;accessory_event(&a,&e);e.buttons=0;accessory_event(&a,&e);assert(clicks==1);
    find_handle=7;
    e=(AppEvent){.flags=APP_KEY,.key=KEY_LEFT,.modifiers=4};
    int old_x=a.window.border.x;accessory_event(&a,&e);assert(a.window.border.x==old_x-8 && !keys);
    e.key='r';e.modifiers=0;accessory_event(&a,&e);assert(keys==1);
    e=(AppEvent){.flags=APP_MESSAGE|APP_TIMER,.message={41}};
    accessory_event(&a,&e);assert(a.window.handle==-1 && ticks==1 && !closed && !deleted);
    accessory_message(&a,m);assert(created==2 && opened_calls==3);
    e=(AppEvent){.flags=APP_KEY|APP_TIMER,.key=27};accessory_event(&a,&e);
    assert(a.window.handle==-1 && closed==1 && deleted==1 && ticks==1);
    assert(!updates && !mouse_hidden && !mouse_control && !palette_writes);
    puts("Resident accessory message, window, input, clipping and hide lifecycle: PASS");
    return 0;
}
