/* Dreamcast hardware services, GPL-2.0-or-later. */
#include <kos.h>
#include <dc/maple/keyboard.h>
#include <dc/maple/mouse.h>
#include <dc/maple/controller.h>
#include <malloc.h>
#include <time.h>
#include "dreamcast/hal.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
static file_t disc = FILEHND_INVALID;
static unsigned long last_present;
static const unsigned char scancode[256] = {
 [4]=0x1e,[5]=0x30,[6]=0x2e,[7]=0x20,[8]=0x12,[9]=0x21,[10]=0x22,[11]=0x23,
 [12]=0x17,[13]=0x24,[14]=0x25,[15]=0x26,[16]=0x32,[17]=0x31,[18]=0x18,[19]=0x19,
 [20]=0x10,[21]=0x13,[22]=0x1f,[23]=0x14,[24]=0x16,[25]=0x2f,[26]=0x11,[27]=0x2d,[28]=0x15,[29]=0x2c,
 [30]=2,[31]=3,[32]=4,[33]=5,[34]=6,[35]=7,[36]=8,[37]=9,[38]=10,[39]=11,
 [40]=0x1c,[41]=1,[42]=0x0e,[43]=0x0f,[44]=0x39,[45]=0x0c,[46]=0x0d,[47]=0x1a,[48]=0x1b,
 [49]=0x2b,[50]=0x2b,[51]=0x27,[52]=0x28,[53]=0x29,[54]=0x33,[55]=0x34,[56]=0x35,
 [58]=0x3b,[59]=0x3c,[60]=0x3d,[61]=0x3e,[62]=0x3f,[63]=0x40,[64]=0x41,[65]=0x42,[66]=0x43,[67]=0x44,
 [73]=0x52,[74]=0x47,[75]=0x49,[76]=0x53,[77]=0x4f,[78]=0x51,[79]=0x4d,[80]=0x4b,[81]=0x50,[82]=0x48
};
void dc_hal_init(void) {
 vid_set_mode(DM_640x480, PM_RGB565);
 kbd_set_repeat_timing(300,40);
 disc=fs_open("/cd/DISC.IMG",O_RDONLY);
 printf("EmuTOS native SH-4: video 640x480; Maple ready; CD image %s\n",disc==FILEHND_INVALID?"absent":"open");
}
unsigned long dc_millis(void) { return (unsigned long)timer_ms_gettime64(); }
void dc_sleep(unsigned int ms) { thd_sleep(ms); }
void dc_present(const unsigned short *p, const unsigned short *pal) {
 static uint16_t previous[480*160] __attribute__((aligned(32)));
 static uint16_t last_palette[16];
 static uint16_t rgb[640*480] __attribute__((aligned(32)));
 unsigned long now=dc_millis(); if(now-last_present<16) return; last_present=now;
 if(!memcmp(previous,p,sizeof(previous))&&!memcmp(last_palette,pal,sizeof(last_palette)))return;
 memcpy(previous,p,sizeof(previous));memcpy(last_palette,pal,sizeof(last_palette));
 uint16_t *dst=rgb;
 for(int y=0;y<480;y++) for(int w=0;w<40;w++) {
  unsigned a=*p++,b=*p++,c=*p++,d=*p++;
  for(unsigned m=0x8000;m;m>>=1) *dst++=pal[(!!(a&m))|((!!(b&m))<<1)|((!!(c&m))<<2)|((!!(d&m))<<3)];
 }
 sq_cpy(vram_s,rgb,sizeof(rgb));
}

int dc_poll_mouse(int *dx,int *dy,int *buttons) {
 static unsigned long last_poll; unsigned long now=dc_millis();
 if(now-last_poll<16) return 0; last_poll=now;
 *dx=*dy=*buttons=0;
 maple_device_t *dev=maple_enum_type(0,MAPLE_FUNC_MOUSE);
 if(dev) { mouse_state_t *m=maple_dev_status(dev); if(m) { *dx=m->dx;*dy=m->dy;*buttons=((m->buttons&MOUSE_LEFTBUTTON)?1:0)|((m->buttons&MOUSE_RIGHTBUTTON)?2:0); } }
 else { dev=maple_enum_type(0,MAPLE_FUNC_CONTROLLER); if(dev) {cont_state_t *c=maple_dev_status(dev); if(c) {
 *dx=(c->joyx>24?1:c->joyx < -24?-1:0)*4+((c->buttons&CONT_DPAD_RIGHT)?4:0)-((c->buttons&CONT_DPAD_LEFT)?4:0);
 *dy=(c->joyy>24?1:c->joyy < -24?-1:0)*4+((c->buttons&CONT_DPAD_DOWN)?4:0)-((c->buttons&CONT_DPAD_UP)?4:0);
 *buttons=((c->buttons&CONT_A)?1:0)|((c->buttons&CONT_B)?2:0);
 } } }
 return 1;
}
int dc_key_modifiers(void) {
 maple_device_t *d=maple_enum_type(0,MAPLE_FUNC_KEYBOARD); if(!d) return 0;
 kbd_state_t *k=kbd_get_state(d); if(!k) return 0;
 unsigned m=k->cond.modifiers.raw; return ((m&0x20)?1:0)|((m&2)?2:0)|((m&0x11)?4:0)|((m&0x44)?8:0)|((k->cond.leds.raw&2)?16:0);
}
unsigned long dc_poll_key(void) {
 maple_device_t *d=maple_enum_type(0,MAPLE_FUNC_KEYBOARD); if(!d) return 0;
 int raw=kbd_queue_pop(d,false); if(raw==KBD_QUEUE_END) return 0;
 unsigned key=raw&255;
 kbd_mods_t mods={.raw=(raw>>8)&255};kbd_leds_t leds={.raw=(raw>>16)&255};
 kbd_state_t *state=kbd_get_state(d);
 unsigned ascii=(unsigned char)kbd_key_to_ascii((kbd_key_t)key,state->region,mods,leds);
 if((mods.raw&0x11)&&key>=4&&key<=29)ascii=key-3;
 if(mods.raw&0x44)ascii=0; /* GEM Alt-letter drive shortcuts */
 if(key==76)ascii=127;

 return ((unsigned long)scancode[key]<<16)|ascii;
}
unsigned long dc_datetime(void) {
 time_t t=time(NULL); struct tm *v=localtime(&t); if(!v) return 0;
 return ((unsigned long)((v->tm_year-80)<<9|(v->tm_mon+1)<<5|v->tm_mday)<<16)|(v->tm_hour<<11)|(v->tm_min<<5)|(v->tm_sec/2);
}
void *dc_alloc(size_t n) { return calloc(1,n); }
void dc_free(void *p) { free(p); }
long dc_available(void) { struct mallinfo m=mallinfo(); return m.fordblks+0x8d000000UL- (uintptr_t)sbrk(0)-131072; }
long dc_disc_read(void *buf,unsigned long pos,unsigned long n) {
 if(disc==FILEHND_INVALID) return -1;
 if(fs_seek(disc,pos,SEEK_SET)<0) return -1;
 return fs_read(disc,buf,n);
}
int main(int argc,char **argv) { (void)argc;(void)argv; dc_hal_init();dc_core_main();return 0; }
/* AES remains cooperative: only the process holding its gate runs GEM code.
 * KOS owns each SH-4 context/stack so IRQ preemption uses valid thread stacks. */
static semaphore_t gates[8];
static void (*entries[8])(void);
static void *aes_process(void *arg) {int id=(int)(intptr_t)arg;sem_wait(&gates[id]);entries[id]();return NULL;}
void dc_context_init(void) {for(int i=0;i<8;i++)sem_init(&gates[i],0);}
int dc_context_create(int id,void (*entry)(void)) {
 kthread_attr_t attr={.stack_size=65536,.label="EmuTOS AES"};entries[id]=entry;
 return thd_create_ex(&attr,aes_process,(void *)(intptr_t)id)?0:-1;
}
void dc_context_switch(int old_id,int new_id) {
 if(old_id==new_id)return;
 sem_signal(&gates[new_id]);sem_wait(&gates[old_id]);
}
