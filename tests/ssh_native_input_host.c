/* Verify the native keyboard boundary against captured TOS/HID event shapes. */
#include "app.h"
#include "platform.h"
#include "dreamcast/control.h"
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
static unsigned raw_event,tos_event;
static long gemdos(int op,...){if(op==0x0b)return -1;if(op==0x08)return tos_event;return 0;}
static long snapshot(void*p,uint32_t n){assert(n==sizeof(struct dc_input_snapshot));struct dc_input_snapshot*s=p;memset(s,0,n);s->version=1;s->key_raw=raw_event;/* physical modifiers already released */return n;}
static const struct dc_native_api api={.version=1,.size=sizeof(api),.gemdos=gemdos,.input_snapshot=snapshot};
const struct dc_native_api *dc_os=&api;
int ssh_main(int argc,char**argv){(void)argc;(void)argv;return 0;}
int main(void){
    raw_event=48|(1<<8);tos_event=0x1b0000|']';assert((ssh_key()&255)==29);assert(ssh_modifiers()==4);
    raw_event=9|(4<<8);tos_event=0x210000;assert((ssh_key()&255)=='f');assert(ssh_modifiers()==2);
    raw_event=82|(2<<8);tos_event=0x480000;assert(ssh_key()==0x480000);assert(ssh_modifiers()==1);
    raw_event=6|(1<<8);tos_event=0x2e0003;assert(ssh_key()==0x2e0003);assert(ssh_modifiers()==4);
    puts("Native SSH keyboard checks passed");return 0;
}
