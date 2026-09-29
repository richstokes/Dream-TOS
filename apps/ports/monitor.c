/* Live, measured system snapshots. GPL-2.0-or-later. */
#define MON_DRIVES 3
#include "accessory.h"
#include "dreamcast/system_info.h"
#include <stdio.h>
#include <string.h>
static Accessory monitor;
static struct dc_system_info info;
static uint32_t history[60], disk_free[MON_DRIVES],disk_total[MON_DRIVES];
static int disk_shown,disk_letter[MON_DRIVES],disk_kind[MON_DRIVES],disk_extra;
static int samples, next_sample, paused, ready, device_offset;
static const char *kind(uint32_t f)
{
    if(f&0x02000000)return "VMU";
    if(f&0x40000000)return "Keyboard";
    if(f&0x00020000)return "Mouse";
    if(f&0x01000000)return "Controller";
    return "Peripheral";
}
static int refresh(void)
{
    ready=APP_HAS(system_info) && dc_os->system_info(&info,sizeof(info))==sizeof(info) && info.version==DC_SYSTEM_INFO_VERSION;
    if(!ready)return 1;
    if(info.device_count>DC_SYSTEM_INFO_DEVICES)info.device_count=DC_SYSTEM_INFO_DEVICES;
    history[next_sample]=info.gem_free_bytes;
    next_sample=(next_sample+1)%60;if(samples<60)samples++;
    /* Mounted drives in letter order; those beyond the display space are counted. */
    int shown=0;disk_extra=0;
    for(int drive=0;drive<26;drive++) {
        if(!(info.drive_mask&(1u<<drive)))continue;
        if(shown==MON_DRIVES){disk_extra++;continue;}
        uint32_t d[4]={0};disk_free[shown]=disk_total[shown]=0;
        disk_letter[shown]='A'+drive;
        disk_kind[shown]=(info.volatile_mask&(1u<<drive))?0:(info.readonly_mask&(1u<<drive))?1:2;
        if(dc_os->gemdos(0x36,d,drive+1)==0) {
            disk_free[shown]=d[0]*d[2]*d[3];disk_total[shown]=d[1]*d[2]*d[3];
        }
        shown++;
    }
    disk_shown=shown;
    if(device_offset>=(int)info.device_count)device_offset=0;
    return 1;
}
static int tick(void){return paused ? 0 : refresh();}
static void opened(void){refresh();}
static int key(int k)
{
    int ch=k&255,scan=k&0xff00;
    if(ch==' '){paused=!paused;return 1;}
    if(ch=='r'||ch=='R')return refresh();
    if(scan==KEY_UP && device_offset>0){device_offset--;return 1;}
    if(scan==KEY_DOWN && device_offset+3<(int)info.device_count){device_offset++;return 1;}
    return 0;
}
static int click(int x,int y,int px,int py)
{
    if(accessory_hit(x,y,326,10,140,24) && accessory_hit(px,py,326,10,140,24))return key(' ');
    return 0;
}
static void bar(int y,uint32_t free,uint32_t total)
{
    int width=total ? (int)((uint64_t)free*440/total) : 0;
    if(width>440)width=440;
    accessory_box(&monitor,14,y,440,8,8);
    if(width)accessory_box(&monitor,14,y,width,8,4);
}
static void draw(void)
{
    char line[96];
    accessory_box(&monitor,0,0,monitor.window.work.w,monitor.window.work.h,0);
    accessory_text(&monitor,12,26,"SYSTEM MONITOR",4);
    accessory_button(&monitor,326,10,140,paused ? "Resume [Space]" : "Pause [Space]",paused);
    if(!ready){accessory_text(&monitor,12,60,"System snapshot unavailable.",2);return;}
    unsigned long sec=info.uptime_seconds;
    snprintf(line,sizeof(line),"Up %02lu:%02lu:%02lu   Console RAM %lu MiB",sec/3600,(sec/60)%60,sec%60,(unsigned long)(info.ram_bytes/(1024*1024)));
    accessory_text(&monitor,12,56,line,1);
    snprintf(line,sizeof(line),"GEM free %lu / %lu KiB  largest %lu",(unsigned long)(info.gem_free_bytes/1024),(unsigned long)(info.gem_pool_bytes/1024),(unsigned long)(info.gem_largest_bytes/1024));
    accessory_text(&monitor,12,80,line,1);bar(88,info.gem_free_bytes,info.gem_pool_bytes);
    snprintf(line,sizeof(line),"KOS heap used: %lu KiB",(unsigned long)(info.heap_used_bytes/1024));
    accessory_text(&monitor,12,116,line,1);
    for(int i=0;i<disk_shown;i++) {
        static const char *dkind[]={"RAM disk","read-only","SD card"};
        int n;
        if(disk_total[i])n=snprintf(line,sizeof(line),"%c: free %lu / %lu KiB  %s",disk_letter[i],(unsigned long)(disk_free[i]/1024),(unsigned long)(disk_total[i]/1024),dkind[disk_kind[i]]);
        else n=snprintf(line,sizeof(line),"%c: unavailable",disk_letter[i]);
        if(i==disk_shown-1&&disk_extra&&n>0&&n<(int)sizeof(line))snprintf(line+n,sizeof(line)-n,"  +%d more",disk_extra);
        accessory_text(&monitor,12,136+i*16,line,1);
    }
    accessory_text(&monitor,12,185,"GEM free memory - last 60 visible samples",9);
    accessory_box(&monitor,14,194,440,50,8);
    for(int i=0;i<samples;i++) {
        int index=(next_sample-samples+i+60)%60;
        int height=info.gem_pool_bytes ? (int)((uint64_t)history[index]*48/info.gem_pool_bytes) : 0;
        if(height>48)height=48;
        if(height)accessory_box(&monitor,16+i*7,243-height,5,height,4);
    }
    snprintf(line,sizeof(line),"Maple devices: %lu  [Up/Down scroll]",(unsigned long)info.device_count);
    accessory_text(&monitor,12,267,line,4);
    for(int i=0;i<3 && device_offset+i<(int)info.device_count;i++) {
        struct dc_device_info *d=&info.devices[device_offset+i];
        snprintf(line,sizeof(line),"%c%lu %-10s %.28s",'A'+d->port,(unsigned long)d->unit,kind(d->functions),d->name);
        accessory_text(&monitor,12,287+i*18,line,1);
    }
    accessory_text(&monitor,12,350,"R refresh | Space pause | Esc hides",9);
}
static Accessory monitor={.menu="  System Monitor",.title="System Monitor",.width=480,.height=368,.interval=1000,
    .opened=opened,.tick=tick,.key=key,.click=click,.draw=draw};
int app_main(int argc,char **argv){(void)argc;(void)argv;return accessory_run(&monitor);}
