#ifdef MACHINE_DREAMCAST
#include "emutos.h"
#endif
/* Portable, conservative VMU file policy engine. GPL-2.0-or-later.
 *
 * Every operation follows the same steps:
 *   1. Validate the card with dc_vmu_inspect_ex (root/FAT/directory, chains,
 *      cycles, cross-links, bounds). Any doubt refuses the operation.
 *   2. Decide whether the name exists, using the same byte-for-byte rule as KOS
 *      vmufs (strncmp over 12 bytes), and refuse if that is ambiguous. This
 *      prevents KOS from creating a duplicate or touching a different file.
 *   3. Check protection, free blocks and directory slots.
 *   4. Keep a copy of a file that is about to be overwritten.
 *   5. Ask the backend (KOS vmufs) to perform the one file-level change.
 *   6. Read everything back: directory, FAT accounting, every other file and
 *      the new file's bytes. On failure, restore the old file if the card is
 *      still consistent, otherwise stop and report DC_VMUF_VERIFY.
 * The root block is never written here (only KOS FAT/directory updates a
 * standard driver would make). There is no format operation. */
#include "dreamcast/vmu_file.h"
#include <string.h>

static struct dc_vmu_info info, before;
static unsigned char raw[DC_VMU_FILES][12], before_raw[DC_VMU_FILES][12];
static struct dc_vmu_layout layout;
static int busy;

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }

int dc_vmuf_name(const char *name, char out[13])
{
    unsigned n=0;
    if (!name) return DC_VMUF_BAD_NAME;
    while (name[n] && n<64) n++;
    if (name[n]) return DC_VMUF_BAD_NAME;
    while (n && name[n-1]==' ') n--;
    if (!n || n>DC_VMUF_NAME_MAX || name[0]==' ') return DC_VMUF_BAD_NAME;
    for (unsigned i=0;i<n;i++)
        if ((unsigned char)name[i]<32 || (unsigned char)name[i]>126) return DC_VMUF_BAD_NAME;
    memcpy(out,name,n); out[n]=0;
    return (int)n;
}

/* An entry's identity: bytes before the first NUL, trailing spaces trimmed. */
static unsigned entry_key(const unsigned char *entry, char out[13])
{
    unsigned n=0;
    while (n<12 && entry[n]) { out[n]=(char)entry[n]; n++; }
    while (n && out[n-1]==' ') n--;
    out[n]=0;
    return n;
}

static int scan(const struct dc_vmu_backend *b, void *context)
{
    int status=dc_vmu_inspect_ex(&info,raw,&layout,b->read,context);
    if (status==DC_VMU_OK) return layout.blocks>DC_VMUF_MAX_BLOCKS ? DC_VMUF_CARD : 0;
    return status==DC_VMU_IO ? DC_VMUF_IO : DC_VMUF_CARD;
}

/* Find the entry for a normalised name. *index is -1 for "not present".
 * fn receives the exact string to hand to KOS. Returns 0 or AMBIGUOUS. */
static int locate(const char *key, int *index, char fn[13])
{
    char entry[13];
    int found=-1, count=0;
    for (unsigned i=0;i<info.file_count;i++) {
        entry_key(raw[i],entry);
        if (!strcmp(entry,key)) { count++; found=(int)i; }
    }
    if (count>1) return DC_VMUF_AMBIGUOUS;
    if (count) { memcpy(fn,raw[found],12); fn[12]=0; }
    else { strcpy(fn,key); }
    /* KOS vmufs_dir_find: strncmp(fn, filename, 12) over non-empty entries. */
    for (unsigned i=0;i<info.file_count;i++)
        if ((int)i!=found && !strncmp(fn,(const char *)raw[i],12)) return DC_VMUF_AMBIGUOUS;
    if (count && strncmp(fn,(const char *)raw[found],12)) return DC_VMUF_AMBIGUOUS;
    *index=found;
    return 0;
}

/* Follow one file's chain. out (blocks*512 bytes) receives it, and expect, if
 * given, is compared block by block. Returns 0, IO, CARD, or 1 on mismatch. */
static int walk(const struct dc_vmu_backend *b, void *context, const struct dc_vmu_file *f,
                unsigned char *out, const unsigned char *expect)
{
    unsigned char fat[512], block[512];
    int mismatch=0;
    if (b->read(context,layout.fat_loc,fat)) return DC_VMUF_IO;
    unsigned current=f->first_block;
    for (unsigned n=0;n<f->blocks;n++) {
        if (current>=layout.blocks) return DC_VMUF_CARD;
        if (b->read(context,current,block)) return DC_VMUF_IO;
        if (out) memcpy(out+512*n,block,512);
        if (expect && memcmp(expect+512*n,block,512)) mismatch=1;
        current=le16(fat+2*current);
    }
    if (current!=0xfffa) return DC_VMUF_CARD;
    return mismatch;
}

static void remember(void)
{
    memcpy(&before,&info,sizeof(info));
    memcpy(before_raw,raw,sizeof(raw));
}

/* Every file in `before` other than `skip` must still exist unchanged. */
static int others_unchanged(int skip)
{
    for (unsigned i=0;i<before.file_count;i++) {
        if ((int)i==skip) continue;
        unsigned j=0;
        for (;j<info.file_count;j++) {
            const struct dc_vmu_file *a=&before.files[i], *c=&info.files[j];
            if (!memcmp(before_raw[i],raw[j],12) && a->blocks==c->blocks && a->first_block==c->first_block &&
                a->type==c->type && a->protected_file==c->protected_file && a->header_block==c->header_block) break;
        }
        if (j==info.file_count) return 0;
    }
    return 1;
}

static int plain(const struct dc_vmu_file *f)
{
    return f->type==0x33 && !f->protected_file && !f->header_block;
}

long dc_vmuf_read(const struct dc_vmu_backend *b, void *context, const char *name, void *buffer, uint32_t bytes)
{
    char key[13], fn[13];
    int index, r;
    if (!b || !b->read || (!buffer && bytes)) return DC_VMUF_BADARG;
    if ((r=dc_vmuf_name(name,key))<0) return r;
    if (busy) return DC_VMUF_BUSY;
    busy=1;
    long result;
    if ((r=scan(b,context)) || (r=locate(key,&index,fn))) result=r;
    else if (index<0) result=DC_VMUF_NOT_FOUND;
    else if (info.files[index].protected_file) result=DC_VMUF_PROTECTED;
    else {
        const struct dc_vmu_file *f=&info.files[index];
        uint32_t size=f->blocks*512;
        if (!buffer) result=size;
        else if (bytes<size) result=DC_VMUF_BUFFER;
        else {
            r=walk(b,context,f,buffer,NULL);
            result=r ? (r==1 ? DC_VMUF_CARD : r) : (long)size;
        }
    }
    busy=0;
    return result;
}

/* After a failed or unverifiable change, find out what the card looks like
 * and put the previous file back if that is still safe. */
static long recover(const struct dc_vmu_backend *b, void *context, const char *key,
                    int existed, int old_index, unsigned old_blocks)
{
    char fn[13];
    int index;
    if (scan(b,context) || locate(key,&index,fn)) return DC_VMUF_VERIFY;
    if (!existed) {
        /* A create failed: fine only if nothing visible changed. */
        if (index<0 && info.file_count==before.file_count && info.free_blocks==before.free_blocks &&
            others_unchanged(-1)) return DC_VMUF_IO;
        return DC_VMUF_VERIFY;
    }
    /* An overwrite failed. Old file intact? */
    if (index>=0 && info.files[index].blocks==old_blocks && plain(&info.files[index]) &&
        info.file_count==before.file_count && info.free_blocks==before.free_blocks &&
        others_unchanged(old_index) && !walk(b,context,&info.files[index],NULL,b->backup))
        return DC_VMUF_IO;
    /* Restore the previous contents, but only onto a consistent card. */
    if (b->put(context,fn,b->backup,old_blocks,index>=0)) return DC_VMUF_VERIFY;
    if (scan(b,context) || locate(key,&index,fn) || index<0) return DC_VMUF_VERIFY;
    if (info.files[index].blocks!=old_blocks || !plain(&info.files[index]) ||
        info.file_count!=before.file_count || info.free_blocks!=before.free_blocks ||
        !others_unchanged(old_index) || walk(b,context,&info.files[index],NULL,b->backup))
        return DC_VMUF_VERIFY;
    return DC_VMUF_RESTORED;
}

long dc_vmuf_write(const struct dc_vmu_backend *b, void *context, const char *name, const void *data,
                   uint32_t bytes, uint32_t flags)
{
    char key[13], fn[13];
    int index, r;
    if (!b || !b->read || !b->put || !b->stage || !b->backup || !data || !bytes || (flags & ~DC_VMUF_OVERWRITE))
        return DC_VMUF_BADARG;
    if (bytes>DC_VMUF_MAX_BYTES) return DC_VMUF_TOO_BIG;
    if ((r=dc_vmuf_name(name,key))<0) return r;
    if (busy) return DC_VMUF_BUSY;
    busy=1;
    long result;
    unsigned blocks=(bytes+511)/512, old_blocks=0;
    int existed;
    if ((r=scan(b,context)) || (r=locate(key,&index,fn))) { result=r; goto done; }
    existed=index>=0;
    unsigned available=info.free_blocks;
    if (existed) {
        const struct dc_vmu_file *old=&info.files[index];
        if (!(flags & DC_VMUF_OVERWRITE)) { result=DC_VMUF_EXISTS; goto done; }
        if (!plain(old)) { result=DC_VMUF_PROTECTED; goto done; }
        available+=old->blocks; old_blocks=old->blocks;
    } else if (info.file_count>=layout.dir_entries) { result=DC_VMUF_DIR_FULL; goto done; }
    if (blocks>available) { result=DC_VMUF_FULL; goto done; }
    if (existed) {
        /* The old contents are the only rollback we have: they must be readable. */
        r=walk(b,context,&info.files[index],b->backup,NULL);
        if (r) { result=r==1 ? DC_VMUF_CARD : r; goto done; }
    }
    remember();
    int old_index=index;
    memset(b->stage,0,blocks*512);
    memcpy(b->stage,data,bytes);
    r=b->put(context,fn,b->stage,blocks,existed);
    if (r) { result=recover(b,context,key,existed,old_index,old_blocks); goto done; }
    /* Verify: card still valid, accounting exact, other files untouched, new bytes identical. */
    if (!scan(b,context) && !locate(key,&index,fn) && index>=0) {
        const struct dc_vmu_file *f=&info.files[index];
        if (f->blocks==blocks && plain(f) && info.file_count==before.file_count+(existed?0u:1u) &&
            info.free_blocks==before.free_blocks+old_blocks-blocks && others_unchanged(old_index) &&
            !walk(b,context,f,NULL,b->stage)) { result=blocks*512; goto done; }
    }
    if (existed) {
        /* Same recovery as a failed put: a verification failure may leave us able to roll back. */
        result=recover(b,context,key,1,old_index,old_blocks);
        if (result==DC_VMUF_IO) result=DC_VMUF_VERIFY; /* the write was reported OK, so IO would be a lie */
    } else result=DC_VMUF_VERIFY;
done:
    busy=0;
    return result;
}

long dc_vmuf_delete(const struct dc_vmu_backend *b, void *context, const char *name)
{
    char key[13], fn[13];
    int index, r;
    if (!b || !b->read || !b->erase) return DC_VMUF_BADARG;
    if ((r=dc_vmuf_name(name,key))<0) return r;
    if (busy) return DC_VMUF_BUSY;
    busy=1;
    long result;
    if ((r=scan(b,context)) || (r=locate(key,&index,fn))) { result=r; goto done; }
    if (index<0) { result=DC_VMUF_NOT_FOUND; goto done; }
    if (!plain(&info.files[index])) { result=DC_VMUF_PROTECTED; goto done; }
    unsigned old_blocks=info.files[index].blocks;
    int old_index=index;
    remember();
    r=b->erase(context,fn);
    if (scan(b,context) || locate(key,&index,fn)) { result=DC_VMUF_VERIFY; goto done; }
    if (r) {
        /* Unchanged is the only harmless outcome of a failed erase. */
        result=index>=0 && info.file_count==before.file_count && info.free_blocks==before.free_blocks &&
               others_unchanged(-1) ? DC_VMUF_IO : DC_VMUF_VERIFY;
        goto done;
    }
    result=index<0 && info.file_count+1==before.file_count && info.free_blocks==before.free_blocks+old_blocks &&
           others_unchanged(old_index) ? 0 : DC_VMUF_VERIFY;
done:
    busy=0;
    return result;
}
