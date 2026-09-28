/* Native libc / filesystem smoke test, including real SH-4 double math. */
#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
static int test(void)
{
    char *p = calloc(300, 4);
    if (!p)
        return 1;
    for (int i = 0; i < 1200; i++)
        if (p[i])
            return 2;
    strcpy(p, "Native SH-4 stdio round trip\n");
    char *q = realloc(p, 3000);
    if (!q)
        return 3;
    FILE *f = fopen("C:\\LIBCTEST.TXT", "wb");
    if (!f)
        return 4;
    if (fwrite(q, 1, strlen(q), f) != strlen(q) || fclose(f))
        return 5;
    char buf[128];
    f = fopen("C:\\LIBCTEST.TXT", "rb");
    if (!f)
        return 6;
    if (!fgets(buf, sizeof(buf), f) || strcmp(buf, q))
        return 7;
    if (fseek(f, 7, SEEK_SET) || ftell(f) != 7)
        return 8;
    if (fclose(f))
        return 9;
    free(q);
    f = fopen("D:\\README.TXT", "wb");
    if (f || errno != EROFS)
        return 10;
    volatile double x = 144.0;
    snprintf(buf, sizeof(buf), "%.2f", sqrt(x) + pow(2.0, 3.0));
    if (strcmp(buf, "20.00"))
        return 11;
    int n = 0;
    if (sscanf("12345", "%d", &n) != 1 || n != 12345)
        return 12;
    /* Multiple blocks exercise ownership, fragmentation and realloc copies. */
    void *blocks[48];
    for (int i = 0; i < 48; i++) {
        blocks[i] = malloc(1024 + i * 16);
        if (!blocks[i])
            return 13;
        memset(blocks[i], i, 1024 + i * 16);
    }
    for (int i = 0; i < 48; i++) {
        if (((unsigned char *)blocks[i])[100] != i)
            return 14;
        free(blocks[i]);
    }
    if (unlink("C:\\LIBCTEST.TXT"))
        return 15;
    f = fopen("C:\\LIBCTEST.TXT", "rb");
    if (f || errno != ENOENT)
        return 16;
    return 0;
}
int app_main(int argc, char **argv)
{
    int r = test();
    if (argc > 1 && !strcmp(argv[1], "TEST"))
        return r;
    if (app_begin("Native runtime diagnostic")) {
        atexit(app_end);
        char msg[100];
        snprintf(msg, sizeof(msg), "Native libc, allocation, math, files: %s (code %d)",
                 r ? "FAIL" : "PASS", r);
        app_alert(msg);
    }
    return r;
}
