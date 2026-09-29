/* Test double for the two KOS vmufs entry points the file service uses. It
 * follows KOS's vmufs.c step for step (top-down data allocation, strncmp name
 * match, delete-then-allocate overwrite, FAT before directory) so the engine is
 * exercised against KOS behaviour even where the SDK sources are unavailable.
 * tests/test_vmu_write.py additionally links KOS's real vmufs.c when found. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#define VMUFS_OVERWRITE 1
#define VMUFS_VMUGAME 2
#define VMUFS_NOCOPY 4
typedef struct {
    uint8_t filetype, copyprotect;
    uint16_t firstblk;
    char filename[12];
    uint8_t stamp[8];
    uint16_t filesize, hdroff;
    uint8_t dirty, pad1[3];
} dir_t;
typedef struct { uint8_t bytes[512]; } block_t;
struct card { uint16_t fat_loc, dir_loc, dir_size, blk_cnt; uint16_t fat[256]; dir_t dir[256]; };
static const uint8_t fixed_stamp[8] = {0x20, 0x26, 0x09, 0x29, 0x12, 0x00, 0x00, 0x01};
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static int load(maple_device_t *dev, struct card *c)
{
    uint8_t root[512];
    if (vmu_block_read(dev, 255, root)) return -1;
    c->fat_loc = le16(root + 70); c->dir_loc = le16(root + 74);
    c->dir_size = le16(root + 76); c->blk_cnt = le16(root + 80);
    if (le16(root + 72) > 1 || c->dir_size > 16) return -1;
    if (vmu_block_read(dev, c->fat_loc, (uint8_t *)c->fat)) return -1;
    memset(c->dir, 0, sizeof(c->dir));
    for (unsigned b = 0; b < c->dir_size; b++)
        if (vmu_block_read(dev, c->dir_loc - b, (uint8_t *)c->dir + 512 * b)) return -1;
    return 0;
}
static int find(struct card *c, const char *fn)
{
    for (unsigned i = 0; i < c->dir_size * 16u; i++)
        if (c->dir[i].filetype && !strncmp(fn, c->dir[i].filename, 12)) return (int)i;
    return -1;
}
static int find_block(struct card *c, int type)
{
    if (type == 0x33) { for (int i = c->blk_cnt - 1; i >= 0; i--) if (c->fat[i] == 0xfffc) return i; }
    else for (int i = 0; i < c->blk_cnt; i++) if (c->fat[i] == 0xfffc) return i;
    return -2;
}
static int delete_entry(struct card *c, int idx)
{
    int blk = c->dir[idx].firstblk;
    while (blk != 0xfffa) {
        if (blk == 0xfffc || blk > c->blk_cnt) return -2;
        int next = c->fat[blk];
        c->fat[blk] = 0xfffc;
        blk = next;
    }
    memset(&c->dir[idx], 0, sizeof(dir_t));
    c->dir[idx].dirty = 1;
    return 0;
}
static int flush(maple_device_t *dev, struct card *c, int dir_first)
{
    /* KOS vmufs_write writes the FAT, then the directory; vmufs_delete the reverse. */
    for (int pass = 0; pass < 2; pass++) {
        if ((pass == 0) == !dir_first) {
            if (vmu_block_write(dev, c->fat_loc, (const uint8_t *)c->fat)) return -1;
        } else {
            for (unsigned b = 0; b < c->dir_size; b++) {
                dir_t *d = c->dir + 16 * b;
                int dirty = 0;
                for (int i = 0; i < 16; i++) { if (d[i].dirty) dirty = 1; d[i].dirty = 0; }
                if (dirty && vmu_block_write(dev, c->dir_loc - b, (const uint8_t *)d)) return -1;
            }
        }
    }
    return 0;
}
int vmufs_write(maple_device_t *dev, const char *fn, void *inbuf, int insize, int flags)
{
    static struct card c;
    insize = (insize + 511) & ~511;
    if (!insize) insize = 512;
    if (load(dev, &c)) return -1;
    int idx = find(&c, fn);
    if (idx >= 0) {
        if (!(flags & VMUFS_OVERWRITE)) return -2;
        if (delete_entry(&c, idx) < 0) return -3;
    }
    dir_t nd;
    memset(&nd, 0, sizeof(nd));
    nd.filetype = (flags & VMUFS_VMUGAME) ? 0xcc : 0x33;
    nd.copyprotect = (flags & VMUFS_NOCOPY) ? 0xff : 0;
    size_t len = strlen(fn) > 12 ? 12 : strlen(fn);
    memcpy(nd.filename, fn, len);
    memcpy(nd.stamp, fixed_stamp, 8);
    nd.filesize = insize / 512;
    nd.hdroff = (flags & VMUFS_VMUGAME) ? 1 : 0;
    nd.dirty = 1;
    int blocks = insize / 512, free_blocks = 0;
    for (int i = 0; i < c.blk_cnt; i++) if (c.fat[i] == 0xfffc) free_blocks++;
    if (find(&c, nd.filename) >= 0) return -4;
    if (free_blocks < blocks) return -7;
    int cur = nd.firstblk = find_block(&c, nd.filetype);
    if (cur < 0) return -4;
    uint8_t *out = inbuf;
    for (int left = blocks; left > 0;) {
        if (vmu_block_write(dev, cur, out)) return -4;
        left--; out += 512;
        if (left) {
            c.fat[cur] = 0xfffa;
            int next = find_block(&c, nd.filetype);
            if (next < 0) return -4;
            c.fat[cur] = next; cur = next;
        } else c.fat[cur] = 0xfffa;
    }
    int slot = -1;
    for (unsigned i = 0; i < c.dir_size * 16u; i++) if (!c.dir[i].filetype) { slot = i; break; }
    if (slot < 0) return -4;
    c.dir[slot] = nd;
    if (flush(&dev[0], &c, 0)) return -5;
    return 0;
}
int vmufs_delete(maple_device_t *dev, const char *fn)
{
    static struct card c;
    if (load(dev, &c)) return -2;
    int idx = find(&c, fn);
    if (idx < 0) return -1;
    if (delete_entry(&c, idx) < 0) return -1;
    return flush(dev, &c, 1) ? -2 : 0;
}
