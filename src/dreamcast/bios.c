/* Native BIOS/XBIOS and Maple event delivery. GPL-2.0-or-later. */
#include "emutos.h"
#include "string.h"
#include "asm.h"
#include "biosbind.h"
#include "xbiosbind.h"
#include "lineavars.h"
#include "vdi_defs.h"
#include "tosvars.h"
#include "biosext.h"
#include "dreamcast/hal.h"
#include "dreamcast/control.h"
#include "gemerror.h"
#include "vt52.h"

static void null_timer(int elapsed)
{
    (void)elapsed;
}
ETV_TIMER_T etv_timer = null_timer;
volatile LONG hz_200, frclock;
WORD timer_ms = 20;
static PFVOID vbl[8];
PFVOID *vblqueue = vbl;
WORD nvbls = 8;
UBYTE conterm = 3, sshiftmod;
UBYTE *phystop = (UBYTE *)0x8d000000, *membot, *memtop = (UBYTE *)0x8cf00000;
static OSHEADER os_header = {
    .os_version = 0x206, .os_conf = 0, .os_date = 0x09282026, .os_dosdate = 0x5d3c};
const OSHEADER *sysbase = &os_header;
extern LONG dc_rwabs(WORD, void *, WORD, LONG, WORD);
extern BPB *dc_getbpb(WORD);
extern void dc_graphics_present(void);
extern WORD dc_setcolor(WORD, WORD);
extern PFVOID user_but, user_mot, user_cur;
static ULONG key;
void mov_cur(void)
{
    newx = GCURX;
    newy = GCURY;
    draw_flag = 1;
}
void mouse_int(void) {}
void dc_poll(void)
{
    static int busy;
    static ULONG tick, blank;
    if (busy)
        return;
    busy = 1;
    ULONG now = dc_millis();
    hz_200 = now / 5;
    if (!tick)
        tick = now;
    while (now - tick >= 20) {
        tick += 20;
        if (etv_timer)
            etv_timer(20);
    }
    int dx, dy, buttons;
    if (dc_poll_mouse(&dx, &dy, &buttons)) {
        int x = GCURX + dx, y = GCURY + dy;
        if (x < 0)
            x = 0;
        if (x > 639)
            x = 639;
        if (y < 0)
            y = 0;
        if (y > 479)
            y = 479;
        if (x != GCURX || y != GCURY) {
            GCURX = x;
            GCURY = y;
            cur_ms_stat |= 0x20;
            if (user_mot)
                user_mot();
            if (user_cur)
                user_cur();
        }
        if (buttons != MOUSE_BT) {
            cur_ms_stat |= (buttons ^ MOUSE_BT) << 6;
            MOUSE_BT = buttons;
            cur_ms_stat = (cur_ms_stat & ~3) | buttons;
            if (user_but)
                user_but();
        }
    }
    if (!key)
        key = dc_poll_key();
    if (now - blank >= 16) {
        blank = now;
        frclock++;
        extern void blink(void);
        blink();
        if (vbl[0])
            vbl[0]();
        dc_graphics_present();
    }
    busy = 0;
}
LONG dc_console_status(void)
{
    dc_poll();
    return key ? -1 : 0;
}
LONG dc_console_in(void)
{
    while (!dc_console_status())
        dc_sleep(1);
    ULONG k = key;
    key = 0;
    return k;
}
int dc_console_break(void)
{
    dc_poll();
    if ((key & 0xff) != 3)
        return 0;
    key = 0;
    return 1;
}
void (*con_state)(WORD);
WORD save_row;
void bell(void) {} /* no audio backend yet */
void dc_console_out(WORD ch)
{
    cputc(ch);
}
short bios_w_w(int op, short a)
{
    (void)a;
    return op == 1 ? dc_console_status() : -1;
}
long bios_l_v(int op)
{
    return op == 6 ? 20 : op == 10 ? drvbits : 0;
}
long bios_l_w(int op, short a)
{
    switch (op) {
    case 1:
        return dc_console_status();
    case 2:
        return dc_console_in();
    case 7:
        return (long)dc_getbpb(a);
    case 8:
        return -1;
    case 9:
        return 0;
    case 11:
        dc_poll();
        return dc_key_modifiers();
    default:
        return EINVFN;
    }
}
long bios_l_ww(int op, short a, short b)
{
    if (op == 3 && a == 2) {
        dc_console_out(b);
        return 0;
    }
    return EINVFN;
}
long bios_l_wl(int op, short a, long b)
{
    if (op == 5 && a == 0x100) {
        long old = (long)etv_timer;
        if (b != -1)
            etv_timer = (ETV_TIMER_T)b;
        return old;
    }
    return 0;
}
long bios_l_wlwwwl(int op, short a, long b, short c, short d, short e, long f)
{
    (void)op;
    return dc_rwabs(a, (void *)b, c, d == -1 ? f : d, e);
}
void xbios_v_v(int op)
{
    if (op == 37) {
        dc_sleep(16);
        dc_poll();
    }
}
void xbios_v_wll(int op, short a, long b, long c)
{
    (void)op;
    (void)a;
    (void)b;
    (void)c;
}
void xbios_v_wl(int op, short a, long b)
{
    (void)op;
    (void)a;
    (void)b;
}
void xbios_v_l(int op, long a)
{
    (void)op;
    (void)a;
}
void xbios_v_llww(int op, long a, long b, short c, short d)
{
    (void)op;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
short xbios_w_v(int op)
{
    return op == 4 ? 0 : op == 89 ? MON_VGA : 0;
}
short xbios_w_w(int op, short a)
{
    (void)op;
    (void)a;
    return 0;
}
short xbios_w_ww(int op, short a, short b)
{
    if (op == 7)
        return dc_setcolor(a, b);
    if (op == 21)
        return cursconf(a, b);
    if (op == 35) {
        struct dc_input_config config;
        dc_input_config(0, &config, sizeof(config));
        WORD old = (config.repeat_delay_ms / 20 << 8) | (config.repeat_interval_ms / 20);
        if (a >= 0) config.repeat_delay_ms = a < 5 ? 100 : a > 50 ? 1000 : a * 20;
        if (b >= 0) config.repeat_interval_ms = b < 1 ? 20 : b > 10 ? 200 : b * 20;
        if (a >= 0 || b >= 0) dc_input_config(1, &config, sizeof(config));
        return old;
    }
    return 0;
}
long xbios_l_v(int op)
{
    static struct kbdvecs kb;
    switch (op) {
    case 2:
    case 3:
        return (long)v_bas_ad;
    case 23:
        return dc_datetime();
    case 34:
        return (long)&kb;
    default:
        return EINVFN;
    }
}
long xbios_l_l(int op, long a)
{
    return op == 38 ? ((long (*)(void))a)() : EINVFN;
}
void set_cache(WORD on)
{
    (void)on;
}
WORD get_cache(void)
{
    return 1;
}
WORD cache_exists(void)
{
    return 1;
}
void flush_data_cache(void *p, long n)
{
    (void)p;
    (void)n;
}
void invalidate_instruction_cache(void *p, long n)
{
    (void)p;
    (void)n;
}
BOOL get_cookie(ULONG cookie, ULONG *value)
{
    (void)cookie;
    (void)value;
    return FALSE;
}
WORD get_shift_mod(void)
{
    return dc_key_modifiers();
}
void get_date_fmt(UWORD *date, UWORD *time, UBYTE *sep)
{
    *date = 0;
    *time = 0;
    *sep = '/';
}
const char *scasb(const char *p, char c)
{
    while (*p && *p != c)
        p++;
    return p;
}
WORD expand_string(WORD *d, const char *s)
{
    WORD n = 0;
    while ((*d++ = (UBYTE)*s++))
        n++;
    return n;
}
int rez_changeable(void)
{
    return 0;
}
const char version[] = "1.4.0 Dreamcast native";
WORD enable_ceh;
ULONG get_idt_cookie(void)
{
    return 0x112f;
}
SBYTE get_default_handle(int n)
{
    return n == 2 ? -2 : n == 3 ? -3 : -1;
}
long xbios_l_lll(int op, long a, long b, long c)
{
    static UBYTE normal[128], shift[128], caps[128];
    static struct {
        UBYTE *normal, *shift, *caps;
    } table = {normal, shift, caps};
    (void)op;
    (void)a;
    (void)b;
    (void)c;
    static const UBYTE scans[] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                  0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                  0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    for (int i = 0; i < 26; i++) {
        normal[scans[i]] = 'a' + i;
        shift[scans[i]] = caps[scans[i]] = 'A' + i;
    }
    return (long)&table;
}
