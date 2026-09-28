/*
 * deskstub.h - EmuDesk entry point, called by AES
 *
 * Copyright (C) 2019 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef _DESKSTUB_H
#define _DESKSTUB_H

#ifdef MACHINE_DREAMCAST
/* The native desktop returns to the AES shell to launch an application. */
void deskstart(void);
#else
void deskstart(void) NORETURN;   /* see deskstart.S */
#endif

#endif /* _DESKSTUB_H */
