/* Exercise the real AES/VDI bindings against a small window-manager model. */
#include "app.h"
#include "window.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct aes_pb { int16_t *c, *g, *i, *o; intptr_t *a, *z; };
struct vdi_pb { int16_t *c, *i, *p, *o, *q; };
static AppRect desktop = {0, 20, 640, 460}, bounds, work, visible[3], clip;
static int visible_index, updates, mouse_control, mouse_hidden, clips, draws;
static int created, opened, closed, deleted, palette_writes, fail_open, find_handle = 7;
static int inspect_drawing, expected_button_state;
static void geometry(AppRect r)
{
    bounds = r;
    work = (AppRect){r.x + 1, r.y + 20, r.w - 18, r.h - 38};
}
static void outrect(struct aes_pb *p, AppRect r)
{
    memcpy(p->o + 1, &r, sizeof(r));
}
static void mock_aes(void *v)
{
    struct aes_pb *p = v;
    memset(p->o, 0, 16 * sizeof(int16_t));
    p->o[0] = 1;
    switch (p->c[0]) {
    case 10: case 19: case 77: break;
    case 78:
        if (p->i[0] == 256) mouse_hidden++;
        if (p->i[0] == 257) mouse_hidden--;
        break;
    case 100:
        assert(p->i[0] == (1 | 2 | 4 | 8 | 32));
        created++;
        p->o[0] = 7;
        break;
    case 101: {
        AppRect r;
        memcpy(&r, p->i + 1, sizeof(r));
        geometry(r);
        opened++;
        p->o[0] = !fail_open;
        break;
    }
    case 102: closed++; break;
    case 103: deleted++; break;
    case 104:
        switch (p->i[1]) {
        case 4: outrect(p, p->i[0] ? work : desktop); break;
        case 5: outrect(p, bounds); break;
        case 11: visible_index = 0; outrect(p, visible[0]); break;
        case 12: outrect(p, visible[++visible_index]); break;
        default: assert(!"Unexpected wind_get field");
        }
        break;
    case 105:
        if (p->i[1] == 5) {
            AppRect r;
            memcpy(&r, p->i + 2, sizeof(r));
            geometry(r);
        } else {
            assert(p->i[1] == 2 || p->i[1] == 10);
        }
        break;
    case 106: p->o[0] = find_handle; break;
    case 107:
        if (p->i[0] == 1) updates++;
        if (p->i[0] == 0) updates--;
        if (p->i[0] == 3) mouse_control++;
        if (p->i[0] == 2) mouse_control--;
        assert(updates >= 0 && mouse_control >= 0);
        break;
    case 108:
        assert(p->i[0] == 0);
        outrect(p, (AppRect){-1, -20, p->i[4] + 18, p->i[5] + 38});
        break;
    case 25: {
        assert(p->i[0] == (APP_KEY | APP_BUTTON | APP_MESSAGE | APP_TIMER));
        assert(p->i[1] == 1 && p->i[2] == 1 && p->i[3] == expected_button_state);
        int16_t *msg = (void *)p->a[0];
        msg[0] = 20;
        msg[3] = 7;
        p->o[0] = APP_KEY | APP_MESSAGE;
        p->o[1] = 111; p->o[2] = 222; p->o[3] = !expected_button_state;
        p->o[4] = 4; p->o[5] = 0x1c0d;
        break;
    }
    default: assert(!"Unexpected AES operation");
    }
}
static void mock_vdi(void *v)
{
    struct vdi_pb *p = v;
    if (p->c[0] == 100) p->c[6] = 11;
    if (p->c[0] == 14) palette_writes++;
    if (p->c[0] == 26) p->o[1] = p->o[2] = p->o[3] = 500;
    if (p->c[0] == 129) {
        clips = p->i[0];
        clip = (AppRect){p->p[0], p->p[1], p->p[2] - p->p[0] + 1, p->p[3] - p->p[1] + 1};
    }
    if (inspect_drawing && p->c[0] == 114) {
        assert(updates == 1 && mouse_hidden == 1 && clips);
        assert(clip.x >= work.x && clip.y >= work.y);
        assert(clip.x + clip.w <= work.x + work.w);
        assert(clip.y + clip.h <= work.y + work.h);
    }
}
