#ifdef MACHINE_DREAMCAST
#include "emutos.h"
#endif
/* Bounded VMU metadata inspection, with no write callbacks. GPL-2.0-or-later. */
#include "dreamcast/vmu_info.h"
#include <string.h>
#include <stdio.h>
static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }
static int bcd(unsigned n) { return (n&15) < 10 && (n>>4) < 10 ? (n>>4)*10+(n&15) : -1; }
static void timestamp(char *out, const unsigned char *p)
{
    int c=bcd(p[0]), y=bcd(p[1]), m=bcd(p[2]), d=bcd(p[3]), h=bcd(p[4]), n=bcd(p[5]);
    if (c<0 || y<0 || m<1 || m>12 || d<1 || d>31 || h<0 || h>23 || n<0 || n>59)
        strcpy(out,"Unknown");
    else snprintf(out,20,"%04d-%02d-%02d %02d:%02d",c*100+y,m,d,h,n);
}
int dc_vmu_inspect(struct dc_vmu_info *out, dc_vmu_reader read, void *context)
{
    unsigned char root[512], fat[512], directory[512], used[256] = {0};
    out->file_count = out->total_blocks = out->free_blocks = 0;
    if (read(context,255,root)) return DC_VMU_IO;
    for (int i=0;i<16;i++) if (root[i]!=0x55) return DC_VMU_UNFORMATTED;
    unsigned fat_loc=le16(root+70), fat_size=le16(root+72);
    unsigned dir_loc=le16(root+74), dir_size=le16(root+76), blocks=le16(root+80);
    if (fat_size!=1 || !dir_size || dir_size>16) return DC_VMU_UNSUPPORTED;
    if (fat_loc>=255 || dir_loc>=255 || dir_loc+1<dir_size || !blocks ||
        blocks>256 || blocks>fat_loc || blocks>dir_loc+1-dir_size ||
        (fat_loc<=dir_loc && fat_loc>=dir_loc+1-dir_size)) return DC_VMU_CORRUPT;
    if (read(context,fat_loc,fat)) return DC_VMU_IO;
    out->total_blocks=blocks;
    for (unsigned i=0;i<blocks;i++) if (le16(fat+2*i)==0xfffc) out->free_blocks++;
    for (unsigned block=0;block<dir_size;block++) {
        if (read(context,dir_loc-block,directory)) return DC_VMU_IO;
        for (int i=0;i<16;i++) {
            const unsigned char *entry=directory+32*i;
            if (!entry[0]) continue;
            if (entry[0]!=0x33 && entry[0]!=0xcc) return DC_VMU_CORRUPT;
            unsigned first=le16(entry+2), count=le16(entry+24), header=le16(entry+26);
            if (!count || count>blocks || first>=blocks || header>=count) return DC_VMU_CORRUPT;
            unsigned current=first;
            for (unsigned n=0;n<count;n++) {
                if (current>=blocks || used[current]) return DC_VMU_CORRUPT;
                used[current]=1;
                current=le16(fat+2*current);
            }
            if (current!=0xfffa || out->file_count>=DC_VMU_FILES) return DC_VMU_CORRUPT;
            struct dc_vmu_file *file=&out->files[out->file_count++];
            memset(file,0,sizeof(*file));
            for (int j=0;j<12;j++) {
                unsigned ch=entry[4+j];
                file->name[j]=(ch>=32 && ch<127) ? ch : ch ? '?' : 0;
            }
            for (int j=11;j>=0 && file->name[j]==' ';j--) file->name[j]=0;
            timestamp(file->modified,entry+16);
            file->blocks=count; file->type=entry[0]; file->protected_file=entry[1]!=0;
            file->first_block=first; file->header_block=header;
        }
    }
    return DC_VMU_OK;
}
