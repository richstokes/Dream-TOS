/* Real clock + real app/window bindings; deterministic AES lifecycle and time. */
#include "gem_model.h"
#include <setjmp.h>
#include "../apps/ports/clock.c"
static jmp_buf finished;
static int step, date_reads, midnight;
static unsigned fake_date = (46<<9)|(9<<5)|28, fake_time = (13<<11)|(42<<5)|29;
static long clock_gemdos(int op, ...)
{
    assert(op == 0x2a || op == 0x2c);
    if (op == 0x2c) return fake_time;
    if (midnight && date_reads++ == 0) return fake_date-1;
    return fake_date;
}
static void clock_aes(void *v)
{
    struct aes_pb *p = v;
    if (p->c[0] == 10) {
        mock_aes(v);
        p->g[2] = 2;
    } else if (p->c[0] == 35) {
        assert(p->i[0] == 2 && !strcmp((char *)p->a[0],"  Clock"));
        p->o[0] = 3;
    } else if (p->c[0] == 25) {
        assert(!updates && !mouse_hidden && !mouse_control && !palette_writes);
        int open = window.handle >= 0;
        assert(p->i[0] == (APP_MESSAGE | (open ? APP_KEY|APP_TIMER : 0)));
        assert(p->i[14] == 500);
        memset(p->o,0,16*sizeof(int16_t));
        int16_t *m = (void *)p->a[0];
        p->o[0] = APP_MESSAGE;
        m[0] = 40; m[4] = 3;
        switch (step++) {
        case 0: assert(!open); m[4] = 8; break; /* Other accessory: ignore. */
        case 1: assert(!open); break;
        case 2:
            assert(open && opened == 1);
            fake_time = (13<<11)|(43<<5);
            p->o[0] = APP_TIMER;
            break;
        case 3:
            assert(!strcmp(time_text,"13:43:00"));
            m[0] = 22; m[3] = window.handle;
            break;
        case 4: assert(!open && closed == 1 && deleted == 1); break;
        case 5:
            assert(open && opened == 2);
            m[0] = 41; /* AES shell, not the accessory, owns window teardown. */
            break;
        case 6: assert(!open && closed == 1 && deleted == 1); break;
        case 7:
            assert(open && opened == 3);
            m[0] = 28; m[3] = window.handle;
            m[4] = 40; m[5] = 80; m[6] = 278; m[7] = 278;
            break;
        case 8:
            assert(window.border.x == 40 && window.border.y == 80);
            m[0] = 27; m[3] = window.handle;
            m[6] = 1; m[7] = 1;
            break;
        case 9:
            assert(window.work.w == 220 && window.work.h == 210);
            m[0] = 22; m[3] = 99;
            break;
        case 10:
            assert(open);
            p->o[0] = APP_KEY; p->o[5] = 27;
            break;
        default:
            assert(!open && closed == 2 && deleted == 2);
            longjmp(finished,1);
        }
    } else {
        mock_aes(v);
    }
}
static const struct dc_native_api api = {.aes=clock_aes,.vdi=mock_vdi,.gemdos=clock_gemdos};
const struct dc_native_api *dc_os = &api;
int main(void)
{
    assert(read_clock());
    assert(!strcmp(time_text,"13:42:58") && !strcmp(date_text,"2026-09-28"));
    assert(!read_clock());
    midnight = 1;
    fake_date = (48<<9)|(2<<5)|29;
    fake_time = 0;
    assert(read_clock() && date_reads == 4);
    assert(!strcmp(time_text,"00:00:00") && !strcmp(date_text,"2028-02-29"));
    midnight = 0;
    visible[0] = desktop;
    inspect_drawing = 1;
    if (!setjmp(finished)) app_main(0,NULL);
    assert(step == 12 && !updates && !mouse_hidden && !mouse_control && !palette_writes);
    puts("Clock time, midnight, menu registration, redraw and accessory lifecycle: PASS");
    return 0;
}
