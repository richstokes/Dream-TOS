#include "tinyexpr.h"
#include <assert.h>
#include <math.h>
int main(void)
{
    int err;
    assert(te_interp("sqrt(144)+2^3", &err) == 20 && err == 0);
    double ans = 20;
    te_variable v = {"ans", &ans, TE_VARIABLE, 0};
    te_expr *e = te_compile("ans*2+cos(0)", &v, 1, &err);
    assert(e && te_eval(e) == 41);
    te_free(e);
    assert(!te_compile("2+*3", 0, 0, &err) && err > 0);
    return 0;
}
