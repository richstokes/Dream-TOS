/* Native boot, AES co-operative contexts, and replacement CPU primitives.
 * GPL-2.0-or-later. No Motorola instruction execution. */
#include "emutos.h"
#include "string.h"
#include "asm.h"
#include "setjmp.h"
#include "struct.h"
#include "aesvars.h"
#include "gemasm.h"
#include "gemdosif.h"
#include "geminput.h"
#include "gemdisp.h"
#include "gemflag.h"
#include "geminit.h"
#include "lineavars.h"
#include "vdi_defs.h"
#include "tosvars.h"
#include "biosext.h"
#include "dreamcast/hal.h"
int vprintf(const char *, __builtin_va_list);
#include <stdarg.h>

extern void disp(void),gem_main(void),run_accs_and_desktop(void),font_init(void),linea_init(void);
extern void b_click(WORD),b_delay(WORD);
extern WORD deskmain(void);
extern void dc_storage_init(void),dc_storage_selftest(void),dc_poll(void);
extern char *ad_envrn,*ad_stail;
static char empty_env[2],tail[128];
static struct { jmp_buf env; PFVOID entry; int saved; } contexts[NUM_PDS];
static int current;
void just_rts(void) {}
void stop_until_interrupt(void) { dc_poll(); dc_sleep(1); }
void bzero_nobuiltin(void *p,size_t n) { memset(p,0,n); }
short strlencpy(char *d,const char *s) { char *p=d; while((*d++=*s++)); return d-p-1; }
WORD mul_div_round(WORD a,WORD b,WORD c) { LONG v=(LONG)a*b; return c ? (v+(v<0?-(c/2):c/2))/c : 0; }
LONG protect_v(LONG (*f)(void)) { return f(); }
LONG protect_w(LONG (*f)(WORD),WORD a) { return f(a); }
LONG protect_ww(LONG (*f)(void),WORD a,WORD b) { return ((LONG (*)(WORD,WORD))f)(a,b); }
LONG protect_wlwwwl(LONG (*f)(void),WORD a,LONG b,WORD c,WORD d,WORD e,LONG g) { return ((LONG(*)(WORD,LONG,WORD,WORD,WORD,LONG))f)(a,b,c,d,e,g); }
#define PRINT_FN(name) int name(const char *f,...) { va_list a;va_start(a,f);int n=vprintf(f,a);va_end(a);return n; }
PRINT_FN(kprintf)
PRINT_FN(kcprintf)
PRINT_FN(cprintf)
void panic(const char *f,...) { va_list a;va_start(a,f);vprintf(f,a);va_end(a);for(;;)dc_sleep(100); }
void halt(void) { for(;;)dc_sleep(100); }
/* Events are delivered on this cooperative thread only, never in a KOS IRQ. */
void disable_interrupts(void) {}
void enable_interrupts(void) {}
void takeerr(void) {}
void giveerr(void) {}
void retake(void) {}
void set_aestrap(void) {}
void unset_aestrap(void) {}
void psetup(AESPD *p,PFVOID entry) { if(dc_context_create(p->p_pid,entry))panic("Cannot create AES context\n"); }
void dsptch(void) {
 if(indisp) return;
 dc_poll();
 current=rlr->p_pid;
 if(!setjmp(contexts[current].env)) {contexts[current].saved=1;indisp=1;disp();}
}
void switchto(UDA *u) {
 (void)u; int old=current,next=rlr->p_pid;indisp=0;
 dc_context_switch(old,next);
 longjmp(contexts[old].env,1);
}

void gotopgm(void) { switchto(rlr->p_uda); }
LONG NUM_TICK,CMP_TICK;
PFVOID drwaddr;
void *tiksav;
void tikcod(void) { if(CMP_TICK) {NUM_TICK++; if(!--CMP_TICK) if(forkq(tchange,NUM_TICK)<0) CMP_TICK++;} b_delay(1); }
void far_bcha(void) { b_click(MOUSE_BT); }
void far_mcha(void) { forkq(mchange,MAKE_ULONG(GCURX,GCURY)); }
void drawrat(WORD x,WORD y) { newx=x;newy=y;draw_flag=1; }
void deskstart(void) { while(!deskmain()); }
void accdesk_start(void) { run_accs_and_desktop(); }
LONG dos_exec(WORD mode,const char *path,const char *tail,const char *env) {
 extern long trap1_pexec(short,const char *,const char *,const char *);
 return trap1_pexec(mode,path,tail,env);
}
void dc_core_main(void) {
 kprintf("EmuTOS: native CPU ABI, upstream AES/VDI/GEMDOS\n");
 dc_storage_init(); dc_storage_selftest();
 extern void dc_bundle_selftest(void);dc_bundle_selftest();
 font_init();linea_init();extern void vt52_init(void);vt52_init();dc_context_init();
 ad_envrn=empty_env;ad_stail=tail;
 kprintf("EmuTOS: entering GEM desktop\n");
 gem_main();halt();
}
