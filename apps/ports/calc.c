/* Graphical GEM calculator frontend for tinyexpr. GPL-2.0-or-later. */
#include "app.h"
#include "tinyexpr.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define COLS 6
#define ROWS 5
#define KEY_COUNT (COLS * ROWS)
#define KEY_X 40
#define KEY_Y 182
#define KEY_W 86
#define KEY_H 42
#define KEY_DX 94
#define KEY_DY 48
#define DISPLAY_CHARS 65

enum { INSERT, CLEAR, BACKSPACE, NEGATE, EQUALS };
enum { PAPER, INK, BACKGROUND, FACE, EDGE, BLUE, GREEN, RED, DISPLAY, FOCUS };
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
        else if (x >= 48 && x < 592 && y >= 70 && y < 98) {
            calc.focus = -1;
            calc.cursor = display_start() + (x - 56 + 4) / 8;
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

static void border(int x, int y, int w, int h, int colour)
{
    app_line(x, y, x + w - 1, y, colour);
    app_line(x, y, x, y + h - 1, colour);
    app_line(x + w - 1, y, x + w - 1, y + h - 1, colour);
    app_line(x, y + h - 1, x + w - 1, y + h - 1, colour);
}

static void draw(int full)
{
    static int last_focus = -1, last_hover = -1, last_pressed = -1;
    if (full) {
        app_clear(BACKGROUND);
        app_text(40, 45, "SCIENTIFIC CALCULATOR", INK);
        app_text(408, 45, "RAD / 12 DIGITS", BLUE);
        app_text(40, 438, "Tab/arrows: keypad   Space: press   Ctrl+U: clear", INK);
        app_status("Type or click | Enter/= calculate | Esc quit");
    }
    app_box(40, 56, 556, 92, DISPLAY);
    border(40, 56, 556, 92, EDGE);
    app_line(41, 57, 594, 57, INK);
    app_line(41, 57, 41, 146, INK);
    char visible[DISPLAY_CHARS + 1];
    snprintf(visible, sizeof(visible), "%s", calc.expr + display_start());
    app_text(56, 85, visible, INK);
    if (display_start())
        app_text(44, 85, "<", EDGE);
    if (calc.focus < 0) {
        int x = 56 + (calc.cursor - display_start()) * 8;
        app_line(x, 89, x + 6, 89, BLUE);
    }
    const char *result = calc.error ? "Error" : calc.result;
    vp[0] = 0;
    vp[1] = 26;
    vdi_call(12, 1, 0);
    app_text(580 - (int)strlen(result) * 16, 134, result, calc.error ? RED : INK);
    vp[0] = 0;
    vp[1] = 13;
    vdi_call(12, 1, 0);
    app_box(40, 150, 556, 26, BACKGROUND);
    app_text(40, 168, calc.message[0] ? calc.message : "ans recalls the last answer.  AC clears all.",
             calc.error ? RED : INK);
    for (int i = 0; i < KEY_COUNT; i++) {
        if (!full && i != calc.focus && i != calc.hover && i != calc.pressed &&
            i != last_focus && i != last_hover && i != last_pressed)
            continue;
        int x = KEY_X + (i % COLS) * KEY_DX, y = KEY_Y + (i / COLS) * KEY_DY;
        int down = calc.pressed == i && calc.hover == i;
        int colour = keys[i].colour;
        app_box(x + 2, y + 2, KEY_W, KEY_H, EDGE);
        app_box(x, y, KEY_W, KEY_H, colour);
        border(x, y, KEY_W, KEY_H, INK);
        app_line(x + 1, y + 1, x + KEY_W - 2, y + 1, down ? EDGE : PAPER);
        app_line(x + 1, y + 1, x + 1, y + KEY_H - 2, down ? EDGE : PAPER);
        app_line(x + 1, y + KEY_H - 2, x + KEY_W - 2, y + KEY_H - 2, down ? PAPER : EDGE);
        app_line(x + KEY_W - 2, y + 1, x + KEY_W - 2, y + KEY_H - 2, down ? PAPER : EDGE);
        if (calc.focus == i || calc.hover == i)
            border(x + 4, y + 4, KEY_W - 8, KEY_H - 8, FOCUS);
        app_text(x + (KEY_W - (int)strlen(keys[i].label) * 8) / 2 + down,
                 y + 26 + down, keys[i].label, colour >= BLUE && colour <= RED ? PAPER : INK);
    }
    last_focus = calc.focus;
    last_hover = calc.hover;
    last_pressed = calc.pressed;
}

int app_main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "TEST"))
        return fabs(te_interp("sqrt(144)+2^3", 0) - 20) > 1e-9;
    if (!app_begin("CALCULATOR"))
        return 1;
    atexit(app_end);
    static const int palette[][3] = {
        {1000, 1000, 1000}, {0, 0, 0}, {800, 820, 820}, {920, 920, 880},
        {400, 440, 440}, {80, 230, 550}, {80, 430, 290}, {650, 120, 100},
        {950, 1000, 910}, {1000, 630, 0}
    };
    for (int i = 0; i < (int)(sizeof(palette) / sizeof(palette[0])); i++)
        app_palette(i, palette[i][0], palette[i][1], palette[i][2]);
    reset();
    draw(1);
    app_mouse(1);
    for (;;) {
        int x, y, buttons;
        int key = app_event(20, &x, &y, &buttons);
        int redraw = event(key, x, y, buttons);
        if (redraw < 0)
            break;
        if (redraw) {
            app_mouse(0);
            draw(0);
            app_mouse(1);
        }
    }
    app_mouse(0); /* Balance app_end's final show. */
    return 0;
}
