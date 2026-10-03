/* Real calculator, tinyexpr and AES/VDI bindings across resident lifecycles. */
#include "gem_model.h"
#include <setjmp.h>
#include "../apps/ports/calc.c"

static jmp_buf finished;
static int step;
static void calc_aes(void *v)
{
    struct aes_pb *p = v;
    if (p->c[0] == 10) {
        mock_aes(v);
        p->g[2] = 2;
    } else if (p->c[0] == 35) {
        assert(p->i[0] == 2 && !strcmp((char *)p->a[0], "  Calculator"));
        p->o[0] = 3;
    } else if (p->c[0] == 25) {
        assert(!updates && !mouse_hidden && !mouse_control && !palette_writes);
        int open = window.handle >= 0;
        assert(p->i[0] == (APP_MESSAGE | (open ? APP_KEY|APP_BUTTON|APP_TIMER : 0)));
        if (open) assert(p->i[14] == 20 && p->i[3] == !(calc.buttons & 1));
        memset(p->o, 0, 16 * sizeof(int16_t));
        int16_t *m = (void *)p->a[0];
        p->o[0] = APP_MESSAGE;
        m[0] = 40; m[4] = 3;
        switch (step++) {
        case 0: assert(!open && !created); m[4] = 99; break;
        case 1: assert(!open && !created); fail_open = 1; break;
        case 2: assert(!open && deleted == 1); fail_open = 0; break;
        case 3: assert(open); p->o[0] = APP_KEY; p->o[5] = '7'; break;
        case 4:
            assert(!strcmp(calc.expr, "7"));
            p->o[0] = APP_KEY; p->o[5] = 13;
            break;
        case 5:
            assert(calc.ans == 7);
            m[0] = 22; m[3] = window.handle;
            break;
        case 6: assert(!open && closed == 1 && calc.ans == 7); break;
        case 7:
            assert(open && calc.ans == 7 && !strcmp(calc.expr, "7"));
            m[0] = 41;
            p->o[0] |= APP_KEY|APP_TIMER|APP_BUTTON;
            p->o[5] = '9'; p->o[3] = 1;
            break;
        default:
            assert(!open && closed == 1 && deleted == 2 && calc.ans == 7);
            assert(!strcmp(calc.expr, "7") && !calc.buttons && calc.pressed == -1);
            longjmp(finished, 1);
        }
    } else {
        mock_aes(v);
    }
}
static const struct dc_native_api api = {.aes = calc_aes, .vdi = mock_vdi};
const struct dc_native_api *dc_os = &api;

static void send_key(int code, int modifiers)
{
    AppEvent e = {.flags = APP_KEY, .key = code, .modifiers = modifiers};
    accessory_event(&e);
}
static void click_key(const char *label)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        if (strcmp(label, keys[i].label)) continue;
        AppEvent e = {.flags = APP_BUTTON, .buttons = 1,
            .x = layout.x + KEY_X + i % COLS * KEY_DX + KEY_W / 2,
            .y = layout.y + KEY_Y + i / COLS * KEY_DY + KEY_H / 2};
        accessory_event(&e);
        e.buttons = 0;
        accessory_event(&e);
        return;
    }
    assert(!"Unknown key");
}
int main(void)
{
    visible[0] = desktop;
    inspect_drawing = 1;
    if (!setjmp(finished)) app_main(0, NULL);
    assert(step == 9);
    AppEvent open = {.flags = APP_MESSAGE, .message = {40, 0, 0, 0, 3}};
    accessory_event(&open);
    int count = created;
    accessory_event(&open);
    assert(created == count); /* Desk selection raises an existing window. */
    click_key("+"); click_key("8"); click_key("=");
    assert(calc.ans == 15);
    AppEvent e = {.flags = APP_MESSAGE, .message = {28, 0, 0, 7, 30, 50, 450, 366}};
    accessory_event(&e);
    assert(layout.x == 31 && layout.y == 70 && layout.w == 432 && layout.h == 328);
    click_key("*"); click_key("2"); click_key("=");
    assert(calc.ans == 30);
    send_key(KEY_RIGHT, 4);
    assert(window.border.x == 46 && layout.x == 47);
    send_key(0x3f00, 0);
    assert(window.full && layout.w == window.work.w);
    send_key(0x3f00, 0);
    assert(!window.full && layout.w == 432);

    /* Covered controls and buttons held in another window cannot activate. */
    find_handle = 9;
    click_key("AC");
    assert(calc.ans == 30);
    find_handle = 7;
    e = (AppEvent){.flags = APP_TIMER, .buttons = 1, .key = 27,
        .x = layout.x + KEY_X + 10, .y = layout.y + KEY_Y + 10};
    accessory_event(&e);
    assert(calc.pressed == -1 && !calc.buttons && window.handle >= 0);
    e.buttons = 0;
    accessory_event(&e);
    assert(calc.ans == 30);

    /* A shell reset cancels a half-click and retains the previous answer. */
    e.flags = APP_BUTTON; e.buttons = 1;
    accessory_event(&e);
    assert(calc.pressed == 0);
    int before = closed;
    AppEvent shell = {.flags = APP_MESSAGE|APP_KEY, .key = '9', .message = {41}};
    accessory_event(&shell);
    assert(window.handle < 0 && closed == before && calc.pressed == -1);
    accessory_event(&open);
    e.buttons = 0;
    accessory_event(&e);
    assert(calc.ans == 30);
    send_key(27, 0);
    assert(window.handle < 0 && closed == before + 1);
    send_key('9', 0);
    assert(calc.ans == 30 && !strcmp(calc.expr, "ans*2"));
    accessory_event(&open);
    send_key('+', 0); send_key('2', 0); send_key(13, 0);
    assert(calc.ans == 32);
    assert(!updates && !mouse_hidden && !mouse_control && !palette_writes);
    puts("Calculator accessory registration, sleep, persistence, input and app switches: PASS");
    return 0;
}
