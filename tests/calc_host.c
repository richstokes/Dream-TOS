/* Exercise the graphical calculator's input path, not just its math library. */
#include "../apps/ports/calc.c"
#include <assert.h>

static void type(const char *s)
{
    while (*s)
        event((unsigned char)*s++, 0, 0, 0);
}
static void click(const char *label)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        if (strcmp(keys[i].label, label))
            continue;
        int x = KEY_X + (i % COLS) * KEY_DX + KEY_W / 2;
        int y = KEY_Y + (i / COLS) * KEY_DY + KEY_H / 2;
        event(0, x, y, 1);
        event(0, x, y, 0);
        return;
    }
    assert(!"Unknown button");
}
int main(void)
{
    reset();
    /* A complete calculation from pointer events, followed by chaining. */
    click("7"); click("+"); click("8"); click("=");
    assert(calc.ans == 15 && !strcmp(calc.result, "15"));
    click("*"); click("2");
    event(13, 0, 0, 0); /* Return calculates even when a keypad button has focus. */
    assert(calc.ans == 30 && !strcmp(calc.expr, "ans*2"));
    click("+/-");
    assert(calc.ans == -30);
    click("9"); click("=");
    assert(calc.ans == 9); /* Digits after equals start a new calculation. */

    click("AC");
    assert(calc.ans == 0 && !calc.expr[0] && !strcmp(calc.result, "0"));
    click("sqrt"); click("1"); click("4"); click("4"); click(")");
    click("+"); click("2"); click("x^y"); click("3"); click("=");
    assert(calc.ans == 20);
    type("ans*2+cos(0)"); event(13, 0, 0, 0);
    assert(calc.ans == 41);

    /* Errors preserve ans; editing and reevaluation recover in the same view. */
    type("2+*3"); event(13, 0, 0, 0);
    assert(calc.error && calc.ans == 41 && strstr(calc.message, "Syntax error"));
    event(KEY_DELETE, 0, 0, 0);
    assert(!strcmp(calc.expr, "2+3"));
    event(13, 0, 0, 0);
    assert(!calc.error && calc.ans == 5);
    type("1/0"); event(13, 0, 0, 0);
    assert(calc.error && calc.ans == 5 && strstr(calc.message, "Math error"));
    event(21, 0, 0, 0); type("sqrt(-1)"); event(13, 0, 0, 0);
    assert(calc.error && calc.ans == 0);

    /* Keyboard-only keypad navigation: Tab -> AC; Down/Right/Right -> 7. */
    reset();
    event(9, 0, 0, 0);
    event(KEY_DOWN, 0, 0, 0);
    event(KEY_RIGHT, 0, 0, 0);
    event(KEY_RIGHT, 0, 0, 0);
    event(' ', 0, 0, 0);
    assert(!strcmp(calc.expr, "7"));
    event(13, 0, 0, 0);
    assert(calc.ans == 7);

    /* Click-drag off a key cancels, and gaps are not active hit areas. */
    reset();
    int x = KEY_X + 2 * KEY_DX + 10, y = KEY_Y + KEY_DY + 10;
    event(0, x, y, 1);
    event(0, x + KEY_DX, y, 1);
    event(0, x + KEY_DX, y, 0);
    assert(!calc.expr[0]);
    event(0, KEY_X + KEY_W + 1, KEY_Y + 10, 1);
    event(0, KEY_X + KEY_W + 1, KEY_Y + 10, 0);
    assert(!calc.expr[0]);

    /* Mid-expression editing and long input scrolling remain bounded. */
    type("123");
    event(KEY_LEFT, 0, 0, 0); event(8, 0, 0, 0); type("0");
    assert(!strcmp(calc.expr, "103"));
    event(KEY_HOME, 0, 0, 0); event(KEY_DELETE, 0, 0, 0);
    assert(!strcmp(calc.expr, "03"));
    event(KEY_END, 0, 0, 0); type("+4"); event(13, 0, 0, 0);
    assert(calc.ans == 7);
    reset();
    for (int i = 0; i < 160; i++) type("1");
    assert(strlen(calc.expr) == 127 && display_start() > 0);
    click("+/-");
    assert(strlen(calc.expr) == 127 && strstr(calc.message, "full"));
    event(KEY_HOME, 0, 0, 0);
    assert(display_start() == 0);
    for (int i = 0; i < 128; i++) event(KEY_DELETE, 0, 0, 0);
    assert(!calc.expr[0]);
    reset(); type("2+3"); click("+/-"); click("=");
    assert(calc.ans == -5);
    assert(event(27, 0, 0, 0) == -1);
    puts("Graphical calculator buttons, keyboard, errors and editing: PASS");
    return 0;
}
