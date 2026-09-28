/* GEMDOS allocation arena with in-place shrink and process ownership.
 * KOS allocations remain separate. GPL-2.0-or-later. */
#include "emutos.h"
#include "fs.h"
#include "proc.h"
#include "bdosstub.h"
#include "mem.h"
#include "gemerror.h"
#include "dreamcast/hal.h"
#include "dreamcast/system_info.h"
#define ARENA_BYTES (3UL * 1024 * 1024)
#define ALIGN 32UL
typedef struct block {
    struct block *next;
    PD *owner;
    ULONG bytes;
    ULONG reserved[5];
} Block;
static Block *arena;
static void merge(void)
{
    for (Block *b = arena; b && b->next;) {
        if (!b->owner && !b->next->owner) {
            b->bytes += sizeof(Block) + b->next->bytes;
            b->next = b->next->next;
        } else
            b = b->next;
    }
}
static void split(Block *b, ULONG n)
{
    if (b->bytes >= n + sizeof(Block) + ALIGN) {
        Block *q = (Block *)((UBYTE *)(b + 1) + n);
        q->bytes = b->bytes - n - sizeof(Block);
        q->owner = NULL;
        q->next = b->next;
        b->next = q;
        b->bytes = n;
    }
}
void *xmalloc(long n)
{
    if (!arena) {
        arena = dc_alloc(ARENA_BYTES);
        if (!arena)
            return NULL;
        arena->bytes = ARENA_BYTES - sizeof(Block);
    }
    if (n == -1) {
        ULONG best = 0;
        for (Block *b = arena; b; b = b->next)
            if (!b->owner && b->bytes > best)
                best = b->bytes;
        return (void *)best;
    }
    if (n <= 0 || n > ARENA_BYTES - sizeof(Block))
        return NULL;
    ULONG size = ((ULONG)n + ALIGN - 1) & ~(ALIGN - 1);
    for (Block *b = arena; b; b = b->next)
        if (!b->owner && b->bytes >= size) {
            split(b, size);
            b->owner = run;
            return b + 1;
        }
    return NULL;
}
void *xmxalloc(long n, int mode)
{
    /* Like EmuTOS, ignore FreeMiNT protection flags (e.g. VDI's MX_SUPER).
     * All four RAM preferences use the single native allocation arena. */
    (void)mode;
    return xmalloc(n);
}
long xmfree(void *p)
{
    for (Block *b = arena; b; b = b->next)
        if (b + 1 == p && b->owner) {
            b->owner = NULL;
            merge();
            return 0;
        }
    return EIMBA;
}
long xsetblk(int ignored, void *p, long n)
{
    (void)ignored;
    if (n <= 0)
        return EGSBF;
    for (Block *b = arena; b; b = b->next)
        if (b + 1 == p && b->owner) {
            ULONG size = ((ULONG)n + ALIGN - 1) & ~(ALIGN - 1);
            if (size > b->bytes)
                return EGSBF;
            split(b, size);
            merge();
            return 0;
        }
    return EIMBA;
}
void dc_free_process_memory(PD *owner)
{
    for (Block *b = arena; b; b = b->next)
        if (b->owner == owner)
            b->owner = NULL;
    merge();
}

void dc_memory_system_info(struct dc_system_info *info)
{
    info->gem_pool_bytes = ARENA_BYTES;
    if (!arena) {
        info->gem_free_bytes = info->gem_largest_bytes = ARENA_BYTES - sizeof(Block);
        return;
    }
    for (Block *b = arena; b; b = b->next) {
        if (!b->owner) {
            info->gem_free_bytes += b->bytes;
            if (b->bytes > info->gem_largest_bytes)
                info->gem_largest_bytes = b->bytes;
        }
    }
}
