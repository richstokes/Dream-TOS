/* Validated relocatable SH-4 program loader. GPL-2.0-or-later.
 * File format: DCNATIVE + six LE32 fields (ABI, file bytes, memory bytes,
 * entry offset, relocation count, reserved=0), image, DIR32 offsets.
 * Atari and arbitrary ELF binaries are rejected, never emulated. */
#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "bdosstub.h"
#include "gemerror.h"
#include "mem.h"
#include "dreamcast/hal.h"
#include "dreamcast/native.h"
#include "dreamcast/system_info.h"
#include "dreamcast/control.h"
#include "dreamcast/vmu_info.h"
#include "dreamcast/settings.h"
#include "dreamcast/net.h"
#include "dreamcast/audio.h"
#include "obdefs.h"
#include "struct.h"
#include "aesvars.h"
#include "gemlib.h"
#include "gemevlib.h"
extern long trap1(int, ...);
extern LONG super(WORD, void *);
extern void dc_vdi(void *), dc_poll(void);
extern void dc_free_process_memory(PD *);
static void native_aes(void *pb)
{
    super(200, pb);
}
static const struct dc_native_api api = {DC_NATIVE_ABI, sizeof(api), trap1,
                                         native_aes,    dc_vdi,      dc_poll, dc_millis,
                                         dc_system_info, dc_input_config, dc_input_snapshot, dc_vmu_info,
                                         dc_control_store, dc_net_info, dc_net_ping, dc_net_resolve,
                                         dc_audio_open, dc_audio_close, dc_audio_write,
                                         dc_audio_space, dc_audio_set, dc_audio_info};
static jmp_buf term_context;
static int executing;
static long exit_status;
static PD child;
/* Accessories have independent SH-4 stacks and termination targets. Never let
 * Pterm in an accessory jump into a foreground application's stack. */
static jmp_buf acc_term[NUM_ACCS];
static int acc_active[NUM_ACCS];
struct native_image { UBYTE *code; ULONG entry; };
long dc_native_terminate(int status)
{
    int pid = rlr ? rlr->p_pid : 0;
    if (pid >= 2 && pid < NUM_PDS && acc_active[pid - 2])
        longjmp(acc_term[pid - 2], 1);
    if (pid != 0 || !executing)
        return EINVFN;
    exit_status = status;
    longjmp(term_context, 1);
    return 0;
}
static ULONG le32(const UBYTE *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}
static long load_image(const char *path, struct native_image *loaded)
{
    if (!path)
        return EFILNF;
    long h = trap1(0x3d, path, 0);
    if (h < 0)
        return h;
    UBYTE header[32];
    long result = EPLFMT;
    UBYTE *image = NULL;
    if (trap1(0x3f, (int)h, 32L, header) != 32 || memcmp(header, "DCNATIVE", 8))
        goto done;
    ULONG size = le32(header + 12), mem = le32(header + 16), entry = le32(header + 20),
          relocs = le32(header + 24);
    if (le32(header + 8) != DC_NATIVE_ABI || le32(header + 28) || size < 2 ||
        size > 2 * 1024 * 1024UL || mem < size || mem > 2 * 1024 * 1024UL || entry >= size ||
        (entry & 1) || relocs > size / 4)
        goto done;
    if (trap1(0x42, 0L, (int)h, 2) != 32L + size + relocs * 4)
        goto done;
    if (trap1(0x42, 32L, (int)h, 0) != 32)
        goto done;
    image = dc_alloc(mem);
    if (!image) {
        result = ENSMEM;
        goto done;
    }
    if (trap1(0x3f, (int)h, (long)size, image) != size)
        goto done;
    ULONG previous = 0;
    for (ULONG i = 0; i < relocs; i++) {
        UBYTE r[4];
        if (trap1(0x3f, (int)h, 4L, r) != 4)
            goto done;
        ULONG offset = le32(r);
        if (offset > size - 4 || (offset & 3) || (i && offset <= previous))
            goto done;
        ULONG value = le32(image + offset);
        if (value > mem)
            goto done;
        *(ULONG *)(image + offset) = value + (ULONG)image;
        previous = offset;
    }
    trap1(0x3e, (int)h);
    h = -1;
    dc_sync_code(image, mem);
    loaded->code = image;
    loaded->entry = entry;
    kprintf("Native SH-4: %s (%lu bytes, %lu relocations)\n", path, mem, relocs);
    return 0;
done:
    if (h >= 0)
        trap1(0x3e, (int)h);
    dc_free(image);
    return result;
}

LONG dc_native_acc_load(const char *path)
{
    struct native_image *image = dc_alloc(sizeof(*image));
    if (!image)
        return -1L;
    long result = load_image(path, image);
    if (result < 0) {
        dc_free(image);
        kprintf("Accessory load failed: %s (%ld)\n", path, result);
        return -1L;
    }
    return (LONG)image;
}
void dc_native_acc_free(LONG address)
{
    struct native_image *image = (void *)address;
    dc_free(image->code);
    dc_free(image);
}
void dc_native_acc_start(void)
{
    struct native_image *image = (void *)rlr->p_ldaddr;
    int id = rlr->p_pid - 2;
    acc_active[id] = 1;
    if (!setjmp(acc_term[id]))
        ((dc_native_entry)(image->code + image->entry))(&api, "", "\0");
    acc_active[id] = 0;
    /* A returning accessory must not return from its scheduler thread and
     * strand the AES gate. Park it while still acknowledging shell shutdown. */
    for (;;) {
        WORD message[8];
        rlr->p_flags |= AP_MESAG;
        ev_mesag(message);
        if (message[0] == 41) /* AC_CLOSE */
            rlr->p_flags |= AP_ACCLOSE;
    }
}
long trap1_pexec(short mode, const char *path, const char *tail, const char *env)
{
    if (mode != 0 || executing || (rlr && rlr->p_pid != 0))
        return EINVFN;
    struct native_image loaded;
    long result = load_image(path, &loaded);
    if (result < 0)
        return result;
    PD *parent = run;
    memset(&child, 0, sizeof(child));
    child.p_parent = parent;
    child.p_curdrv = parent->p_curdrv;
    child.p_flags = PF_STANDARD;
    child.p_xdta = (DTA *)child.p_cmdlin;
    child.p_env = (char *)(env ? env : parent->p_env);
    for (int i = 0; i < NUMSTD; i++) {
        WORD f = parent->p_uft[i];
        if (f > 0)
            ixforce(i, f, &child);
        else
            child.p_uft[i] = f;
    }
    for (int i = 0; i < NUMCURDIR; i++) {
        int d = parent->p_curdir[i];
        child.p_curdir[i] = d;
        if (d)
            dirtbl[d].use++;
    }
    kprintf("Pexec: native SH-4 %s\n", path);
    run = &child;
    executing = 1;
    exit_status = 0;
    if (!setjmp(term_context))
        exit_status = ((dc_native_entry)(loaded.code + loaded.entry))(&api, tail ? tail : "",
                                                         child.p_env ? child.p_env : "\0");
    for (int i = 0; i < NUMSTD; i++)
        if (child.p_uft[i] > 0)
            trap1(0x3e, (int)child.p_uft[i]);
    for (int i = 0; i < OPNFILES; i++)
        if (sft[i].f_own == &child)
            trap1(0x3e, i + NUMSTD);
    for (int i = 0; i < NUMCURDIR; i++)
        if (child.p_curdir[i])
            decr_curdir_usage(child.p_curdir[i]);
    dc_audio_close(); /* never leave the AICA playing after an exit or Pterm */
    dc_free_process_memory(&child);
    run = parent;
    executing = 0;
    result = exit_status;
    kprintf("Pexec: native program returned %ld\n", result);
    dc_free(loaded.code);
    return result;
}
