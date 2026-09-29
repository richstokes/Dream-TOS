/* Conservative VMU file service and LCD: the only general VMU write API offered
 * to native programs (besides the named EMUTOS.CFG store in settings.h).
 * GPL-2.0-or-later. All values are scalar; nothing here can format a card. */
#ifndef DC_VMU_FILE_H
#define DC_VMU_FILE_H
#include <stdint.h>
#include "dreamcast/vmu_info.h"

#define DC_VMUF_VERSION 1
#define DC_VMUF_MAX_BLOCKS 200u                 /* a standard VMU has 200 user blocks */
#define DC_VMUF_BLOCK 512u
#define DC_VMUF_MAX_BYTES (DC_VMUF_MAX_BLOCKS * DC_VMUF_BLOCK)
#define DC_VMUF_NAME_MAX 12u                    /* printable ASCII, 1..12 characters */
#define DC_VMUF_OVERWRITE 1u                    /* flags for vmu_file_write */
#define DC_VMU_LCD_BYTES 192u                   /* 48x32, 1 bit per pixel */

/* Non-negative results are byte counts (or 0). Failures are negative. Each
 * failure states what happened to the card: "unchanged" means nothing was
 * written; DC_VMUF_VERIFY means a write was attempted and the card must be
 * checked (for example in the console BIOS file manager) before further use. */
#define DC_VMUF_ABSENT -1       /* no such card, or it has no memory function; unchanged */
#define DC_VMUF_IO -2           /* Maple/flash error, old contents confirmed intact; unchanged */
#define DC_VMUF_CARD -3         /* unformatted, unsupported or damaged (cross-links, cycles): refused, unchanged */
#define DC_VMUF_NOT_FOUND -4    /* no file of that name; unchanged */
#define DC_VMUF_EXISTS -5       /* name exists and DC_VMUF_OVERWRITE was not given; unchanged */
#define DC_VMUF_FULL -6         /* not enough free blocks (an overwrite reuses the old blocks); unchanged */
#define DC_VMUF_DIR_FULL -7     /* all directory entries in use; unchanged */
#define DC_VMUF_PROTECTED -8    /* copy-protected, game or otherwise unusual file: refused, unchanged */
#define DC_VMUF_BAD_NAME -9     /* not 1..12 printable ASCII characters; unchanged */
#define DC_VMUF_TOO_BIG -10     /* over 200 blocks; unchanged */
#define DC_VMUF_VERIFY -11      /* write attempted but read-back or consistency check failed: card may be damaged */
#define DC_VMUF_RESTORED -12    /* a write failed and the previous file was restored and verified */
#define DC_VMUF_AMBIGUOUS -13   /* duplicate or oddly padded names could match more than one entry: refused, unchanged */
#define DC_VMUF_BUSY -14        /* another VMU file call is active, or the LCD frame is busy; retry */
#define DC_VMUF_UNSUPPORTED -15 /* device has no LCD */
#define DC_VMUF_BUFFER -16      /* read buffer smaller than the file; unchanged */
#define DC_VMUF_BADARG -64      /* invalid port, unit, pointer, zero size or unknown flag */

/* --- OS services (hal_vmu.c), reached through struct dc_native_api ---------
 * All calls issue Maple I/O, take noticeable time and must be user-initiated.
 * Files are whole 512-byte blocks: a write is zero-padded to a block multiple.
 * Only ordinary data files (type 0x33, no copy protection) are ever
 * overwritten or deleted. New files are always ordinary copyable data files.
 * Reading a copy-protected file is refused. Writes are not power-loss atomic. */

/* Reads a file: returns its size (blocks*512) and fills buffer. With a NULL
 * buffer and bytes==0 only the size is returned. */
long dc_vmu_file_read(uint32_t port, uint32_t unit, const char *name, void *buffer, uint32_t bytes);
/* Creates or (with DC_VMUF_OVERWRITE) replaces a file with data[0..bytes).
 * Returns the stored size (blocks*512) after a successful read-back. */
long dc_vmu_file_write(uint32_t port, uint32_t unit, const char *name, const void *data,
                       uint32_t bytes, uint32_t flags);
/* Deletes one ordinary data file. Returns 0 after verifying the card. */
long dc_vmu_file_delete(uint32_t port, uint32_t unit, const char *name);
/* Draws a 48x32 bitmap on the VMU LCD: 6 bytes per row, top row first, most
 * significant bit is the leftmost pixel, 1 = dark. bytes must be 192. Returns 0. */
long dc_vmu_screen(uint32_t port, uint32_t unit, const void *bitmap, uint32_t bytes);

/* --- Portable engine (vmu_file.c): all policy, no KOS. Host-testable. -------
 * The backend supplies one bounded block read, plus file-level put/erase
 * (KOS vmufs_write/vmufs_delete on the console). The engine validates the
 * card with dc_vmu_inspect_ex first, matches names exactly as KOS would,
 * checks space and slots, saves the old contents of an overwritten file, and
 * verifies every write by reading the file and the directory back. It never
 * touches the root block, and holds static state: one call at a time. */
struct dc_vmu_backend {
    dc_vmu_reader read;
    /* Create (overwrite==0) or replace (overwrite!=0) a file; name is a NUL-
     * terminated 1..12 character name and data is blocks*512 bytes. 0 = ok;
     * nonzero = failed, at an unknown point (the engine then inspects the card). */
    int (*put)(void *context, const char *name, const unsigned char *data, unsigned blocks, int overwrite);
    int (*erase)(void *context, const char *name);
    unsigned char *stage, *backup; /* DC_VMUF_MAX_BYTES each (both required for writes) */
};
long dc_vmuf_read(const struct dc_vmu_backend *b, void *context, const char *name, void *buffer, uint32_t bytes);
long dc_vmuf_write(const struct dc_vmu_backend *b, void *context, const char *name, const void *data,
                   uint32_t bytes, uint32_t flags);
long dc_vmuf_delete(const struct dc_vmu_backend *b, void *context, const char *name);
/* Normalises a name (trailing spaces trimmed). Returns its length or DC_VMUF_BAD_NAME. */
int dc_vmuf_name(const char *name, char out[13]);
#endif
