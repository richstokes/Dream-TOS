/* Native integer operations. GPL-2.0-or-later. */
#pragma once
ULONG Isqrt(ULONG x);
#define min(a,b) ({ __typeof__(a) aa=(a); __typeof__(b) bb=(b); aa<bb?aa:bb; })
#define max(a,b) ({ __typeof__(a) aa=(a); __typeof__(b) bb=(b); aa>bb?aa:bb; })
static inline WORD mul_div(WORD a,WORD b,WORD c) { return ((LONG)a*b)/c; }
static inline UWORD umul_shift(UWORD a,UWORD b) { return ((ULONG)a*b+32768)>>16; }
static inline LONG muls(WORD a,WORD b) { return (LONG)a*b; }
static inline UWORD divu(ULONG a,UWORD b) { return a/b; }
