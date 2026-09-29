/* Dreamcast input control panel. GPL-2.0-or-later. */
#include "accessory.h"
#include "dreamcast/control.h"
#include "dreamcast/settings.h"
#include "dreamcast/system_info.h"
#include <stdio.h>
#include <string.h>
static Accessory panel;
static struct dc_input_config config;
static struct dc_input_snapshot input;
static int ready, focus, colour, palette_saved, pointer_x, pointer_y;
static int16_t original_green[3];
static char status[64]="Changes are unsaved. S saves to the selected VMU.";
static int cards[DC_SYSTEM_INFO_DEVICES],card_count,selected_card=-1,card_present;
static const char *colours[]={"Original","Teal","Slate","Amber"};
static const int themes[][3]={{0,0,0},{0,500,500},{400,400,500},{650,450,0}};
static void apply_colour(void);
static int refresh_cards(void)
{
    struct dc_system_info info;
    int old_count=card_count,old_present=card_present;
    card_count=card_present=0;
    if(APP_HAS(system_info) && dc_os->system_info(&info,sizeof(info))==sizeof(info) &&
       info.version==DC_SYSTEM_INFO_VERSION && info.bytes==sizeof(info)) {
        for(uint32_t i=0;i<info.device_count && i<DC_SYSTEM_INFO_DEVICES;i++) {
            const struct dc_device_info *d=&info.devices[i];
            if((d->functions&0x02000000) && d->port<4 && d->unit<6)
                cards[card_count++]=d->port*6+d->unit;
        }
    }
    if(selected_card<0 && card_count)selected_card=cards[0];
    for(int i=0;i<card_count;i++)if(cards[i]==selected_card)card_present=1;
    return old_count!=card_count || old_present!=card_present;
}
static void store_error(long error)
{
    const char *message="VMU I/O failed. Keep the card inserted and retry.";
    if(error==DC_SETTINGS_ABSENT)message="Selected VMU is not connected.";
    else if(error==DC_SETTINGS_MISSING)message="No saved settings on this VMU. S saves them.";
    else if(error==DC_SETTINGS_INVALID)message="Invalid card or settings. Nothing loaded/saved.";
    else if(error==DC_SETTINGS_FULL)message="VMU is full. Settings need one free block.";
    else if(error==DC_SETTINGS_CONFLICT)message="EMUTOS.CFG is incompatible; it was not changed.";
    strcpy(status,message);
}
static int store_settings(int write)
{
    if(!ready)return 0;
    if(!APP_HAS(control_store)) {strcpy(status,"VMU settings API unavailable.");return 1;}
    refresh_cards();
    if(!card_present){store_error(DC_SETTINGS_ABSENT);return 1;}
    struct dc_control_settings saved={DC_SETTINGS_VERSION,sizeof(saved),config,(uint32_t)colour};
    long result=dc_os->control_store(write,selected_card/6,selected_card%6,&saved,sizeof(saved));
    if(result!=sizeof(saved)){store_error(result);return 1;}
    if(!write) {
        if(saved.version!=DC_SETTINGS_VERSION || saved.bytes!=sizeof(saved) || saved.desktop_colour>=4 ||
           dc_os->input_config(1,&saved.input,sizeof(saved.input))!=sizeof(saved.input)) {
            store_error(DC_SETTINGS_INVALID);return 1;
        }
        config=saved.input;colour=saved.desktop_colour;apply_colour();
    }
    snprintf(status,sizeof(status),"Settings %s VMU %c%d.",write ? "saved to" : "loaded from",
             'A'+selected_card/6,selected_card%6);
    return 1;
}
static int select_card(int direction)
{
    refresh_cards();
    if(card_count) {
        int index=-1;
        for(int i=0;i<card_count;i++)if(cards[i]==selected_card)index=i;
        if(index<0)index=direction<0 ? 0 : -1;
        selected_card=cards[(index+direction+card_count)%card_count];card_present=1;
    }
    return 1;
}
static int tick(void)
{
    struct dc_input_snapshot previous=input;
    if(APP_HAS(input_snapshot))dc_os->input_snapshot(&input,sizeof(input));
    aes_call(79,0,5,0);
    int changed=pointer_x!=ao[1] || pointer_y!=ao[2] || memcmp(&previous,&input,sizeof(input));
    pointer_x=ao[1];pointer_y=ao[2];
    return refresh_cards() || changed;
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
    if(ready && APP_HAS(control_store)) {
        int first=selected_card;
        for(int i=0;i<card_count;i++) {
            struct dc_control_settings saved;
            int address=cards[i];
            if(dc_os->control_store(0,address/6,address%6,&saved,sizeof(saved))==sizeof(saved) &&
               saved.version==DC_SETTINGS_VERSION && saved.bytes==sizeof(saved) && saved.desktop_colour<4 &&
               dc_os->input_config(1,&saved.input,sizeof(saved.input))==sizeof(saved.input)) {
                selected_card=address;config=saved.input;colour=saved.desktop_colour;apply_colour();
                snprintf(status,sizeof(status),"Settings loaded from VMU %c%d.",'A'+address/6,address%6);
                return;
            }
        }
        selected_card=first;
        strcpy(status,"No saved settings loaded. S saves to selected VMU.");
    }
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
    strcpy(status,"Changes are unsaved. S saves to the selected VMU.");
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
        config=next;colour=0;apply_colour();strcpy(status,"Defaults applied. Press S to save them to VMU.");
    }
    return 1;
}
static int key(int k)
{
    int ch=k&255,scan=k&0xff00;
    if(ch==9){focus=(focus+1)%8;return 1;}
    if(ch=='d'||ch=='D')return defaults();
    if(ch=='s'||ch=='S')return store_settings(1);
    if(ch=='l'||ch=='L')return store_settings(0);
    if(focus>=5 && (ch==13 || ch==' '))return focus==7 ? defaults() : store_settings(focus==5);
    if(focus==4 && (scan==KEY_LEFT || scan==KEY_RIGHT || ch==' '))return select_card(scan==KEY_LEFT ? -1 : 1);
    if(focus<4 && (scan==KEY_LEFT || scan==KEY_RIGHT || ch==' '))return adjust(focus,scan==KEY_LEFT ? -1 : 1);
    return 0;
}
static int target(int x,int y)
{
    for(int row=0;row<5;row++) {
        if(accessory_hit(x,y,300,32+row*32,28,24))return row*2;
        if(accessory_hit(x,y,454,32+row*32,28,24))return row*2+1;
    }
    if(accessory_hit(x,y,12,194,130,24))return 10;
    if(accessory_hit(x,y,170,194,130,24))return 11;
    return accessory_hit(x,y,352,194,130,24) ? 12 : -1;
}
static int click(int x,int y,int px,int py)
{
    int hit=target(x,y);if(hit<0 || hit!=target(px,py))return 0;
    focus=hit>=10 ? hit-5 : hit/2;
    if(hit>=10)return hit==12 ? defaults() : store_settings(hit==10);
    return focus==4 ? select_card((hit&1) ? 1 : -1) : adjust(focus,(hit&1) ? 1 : -1);
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
    accessory_text(&panel,12,177,"Settings VMU",focus==4 ? 4 : 1);
    accessory_button(&panel,300,160,28,"<",focus==4);
    accessory_button(&panel,454,160,28,">",focus==4);
    if(selected_card>=0)snprintf(line,sizeof(line),"%c%d%s",'A'+selected_card/6,selected_card%6,card_present ? "" : " absent");
    else strcpy(line,"None");
    accessory_text(&panel,342,177,line,1);
    accessory_button(&panel,12,194,130,"Save [S]",focus==5);
    accessory_button(&panel,170,194,130,"Load [L]",focus==6);
    accessory_button(&panel,352,194,130,"Defaults [D]",focus==7);
    accessory_text(&panel,12,241,"LIVE INPUT TEST",4);
    if(input.present&DC_INPUT_MOUSE)
        snprintf(line,sizeof(line),"Mouse %c: dx %ld dy %ld buttons %02lx",'A'+input.mouse_port,(long)input.mouse_dx,(long)input.mouse_dy,(unsigned long)input.mouse_buttons);
    else strcpy(line,"Mouse: not connected");
    accessory_text(&panel,12,266,line,1);
    snprintf(line,sizeof(line),"GEM pointer %d,%d   raw packets %lu",pointer_x,pointer_y,(unsigned long)input.mouse_packets);
    accessory_text(&panel,12,286,line,1);
    if(input.present&DC_INPUT_KEYBOARD)
        snprintf(line,sizeof(line),"Keyboard %c: key %02lx  mods %02lx  events %lu",'A'+input.keyboard_port,(unsigned long)(input.key_raw&255),(unsigned long)input.modifiers,(unsigned long)input.key_events);
    else strcpy(line,"Keyboard: not connected");
    accessory_text(&panel,12,306,line,1);
    if(input.present&DC_INPUT_CONTROLLER)
        snprintf(line,sizeof(line),"Pad %c: buttons %04lx  stick %ld,%ld",'A'+input.controller_port,(unsigned long)input.controller_buttons,(long)input.joy_x,(long)input.joy_y);
    else strcpy(line,"Controller: not connected");
    accessory_text(&panel,12,326,line,1);
    snprintf(line,sizeof(line),"Triggers L:%lu R:%lu",(unsigned long)input.trigger_left,(unsigned long)input.trigger_right);
    accessory_text(&panel,12,346,line,1);
    accessory_text(&panel,12,373,status,9);
    accessory_text(&panel,12,397,"Tab selects | Left/Right changes | Esc hides",1);
}
static Accessory panel={.menu="  DC Control",.title="Dreamcast Control Panel",.width=500,.height=410,.interval=200,
    .init=init,.opened=opened,.tick=tick,.key=key,.click=click,.draw=draw};
int app_main(int argc,char **argv){(void)argc;(void)argv;return accessory_run(&panel);}
