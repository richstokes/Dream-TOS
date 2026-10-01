/* Dreamcast hardware services, GPL-2.0-or-later. */
#include <kos.h>
#include <dc/maple/keyboard.h>
#include <dc/maple/mouse.h>
#include <dc/maple/controller.h>
#include <malloc.h>
#include <kos/version.h>
#include <dc/fs_dcload.h>
#include <time.h>
#include "dreamcast/hal.h"
#include "dreamcast/system_info.h"
#include "dreamcast/control.h"
#include "dreamcast/net.h"
static unsigned mouse_percent=100;
static int mouse_fraction_x, mouse_fraction_y;
static struct dc_input_snapshot input_snapshot;
KOS_INIT_FLAGS(INIT_DEFAULT);
static file_t disc = FILEHND_INVALID;
static unsigned long last_present;
static const char *boot_stage = "01 Video ready";
static int boot_screen_visible;
void dc_boot_status(const char *stage) {
 boot_stage=stage;
 dc_boot_draw(vram_s,boot_stage,NULL);
 boot_screen_visible=1;
 printf("BOOT: %s\n",stage);
}
void dc_boot_failure(const char *message) {
 dc_boot_draw(vram_s,boot_stage,message);
 boot_screen_visible=1;
}
/* KOS calls this only for exceptions with no registered handler. The renderer
 * uses the built-in font and direct VRAM stores, so it also works in IRQ context.
 * Halt here: KOS's default panic can reboot and erase the visible diagnosis. */
static void boot_exception(irq_t event,irq_context_t *ctx,void *unused) {
 char message[128];
 (void)unused;
 snprintf(message,sizeof(message),"SH-4 exception %04x\nPC %08lx  PR %08lx\nSP %08lx  SR %08lx",
          (unsigned)event,(unsigned long)ctx->pc,(unsigned long)ctx->pr,
          (unsigned long)ctx->r[15],(unsigned long)ctx->sr);
 dc_boot_failure(message);
 dbgio_printf("%s\n",message);
 irq_disable();
 for(;;) __asm__ volatile("nop");
}
static void *capture_mouse(void *);
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
 dc_boot_status("01 Video ready");
 irq_set_handler(EXC_UNHANDLED_EXC,boot_exception,NULL);
 kbd_set_repeat_timing(300,40);
 kthread_attr_t input_attr={.stack_size=8192,.prio=PRIO_DEFAULT-1,.label="Dream TOS Maple"};
 if(!thd_create_ex(&input_attr,capture_mouse,NULL)) {
  dc_boot_failure("Cannot start Maple input thread");
  arch_panic("Cannot start Maple input thread");
 }
 dc_hal_net_start(); /* background: DHCP may take a while or find nothing */
 dc_boot_status("02 Opening CD volume");
 disc=fs_open("/cd/DISC.IMG",O_RDONLY);
 /* dc-tool -m maps the bundled FAT volume into /pc/. Keep the same D: image
  * and boot checks when testing an ELF without replacing the GDEMU disc. */
 if(disc==FILEHND_INVALID && dcload_type!=DCLOAD_TYPE_NONE) {
  dc_boot_status("02 Opening host application volume");
  disc=fs_open("/pc/DISC.IMG",O_RDONLY);
 }
 printf("Dream TOS native SH-4: video 640x480; Maple ready; CD image %s\n",disc==FILEHND_INVALID?"absent":"open");
}
unsigned long dc_millis(void) { return (unsigned long)timer_ms_gettime64(); }
/* Shut KOS down before entering the boot ROM. The BIOS chooses how to boot
 * the currently inserted disc, including returning here for our own CDI. */
void dc_boot_disc(void) {
 printf("Dream TOS: rebooting to boot the inserted disc\n");
 /* KOS tears ISO9660 down before its final file-table cleanup. Close our
  * long-lived D: handle while the driver's mutexes are still alive. */
 if(disc!=FILEHND_INVALID){fs_close(disc);disc=FILEHND_INVALID;}
 arch_set_exit_path(ARCH_EXIT_REBOOT);
 arch_exit();
}
void dc_sleep(unsigned int ms) { thd_sleep(ms); }
void dc_present(const unsigned short *p, const unsigned short *pal) {
 static uint16_t previous[480*160] __attribute__((aligned(32)));
 static uint16_t last_palette[16];
 static uint16_t rgb[640*480] __attribute__((aligned(32)));
 unsigned long now=dc_millis(); if(now-last_present<16) return; last_present=now;
 if(!boot_screen_visible&&!memcmp(previous,p,sizeof(previous))&&!memcmp(last_palette,pal,sizeof(last_palette)))return;
 memcpy(previous,p,sizeof(previous));memcpy(last_palette,pal,sizeof(last_palette));
 uint16_t *dst=rgb;
 for(int y=0;y<480;y++) for(int w=0;w<40;w++) {
  unsigned a=*p++,b=*p++,c=*p++,d=*p++;
  for(unsigned m=0x8000;m;m>>=1) *dst++=pal[(!!(a&m))|((!!(b&m))<<1)|((!!(c&m))<<2)|((!!(d&m))<<3)];
 }
 sq_cpy(vram_s,rgb,sizeof(rgb));
 boot_screen_visible=0;
}

/* Consume each Maple relative delta once. Capture independently of GEM redraws
 * so a slow application cannot discard mouse packets or short button presses. */
struct mouse_packet {int dx,dy,buttons;};
static struct mouse_packet mouse_queue[128];
static unsigned mouse_head,mouse_tail;
static void *capture_mouse(void *unused) {
 (void)unused;int previous_buttons=0;
 for(;;) {
  int dx=0,dy=0,buttons=0;maple_device_t *dev=maple_enum_type(0,MAPLE_FUNC_MOUSE);
  irq_mask_t irq=irq_disable();
  if(dev) {mouse_state_t *m=maple_dev_status(dev);if(m){dx=m->dx;dy=m->dy;m->dx=m->dy=0;buttons=((m->buttons&MOUSE_LEFTBUTTON)?1:0)|((m->buttons&MOUSE_RIGHTBUTTON)?2:0);}}
  else {dev=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);if(dev){cont_state_t *c=maple_dev_status(dev);if(c){
   dx=(c->joyx>24?1:c->joyx < -24?-1:0)+!!(c->buttons&CONT_DPAD_RIGHT)-!!(c->buttons&CONT_DPAD_LEFT);
   dy=(c->joyy>24?1:c->joyy < -24?-1:0)+!!(c->buttons&CONT_DPAD_DOWN)-!!(c->buttons&CONT_DPAD_UP);
   buttons=((c->buttons&CONT_A)?1:0)|((c->buttons&CONT_B)?2:0);
  }}}
  if(dev && (dev->info.functions&MAPLE_FUNC_MOUSE)) {
   if(dx||dy||buttons!=previous_buttons) {
    input_snapshot.mouse_dx=dx;input_snapshot.mouse_dy=dy;
    input_snapshot.mouse_packets++;
   }
   dx=dc_scale_motion(dx,mouse_percent,&mouse_fraction_x);
   dy=dc_scale_motion(dy,mouse_percent,&mouse_fraction_y);
  }
  if(dx||dy||buttons!=previous_buttons){unsigned next=(mouse_head+1)%128;
   if(next!=mouse_tail){mouse_queue[mouse_head]=(struct mouse_packet){dx,dy,buttons};mouse_head=next;previous_buttons=buttons;}
  }
  irq_restore(irq);thd_sleep(4);
 }
 return NULL;
}
int dc_poll_mouse(int *dx,int *dy,int *buttons) {
 irq_mask_t irq=irq_disable();
 if(mouse_head==mouse_tail){irq_restore(irq);return 0;}
 struct mouse_packet m=mouse_queue[mouse_tail];mouse_tail=(mouse_tail+1)%128;
 irq_restore(irq);*dx=m.dx;*dy=m.dy;*buttons=m.buttons;return 1;
}
int dc_key_modifiers(void) {
 maple_device_t *d=maple_enum_type(0,MAPLE_FUNC_KEYBOARD); if(!d) return 0;
 kbd_state_t *k=kbd_get_state(d); if(!k) return 0;
 unsigned m=k->cond.modifiers.raw; return ((m&0x20)?1:0)|((m&2)?2:0)|((m&0x11)?4:0)|((m&0x44)?8:0)|((k->cond.leds.raw&2)?16:0);
}
unsigned long dc_poll_key(void) {
 maple_device_t *d=maple_enum_type(0,MAPLE_FUNC_KEYBOARD); if(!d) return 0;
 int raw=kbd_queue_pop(d,false); if(raw==KBD_QUEUE_END) return 0;
 input_snapshot.key_raw=(unsigned)raw;input_snapshot.key_events++;
 unsigned key=raw&255;
 kbd_mods_t mods={.raw=(raw>>8)&255};kbd_leds_t leds={.raw=(raw>>16)&255};
 /* Match TOS keyboard mouse controls: Alt-arrows, Alt-Insert click.
  * Queue transitions, including release, so AES sees a complete click. */
 if((mods.raw&0x44)&&((key>=79&&key<=82)||key==73||key==44)) {
  int step=(mods.raw&0x22)?1:8;
  irq_mask_t irq=irq_disable();unsigned n=(mouse_head+1)%128;
  if(n!=mouse_tail){mouse_queue[mouse_head]=(struct mouse_packet){key==79?step:key==80?-step:0,key==81?step:key==82?-step:0,(key==73||key==44)?1:0};mouse_head=n;}
  if(key==73||key==44){n=(mouse_head+1)%128;if(n!=mouse_tail){mouse_queue[mouse_head]=(struct mouse_packet){0,0,0};mouse_head=n;}}
  irq_restore(irq);return 0;
 }
 kbd_state_t *state=kbd_get_state(d);
 unsigned ascii=(unsigned char)kbd_key_to_ascii((kbd_key_t)key,state->region,mods,leds);
 if((mods.raw&0x11)&&key>=4&&key<=29)ascii=key-3;
 if(mods.raw&0x44)ascii=0; /* GEM Alt-letter drive shortcuts */
 if(key==76)ascii=127;

 input_snapshot.key_tos=((unsigned long)scancode[key]<<16)|ascii;
 return input_snapshot.key_tos;
}
unsigned long dc_datetime(void) {
 time_t t=time(NULL); struct tm *v=localtime(&t); if(!v) return 0;
 return ((unsigned long)((v->tm_year-80)<<9|(v->tm_mon+1)<<5|v->tm_mday)<<16)|(v->tm_hour<<11)|(v->tm_min<<5)|(v->tm_sec/2);
}
void *dc_alloc(size_t n) { return calloc(1,n); }
void dc_free(void *p) { free(p); }
long dc_available(void) { struct mallinfo m=mallinfo(); return m.fordblks+0x8d000000UL- (uintptr_t)sbrk(0)-131072; }
long dc_disc_read(void *buf,unsigned long pos,unsigned long n) {
 /* GEMDOS buffers have two-byte alignment. KOS ISO9660 can switch between
  * streaming and cached reads according to destination alignment; mixing
  * those paths on consecutive reads can leave its stream position behind.
  * Always read into the same DMA-aligned buffer, then copy to GEMDOS. */
 static unsigned char sector_buffer[2048] __attribute__((aligned(32)));
 if(disc==FILEHND_INVALID) return -1;
 if(fs_seek(disc,pos,SEEK_SET)<0) return -1;
 unsigned long done=0;
 while(done<n) {
  unsigned long count=n-done;
  if(count>sizeof(sector_buffer))count=sizeof(sector_buffer);
  long got=fs_read(disc,sector_buffer,count);
  if(got<=0)return done ? (long)done : got;
  memcpy((unsigned char *)buf+done,sector_buffer,got);
  done+=got;
  if((unsigned long)got<count)break;
 }
 return done;
}
int main(int argc,char **argv) { (void)argc;(void)argv; dc_hal_init();dc_core_main();return 0; }
/* AES remains cooperative: only the process holding its gate runs GEM code.
 * KOS owns each SH-4 context/stack so IRQ preemption uses valid thread stacks. */
static semaphore_t gates[8];
static void (*entries[8])(void);
static void *aes_process(void *arg) {int id=(int)(intptr_t)arg;sem_wait(&gates[id]);entries[id]();return NULL;}
void dc_context_init(void) {for(int i=0;i<8;i++)sem_init(&gates[i],0);}
int dc_context_create(int id,void (*entry)(void)) {
 kthread_attr_t attr={.stack_size=65536,.prio=PRIO_DEFAULT,.label="Dream TOS AES"};entries[id]=entry;
 return thd_create_ex(&attr,aes_process,(void *)(intptr_t)id)?0:-1;
}
void dc_context_switch(int old_id,int new_id) {
 if(old_id==new_id)return;
 sem_signal(&gates[new_id]);sem_wait(&gates[old_id]);
}

void dc_sync_code(void *p,unsigned long bytes){dcache_wback_range((uintptr_t)p,bytes);icache_sync_range((uintptr_t)p,bytes);}

/* Keep KOS structs on this side of the packing boundary. No bus transactions
 * are sent here: Maple info comes from devices already enumerated by KOS. */
void dc_hal_system_info(struct dc_system_info *info)
{
    int region;
    info->system_type = hardware_sys_mode(&region);
    info->ram_bytes = HW_MEMSIZE;
    info->heap_used_bytes = mallinfo().uordblks;
    info->uptime_seconds = timer_ms_gettime64() / 1000;
    strlcpy(info->kos_version, kos_version_string(), sizeof(info->kos_version));
    info->video_cable = vid_check_cable();
    if (vid_mode) {
        info->flags |= DC_INFO_VIDEO;
        info->video_width = vid_mode->width;
        info->video_height = vid_mode->height;
        info->video_pixel_mode = vid_mode->pm;
        info->video_flags = vid_mode->flags;
    }
    /* Copy under the IRQ gate so asynchronous detach cannot invalidate a
     * device pointer partway through the snapshot. */
    irq_mask_t irq = irq_disable();
    for (int p = 0; p < MAPLE_PORT_COUNT; p++) {
        for (int u = 0; u < MAPLE_UNIT_COUNT; u++) {
            maple_device_t *dev = maple_enum_dev(p, u);
            if (!dev || info->device_count == DC_SYSTEM_INFO_DEVICES)
                continue;
            struct dc_device_info *dst = &info->devices[info->device_count++];
            dst->port = p;
            dst->unit = u;
            dst->functions = dev->info.functions;
            unsigned n = 0;
            for (; n < sizeof(dev->info.product_name); n++) {
                unsigned char c = dev->info.product_name[n];
                if (!c)
                    break;
                dst->name[n] = (c >= 32 && c < 127) ? c : '?';
            }
            while (n && dst->name[n - 1] == ' ')
                n--;
            dst->name[n] = 0;
        }
    }
    irq_restore(irq);
}

void dc_hal_input_config_changed(uint32_t speed,uint32_t delay,uint32_t interval)
{
 irq_mask_t irq=irq_disable();
 mouse_percent=speed;mouse_fraction_x=mouse_fraction_y=0;
 irq_restore(irq);
 kbd_set_repeat_timing(delay,interval);
}
long dc_input_snapshot(void *buffer,uint32_t bytes)
{
 if(!buffer)return bytes ? -64 : (long)sizeof(struct dc_input_snapshot);
 if(bytes<sizeof(struct dc_input_snapshot))return -64;
 struct dc_input_snapshot *out=buffer;
 irq_mask_t irq=irq_disable();
 *out=input_snapshot;
 out->version=DC_CONTROL_VERSION;out->bytes=sizeof(*out);out->present=0;
 maple_device_t *d=maple_enum_type(0,MAPLE_FUNC_MOUSE);
 mouse_state_t *m=d ? maple_dev_status(d) : NULL;
 if(m){out->present|=DC_INPUT_MOUSE;out->mouse_port=d->port;out->mouse_buttons=m->buttons;}
 d=maple_enum_type(0,MAPLE_FUNC_KEYBOARD);
 kbd_state_t *k=d ? maple_dev_status(d) : NULL;
 if(k){out->present|=DC_INPUT_KEYBOARD;out->keyboard_port=d->port;
  out->modifiers=k->cond.modifiers.raw;
  for(int i=0;i<6;i++)out->keys[i]=k->cond.keys[i];
 }
 d=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
 cont_state_t *c=d ? maple_dev_status(d) : NULL;
 if(c){out->present|=DC_INPUT_CONTROLLER;out->controller_port=d->port;
  out->controller_buttons=c->buttons;out->joy_x=c->joyx;out->joy_y=c->joyy;
  out->trigger_left=c->ltrig;out->trigger_right=c->rtrig;
 }
 irq_restore(irq);
 return sizeof(*out);
}
