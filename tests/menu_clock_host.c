/* Real menu clock with deterministic GEMDOS time and AES drawing boundaries. */
#include "emutos.h"
#include "struct.h"
#include "aesext.h"
#include "gemgsxif.h"
#include "gemoblib.h"
#include "gemmnlib.h"
#include "dreamcast/menu_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

WORD gl_wchar = 8, gl_hchar = 16, gl_hbox = 20;
static OBJECT menu[3];
OBJECT *gl_mntree;
static GRECT bar = {0, 0, 640, 19}, titles = {0, 0, 300, 19};
static GRECT clip = {20, 30, 40, 50};
static const GRECT caller_clip = {20, 30, 40, 50};
static UWORD now;
static int draws, backgrounds, mouse_hidden;
static char visible[6];

long xgettime(void) { return now; }
void ob_actxywh(OBJECT *tree, WORD obj, GRECT *r)
{
    assert(tree == menu && (obj == 1 || obj == 2));
    *r = obj == 1 ? bar : titles;
}
void gsx_gclip(GRECT *r) { *r = clip; }
void gsx_sclip(const GRECT *r) { clip = *r; }
void gsx_moff(void) { mouse_hidden++; }
void gsx_mon(void) { assert(mouse_hidden > 0); mouse_hidden--; }
void ob_draw(OBJECT *tree, WORD obj, WORD depth)
{
    assert(mouse_hidden && depth == 0);
    assert(clip.g_x >= titles.g_x + titles.g_w + gl_wchar);
    assert(clip.g_x + clip.g_w <= bar.g_x + bar.g_w - gl_wchar);
    assert(clip.g_y + clip.g_h <= gl_hbox - 1);
    if (tree == menu) {
        assert(obj == 1);
        backgrounds++;
    } else {
        assert(obj == ROOT && tree->ob_type == G_STRING);
        assert(tree->ob_x == clip.g_x && tree->ob_width == 5 * gl_wchar);
        assert(backgrounds == draws + 1); /* Old digits cleared first. */
        strcpy(visible, (char *)tree->ob_spec);
        draws++;
    }
}

static void tick(unsigned hour, unsigned minute, unsigned second, const char *expected)
{
    now = (hour << 11) | (minute << 5) | (second / 2);
    dc_menu_clock(FALSE);
    assert(!strcmp(visible, expected));
    assert(!mouse_hidden && !memcmp(&clip, &caller_clip, sizeof(clip)));
}

int main(void)
{
    dc_menu_clock(TRUE);
    assert(draws == 0); /* Console mode has no menu. */
    gl_mntree = menu;
    tick(9, 7, 0, "09:07");
    assert(draws == 1);
    tick(9, 7, 58, "09:07");
    assert(draws == 1); /* No periodic flicker within a minute. */
    tick(9, 8, 0, "09:08");
    tick(23, 59, 58, "23:59");
    tick(0, 0, 0, "00:00");
    tick(12, 0, 0, "12:00");
    assert(draws == 5);
    dc_menu_clock(TRUE); /* Newly shown menu must draw even in the same minute. */
    assert(draws == 6);
    gl_mntree = NULL;
    dc_menu_clock(TRUE);
    tick(12, 1, 0, "12:00");
    assert(draws == 6);
    gl_mntree = menu;
    tick(12, 1, 0, "12:01");
    assert(draws == 7);

    /* Long menus win over the optional clock; it returns when space permits. */
    titles.g_w = 600;
    tick(12, 2, 0, "12:01");
    assert(draws == 7);
    titles.g_w = 300;
    tick(12, 2, 0, "12:02");
    assert(draws == 8);
    bar.g_w = 320;
    titles.g_w = 200;
    gl_hchar = 8;
    gl_hbox = 12;
    bar.g_h = 12; /* Clip out the separator even for a taller bar object. */
    tick(12, 3, 0, "12:03");
    assert(draws == 9);
    puts("Menu clock: rollover, repaint, menu lifecycle, title spacing and clipping PASS");
}
