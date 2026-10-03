/* Native GEMDOS ABI and block devices. Retains upstream FAT/filesystem code.
 * C: 4 MiB volatile FAT16. D: read-only FAT volume in /cd/DISC.IMG.
 * E:-H: writable FAT12/FAT16 volumes of an SD card on the serial port.
 * GPL-2.0-or-later. */
#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "mem.h"
#include "bdosstub.h"
#include "gemerror.h"
#include "tosvars.h"
#include "ahdi.h"
#include "biosbind.h"
#include "dreamcast/hal.h"
#include "dreamcast/system_info.h"
#include "dreamcast/sd.h"
#include "dreamcast/sd_format.h"
#include <stdarg.h>

#define SECTORS 8192UL
#define FATSECS 32
#define ROOTSECS 8
#define DATASEC (1 + 2 * FATSECS + ROOTSECS)
static UBYTE *ramdisk;
static BPB ram_bpb = {512, 1, 512, ROOTSECS, FATSECS, 1 + FATSECS, DATASEC, SECTORS - DATASEC,
                      B_16};
static BPB disc_bpb;
#define SD_FIRST_DRIVE 4
static struct dc_sd_volume sd_vol[DC_SD_MAX_VOLUMES];
static BPB sd_bpb[DC_SD_MAX_VOLUMES];
static int sd_count;
static uint32_t sd_sectors;
#define SD_DRIVE_MASK (0xfu << SD_FIRST_DRIVE)
static PD basepage;
PD *run = &basepage;
static DTA dta;
LONG drvbits = 4, drvrem;
WORD bootdev = 2, nflops;
UBYTE bootflags;
BCB *bufl[2];
static PUN_INFO pun = {.max_sect_siz = 512};
PUN_INFO *pun_ptr = &pun;
static void put16(UBYTE *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static unsigned get16(const UBYTE *p)
{
    return p[0] | p[1] << 8;
}
void *balloc_stram(LONG n, BOOL top)
{
    (void)top;
    return dc_alloc(n);
}
void *xmgetblk(WORD kind)
{
    (void)kind;
    return dc_alloc(128);
}
void xmfreblk(void *p)
{
    dc_free(p);
}
static int sd_scan_read(void *context, uint32_t sector, uint32_t count, void *buffer)
{
    (void)context;
    return dc_hal_sd_read(sector, count, buffer);
}
/* Probe the serial-port SD adapter once, at boot. The adapter has no card
 * detect line, so cards cannot be swapped without a restart. */
static void sd_mount(void)
{
    uint32_t sectors = 0, skipped = 0;
    sd_count = 0;
    if (dc_hal_sd_init(&sectors)) {
        kprintf("GEMDOS: no SD card on the serial port\n");
        return;
    }
    sd_sectors = sectors;
    int n = dc_sd_scan(sd_scan_read, NULL, sectors, sd_vol, DC_SD_MAX_VOLUMES, &skipped);
    for (int i = 0; i < n; i++) {
        const struct dc_sd_volume *v = &sd_vol[i];
        BPB *b = &sd_bpb[i];
        b->recsiz = 512;
        b->clsiz = v->clsiz;
        b->clsizb = v->clsiz * 512;
        b->rdlen = v->rdlen;
        b->fsiz = v->fsiz;
        b->fatrec = v->fatrec;
        b->datrec = v->datrec;
        b->numcl = v->numcl;
        b->b_flags = (v->fat16 ? B_16 : 0) | (v->nfats == 1 ? B_1FAT : 0);
        drvbits |= 1L << (SD_FIRST_DRIVE + i);
        kprintf("GEMDOS: %c: SD FAT%d, %lu KiB\n", 'A' + SD_FIRST_DRIVE + i, v->fat16 ? 16 : 12,
                (unsigned long)(v->numcl * v->clsiz / 2));
    }
    sd_count = n < 0 ? 0 : n;
    if (skipped & DC_SD_SKIP_FAT32)
        kprintf("GEMDOS: SD FAT32 volume ignored; TOS needs FAT16 (2 GiB or smaller)\n");
    if (skipped & DC_SD_SKIP_OTHER)
        kprintf("GEMDOS: SD volume with unsupported or damaged FAT ignored\n");
    if (!sd_count)
        kprintf("GEMDOS: SD card present but has no FAT12/FAT16 volume\n");
}
/* Volume for an SD drive number, or NULL. */
static const struct dc_sd_volume *sd_volume(WORD drive)
{
    return drive >= SD_FIRST_DRIVE && drive < SD_FIRST_DRIVE + sd_count ? &sd_vol[drive - SD_FIRST_DRIVE] : NULL;
}
void dc_storage_sd_status(uint32_t *sectors, uint32_t *mask)
{
    *sectors = sd_sectors;
    *mask = drvbits & SD_DRIVE_MASK;
}
int dc_storage_sd_prepare(void)
{
    /* Never format underneath open files, including redirected streams.
     * SH-4 pointers have the high bit set; only -1..-3 are device handles. */
    for (int i = 0; i < OPNFILES; i++) {
        OFD *o = sft[i].f_ofd;
        if (o && (ULONG)o < (ULONG)-3 && o->o_dmd &&
            o->o_dmd->m_drvnum >= SD_FIRST_DRIVE &&
            o->o_dmd->m_drvnum < SD_FIRST_DRIVE + DC_SD_MAX_VOLUMES)
            return DC_SD_BUSY;
    }
    /* Discard SD sector buffers before the first raw write. Existing DMD/DND
     * references remain alive until reboot, but can never reach the card. */
    for (int i = 0; i < 2; i++)
        for (BCB *b = bufl[i]; b; b = b->b_link)
            if (b->b_bufdrv >= SD_FIRST_DRIVE &&
                b->b_bufdrv < SD_FIRST_DRIVE + DC_SD_MAX_VOLUMES) {
                b->b_dirty = 0;
                b->b_bufdrv = -1;
            }
    sd_count = 0;
    drvbits &= ~SD_DRIVE_MASK;
    return (drvsel & SD_DRIVE_MASK) != 0;
}
int dc_storage_sd_remount(void)
{
    /* No safe general media-change teardown in the native GEMDOS dispatcher.
     * Remount only when there can be no cached directory/drive references. */
    if (drvsel & SD_DRIVE_MASK) return -1;
    sd_mount();
    if (sd_count == 1) return 0;
    sd_count = 0;
    drvbits &= ~SD_DRIVE_MASK;
    return -1;
}
static int writable_drive(int d)
{
    return d == 2 || sd_volume(d);
}
void dc_storage_init(void)
{
    extern void time_init(void);
    dc_boot_status("03 Creating RAM disk");
    ramdisk = dc_alloc(SECTORS * 512);
    if (!ramdisk) {
        extern void panic(const char *, ...);
        panic("RAM disk allocation failed\n");
    }
    memcpy(ramdisk,
           "\xeb\x3c\x90"
           "EMUTOSDC",
           11);
    put16(ramdisk + 11, 512);
    ramdisk[13] = 1;
    put16(ramdisk + 14, 1);
    ramdisk[16] = 2;
    put16(ramdisk + 17, 128);
    put16(ramdisk + 19, SECTORS);
    ramdisk[21] = 0xf8;
    put16(ramdisk + 22, FATSECS);
    put16(ramdisk + 24, 32);
    put16(ramdisk + 26, 64);
    ramdisk[38] = 0x29;
    memcpy(ramdisk + 43, "DREAM TOS  ", 11);
    memcpy(ramdisk + 54, "FAT16   ", 8);
    ramdisk[510] = 0x55;
    ramdisk[511] = 0xaa;
    for (int i = 0; i < 2; i++) {
        put16(ramdisk + (1 + i * FATSECS) * 512, 0xfff8);
        put16(ramdisk + (1 + i * FATSECS) * 512 + 2, 0xffff);
    }
    UBYTE boot[512];
    dc_boot_status("04 Reading CD filesystem");
    if (dc_disc_read(boot, 0, 512) == 512 && get16(boot + 11) == 512 && boot[13] == 1 &&
        boot[16] == 2 && get16(boot + 22) == 32 && get16(boot + 17) == 128 &&
        get16(boot + 19) == 8192 && get16(boot + 14) == 1 && boot[510] == 0x55 &&
        boot[511] == 0xaa) {
        disc_bpb = ram_bpb;
        drvbits |= 8;
    }
    dc_boot_status("05 Probing serial SD card");
    sd_mount();
    memset(&basepage, 0, sizeof(basepage));
    run->p_curdrv = 2;
    run->p_xdta = &dta;
    run->p_flags = PF_STANDARD;
    for (int i = 0; i < NUMSTD; i++)
        run->p_uft[i] = -1;
    bufl_init();
    time_init();
    kprintf("GEMDOS: C: 4 MiB FAT16 RAM, D: %s\n", (drvbits & 8) ? "read-only CD" : "no disc");
}
LONG dc_rwabs(WORD rw, void *buf, WORD count, LONG sector, WORD drive)
{
    const struct dc_sd_volume *sd = sd_volume(drive);
    ULONG limit = sd ? sd->sectors : SECTORS;
    if (count < 0 || sector < 0 || (ULONG)sector > limit || (ULONG)count > limit - (ULONG)sector)
        return ESECNF;
    if (!count)
        return 0;
    if (sd) {
        int r = (rw & 1) ? dc_hal_sd_write(sd->start + sector, count, buf)
                         : dc_hal_sd_read(sd->start + sector, count, buf);
        return r ? ((rw & 1) ? EWRITF : EREADF) : 0;
    }
    if (drive == 2) {
        if (rw & 1)
            memcpy(ramdisk + sector * 512, buf, count * 512);
        else
            memcpy(buf, ramdisk + sector * 512, count * 512);
        return 0;
    }
    if (drive == 3 && (drvbits & 8)) {
        if (rw & 1)
            return EWRPRO;
        return dc_disc_read(buf, sector * 512, count * 512) == count * 512 ? 0 : EREADF;
    }
    return EDRVNR;
}
BPB *dc_getbpb(WORD drive)
{
    if (sd_volume(drive))
        return &sd_bpb[drive - SD_FIRST_DRIVE];
    return drive == 2 ? &ram_bpb : drive == 3 && (drvbits & 8) ? &disc_bpb : NULL;
}
static int readonly_path(const char *p)
{
    int d = run->p_curdrv;
    if (p && p[0] && p[1] == ':')
        d = toupper(p[0]) - 'A';
    return !writable_drive(d);
}
static int readonly_handle(int h)
{
    OFD *o = getofd(h);
    return o && (ULONG)o < (ULONG)-3 && !writable_drive(o->o_dmd->m_drvnum);
}

/* BIOS devices can be direct handles, standard streams, or Fdup results.
 * SH-4 RAM addresses also have the high bit set: test only -1..-3. */
static int device_handle(int h)
{
    if (h >= 0 && h < NUMSTD)
        h = run->p_uft[h];
    if (h >= NUMSTD && h < NUMHANDLES) {
        OFD *ofd = getofd(h);
        if ((ULONG)ofd >= (ULONG)-3L)
            h = (LONG)ofd;
    }
    return h >= -3 && h <= -1 ? h : 0;
}

static long stream_write(int h, long n, const UBYTE *buf)
{
    extern void dc_console_out(WORD);
    extern int dc_console_break(void);
    extern long dc_native_terminate(int);
    if (n < 0)
        return EINVFN;
    int device = device_handle(h);
    if (!device)
        return readonly_handle(h) ? EWRPRO : xwrite(h, n, (void *)buf);
    if (device != -1)
        return EUNDEV;
    for (long i = 0; i < n; i++) {
        /* Text files keep their original bytes; only the VT52 display needs CR. */
        if (buf[i] == '\n')
            dc_console_out('\r');
        dc_console_out(buf[i]);
        if (!(i & 255) && dc_console_break())
            dc_native_terminate(130);
    }
    return n;
}

static long stream_read(int h, long n, UBYTE *buf)
{
    extern LONG dc_console_in(void);
    extern void dc_console_out(WORD);
    extern long dc_native_terminate(int);
    if (n < 0)
        return EINVFN;
    int device = device_handle(h);
    if (!device)
        return xread(h, n, buf);
    if (device != -1)
        return EUNDEV;
    long i = 0;
    while (i < n) {
        UBYTE ch = dc_console_in();
        if (!ch) continue;
        if (ch == 3) { dc_native_terminate(130); break; }
        if (ch == 4 || ch == 26) break; /* Ctrl+D / Ctrl+Z: end of input */
        if (ch == 8 || ch == 127) {
            if (i) { i--; dc_console_out(8); dc_console_out(' '); dc_console_out(8); }
            continue;
        }
        buf[i++] = ch == '\r' ? '\n' : ch;
        dc_console_out(ch);
        if (ch == '\r') dc_console_out('\n');
        if (ch == '\r') break;
    }
    return i;
}

void dc_storage_system_info(struct dc_system_info *info)
{
    info->drive_mask = drvbits;
    info->volatile_mask = ramdisk ? (1u << 2) : 0;
    for (int d = 0; d < 26; d++) {
        char path[] = "A:\\";
        path[0] += d;
        if ((info->drive_mask & (1u << d)) && readonly_path(path))
            info->readonly_mask |= 1u << d;
    }
}
/* Every va_arg matches the promoted native SH ABI. No 68k word-stack casts. */
static long dispatch(int op, va_list ap)
{
    int h, a, b;
    long n;
    char *p, *q;
    void *buf;
    extern LONG dc_console_in(void), dc_console_status(void);
    extern void dc_console_out(WORD);
    switch (op) {
    case 0x00:
    case 0x4c: {
        extern long dc_native_terminate(int);
        return dc_native_terminate(op ? va_arg(ap, int) : 0);
    }
    case 0x4b: {
        extern long trap1_pexec(short, const char *, const char *, const char *);
        a = va_arg(ap, int);
        p = va_arg(ap, char *);
        q = va_arg(ap, char *);
        const char *env = va_arg(ap, char *);
        return trap1_pexec(a, p, q, env);
    }
    case 0x01:
    case 0x07:
    case 0x08:
        return dc_console_in();
    case 0x02: {
        UBYTE ch = va_arg(ap, int);
        return stream_write(1, 1, &ch) < 0 ? EIHNDL : 0;
    }
    case 0x06:
        a = va_arg(ap, int);
        if (a == 0xff)
            return dc_console_status() ? dc_console_in() : 0;
        dc_console_out(a);
        return 0;
    case 0x09:
        p = va_arg(ap, char *);
        return stream_write(1, strlen(p), (UBYTE *)p);
    case 0x0b:
        return dc_console_status();
    case 0x0e:
        return xsetdrv(va_arg(ap, int));
    case 0x19:
        return xgetdrv();
    case 0x1a:
        xsetdta(va_arg(ap, void *));
        return 0;
    case 0x2a:
        return current_date;
    case 0x2c:
        return current_time;
    case 0x2f:
        return (long)xgetdta();
    case 0x30:
        return 0x2000;
    case 0x36:
        buf = va_arg(ap, void *);
        a = va_arg(ap, int);
        return xgetfree(buf, a);
    case 0x39:
        p = va_arg(ap, char *);
        return readonly_path(p) ? EWRPRO : xmkdir(p);
    case 0x3a:
        p = va_arg(ap, char *);
        return readonly_path(p) ? EWRPRO : xrmdir(p);
    case 0x3b:
        return xchdir(va_arg(ap, char *));
    case 0x3c:
        p = va_arg(ap, char *);
        a = va_arg(ap, int);
        return readonly_path(p) ? EWRPRO : xcreat(p, a);
    case 0x3d:
        p = va_arg(ap, char *);
        a = va_arg(ap, int);
        return a && readonly_path(p) ? EWRPRO : xopen(p, a);
    case 0x3e:
        return xclose(va_arg(ap, int));
    case 0x3f:
        h = va_arg(ap, int);
        n = va_arg(ap, long);
        buf = va_arg(ap, void *);
        return stream_read(h, n, buf);
    case 0x40:
        h = va_arg(ap, int);
        n = va_arg(ap, long);
        buf = va_arg(ap, void *);
        return stream_write(h, n, buf);
    case 0x41:
        p = va_arg(ap, char *);
        return readonly_path(p) ? EWRPRO : xunlink(p);
    case 0x42:
        n = va_arg(ap, long);
        h = va_arg(ap, int);
        a = va_arg(ap, int);
        return device_handle(h) ? EIHNDL : xlseek(n, h, a);
    case 0x43:
        p = va_arg(ap, char *);
        a = va_arg(ap, int);
        b = va_arg(ap, int);
        return a && readonly_path(p) ? EWRPRO : xchmod(p, a, b);
    case 0x44:
        n = va_arg(ap, long);
        a = va_arg(ap, int);
        return (long)xmxalloc(n, a);
    case 0x45:
        return xdup(va_arg(ap, int));
    case 0x46:
        a = va_arg(ap, int);
        h = va_arg(ap, int);
        return xforce(a, h);
    case 0x47:
        p = va_arg(ap, char *);
        a = va_arg(ap, int);
        return xgetdir(p, a);
    case 0x48:
        return (long)xmalloc(va_arg(ap, long));
    case 0x49:
        return xmfree(va_arg(ap, void *));
    case 0x4a:
        a = va_arg(ap, int);
        buf = va_arg(ap, void *);
        n = va_arg(ap, long);
        return xsetblk(a, buf, n);
    case 0x4e:
        p = va_arg(ap, char *);
        a = va_arg(ap, int);
        return xsfirst(p, a);
    case 0x4f:
        return xsnext();
    case 0x56:
        a = va_arg(ap, int);
        p = va_arg(ap, char *);
        q = va_arg(ap, char *);
        return readonly_path(p) || readonly_path(q) ? EWRPRO : xrename(a, p, q);
    case 0x57:
        buf = va_arg(ap, void *);
        h = va_arg(ap, int);
        a = va_arg(ap, int);
        return a && readonly_handle(h) ? EWRPRO : xgsdtof(buf, h, a);
    default:
        kprintf("Unimplemented native GEMDOS call %02x\n", op);
        return EINVFN;
    }
}
long trap1(int op, ...)
{
    va_list ap;
    long result;
    /* The FAT implementation reports physical IO failures with longjmp. */
    if (setjmp(errbuf))
        return errcode;
    va_start(ap, op);
    result = dispatch(op, ap);
    va_end(ap);
    return result;
}
void dc_storage_selftest(void)
{
    static const char hello[] = "Dream TOS on Sega Dreamcast\r\nC: is a volatile RAM disk.\r\n";
    char buf[sizeof(hello)];
    long h = trap1(0x3c, "C:\\WELCOME.TXT", 0), r;
    if (h < 0)
        goto fail;
    r = trap1(0x40, (int)h, (long)sizeof(hello) - 1, (void *)hello);
    if (r != sizeof(hello) - 1)
        goto fail;
    if (trap1(0x3e, (int)h))
        goto fail;
    h = trap1(0x3d, "C:\\WELCOME.TXT", 0);
    if (h < 0)
        goto fail;
    r = trap1(0x3f, (int)h, (long)sizeof(buf), buf);
    trap1(0x3e, (int)h);
    if (r != sizeof(hello) - 1 || memcmp(buf, hello, r))
        goto fail;
    if (trap1(0x39, "C:\\TEMP"))
        goto fail;
    /* Cross-sector FAT allocation, seek, rename, deletion, and space recovery. */
    UBYTE pattern[2049], copy[2049];
    LONG before[4], after[4];
    for (unsigned i = 0; i < sizeof(pattern); i++)
        pattern[i] = (i * 37) ^ ((i >> 8) + 7);
    if (trap1(0x36, before, 3))
        goto fail;
    h = trap1(0x3c, "C:\\TEMP\\CHAIN.BIN", 0);
    if (h < 0)
        goto fail;
    if (trap1(0x40, (int)h, (long)sizeof(pattern), pattern) != sizeof(pattern))
        goto fail;
    if (trap1(0x42, 0L, (int)h, 0))
        goto fail;
    if (trap1(0x3f, (int)h, (long)sizeof(copy), copy) != sizeof(copy) ||
        memcmp(pattern, copy, sizeof(copy)))
        goto fail;
    if (trap1(0x3e, (int)h) || trap1(0x56, 0, "C:\\TEMP\\CHAIN.BIN", "C:\\TEMP\\RENAMED.BIN"))
        goto fail;
    if (trap1(0x41, "C:\\TEMP\\RENAMED.BIN"))
        goto fail;
    if (trap1(0x36, after, 3) || before[0] != after[0])
        goto fail;
    void *block = xmalloc(4096);
    if (!block || xsetblk(0, block, 128) || xmfree(block) || xmfree(block) != EIMBA)
        goto fail;
    kprintf("SELFTEST: FAT cluster chain/seek/rename/delete/space and Mshrink PASS\n");
    if (trap1(0x3c, "D:\\FORBID.TXT", 0) != EWRPRO)
        goto fail;
    kprintf("SELFTEST: GEMDOS create/write/close/read/mkdir/read-only PASS\n");
    if (drvbits & 8) {
        extern long trap1_pexec(short, const char *, const char *, const char *);
        static const char test_tail[] = {4, 'T', 'E', 'S', 'T', 0};
        void *largest = xmalloc(-1);
        if (trap1_pexec(0, "D:\\UTILS\\HELLO.PRG", test_tail, NULL) != 42 || xmalloc(-1) != largest)
            goto fail;
        static const char pterm_tail[] = {4, 'P', 'T', 'E', 'R', 0};
        if (trap1_pexec(0, "D:\\UTILS\\HELLO.PRG", pterm_tail, NULL) != 43 || xmalloc(-1) != largest)
            goto fail;
        kprintf("SELFTEST: native SH-4 relocation/BSS/GEMDOS/return/Pterm/cleanup PASS\n");
    }
    return;
fail: {
    extern void panic(const char *, ...);
    panic("SELFTEST: filesystem failed h=%ld err=%ld\n", h, errcode);
}
}
