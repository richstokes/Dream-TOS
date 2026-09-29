/* KOS boundary: scalar types only, independent of GEM structure packing. */
#pragma once
#include <stddef.h>
void dc_hal_init(void);
void dc_boot_disc(void) __attribute__((noreturn));
unsigned long dc_millis(void);
void dc_sleep(unsigned int ms);
void dc_present(const unsigned short *planes, const unsigned short *palette);
int dc_poll_mouse(int *dx,int *dy,int *buttons);
unsigned long dc_poll_key(void);
int dc_key_modifiers(void);
unsigned long dc_datetime(void);
void *dc_alloc(size_t bytes);
void dc_free(void *p);
long dc_available(void);
long dc_disc_read(void *buf,unsigned long offset,unsigned long bytes);
void dc_core_main(void);
void dc_context_init(void);
int dc_context_create(int id, void (*entry)(void));
void dc_context_switch(int old_id,int new_id);

void dc_sync_code(void *p,unsigned long bytes);
