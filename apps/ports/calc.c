/* Graphical GEM calculator frontend for tinyexpr. GPL-2.0-or-later. */
#include "app.h"
#include "window.h"
#include "tinyexpr.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define COLS 6
#define ROWS 5
#define KEY_COUNT (COLS * ROWS)
static AppWindow window = {.handle = -1};
static int menu_id;
static struct { int x, y, w, h, dx, dy, chars; } layout = {0, 0, 480, 360, 76, 40, 54};
#define KEY_X 10
#define KEY_Y 124
#define KEY_W (layout.dx - 6)
#define KEY_H (layout.dy - 6)
#define KEY_DX layout.dx
#define KEY_DY layout.dy
#define DISPLAY_CHARS layout.chars

enum { INSERT, CLEAR, BACKSPACE, NEGATE, EQUALS };
/* Standard GEM logical colours: a window must share the desktop palette. */
enum { PAPER = 0, INK = 1, BACKGROUND = 8, FACE = 8, EDGE = 9,
       BLUE = 4, GREEN = 11, RED = 2, DISPLAY = 0, FOCUS = 6 };
static const struct button {
    const char *label, *input;
    int action, colour;
} keys[KEY_COUNT] = {
    {"AC", "", CLEAR, RED}, {"DEL", "", BACKSPACE, FACE},
    {"(", "(", INSERT, FACE}, {")", ")", INSERT, FACE},
    {"ans", "ans", INSERT, FACE}, {"/", "/", INSERT, BLUE},
    {"sin", "sin(", INSERT, FACE}, {"cos", "cos(", INSERT, FACE},
    {"7", "7", INSERT, PAPER}, {"8", "8", INSERT, PAPER},
    {"9", "9", INSERT, PAPER}, {"*", "*", INSERT, BLUE},
    {"tan", "tan(", INSERT, FACE}, {"sqrt", "sqrt(", INSERT, FACE},
    {"4", "4", INSERT, PAPER}, {"5", "5", INSERT, PAPER},
    {"6", "6", INSERT, PAPER}, {"-", "-", INSERT, BLUE},
    {"ln", "ln(", INSERT, FACE}, {"log", "log(", INSERT, FACE},
    {"1", "1", INSERT, PAPER}, {"2", "2", INSERT, PAPER},
    {"3", "3", INSERT, PAPER}, {"+", "+", INSERT, BLUE},
    {"pi", "pi", INSERT, FACE}, {"x^y", "^", INSERT, FACE},
    {"+/-", "", NEGATE, FACE}, {"0", "0", INSERT, PAPER},
    {".", ".", INSERT, PAPER}, {"=", "", EQUALS, GREEN}
};
static struct {
    char expr[128], result[48], message[96];
    double ans;
    int cursor, evaluated, error, focus, pressed, buttons, hover;
} calc;

static void reset(void)
{
    memset(&calc, 0, sizeof(calc));
    strcpy(calc.result, "0");
    calc.focus = calc.pressed = calc.hover = -1;
}

static void changed(void)
{
    calc.evaluated = calc.error = 0;
    calc.message[0] = 0;
}

static void insert(const char *text)
{
    if (calc.evaluated) {
        /* Continue with the full-precision answer, not the rounded display. */
        strcpy(calc.expr, strchr("+-*/%^", text[0]) ? "ans" : "");
        calc.cursor = strlen(calc.expr);
    }
    size_t len = strlen(calc.expr), n = strlen(text);
    if (len + n >= sizeof(calc.expr)) {
        strcpy(calc.message, "Expression full: delete some characters first.");
        return;
    }
    memmove(calc.expr + calc.cursor + n, calc.expr + calc.cursor, len - calc.cursor + 1);
    memcpy(calc.expr + calc.cursor, text, n);
    calc.cursor += n;
    changed();
}

static void erase(int forward)
{
    int len = strlen(calc.expr);
    if ((forward && calc.cursor < len) || (!forward && calc.cursor > 0)) {
        if (!forward)
            calc.cursor--;
        memmove(calc.expr + calc.cursor, calc.expr + calc.cursor + 1, len - calc.cursor);
    }
    changed();
}

static void evaluate(void)
{
    if (!calc.expr[0])
        return;
    int error;
    te_variable vars[] = {{"ans", &calc.ans, TE_VARIABLE, 0}};
    te_expr *e = te_compile(calc.expr, vars, 1, &error);
    if (!e) {
        snprintf(calc.message, sizeof(calc.message), "Syntax error at character %d", error);
        calc.cursor = error > 0 ? error - 1 : 0;
        if (calc.cursor > (int)strlen(calc.expr))
            calc.cursor = strlen(calc.expr);
    } else {
        double value = te_eval(e);
        te_free(e);
        if (isfinite(value)) {
            calc.ans = value;
            snprintf(calc.result, sizeof(calc.result), "%.12g", value);
            calc.message[0] = 0;
            calc.error = 0;
            calc.evaluated = 1;
            calc.cursor = strlen(calc.expr);
            return;
        }
        strcpy(calc.message, "Math error: check the range or division by zero.");
    }
    /* Invalid expressions never destroy the last valid answer. */
    calc.error = 1;
    calc.evaluated = 0;
}

static void negate(void)
{
    if (calc.evaluated) {
        strcpy(calc.expr, "-ans");
        evaluate();
    } else {
        size_t n = strlen(calc.expr);
        if (n + 3 >= sizeof(calc.expr)) {
            strcpy(calc.message, "Expression full: cannot change its sign.");
            return;
        }
        if (!n) {
            insert("-");
            return;
        }
        memmove(calc.expr + 2, calc.expr, n);
        calc.expr[0] = '-';
        calc.expr[1] = '(';
        calc.expr[n + 2] = ')';
        calc.expr[n + 3] = 0;
        calc.cursor = n + 3;
        changed();
    }
}

static void activate(int i)
{
    switch (keys[i].action) {
    case CLEAR: {
        int focus = calc.focus, hover = calc.hover;
        reset();
        calc.focus = focus;
        calc.hover = hover;
        break;
    }
    case BACKSPACE: erase(0); break;
    case NEGATE: negate(); break;
    case EQUALS: evaluate(); break;
    default: insert(keys[i].input); break;
    }
}

static int hit_key(int x, int y)
{
    x -= KEY_X;
    y -= KEY_Y;
    if (x < 0 || y < 0 || x >= COLS * KEY_DX || y >= ROWS * KEY_DY ||
        x % KEY_DX >= KEY_W || y % KEY_DY >= KEY_H)
        return -1;
    return (y / KEY_DY) * COLS + x / KEY_DX;
}

static int display_start(void)
{
    return calc.cursor >= DISPLAY_CHARS ? calc.cursor - DISPLAY_CHARS + 1 : 0;
}

/* One shared event path for actual Maple input and host interaction tests. */
static int event(int key, int x, int y, int buttons)
{
    int oldhover = calc.hover, oldbuttons = calc.buttons;
    calc.hover = hit_key(x, y);
    calc.buttons = buttons;
    int dirty = oldhover != calc.hover;
    if ((buttons & 1) && !(oldbuttons & 1)) {
        calc.pressed = calc.hover;
        if (calc.hover >= 0)
            calc.focus = calc.hover;
        else if (x >= 18 && x < layout.w - 18 && y >= 30 && y < 54) {
            calc.focus = -1;
            calc.cursor = display_start() + (x - 22 + 4) / 8;
            if (calc.cursor < 0)
                calc.cursor = 0;
            if (calc.cursor > (int)strlen(calc.expr))
                calc.cursor = strlen(calc.expr);
            calc.evaluated = 0;
        }
        dirty = 1;
    }
    if (!(buttons & 1) && (oldbuttons & 1)) {
        int pressed = calc.pressed;
        calc.pressed = -1;
        if (pressed >= 0 && pressed == calc.hover)
            activate(pressed);
        dirty = 1;
    }
    if (!key)
        return dirty;
    int ch = key & 255, scan = (key >> 8) & 255;
    if (ch == 27)
        return -1;
    if (ch == 9 || scan == 15) {
        calc.focus = (calc.focus + 1) % KEY_COUNT;
    } else if (ch == 21) {
        reset();
    } else if (ch == '=' || ch == 13) {
        evaluate();
    } else if (calc.focus >= 0 && ch == ' ') {
        activate(calc.focus);
    } else if (calc.focus >= 0 && (scan == 72 || scan == 80 || scan == 75 || scan == 77)) {
        int row = calc.focus / COLS, col = calc.focus % COLS;
        row = (row + (scan == 72 ? ROWS - 1 : scan == 80 ? 1 : 0)) % ROWS;
        col = (col + (scan == 75 ? COLS - 1 : scan == 77 ? 1 : 0)) % COLS;
        calc.focus = row * COLS + col;
    } else if (scan == 75 || scan == 77 || scan == 71 || scan == 79) {
        int len = strlen(calc.expr);
        calc.focus = -1;
        if (scan == 71) calc.cursor = 0;
        if (scan == 79) calc.cursor = len;
        if (scan == 75 && calc.cursor) calc.cursor--;
        if (scan == 77 && calc.cursor < len) calc.cursor++;
        calc.evaluated = 0;
    } else if (ch == 8 || ch == 127) {
        calc.focus = -1;
        erase(ch == 127);
    } else if (ch >= 32 && ch < 127) {
        char s[] = {ch, 0};
        calc.focus = -1;
        insert(s);
    }
    return 1;
}

static void resize_layout(void)
{
    layout.x = window.work.x;
    layout.y = window.work.y;
    layout.w = window.work.w;
    layout.h = window.work.h;
    layout.dx = (layout.w - 20) / COLS;
    layout.dy = (layout.h - KEY_Y - 36) / ROWS;
    layout.chars = (layout.w - 44) / 8;
    calc.hover = calc.pressed = -1;
}
static void box(int x, int y, int w, int h, int colour)
{
    app_box(layout.x + x, layout.y + y, w, h, colour);
}
static void text(int x, int y, const char *s, int colour)
{
    app_text(layout.x + x, layout.y + y, s, colour);
}
static void line(int x, int y, int x2, int y2, int colour)
{
    app_line(layout.x + x, layout.y + y, layout.x + x2, layout.y + y2, colour);
}
static void border(int x, int y, int w, int h, int colour)
{
    line(x, y, x + w - 1, y, colour);
    line(x, y, x, y + h - 1, colour);
    line(x + w - 1, y, x + w - 1, y + h - 1, colour);
    line(x, y + h - 1, x + w - 1, y + h - 1, colour);
}
/* Called once per visible rectangle, with the GEM clipping region in force. */
static void draw(void)
{
    box(0, 0, layout.w, layout.h, BACKGROUND);
    text(10, 18, "SCIENTIFIC", INK);
    text(layout.w - 130, 18, "RAD / 12 DIGITS", BLUE);
    box(10, 26, layout.w - 20, 70, DISPLAY);
    border(10, 26, layout.w - 20, 70, EDGE);
    line(11, 27, layout.w - 12, 27, INK);
    line(11, 27, 11, 94, INK);
    char visible[128];
    snprintf(visible, DISPLAY_CHARS + 1, "%s", calc.expr + display_start());
    text(22, 46, visible, INK);
    if (display_start())
        text(12, 46, "<", EDGE);
    if (calc.focus < 0) {
        int x = 22 + (calc.cursor - display_start()) * 8;
        line(x, 50, x + 6, 50, BLUE);
    }
    const char *result = calc.error ? "Error" : calc.result;
    vp[0] = 0;
    vp[1] = 26;
    vdi_call(12, 1, 0);
    text(layout.w - 24 - (int)strlen(result) * 16, 88, result, calc.error ? RED : INK);
    vp[0] = 0;
    vp[1] = 13;
    vdi_call(12, 1, 0);
    text(10, 116, calc.message[0] ? calc.message : "ans: last answer   AC: clear   Esc: close",
         calc.error ? RED : INK);
    for (int i = 0; i < KEY_COUNT; i++) {
        int x = KEY_X + (i % COLS) * KEY_DX, y = KEY_Y + (i / COLS) * KEY_DY;
        int down = calc.pressed == i && calc.hover == i;
        int colour = keys[i].colour;
        box(x + 2, y + 2, KEY_W, KEY_H, EDGE);
        box(x, y, KEY_W, KEY_H, colour);
        border(x, y, KEY_W, KEY_H, INK);
        line(x + 1, y + 1, x + KEY_W - 2, y + 1, down ? EDGE : PAPER);
        line(x + 1, y + 1, x + 1, y + KEY_H - 2, down ? EDGE : PAPER);
        line(x + 1, y + KEY_H - 2, x + KEY_W - 2, y + KEY_H - 2, down ? PAPER : EDGE);
        line(x + KEY_W - 2, y + 1, x + KEY_W - 2, y + KEY_H - 2, down ? PAPER : EDGE);
        if (calc.focus == i || calc.hover == i)
            border(x + 4, y + 4, KEY_W - 8, KEY_H - 8, FOCUS);
        text(x + (KEY_W - (int)strlen(keys[i].label) * 8) / 2 + down,
             y + (KEY_H + 13) / 2 + down, keys[i].label,
             colour == BLUE || colour == RED ? PAPER : INK);
    }
    text(10, layout.h - 20, "Tab/arrows: keypad  Space: press  Enter: =", INK);
    text(10, layout.h - 4, "Ctrl+arrows: move  +Shift: size  F5: full", INK);
}
static int window_key(int key, int modifiers)
{
    int scan = (key >> 8) & 255;
    if (scan == 0x3f) { /* F5: GEM fuller box, accessible from the keyboard. */
        app_window_full(&window);
    } else if ((modifiers & 4) && (scan == 72 || scan == 80 || scan == 75 || scan == 77)) {
        AppRect bounds = window.border;
        int dx = scan == 75 ? -16 : scan == 77 ? 16 : 0;
        int dy = scan == 72 ? -16 : scan == 80 ? 16 : 0;
        if (modifiers & 3) {
            bounds.w += dx;
            bounds.h += dy;
        } else {
            bounds.x += dx;
            bounds.y += dy;
        }
        app_window_bounds(&window, bounds);
        window.full = 0;
    } else {
        return 0;
    }
    resize_layout();
    return 1;
}
/* Return true when the user wants to hide the accessory window. */
static int window_event(const AppEvent *e)
{
    int redraw = 0;
    if (e->flags & APP_MESSAGE) {
        int result = app_window_message(&window, e->message);
        if (result == WINDOW_CLOSE)
            return 1;
        if (result == WINDOW_CHANGED) {
            resize_layout();
            redraw = 1;
        } else if (result == WINDOW_REDRAW) {
            AppRect damage = window.work;
            if (e->message[0] == 20)
                damage = (AppRect){e->message[4], e->message[5], e->message[6], e->message[7]};
            app_window_redraw(&window, damage, draw);
        }
    }
    int key = (e->flags & APP_KEY) ? e->key : 0;
    if (window_key(key, e->modifiers)) {
        redraw = 1;
    } else if (e->flags & (APP_KEY | APP_BUTTON | APP_TIMER)) {
        int inside = app_window_contains(&window, e->x, e->y);
        /* Timer samples update hover, but must not turn another window's
         * mouse press into a calculator click. */
        int buttons = (e->flags & APP_BUTTON) ? e->buttons : e->buttons & calc.buttons;
        int result = event(key, inside ? e->x - layout.x : -1,
                           inside ? e->y - layout.y : -1, buttons);
        if (result < 0)
            return 1;
        redraw |= result;
    }
    if (redraw)
        app_window_redraw(&window, window.work, draw);
    return 0;
}
static void accessory_event(const AppEvent *e)
{
    if (e->flags & APP_MESSAGE) {
        if (e->message[0] == 41) { /* AC_CLOSE: AES already discarded the window. */
            window.handle = -1;
            calc.buttons = 0;
            calc.pressed = calc.hover = -1;
            return;
        }
        if (e->message[0] == 40 && e->message[4] == menu_id) { /* AC_OPEN */
            if (window.handle < 0) {
                if (!app_window_open(&window, "Calculator", 480, 360, 432, 328))
                    return;
                calc.buttons = 0;
                resize_layout();
            } else {
                int16_t top[8] = {21, 0, 0, window.handle};
                app_window_message(&window, top);
            }
            app_window_redraw(&window, window.work, draw);
            return;
        }
    }
    if (window.handle >= 0 && window_event(e)) {
        app_window_close(&window);
        calc.buttons = 0;
        calc.pressed = calc.hover = -1;
    }
}
int app_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!app_begin_windowed())
        return 1;
    reset();
    ai[0] = ag[2];
    aa[0] = (intptr_t)"  Calculator";
    aes_call(35, 1, 1, 1); /* menu_register */
    menu_id = ao[0];
    if (menu_id < 0) {
        app_end();
        return 1;
    }
    for (;;) {
        AppEvent e = {0};
        if (window.handle >= 0) {
            app_window_event(&e, 20, calc.buttons);
        } else {
            memset(ai, 0, sizeof(ai));
            ai[0] = APP_MESSAGE; /* No timer or input polling while hidden. */
            aa[0] = (intptr_t)e.message;
            aes_call(25, 16, 7, 1);
            e.flags = ao[0];
        }
        accessory_event(&e);
    }
}
