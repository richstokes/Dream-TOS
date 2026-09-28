#ifndef UTIL_WORM_HEADERS
#define UTIL_WORM_HEADERS

/* Mostly compatability stuff */

#include <string.h>

#ifdef __GNUC__

#include <gem.h>

#define HIWORD(x)  ((int16_t)((uint32_t)x >> 16))
#define LOWORD(x)  ((int16_t)((uint32_t)x & 0xFFFF)) 

#else

#include <portab.h>
#include <aes.h>
#include <vdi.h>

#endif /* __GNUC__ */

#ifndef WF_WXYWH
#define WF_WXYWH    WF_WORKXYWH
#endif

#ifndef WF_CXYWH
#define WF_CXYWH    WF_CURRXYWH
#endif

#define max(x,y)    x>y ? x : y
#define min(x,y)    x<y ? x : y

#ifndef MGEMLIB
static int rc_intersect(const GRECT *one, GRECT *two)
{
WORD tx,ty,tw,th;

    tw = min(one->g_x+one->g_w,two->g_x+two->g_w);
    th = min(one->g_y+one->g_h,two->g_y+two->g_h);
    tx = max(one->g_x,two->g_x);
    ty = max(one->g_y,two->g_y);
    two->g_x = tx;
    two->g_y = ty;
    two->g_w = tw-tx;
    two->g_h = th-ty;
    return ( (tw > tx) && (th > ty) );
}
#endif

static void set_resource_string(OBJECT *tree, short obj, const char *string)
{
long addr;

    /* What a freakin' nightmare... */
    addr = tree[obj].ob_spec.index;
    strcpy((char *)addr, string);
}

#endif /* UTIL_WORM_HEADERS */
