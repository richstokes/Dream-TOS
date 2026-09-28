/* GEM calculator frontend for tinyexpr. GPL-2.0-or-later. */
#include "app.h"
#include "tinyexpr.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
int app_main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "TEST"))
        return fabs(te_interp("sqrt(144)+2^3", 0) - 20) > 1e-9;
    if (!app_begin("CALC - tinyexpr native SH-4 calculator"))
        return 1;
    atexit(app_end);
    char expr[128] = "sqrt(144) + 2^3", answer[128] = "";
    double ans = 0;
    for (;;) {
        app_clear(0);
        app_text(24, 58, "Enter an expression. The last result is available as ans.", 1);
        app_text(24, 88, "+ - * / % ^   sin cos tan sqrt abs log ln exp pi() e()", 1);
        app_text(24, 118, "Trig functions use radians. ^ is left associative.", 1);
        app_text(24, 334, answer, 1);
        app_status("Expression calculator | Esc: exit");
        if (!app_prompt("Expression:", expr, sizeof(expr)))
            break;
        int error;
        te_variable vars[] = {{"ans", &ans, TE_VARIABLE, 0}};
        te_expr *e = te_compile(expr, vars, 1, &error);
        if (!e)
            snprintf(answer, sizeof(answer), "Syntax error at character %d", error);
        else {
            ans = te_eval(e);
            te_free(e);
            snprintf(answer, sizeof(answer), "= %.12g", ans);
        }
    }
    return 0;
}
