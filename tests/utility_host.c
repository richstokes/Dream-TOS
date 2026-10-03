#include "gem_model.h"
#include "dreamcast/control.h"
#include "dreamcast/system_info.h"
#include "dreamcast/vmu_info.h"
#include "dreamcast/settings.h"
#include <stdarg.h>
#define opened utility_opened
#if defined(TEST_CONTROL)
#include "../apps/ports/control.c"
#elif defined(TEST_MONITOR)
#include "../apps/ports/monitor.c"
#endif
#undef opened
static struct dc_system_info fixture;
static struct dc_input_config settings={1,sizeof(settings),100,300,40};
static int config_writes,card_reads,fail_card;
static struct dc_control_settings preferences[2];
static int saved_preferences[2],preference_reads,preference_writes,preference_error;
static long store_api(int write,uint32_t port,uint32_t unit,void *buf,uint32_t bytes)
{
    assert(port<2 && unit==1 && bytes==sizeof(preferences[0]));
    if(preference_error)return preference_error;
    if(write){preferences[port]=*(struct dc_control_settings *)buf;saved_preferences[port]=1;preference_writes++;}
    else {preference_reads++;if(!saved_preferences[port])return DC_SETTINGS_MISSING;memcpy(buf,&preferences[port],bytes);}
    return bytes;
}
static long config_api(int write,void *buf,uint32_t bytes)
{
    assert(bytes==sizeof(settings));
    if(write){settings=*(struct dc_input_config *)buf;config_writes++;}
    else memcpy(buf,&settings,bytes);
    return bytes;
}
static long input_api(void *buf,uint32_t bytes)
{
    assert(bytes==sizeof(struct dc_input_snapshot));memset(buf,0,bytes);return bytes;
}
static long system_api(void *buf,uint32_t bytes)
{
    assert(bytes==sizeof(fixture));memcpy(buf,&fixture,bytes);return bytes;
}
static long vmu_api(uint32_t port,uint32_t unit,void *buf,uint32_t bytes)
{
    assert(port<4 && unit==1 && bytes==sizeof(struct dc_vmu_info));card_reads++;
    struct dc_vmu_info *v=buf;memset(v,0,bytes);v->version=1;v->bytes=bytes;
    v->port=port;v->unit=unit;v->status=fail_card ? DC_VMU_IO : 0;
    if(!fail_card){v->total_blocks=200;v->free_blocks=180;v->file_count=12;
        for(int i=0;i<12;i++){snprintf(v->files[i].name,16,"SAVE%02d",i);v->files[i].blocks=1;}}
    return bytes;
}
static long gemdos_api(int op,...)
{
    assert(op==0x36);va_list a;va_start(a,op);uint32_t *disk=va_arg(a,uint32_t *);
    int drive=va_arg(a,int);assert(drive==3 || drive==4);
    disk[0]=drive==3 ? 5 : 8;disk[1]=10;disk[2]=512;disk[3]=2;va_end(a);return 0;
}
static void aes_api(void *v)
{
    struct aes_pb *p=v;
    if(p->c[0]==79){p->o[0]=1;p->o[1]=123;p->o[2]=234;}
    else mock_aes(v);
}
static struct dc_native_api api={.size=sizeof(api),.aes=aes_api,.vdi=mock_vdi,.gemdos=gemdos_api,
    .input_config=config_api,.input_snapshot=input_api,.system_info=system_api,.vmu_info=vmu_api,
    .control_store=store_api};
const struct dc_native_api *dc_os=&api;
int main(void)
{
    assert(app_begin_windowed());visible[0]=desktop;inspect_drawing=1;
    fixture.version=1;fixture.bytes=sizeof(fixture);fixture.gem_pool_bytes=1000;fixture.gem_free_bytes=750;
    fixture.gem_largest_bytes=500;fixture.ram_bytes=16*1024*1024;fixture.drive_mask=12;
    fixture.device_count=2;
    for(int i=0;i<2;i++){fixture.devices[i].port=i;fixture.devices[i].unit=1;fixture.devices[i].functions=0x02000000;}
#if defined(TEST_CONTROL)
    init();assert(ready && config.mouse_percent==100 && pointer_x==123);
    for(int i=0;i<20;i++)adjust(0,1);assert(config.mouse_percent==400);
    for(int i=0;i<30;i++)adjust(0,-1);assert(config.mouse_percent==25);
    adjust(3,1);assert(colour==1 && palette_writes==1);
    defaults();assert(config.mouse_percent==100 && colour==0 && palette_writes==2);
    int writes=config_writes;assert(!click(310,40,460,40) && config_writes==writes);
    click(460,40,460,40);assert(config.mouse_percent==125);
    assert(!preference_writes && preference_reads==2 && selected_card==1);
    key('s');assert(preference_writes==1 && preferences[0].input.mouse_percent==125);
    defaults();key('l');assert(config.mouse_percent==125 && preference_writes==1);
    focus=4;key(KEY_RIGHT);assert(selected_card==7);
    adjust(3,2);key('s');assert(preferences[1].desktop_colour==2 && preference_writes==2);
    preference_error=DC_SETTINGS_FULL;key('s');assert(strstr(status,"full") && preference_writes==2);
    preference_error=DC_SETTINGS_INVALID;key('l');assert(strstr(status,"Invalid") && colour==2);
    preference_error=0;
    int before_reads=preference_reads;
    for(int i=0;i<10;i++)tick();assert(preference_reads==before_reads && preference_writes==2);
    fixture.device_count=1;tick();assert(!card_present && selected_card==7);
    key('s');assert(strstr(status,"not connected") && preference_writes==2);
    fixture.device_count=2;tick();assert(card_present);
    /* Only the later card has a valid save: boot must find and apply it. */
    saved_preferences[0]=0;defaults();init();assert(colour==2 && config.mouse_percent==125 && selected_card==7);
    assert(!click(20,200,180,200) && preference_writes==2);
    click(20,200,20,200);assert(preference_writes==3);
    /* Old API keeps session settings usable and does no persistence I/O. */
    api.size=offsetof(struct dc_native_api,control_store);init();assert(ready);
    key('s');assert(strstr(status,"unavailable") && preference_writes==3);
    api.size=sizeof(api);
    panel.window.handle=-1;int16_t m[8]={40};accessory_message(&panel,m);assert(opened==1);
    api.size=offsetof(struct dc_native_api,input_config);utility_opened();assert(!ready);
#elif defined(TEST_MONITOR)
    assert(refresh() && ready && samples==1 && history[0]==750);
    assert(disk_free[0]==5120 && disk_total[0]==10240 && disk_free[1]==8192);
    for(int i=0;i<70;i++)refresh();assert(samples==60);
    int old=next_sample;key(' ');assert(!tick() && next_sample==old);key('r');assert(next_sample!=old);
    monitor.window.handle=-1;int16_t m[8]={40};accessory_message(&monitor,m);assert(opened==1);
#endif
    assert(!updates && !mouse_hidden && !mouse_control);
    puts("Accessory controls, queried values and device changes: PASS");return 0;
}
