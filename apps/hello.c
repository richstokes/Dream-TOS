/* A real, separately loaded native SH-4 GEM application. */
#include "dreamcast/native.h"
static int16_t control[5], global[16], intin[16], intout[16];
static intptr_t addrin[8], addrout[2];
static struct {
    int16_t *control, *global, *intin, *intout;
    intptr_t *addrin, *addrout;
} pb = {control, global, intin, intout, addrin, addrout};
static void aes(const struct dc_native_api *os, int op, int ni, int no, int na)
{
    control[0] = op;
    control[1] = ni;
    control[2] = no;
    control[3] = na;
    control[4] = 0;
    os->aes(&pb);
}
long dc_app_main(const struct dc_native_api *os, const char *tail, const char *env)
{
    (void)env;
    if (os->version != DC_NATIVE_ABI)
        return -32;
    /* Exercise BSS, initialized pointers and code/data relocations. */
    if (global[0] || intin[0])
        return -66;
    long h = os->gemdos(0x3c, "C:\\NATIVE.TXT", 0);
    static const char message[] = "Written by native SH-4 code using GEMDOS.\r\n";
    if (h < 0)
        return h;
    long result = os->gemdos(0x40, (int)h, (long)sizeof(message) - 1, message);
    os->gemdos(0x3e, (int)h);
    if (result != sizeof(message) - 1)
        return -10;
    /* The boot test uses a length-prefixed TEST command line. */
    if (tail && (unsigned char)tail[0] == 4 && tail[1] == 'T')
        return 42;
    aes(os, 10, 0, 1, 0);
    intin[0] = 1;
    addrin[0] = (intptr_t)"[1][Native SH-4 application|GEM AES and GEMDOS work!|Created "
                          "C:\\NATIVE.TXT][OK]";
    aes(os, 52, 1, 1, 1);
    aes(os, 19, 0, 1, 0);
    return 0;
}
