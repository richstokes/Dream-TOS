/*
 * biosbind.h - Bindings for BIOS access
 *
 * Copyright (C) 2001-2016 The EmuTOS development team
 *
 * Authors:
 *  MAD   Martin Doering
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#ifndef BIOSBIND_H
#define BIOSBIND_H

#define Getmpb(a) bios_v_l(0x0,a)
#define Bconstat(a) bios_w_w(0x1,a)
#define Bconin(a) bios_l_w(0x2,a)
#define Bconout(a,b) bios_l_ww(0x3,a,b)
#define Rwabs(a,b,c,d,e,lrec) bios_l_wlwwwl(0x4,a,b,c,d,e,lrec)
#define Setexc(a,b) bios_l_wl(0x5,a,b)
#define Tickcal() bios_l_v(0x6)
#define Getbpb(a) bios_l_w(0x7,a)
#define Bcostat(a) bios_l_w(0x8,a)
#define Mediach(a) bios_l_w(0x9,a)
#define Drvmap() bios_l_v(0xa)
#define Kbshift(a) bios_l_w(0xb,a)




void bios_v_l(int op, long a);
void bios_v_ww(int op, short a, short b);
short bios_w_w(int op, short a);
long bios_l_v(int op);
long bios_l_w(int op, short a);
long bios_l_ww(int op, short a, short b);
long bios_l_wl(int op, short a, long b);
long bios_l_wlwwwl(int op, short a, long b, short c, short d, short e, long f);
#endif
