#include <kos.h>
#include "dreamcast/settings.h"
#include "dreamcast/vmu_info.h"
#include <assert.h>
#include <stdio.h>

static maple_device_t device={{MAPLE_FUNC_MEMCARD},0,1};
static struct dc_vmu_info info;
static uint32_t saved_block[256];
static int absent,reads,writes,write_fail,read_fail,verify_fail,crc_fail,build_fail,verify_crc_fail;
static int fail_block=-1,chain_error;
static int applied;
void dc_hal_input_config_changed(uint32_t a,uint32_t b,uint32_t c)
{ (void)a;(void)b;(void)c;applied++; }
maple_device_t *maple_enum_dev(unsigned port,unsigned unit)
{ assert(port==0 && unit==1);return absent ? NULL : &device; }
long dc_vmu_info(uint32_t port,uint32_t unit,void *buffer,uint32_t bytes)
{
    assert(port==0 && unit==1 && bytes==sizeof(info));
    memcpy(buffer,&info,bytes);return bytes;
}
int vmu_block_read(maple_device_t *d,unsigned block,uint8_t *out)
{
    assert(d==&device);reads++;
    if(read_fail || (int)block==fail_block)return -1;
    memset(out,0,512);
    if(block==255) {
        out[70]=chain_error==4 ? 255 : 254;out[72]=1;out[80]=200;
    } else if(block==254) {
        unsigned next=chain_error==1 ? 200 : chain_error==2 ? 17 : 42;
        out[2*17]=next;out[2*42]=chain_error==3 ? 0xfc : 0xfa;out[2*42+1]=0xff;
    } else {
        assert(block==17 || block==42);
        unsigned offset=block==17 ? 0 : 512;
        memcpy(out,(unsigned char *)saved_block+offset,512);
        if(verify_fail && block==42)out[128]^=1;
    }
    return 0;
}
int vmufs_write(maple_device_t *d,const char *name,void *buf,int size,int flags)
{
    assert(d==&device && !strcmp(name,DC_SETTINGS_FILE) && size==1024 && flags==VMUFS_OVERWRITE);
    /* Read every byte just as KOS does, catching unpadded package allocations. */
    const unsigned char *raw=buf;
    for(unsigned i=640+DC_SETTINGS_DATA_BYTES;i<1024;i++)assert(raw[i]==0);
    writes++;
    if(write_fail)return -1;
    memcpy(saved_block,buf,sizeof(saved_block));
    info.file_count=1;
    info.files[0]=(struct dc_vmu_file){.blocks=2,.type=0x33,.first_block=17};
    strcpy(info.files[0].name,DC_SETTINGS_FILE);
    if(verify_crc_fail)crc_fail=1;
    return 0;
}
/* Stub the SDK packager, whose CRC implementation is checked in Flycast.
 * Keep its real 128-byte header and unpadded allocation behaviour. */
int vmu_pkg_build(vmu_pkg_t *p,uint8_t **out,int *bytes)
{
    assert(sizeof(vmu_hdr_t)==128 && p->icon_cnt==1 && !p->eyecatch_type && p->data_len==32);
    assert(p->icon_pal[0]==0 && p->icon_pal[3]!=p->icon_pal[7]);
    unsigned coloured=0;
    for(int i=0;i<512;i++)coloured+=p->icon_data[i]!=0;
    assert(coloured>200 && coloured<512); /* visible art with a transparent margin */
    if(build_fail)return -1;
    *bytes=640+p->data_len;*out=calloc(1,*bytes);assert(*out);
    vmu_hdr_t *hdr=(void *)*out;
    strcpy(hdr->app_id,p->app_id);hdr->data_len=p->data_len;hdr->icon_cnt=p->icon_cnt;
    memcpy(hdr->icon_pal,p->icon_pal,sizeof(hdr->icon_pal));
    memcpy(*out+128,p->icon_data,512);
    memcpy(*out+640,p->data,p->data_len);return 0;
}
int vmu_pkg_parse(uint8_t *buf,size_t size,vmu_pkg_t *p)
{
    assert(size==512 || size==1024);
    vmu_hdr_t *hdr=(void *)buf;
    assert(hdr->icon_cnt==size/512-1 && !hdr->eyecatch_type && hdr->data_len==32);
    if(crc_fail)return -1;
    p->data=buf+128+512*hdr->icon_cnt;return 0;
}
static struct dc_control_settings initial={1,sizeof(initial),{1,20,175,600,80},2};
static long save(struct dc_control_settings *s){return dc_control_store(1,0,1,s,sizeof(*s));}
static long load(struct dc_control_settings *s){return dc_control_store(0,0,1,s,sizeof(*s));}
static void failed_load(int expected)
{
    struct dc_control_settings out,before;
    memset(&out,0xa5,sizeof(out));before=out;
    assert(load(&out)==expected && !memcmp(&out,&before,sizeof(out)));
}
int main(void)
{
    assert(sizeof(initial)==DC_SETTINGS_DATA_BYTES);
    struct dc_control_settings value=initial,out;
    info.status=DC_VMU_OK;info.free_blocks=200;
    assert(dc_control_store(0,0,1,NULL,0)==sizeof(value));
    assert(dc_control_store(1,0,1,NULL,0)==-64);
    assert(dc_control_store(0,4,1,&out,sizeof(out))==-64);
    assert(dc_control_store(0,0,6,&out,sizeof(out))==-64);
    assert(dc_control_store(2,0,1,&out,sizeof(out))==-64);
    assert(dc_control_store(0,0,1,&out,sizeof(out)-1)==-64);
    failed_load(DC_SETTINGS_MISSING);
    info.status=DC_VMU_ABSENT;assert(save(&value)==DC_SETTINGS_ABSENT);
    info.status=DC_VMU_CORRUPT;assert(save(&value)==DC_SETTINGS_INVALID);
    info.status=DC_VMU_IO;failed_load(DC_SETTINGS_IO);
    info.status=0;info.free_blocks=0;assert(save(&value)==DC_SETTINGS_FULL);
    info.free_blocks=1;assert(save(&value)==DC_SETTINGS_FULL);
    assert(!writes);
    info.free_blocks=200;
    uint32_t *fields[]={&value.version,&value.bytes,&value.input.version,&value.input.bytes,
        &value.input.mouse_percent,&value.input.repeat_delay_ms,&value.input.repeat_interval_ms};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++) {
        uint32_t old=*fields[i];*fields[i]=0;assert(save(&value)==-64);*fields[i]=old;
    }
    value.desktop_colour=4;assert(save(&value)==-64);value=initial;
    value.input.mouse_percent=401;assert(save(&value)==-64);value=initial;
    value.input.repeat_delay_ms=1001;assert(save(&value)==-64);value=initial;
    value.input.repeat_interval_ms=201;assert(save(&value)==-64);value=initial;
    assert(!writes);
    build_fail=1;assert(save(&value)==DC_SETTINGS_IO);build_fail=0;
    absent=1;assert(save(&value)==DC_SETTINGS_ABSENT && !writes);absent=0;
    assert(save(&value)==sizeof(value) && writes==1);
    assert(load(&out)==sizeof(out) && !memcmp(&out,&value,sizeof(out)) && !applied);
    const unsigned char *raw=(void *)saved_block;
    assert(raw[640+16]==175 && raw[640+20]==0x58 && raw[640+21]==2 && raw[640+28]==2);
    assert(save(&value)==sizeof(value) && writes==1); /* identical saves do not wear flash */
    info.free_blocks=0;value.desktop_colour=3;
    assert(save(&value)==sizeof(value) && writes==2); /* existing blocks can be reused */
    uint32_t good_block[256];memcpy(good_block,saved_block,sizeof(good_block));
    vmu_hdr_t *hdr=(void *)saved_block;
    hdr->app_id[0]='X';failed_load(DC_SETTINGS_CONFLICT);assert(save(&initial)==DC_SETTINGS_CONFLICT);
    memcpy(saved_block,good_block,sizeof(good_block));hdr->data_len=0xffffffff;failed_load(DC_SETTINGS_INVALID);
    memcpy(saved_block,good_block,sizeof(good_block));hdr->icon_cnt=65535;failed_load(DC_SETTINGS_INVALID);
    memcpy(saved_block,good_block,sizeof(good_block));hdr->icon_cnt=0;failed_load(DC_SETTINGS_INVALID);
    memcpy(saved_block,good_block,sizeof(good_block));hdr->eyecatch_type=3;failed_load(DC_SETTINGS_INVALID);
    memcpy(saved_block,good_block,sizeof(good_block));crc_fail=1;failed_load(DC_SETTINGS_INVALID);
    assert(save(&initial)==DC_SETTINGS_INVALID);crc_fail=0;
    info.files[0].protected_file=1;assert(save(&initial)==DC_SETTINGS_CONFLICT);info.files[0].protected_file=0;
    info.files[0].blocks=3;failed_load(DC_SETTINGS_CONFLICT);info.files[0].blocks=2;
    info.files[0].blocks=1;failed_load(DC_SETTINGS_INVALID);info.files[0].blocks=2;
    info.files[0].header_block=1;failed_load(DC_SETTINGS_CONFLICT);info.files[0].header_block=0;
    assert(writes==2);
    for(chain_error=1;chain_error<=4;chain_error++)failed_load(DC_SETTINGS_INVALID);
    chain_error=0;
    fail_block=42;failed_load(DC_SETTINGS_IO);fail_block=254;failed_load(DC_SETTINGS_IO);fail_block=-1;
    unsigned char *payload=(unsigned char *)saved_block+640;
    for(unsigned i=0;i<8;i++) {
        memcpy(saved_block,good_block,sizeof(good_block));payload[4*i]=0xff;payload[4*i+1]=0xff;
        failed_load(DC_SETTINGS_INVALID);
    }
    memcpy(saved_block,good_block,sizeof(good_block));
    read_fail=1;failed_load(DC_SETTINGS_IO);assert(save(&initial)==DC_SETTINGS_IO);read_fail=0;
    write_fail=1;assert(save(&initial)==DC_SETTINGS_IO);write_fail=0;
    assert(!memcmp(saved_block,good_block,sizeof(good_block)));
    info.file_count=0;info.free_blocks=2;verify_fail=1;
    assert(save(&initial)==DC_SETTINGS_IO);verify_fail=0;
    info.file_count=0;verify_crc_fail=1;
    assert(save(&initial)==DC_SETTINGS_IO);verify_crc_fail=crc_fail=0;
    /* Original iconless saves still load. Even identical data upgrades on Save,
     * but needs one extra free block and leaves the old file alone if full. */
    hdr->icon_cnt=0;info.files[0].blocks=1;
    memmove((unsigned char *)saved_block+128,(unsigned char *)saved_block+640,32);
    memset((unsigned char *)saved_block+160,0,sizeof(saved_block)-160);
    assert(load(&out)==sizeof(out) && !memcmp(&out,&initial,sizeof(out)));
    int before=writes;memcpy(good_block,saved_block,sizeof(good_block));
    info.free_blocks=0;assert(save(&initial)==DC_SETTINGS_FULL && writes==before);
    assert(!memcmp(saved_block,good_block,sizeof(good_block)));
    info.free_blocks=1;assert(save(&initial)==sizeof(initial) && writes==before+1 && hdr->icon_cnt==1);
    assert(load(&out)==sizeof(out) && !memcmp(&out,&initial,sizeof(out)));
    assert(save(&initial)==sizeof(initial) && writes==before+1);
    puts("VMU settings icon, fragmented files, legacy migration, errors and verification: PASS");
    return 0;
}
