/*
 * xbiosbind.h - Bindings for XBIOS access
 *
 * Copyright (C) 2001-2022 The EmuTOS development team
 *
 * Authors:
 *  MAD   Martin Doering
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#ifndef XBIOSBIND_H
#define XBIOSBIND_H

#define Initmous(a,b,c) xbios_v_wll(0,a,b,c)
#define Ssbrk(a) xbios_l_w(1,a)
#define Physbase() xbios_l_v(2)
#define Logbase() xbios_l_v(3)
#define Getrez() xbios_w_v(4)
#define Setscreen(a,b,c,d) xbios_v_llww(5,a,b,c,d)
#define Setpalette(a) xbios_v_l(6,a)
#define Setcolor(a,b) xbios_w_ww(7,a,b)
#define Floprd(a,b,c,d,e,f,g) xbios_w_llwwwww(8,a,b,c,d,e,f,g)
#define Flopwr(a,b,c,d,e,f,g) xbios_w_llwwwww(9,a,b,c,d,e,f,g)
#define Flopfmt(a,b,c,d,e,f,g,h,i) xbios_w_llwwwwwlw(10,a,b,c,d,e,f,g,h,i)
#define Midiws(a,b) xbios_v_wl(12,a,b)
#define Mfpint(a,b) xbios_v_wl(13,a,b)
#define Iorec(a) xbios_l_w(14,a)
#define Rsconf(a,b,c,d,e,f) xbios_v_wwwwww(15,a,b,c,d,e,f)
#define Keytbl(a,b,c) xbios_l_lll(16,a,b,c)
#define Random() xbios_l_v(17)
#define Protobt(a,b,c,d) xbios_v_llww(18,a,b,c,d)
#define Flopver(a,b,c,d,e,f,g) xbios_w_llwwwww(19,a,b,c,d,e,f,g)
#define Scrdmp() xbios_v_v(20)
#define Cursconf(a,b) xbios_w_ww(21,a,b)
#define Settime(a) xbios_v_l(22,a)
#define Gettime() xbios_l_v(23)
#define Bioskeys() xbios_v_v(24)
#define Ikbdws(a,b) xbios_v_wl(25,a,b)
#define Jdisint(a) xbios_v_w(26,a)
#define Jenabint(a) xbios_v_w(27,a)
#define Giaccess(a,b) xbios_w_ww(28,a,b)
#define Offgibit(a) xbios_v_w(29,a)
#define Ongibit(a) xbios_v_w(30,a)
#define Xbtimer(a,b,c,d) xbios_v_wwwl(31,a,b,c,d)
#define Dosound(a) xbios_v_l(32,a)
#define Setprt(a) xbios_w_w(33,a)
#define Kbdvbase() xbios_l_v(34)
#define Kbrate(a,b) xbios_w_ww(35,a,b)
#define Prtblk() xbios_v_v(36)
#define Vsync() xbios_v_v(37)
#define Supexec(a) xbios_l_l(38,a)
#define Puntaes() xbios_v_v(39)
#define Blitmode(a) xbios_w_w(64, a)
#define EgetShift() xbios_w_v(81)
#define EsetColor(a,b) xbios_w_ww(83,a,b)
#define VsetMode(a) xbios_w_w(88,a)
#define VgetMonitor() xbios_w_v(89)
#define VgetSize(a) xbios_l_w(91,a)
#define VsetRGB(a,b,c) xbios_v_wwl(93,a,b,c)
#define VgetRGB(a,b,c) xbios_v_wwl(94,a,b,c)

#define FLOPFMT_MAGIC   0x87654321UL


/*
 *
 */


void xbios_v_v(int op);
void xbios_v_w(int op, short a);
void xbios_v_wl(int op, short a, long b);
void xbios_v_wll(int op, short a, long b, long c);
void xbios_v_wwl(int op, short a, short b, long c);
void xbios_v_wwwl(int op, short a, short b, short c, long d);
void xbios_v_wwwwww(int op, short a, short b, short c, short d, short e, short f);
void xbios_v_l(int op, long a);
void xbios_v_llw(int op, long a, long b, short c);
void xbios_v_llww(int op, long a, long b, short c, short d);
short xbios_w_v(int op);
short xbios_w_w(int op, short a);
short xbios_w_ww(int op, short a, short b);
short xbios_w_llwwwww(int op,
    long a, long b, short c, short d, short e, short f, short g);
short xbios_w_llwwwwwlw(int op,
    long a, long b, short c, short d, short e, short f, short g, long h, short i);
long xbios_l_v(int op);
long xbios_l_w(int op, short a);
long xbios_l_l(int op, long a);
long xbios_l_lll(int op, long a, long b, long c);
#endif
