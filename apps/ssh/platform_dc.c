/* Reuse the OS TCP and GEMDOS raw console interfaces. GPL-3.0-or-later. */
#include "platform.h"
#include "app.h"
#include "drives.h"
#include "dreamcast/net.h"
#include "dreamcast/control.h"
#define HAS(m) (dc_os->size >= offsetof(struct dc_native_api,m)+sizeof(dc_os->m) && dc_os->m)
uint32_t ssh_now(void) { return (uint32_t)dc_os->millis(); }
void ssh_idle(void) { dc_os->yield(); }
static unsigned last_modifiers;
int ssh_key(void)
{
    if(!dc_os->gemdos(0x0b))return 0;
    int key=(int)dc_os->gemdos(0x08);
    struct dc_input_snapshot s;
    last_modifiers=0;
    if(HAS(input_snapshot)&&dc_os->input_snapshot(&s,sizeof(s))==sizeof(s)) {
        /* The raw event retains modifiers even after the physical key is up. */
        unsigned m=(s.key_raw>>8)&255,raw=s.key_raw&255;
        last_modifiers=((m&0x22)?1:0)|((m&0x44)?2:0)|((m&0x11)?4:0);
        unsigned ch=key&255;
        /* GEM reserves Alt letters for drive shortcuts and clears their ASCII. */
        if(!ch&&(m&0x44)&&raw>=4&&raw<=29)ch='a'+raw-4;
        if((m&0x11)&&ch>='@'&&ch<='_')ch&=31;
        key=(key&~255)|(int)ch;
    }
    return key;
}
unsigned ssh_modifiers(void){return last_modifiers;}
void ssh_output(const char *p,size_t n) { while(n--) dc_os->gemdos(0x06,(unsigned char)*p++); }
int ssh_network_ready(void)
{
    struct dc_net_info n;
    if (!HAS(tcp_connect) || !HAS(tcp_connected) || !HAS(net_resolve) || !HAS(net_info) ||
        !HAS(tcp_recv) || !HAS(tcp_send) || !HAS(tcp_close) || !HAS(millis) || !dc_os->yield) return 0;
    return dc_os->net_info(&n,sizeof(n)) == sizeof(n) && n.state == DC_NET_UP;
}
int ssh_resolve(const char *host,uint8_t ip[4]) { return dc_os->net_resolve(host,8000,ip); }
long ssh_tcp_connect(const uint8_t ip[4],unsigned port) { return dc_os->tcp_connect(ip,port); }
long ssh_tcp_connected(int h) { return dc_os->tcp_connected(h); }
long ssh_tcp_read(int h,void *p,unsigned n) { return dc_os->tcp_recv(h,p,n); }
long ssh_tcp_write(int h,const void *p,unsigned n) { return dc_os->tcp_send(h,p,n); }
void ssh_tcp_close(int h) { dc_os->tcp_close(h); }
char ssh_default_drive(void) { return dc_storage_drive(); }
int app_main(int argc,char **argv) { return ssh_main(argc,argv); }
