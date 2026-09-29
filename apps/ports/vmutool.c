/* Read-only card browser. It calls no VMU write API; VMUEDIT.PRG is the editor. GPL-2.0-or-later. */
#include "accessory.h"
#include "dreamcast/system_info.h"
#include "dreamcast/vmu_info.h"
#include <stdio.h>
#include <string.h>
static Accessory toolbox;
static struct dc_vmu_info card;
static struct dc_device_info cards[DC_SYSTEM_INFO_DEVICES];
static int card_count,selected_card,selected_file,top_file,ready;
static const char *error_text(int error)
{
    switch(error) {
    case DC_VMU_NOT_READ:return "Card detected. Press R to read its directory.";
    case DC_VMU_ABSENT:return "The selected card has been disconnected.";
    case DC_VMU_IO:return "Card read failed. Check it and press R.";
    case DC_VMU_UNFORMATTED:return "Card is unformatted; nothing was changed.";
    case DC_VMU_UNSUPPORTED:return "This card layout is not supported.";
    default:return "Card metadata is damaged; nothing was changed.";
    }
}
static int scan(void)
{
    struct dc_system_info info;
    int old_count=card_count;
    unsigned port=card_count ? cards[selected_card].port : 99,unit=card_count ? cards[selected_card].unit : 99;
    card_count=0;selected_card=0;
    ready=APP_HAS(vmu_info) && APP_HAS(system_info) && dc_os->system_info(&info,sizeof(info))==sizeof(info);
    if(!ready)return 1;
    for(unsigned i=0;i<info.device_count && i<DC_SYSTEM_INFO_DEVICES;i++)
        if(info.devices[i].functions&0x02000000) {
            cards[card_count]=info.devices[i];
            if(cards[card_count].port==port && cards[card_count].unit==unit)selected_card=card_count;
            card_count++;
        }
    int changed=old_count!=card_count || (card_count && (cards[selected_card].port!=port || cards[selected_card].unit!=unit));
    if(!card_count || changed) {memset(&card,0,sizeof(card));card.status=card_count ? DC_VMU_NOT_READ : DC_VMU_ABSENT;selected_file=top_file=0;}
    return changed;
}
static void read_card(void)
{
    selected_file=top_file=0;
    if(!ready || !card_count)return;
    struct dc_device_info *dev=&cards[selected_card];
    long result=dc_os->vmu_info(dev->port,dev->unit,&card,sizeof(card));
    if(result!=sizeof(card) || card.version!=DC_VMU_VERSION) {
        memset(&card,0,sizeof(card));card.status=DC_VMU_IO;
    }
    if(card.file_count>DC_VMU_FILES){card.file_count=0;card.status=DC_VMU_CORRUPT;}
}
static void opened(void){scan();read_card();}
static int refresh(void){scan();read_card();return 1;}
static int tick(void)
{
    /* Enumeration only: never repeatedly read a card in the background. */
    return scan();
}
static int move_card(int direction)
{
    if(!card_count)return 0;
    selected_card=(selected_card+direction+card_count)%card_count;
    read_card();return 1;
}
static int select_file(int index)
{
    if(card.status!=DC_VMU_OK || !card.file_count)return 0;
    if(index<0)index=0;if(index>=(int)card.file_count)index=card.file_count-1;
    selected_file=index;
    if(top_file>index)top_file=index;
    if(index>=top_file+8)top_file=index-7;
    return 1;
}
static int key(int k)
{
    int ch=k&255,scan_code=k&0xff00;
    if(ch=='r'||ch=='R')return refresh();
    if(scan_code==KEY_LEFT)return move_card(-1);
    if(scan_code==KEY_RIGHT)return move_card(1);
    if(scan_code==KEY_UP)return select_file(selected_file-1);
    if(scan_code==KEY_DOWN)return select_file(selected_file+1);
    if(scan_code==0x4900)return select_file(selected_file-8);
    if(scan_code==0x5100)return select_file(selected_file+8);
    return 0;
}
static int target(int x,int y)
{
    if(accessory_hit(x,y,12,12,80,24))return -2;
    if(accessory_hit(x,y,100,12,80,24))return -3;
    if(accessory_hit(x,y,376,12,130,24))return -4;
    if(accessory_hit(x,y,12,110,494,160))return top_file+(y-110)/20;
    return -1;
}
static int click(int x,int y,int px,int py)
{
    int hit=target(x,y);if(hit!=target(px,py))return 0;
    if(hit==-2)return move_card(-1);if(hit==-3)return move_card(1);if(hit==-4)return refresh();
    return hit>=0 && hit<(int)card.file_count ? select_file(hit) : 0;
}
static void draw(void)
{
    char line[96];
    accessory_box(&toolbox,0,0,toolbox.window.work.w,toolbox.window.work.h,0);
    accessory_button(&toolbox,12,12,80,"< Card",0);
    accessory_button(&toolbox,100,12,80,"Card >",0);
    accessory_button(&toolbox,376,12,130,"Refresh [R]",0);
    accessory_text(&toolbox,204,29,"READ ONLY (VMUEDIT)",4);
    if(!ready){accessory_text(&toolbox,12,70,"VMU inspection API unavailable.",2);return;}
    if(!card_count) {
        accessory_text(&toolbox,12,70,"No VMU or memory card connected.",1);
        accessory_text(&toolbox,12,98,"Connect a card, then press R to inspect it.",9);
        return;
    }
    struct dc_device_info *dev=&cards[selected_card];
    snprintf(line,sizeof(line),"%c%lu: %.30s   %d/%d",'A'+dev->port,(unsigned long)dev->unit,dev->name,selected_card+1,card_count);
    accessory_text(&toolbox,12,60,line,1);
    if(card.status!=DC_VMU_OK) {
        accessory_text(&toolbox,12,90,error_text(card.status),2);
        accessory_text(&toolbox,12,118,"Press R to read the directory.",9);
        return;
    }
    snprintf(line,sizeof(line),"%lu / %lu blocks free   %lu files (512 B/block)",(unsigned long)card.free_blocks,(unsigned long)card.total_blocks,(unsigned long)card.file_count);
    accessory_text(&toolbox,12,82,line,1);
    accessory_text(&toolbox,12,105,"NAME             BLOCKS   TYPE     COPY",9);
    for(int row=0;row<8 && top_file+row<(int)card.file_count;row++) {
        struct dc_vmu_file *file=&card.files[top_file+row];
        int chosen=top_file+row==selected_file,y=110+20*row;
        if(chosen)accessory_box(&toolbox,12,y,494,20,4);
        snprintf(line,sizeof(line),"%-16.12s %5lu    %-7s  %s",file->name,(unsigned long)file->blocks,file->type==0xcc ? "Game" : "Data",file->protected_file ? "Protected" : "Allowed");
        accessory_text(&toolbox,14,y+15,line,chosen ? 0 : 1);
    }
    if(!card.file_count)accessory_text(&toolbox,12,138,"This card is empty.",1);
    else {
        struct dc_vmu_file *file=&card.files[selected_file];
        snprintf(line,sizeof(line),"Modified: %s",file->modified);
        accessory_text(&toolbox,12,300,line,1);
        snprintf(line,sizeof(line),"%lu bytes   first block %lu   header +%lu",(unsigned long)(file->blocks*512),(unsigned long)file->first_block,(unsigned long)file->header_block);
        accessory_text(&toolbox,12,320,line,1);
    }
    accessory_text(&toolbox,12,350,"Left/Right cards | Up/Down files | Esc hides",9);
}
static Accessory toolbox={.menu="  VMU Toolbox",.title="VMU Toolbox (read only)",.width=520,.height=368,.interval=1000,
    .opened=opened,.tick=tick,.key=key,.click=click,.draw=draw};
int app_main(int argc,char **argv){(void)argc;(void)argv;return accessory_run(&toolbox);}
