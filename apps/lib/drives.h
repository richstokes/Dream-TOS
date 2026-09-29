/* Which GEMDOS drives an application may write to. GPL-2.0-or-later.
 * Header-only so every application and its host test builds unchanged. */
#ifndef DC_DRIVES_H
#define DC_DRIVES_H
#include "app.h"
#include "dreamcast/system_info.h"
#include <ctype.h>
#include <stddef.h>

#define DC_DRIVE_NONE 0     /* not mounted, or read-only */
#define DC_DRIVE_PERSIST 1  /* writable and survives a reset (SD card) */
#define DC_DRIVE_VOLATILE 2 /* writable, RAM-backed */

/* Without the system-info call only the RAM disk C: is assumed writable. */
static inline int dc_drive_state(int letter)
{
    static struct dc_system_info info;
    unsigned d = (unsigned)(toupper((unsigned char)letter) - 'A');
    if (d >= 26)
        return DC_DRIVE_NONE;
    if (!dc_os || dc_os->size < offsetof(struct dc_native_api, system_info) + sizeof(dc_os->system_info) ||
        !dc_os->system_info || dc_os->system_info(&info, sizeof(info)) != (long)sizeof(info) ||
        info.version != DC_SYSTEM_INFO_VERSION)
        return d == 2 ? DC_DRIVE_VOLATILE : DC_DRIVE_NONE;
    if (!(info.drive_mask & (1u << d)) || (info.readonly_mask & (1u << d)))
        return DC_DRIVE_NONE;
    return info.volatile_mask & (1u << d) ? DC_DRIVE_VOLATILE : DC_DRIVE_PERSIST;
}

/* Where to keep data that should outlive a reset: the first writable
 * persistent drive (an SD card), else the RAM disk. */
static inline char dc_storage_drive(void)
{
    for (char d = 'C'; d <= 'Z'; d++)
        if (dc_drive_state(d) == DC_DRIVE_PERSIST)
            return d;
    return 'C';
}

/* Suffix for "Saved ..." messages. */
static inline const char *dc_drive_note(int letter)
{
    return dc_drive_state(letter) == DC_DRIVE_VOLATILE ? " (lost at reset)" : "";
}
#endif
