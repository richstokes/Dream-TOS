/* Native equivalents of the Motorola compiler primitives. GPL-2.0-or-later. */
#pragma once
void just_rts(void);
void stop_until_interrupt(void);
WORD mul_div_round(WORD, WORD, WORD);
LONG protect_v(LONG (*f)(void));
LONG protect_w(LONG (*f)(WORD), WORD);
LONG protect_ww(LONG (*f)(void), WORD, WORD);
LONG protect_wlwwwl(LONG (*f)(void), WORD, LONG, WORD, WORD, WORD, LONG);
#define swpw(a) ((a) = __builtin_bswap16(a))
#define swpl(a) ((a) = __builtin_bswap32(a))
#define swpw2(a) ((a) = (((ULONG)(a)&0x00ff00ff)<<8)|(((ULONG)(a)&0xff00ff00)>>8))
static inline void swpcopyw(const UWORD *s,UWORD *d) { *d=__builtin_bswap16(*s); }
#define rolw1(a) ((a)=(UWORD)(((UWORD)(a)<<1)|((UWORD)(a)>>15)))
#define rorw1(a) ((a)=(UWORD)(((UWORD)(a)>>1)|((UWORD)(a)<<15)))
#define rorw(a,n) ((a)=(UWORD)(((UWORD)(a)>>(n))|((UWORD)(a)<<(16-(n)))))
#define rolw(a,n) ((a)=(UWORD)(((UWORD)(a)<<(n))|((UWORD)(a)>>(16-(n)))))
#define get_sr() 0
/* GEM event handlers run cooperatively, never in a hardware IRQ. */
static inline UWORD set_sr(UWORD value) { (void)value;return 0; }
#define regsafe_call(a) ((PFVOID)(a))()
#define delay_loop(n) do { volatile ULONG i=(n); while(i--) {} } while(0)
