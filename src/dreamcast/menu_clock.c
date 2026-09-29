/* A clock in the unused right-hand end of the GEM menu bar.
 * GPL-2.0-or-later. */
#include "emutos.h"
#include "struct.h"
#include "aesext.h"
#include "gemgsxif.h"
#include "gemoblib.h"
#include "gemmnlib.h"
#include "time.h" /* GEMDOS xgettime() */
#include "dreamcast/menu_clock.h"

void dc_menu_clock(int force)
{
    static WORD last_minute = -1;
    GRECT bar, titles, saved_clip, area;
    UWORD time, hour, minute;
    char label[6];
    OBJECT clock = {
        .ob_next = -1, .ob_head = -1, .ob_tail = -1,
        .ob_type = G_STRING, .ob_flags = LASTOB, .ob_state = NORMAL,
        .ob_spec = (LONG)label
    };

    if (!gl_mntree) {
        last_minute = -1;
        return;
    }

    /* Match the menu's character metrics, with a one-cell right margin.
     * Never cover titles in a crowded application menu. */
    ob_actxywh(gl_mntree, 1, &bar);
    ob_actxywh(gl_mntree, 2, &titles);
    area = bar;
    area.g_w = 5 * gl_wchar;
    area.g_x = bar.g_x + bar.g_w - area.g_w - gl_wchar;
    if (area.g_x < titles.g_x + titles.g_w + gl_wchar ||
        area.g_x < bar.g_x || bar.g_h < gl_hchar) {
        last_minute = -1;
        return;
    }

    time = (UWORD)xgettime();
    if (!force && (time >> 5) == last_minute)
        return;
    last_minute = time >> 5;
    hour = (time >> 11) & 31;
    minute = (time >> 5) & 63;
    label[0] = '0' + hour / 10;
    label[1] = '0' + hour % 10;
    label[2] = ':';
    label[3] = '0' + minute / 10;
    label[4] = '0' + minute % 10;
    label[5] = '\0';

    clock.ob_x = area.g_x;
    clock.ob_y = bar.g_y;
    clock.ob_width = area.g_w;
    clock.ob_height = bar.g_h;

    /* Restore the bar's own background in just the clock rectangle. Keep
     * the separator and the caller's clipping intact, and protect any
     * mouse pointer overlapping the clock. */
    if (area.g_y + area.g_h >= gl_hbox)
        area.g_h = gl_hbox - 1 - area.g_y;
    gsx_gclip(&saved_clip);
    gsx_sclip(&area);
    gsx_moff();
    ob_draw(gl_mntree, 1, 0);
    ob_draw(&clock, ROOT, 0);
    gsx_mon();
    gsx_sclip(&saved_clip);
}
