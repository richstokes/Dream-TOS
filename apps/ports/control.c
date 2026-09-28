/* Dreamcast input control panel. GPL-2.0-or-later. */
#include "accessory.h"
#include "dreamcast/control.h"
#include <stdio.h>
#include <string.h>
static Accessory panel;
static struct dc_input_config config;
static struct dc_input_snapshot input;
static int ready, focus, colour, palette_saved, pointer_x, pointer_y;
static int16_t original_green[3];
static char status[64]="Changes last until reset.";
static const char *colours[]={"Original","Teal","Slate","Amber"};
static const int themes[][3]={{0,0,0},{0,500,500},{400,400,500},{650,450,0}};
static int tick(void)
{
    struct dc_input_snapshot previous=input;
    if(APP_HAS(input_snapshot))dc_os->input_snapshot(&input,sizeof(input));
    aes_call(79,0,5,0);
    int changed=pointer_x!=ao[1] || pointer_y!=ao[2] || memcmp(&previous,&input,sizeof(input));
    pointer_x=ao[1];pointer_y=ao[2];
    return changed;
}
static void opened(void)
{
    ready=APP_HAS(input_config) && dc_os->input_config(0,&config,sizeof(config))==sizeof(config);
    tick();
}
static void init(void)
{
    vi[0]=3;vi[1]=0;vdi_call(26,0,2);
    memcpy(original_green,vo+1,sizeof(original_green));palette_saved=1;
    opened();
}
static void apply_colour(void)
{
    const int16_t *p=original_green;
    if(colour)app_palette(3,themes[colour][0],themes[colour][1],themes[colour][2]);
    else if(palette_saved)app_palette(3,p[0],p[1],p[2]);
}
static int adjust(int row,int direction)
{
    if(!ready)return 0;
    if(row==3) {colour=(colour+direction+4)%4;apply_colour();return 1;}
    struct dc_input_config next=config;
    uint32_t *value=row==0 ? &next.mouse_percent : row==1 ? &next.repeat_delay_ms : &next.repeat_interval_ms;
    int amount=row==0 ? 25 : row==1 ? 100 : 20;
    int minimum=row==0 ? 25 : row==1 ? 100 : 20;
    int maximum=row==0 ? 400 : row==1 ? 1000 : 200;
    int v=(int)*value+direction*amount;
    if(v<minimum)v=minimum;if(v>maximum)v=maximum;
    *value=v;
    if(dc_os->input_config(1,&next,sizeof(next))==sizeof(next))config=next;
    else strcpy(status,"Could not apply input settings.");
    return 1;
}
static int defaults(void)
{
    if(!ready)return 0;
    struct dc_input_config next={DC_CONTROL_VERSION,sizeof(next),100,300,40};
    if(dc_os->input_config(1,&next,sizeof(next))==sizeof(next)) {
        config=next;colour=0;apply_colour();strcpy(status,"Defaults restored. Changes last until reset.");
    }
    return 1;
}
static int key(int k)
{
    int ch=k&255,scan=k&0xff00;
    if(ch==9){focus=(focus+1)%5;return 1;}
    if(ch=='d'||ch=='D')return defaults();
    if(focus==4 && (ch==13 || ch==' '))return defaults();
    if(focus<4 && (scan==KEY_LEFT || scan==KEY_RIGHT || ch==' '))return adjust(focus,scan==KEY_LEFT ? -1 : 1);
    return 0;
}
static int target(int x,int y)
{
    for(int row=0;row<4;row++) {
        if(accessory_hit(x,y,300,32+row*32,28,24))return row*2;
        if(accessory_hit(x,y,454,32+row*32,28,24))return row*2+1;
    }
    return accessory_hit(x,y,352,166,130,24) ? 8 : -1;
}
static int click(int x,int y,int px,int py)
{
    int hit=target(x,y);if(hit<0 || hit!=target(px,py))return 0;
    focus=hit==8 ? 4 : hit/2;
    return hit==8 ? defaults() : adjust(focus,(hit&1) ? 1 : -1);
}
static void draw(void)
{
    char line[96];
    accessory_box(&panel,0,0,panel.window.work.w,panel.window.work.h,0);
    accessory_text(&panel,12,19,"INPUT & DISPLAY",4);
    if(!ready){accessory_text(&panel,12,50,"Input settings API unavailable.",2);return;}
    const char *labels[]={"Mouse speed","Key repeat delay","Key repeat interval","Desktop colour"};
    for(int i=0;i<4;i++) {
        int y=32+i*32;
        accessory_text(&panel,12,y+17,labels[i],focus==i ? 4 : 1);
        accessory_button(&panel,300,y,28,"-",focus==i);
        accessory_button(&panel,454,y,28,"+",focus==i);
        if(i==0)snprintf(line,sizeof(line),"%u%%",(unsigned)config.mouse_percent);
        else if(i==1)snprintf(line,sizeof(line),"%u ms",(unsigned)config.repeat_delay_ms);
        else if(i==2)snprintf(line,sizeof(line),"%u ms",(unsigned)config.repeat_interval_ms);
        else snprintf(line,sizeof(line),"%s",colours[colour]);
        accessory_text(&panel,342,y+17,line,1);
    }
    accessory_text(&panel,12,181,"LIVE INPUT TEST",4);
    accessory_button(&panel,352,166,130,"Defaults [D]",focus==4);
    if(input.present&DC_INPUT_MOUSE)
        snprintf(line,sizeof(line),"Mouse %c: dx %ld dy %ld buttons %02lx",'A'+input.mouse_port,(long)input.mouse_dx,(long)input.mouse_dy,(unsigned long)input.mouse_buttons);
    else strcpy(line,"Mouse: not connected");
    accessory_text(&panel,12,216,line,1);
    snprintf(line,sizeof(line),"GEM pointer %d,%d   raw packets %lu",pointer_x,pointer_y,(unsigned long)input.mouse_packets);
    accessory_text(&panel,12,236,line,1);
    if(input.present&DC_INPUT_KEYBOARD)
        snprintf(line,sizeof(line),"Keyboard %c: key %02lx  mods %02lx  events %lu",'A'+input.keyboard_port,(unsigned long)(input.key_raw&255),(unsigned long)input.modifiers,(unsigned long)input.key_events);
    else strcpy(line,"Keyboard: not connected");
    accessory_text(&panel,12,256,line,1);
    if(input.present&DC_INPUT_CONTROLLER)
        snprintf(line,sizeof(line),"Pad %c: buttons %04lx  stick %ld,%ld",'A'+input.controller_port,(unsigned long)input.controller_buttons,(long)input.joy_x,(long)input.joy_y);
    else strcpy(line,"Controller: not connected");
    accessory_text(&panel,12,276,line,1);
    snprintf(line,sizeof(line),"Triggers L:%lu R:%lu",(unsigned long)input.trigger_left,(unsigned long)input.trigger_right);
    accessory_text(&panel,12,296,line,1);
    accessory_text(&panel,12,328,status,9);
    accessory_text(&panel,12,350,"Tab selects | Left/Right changes | Esc hides",1);
}
static Accessory panel={.menu="  DC Control",.title="Dreamcast Control Panel",.width=500,.height=368,.interval=200,
    .init=init,.opened=opened,.tick=tick,.key=key,.click=click,.draw=draw};
int app_main(int argc,char **argv){(void)argc;(void)argv;return accessory_run(&panel);}
