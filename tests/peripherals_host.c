#include "dreamcast/control.h"
#include "dreamcast/vmu_info.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int applied;
void dc_hal_input_config_changed(uint32_t speed,uint32_t delay,uint32_t interval)
{assert(speed>=25 && speed<=400 && delay>=100 && interval>=20);applied++;}
static unsigned char image[256][512],saved[256][512];
static struct dc_vmu_info info;
static int reads, fail_block=-1;
static void put16(unsigned char *p,unsigned v){p[0]=v;p[1]=v>>8;}
static int read_block(void *ctx,unsigned block,unsigned char *out)
{
    assert(ctx==image && block<256 && ++reads<=18);
    if((int)block==fail_block)return -1;
    memcpy(out,image[block],512);return 0;
}
static void fixture(void)
{
    memset(image,0,sizeof(image));
    memset(image[255],0x55,16);
    put16(image[255]+70,254);put16(image[255]+72,1);
    put16(image[255]+74,253);put16(image[255]+76,13);put16(image[255]+80,200);
    for(int i=0;i<256;i++)put16(image[254]+2*i,i<200 ? 0xfffc : 0xfffa);
    reads=0;fail_block=-1;
}
static int inspect(void){reads=0;return dc_vmu_inspect(&info,read_block,image);}
int main(void)
{
    struct dc_input_config c,previous;
    assert(dc_input_config(0,NULL,0)==sizeof(c));
    assert(dc_input_config(0,&c,sizeof(c))==sizeof(c));
    assert(c.mouse_percent==100 && c.repeat_delay_ms==300 && c.repeat_interval_ms==40);
    previous=c;c.mouse_percent=25;c.repeat_delay_ms=1000;c.repeat_interval_ms=200;
    assert(dc_input_config(1,&c,sizeof(c))==sizeof(c) && applied==1);
    c.mouse_percent=0;
    assert(dc_input_config(1,&c,sizeof(c))==-64 && applied==1 && c.mouse_percent==0);
    assert(dc_input_config(0,&c,sizeof(c))==sizeof(c) && c.mouse_percent==25);
    assert(dc_input_config(0,&c,sizeof(c)-1)==-64 && c.mouse_percent==25);
    c.version=9;assert(dc_input_config(1,&c,sizeof(c))==-64 && applied==1);
    assert(dc_input_config(2,&c,sizeof(c))==-64);
    assert(dc_input_config(1,&previous,sizeof(previous))==sizeof(c));
    int remainder=0,sum=0;
    for(int i=0;i<100;i++)sum+=dc_scale_motion(1,25,&remainder);
    assert(sum==25 && remainder==0);
    for(int i=0;i<100;i++)sum+=dc_scale_motion(-1,25,&remainder);
    assert(sum==0 && remainder==0);
    assert(dc_scale_motion(-23,400,&remainder)==-92);

    fixture();assert(inspect()==0 && info.free_blocks==200 && !info.file_count && reads==15);
    unsigned char *e=image[253];e[0]=0x33;put16(e+2,2);memcpy(e+4,"NOTES       ",12);
    unsigned char stamp[8]={0x20,0x26,0x09,0x28,0x16,0x45,0,0};memcpy(e+16,stamp,8);
    put16(e+24,2);put16(image[254]+4,3);put16(image[254]+6,0xfffa);
    e+=32;e[0]=0xcc;e[1]=255;put16(e+2,4);memcpy(e+4,"GAME",4);put16(e+24,1);put16(image[254]+8,0xfffa);
    memcpy(saved,image,sizeof(image));
    assert(inspect()==0 && info.file_count==2 && info.free_blocks==197);
    assert(!strcmp(info.files[0].name,"NOTES") && info.files[0].blocks==2);
    assert(!strcmp(info.files[0].modified,"2026-09-28 16:45"));
    assert(info.files[1].type==0xcc && info.files[1].protected_file);
    assert(!strcmp(info.files[1].modified,"Unknown"));
    assert(!memcmp(saved,image,sizeof(image))); /* Inspection never mutates card data. */
    put16(image[254]+4,2);assert(inspect()==DC_VMU_CORRUPT); /* cycle */
    memcpy(image,saved,sizeof(image));put16(image[253]+34,2);assert(inspect()==DC_VMU_CORRUPT); /* cross-link */
    memcpy(image,saved,sizeof(image));put16(image[253]+24,201);assert(inspect()==DC_VMU_CORRUPT);
    fixture();image[255][0]=0;assert(inspect()==DC_VMU_UNFORMATTED && reads==1);
    fixture();put16(image[255]+72,2);assert(inspect()==DC_VMU_UNSUPPORTED && reads==1);
    fixture();put16(image[255]+74,1);assert(inspect()==DC_VMU_CORRUPT && reads==1);
    fixture();fail_block=252;assert(inspect()==DC_VMU_IO);
    puts("Input validation/scaling and bounded read-only VMU metadata: PASS");
    return 0;
}
