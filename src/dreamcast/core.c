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
int vsnprintf(char *,size_t,const char *,va_list);

extern void disp(void),gem_main(void),run_accs_and_desktop(void),font_init(void),linea_init(void);
extern void b_click(WORD),b_delay(WORD);
extern WORD deskmain(void);
extern void dc_storage_init(void),dc_storage_selftest(void),dc_poll(void);
extern char *ad_envrn,*ad_stail;
static char shell_env[] = "PATH=D:\\APPS;D:\\GAMES;D:\\UTILS;D:\\;C:\\\0",tail[128];
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
void panic(const char *f,...) {
 char message[256];va_list a;
 va_start(a,f);vsnprintf(message,sizeof(message),f,a);va_end(a);
 dc_boot_failure(message);
 va_start(a,f);vprintf(f,a);va_end(a);
 for(;;)dc_sleep(100);
}
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
/* Maple motion arrives in coarse 60 Hz steps. When a move ends the double-click
 * wait, queue the click ahead of that move so the AES acts on it where the
 * button went down; otherwise a quick drag can step right off a small gadget. */
void far_mcha(void) {
 static WORD last_x,last_y;
 WORD dx=GCURX-last_x,dy=GCURY-last_y;
 if(gl_bdely && (dx>2||dx<-2||dy>2||dy<-2)) b_delay(gl_bdely);
 last_x=GCURX;last_y=GCURY;
 forkq(mchange,MAKE_ULONG(GCURX,GCURY));
}
void drawrat(WORD x,WORD y) { newx=x;newy=y;draw_flag=1; }
void deskstart(void) { while(!deskmain()); }
void accdesk_start(void) { run_accs_and_desktop(); }
LONG dos_exec(WORD mode,const char *path,const char *tail,const char *env) {
 extern long trap1_pexec(short,const char *,const char *,const char *);
 return trap1_pexec(mode,path,tail,env);
}
void dc_core_main(void) {
 kprintf("Dream TOS: native CPU ABI, upstream AES/VDI/GEMDOS\n");
 dc_storage_init();
 dc_boot_status("06 Checking RAM disk and native loader");
 dc_storage_selftest();
 dc_boot_status("07 Checking bundled applications");
 extern void dc_bundle_selftest(void);dc_bundle_selftest();
 dc_boot_status("08 Initializing GEM graphics");
 font_init();linea_init();extern void vt52_init(void);vt52_init();dc_context_init();
 ad_envrn=shell_env;ad_stail=tail;
 dc_boot_status("09 Checking command-line runtime");
 extern void dc_cli_selftest(void);dc_cli_selftest();
 dc_boot_status("10 Starting GEM desktop");
 kprintf("Dream TOS: entering GEM desktop\n");
 gem_main();halt();
}
