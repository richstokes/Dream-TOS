/* libvterm VT220/xterm state mapped onto the native 80x30 VT52 display.
 * GPL-3.0-or-later. No escape string is passed from the server to GEMDOS. */
#include "terminal.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
static const unsigned char palette[16][3]={
 {0,0,0},{170,0,0},{0,170,0},{170,170,0},{0,0,170},{170,0,170},{0,170,170},{170,170,170},
 {85,85,85},{255,85,85},{85,255,85},{255,255,85},{85,85,255},{255,85,255},{85,255,255},{255,255,255}};
static const unsigned char console_colour[16]={15,1,2,3,4,5,6,7,8,9,10,11,12,13,14,0};
static unsigned char colour(SshTerminal *t,VTermColor c)
{
    vterm_screen_convert_color_to_rgb(t->screen,&c);
    unsigned best=~0u,index=0;
    for(unsigned i=0;i<16;i++) {int r=(int)c.rgb.red-palette[i][0],g=(int)c.rgb.green-palette[i][1],b=(int)c.rgb.blue-palette[i][2];unsigned d=r*r+g*g+b*b;if(d<best){best=d;index=i;}}
    return console_colour[index];
}
static unsigned char glyph(uint32_t c)
{
    if(!c || c==0xffffffffu)return ' ';
    if(c>=32&&c<127)return (unsigned char)c;
    /* Readable fallbacks for Unicode line drawing and symbols in TUIs. */
    if(c==0x2500||c==0x2501||c==0x2550)return '-';
    if(c==0x2502||c==0x2503||c==0x2551)return '|';
    if(c>=0x2500&&c<=0x257f)return '+';
    if(c>=0x2580&&c<=0x259f)return '#';
    if(c==0xa0)return ' ';
    if(c==0x2190)return '<';if(c==0x2192)return '>';if(c==0x2191)return '^';if(c==0x2193)return 'v';
    /* Common Western text mapped to available Atari ST font glyphs. */
    static const uint16_t extended[]={0xc7,0xfc,0xe9,0xe2,0xe4,0xe0,0xe5,0xe7,0xea,0xeb,0xe8,0xef,0xee,0xec,0xc4,0xc5,
        0xc9,0xe6,0xc6,0xf4,0xf6,0xf2,0xfb,0xf9,0xff,0xd6,0xdc,0xa2,0xa3,0xa5,0xdf};
    for(unsigned i=0;i<sizeof(extended)/sizeof(*extended);i++)if(c==extended[i])return 128+i;
    return '?';
}
static SshCell cell(SshTerminal *t,const VTermScreenCell *v)
{
    SshCell c={glyph(v->chars[0]),colour(t,v->fg),colour(t,v->bg)};
    if(v->attrs.reverse){unsigned char tmp=c.fg;c.fg=c.bg;c.bg=tmp;}
    if(v->attrs.conceal)c.ch=' ';
    return c;
}
static void all_dirty(SshTerminal *t){memset(t->dirty,1,sizeof(t->dirty));}
static int damage(VTermRect r,void *p)
{SshTerminal *t=p;for(int y=r.start_row;y<r.end_row&&y<SSH_ROWS;y++)if(y>=0)t->dirty[y]=1;return 1;}
static int cursor(VTermPos pos,VTermPos old,int visible,void *p)
{(void)old;SshTerminal *t=p;t->cursor=pos;t->visible=visible;return 1;}
static int property(VTermProp prop,VTermValue *v,void *p)
{
    SshTerminal *t=p;
    if(prop==VTERM_PROP_CURSORVISIBLE)t->visible=v->boolean;
    if(prop==VTERM_PROP_ALTSCREEN){t->alt=v->boolean;t->offset=0;all_dirty(t);}
    return 1; /* Ignore titles, icon names and other window-manager requests. */
}
static int pushline(int cols,const VTermScreenCell *cells,void *p)
{
    SshTerminal *t=p;if(cols!=SSH_COLS)return 0;
    for(int i=0;i<cols;i++)t->history[t->history_next][i]=cell(t,&cells[i]);
    t->history_next=(t->history_next+1)%SSH_HISTORY;
    if(t->history_count<SSH_HISTORY)t->history_count++;
    if(t->offset){if(t->offset<t->history_count)t->offset++;all_dirty(t);}
    return 1;
}
static int clear_history(void *p)
{SshTerminal *t=p;t->offset=t->history_count=t->history_next=0;all_dirty(t);return 1;}
static void output(const char *p,size_t n,void *user)
{
    SshTerminal *t=user;
    if(n>sizeof(t->output)-t->output_size){t->failed=1;return;}
    memcpy(t->output+t->output_size,p,n);t->output_size+=n;
}
SshTerminal *ssh_terminal_new(void)
{
    SshTerminal *t=calloc(1,sizeof(*t));if(!t)return NULL;
    t->vt=vterm_new(SSH_ROWS,SSH_COLS);if(!t->vt){free(t);return NULL;}
    vterm_set_utf8(t->vt,1);vterm_output_set_callback(t->vt,output,t);
    t->screen=vterm_obtain_screen(t->vt);if(!t->screen){vterm_free(t->vt);free(t);return NULL;}
    VTermColor fg,bg;vterm_color_rgb(&fg,170,170,170);vterm_color_rgb(&bg,0,0,0);
    VTermState *state=vterm_obtain_state(t->vt);
    vterm_state_set_default_colors(state,&fg,&bg);
    for(int i=0;i<16;i++){VTermColor c;vterm_color_rgb(&c,palette[i][0],palette[i][1],palette[i][2]);vterm_state_set_palette_color(state,i,&c);}
    vterm_state_set_bold_highbright(state,1);
    static const VTermScreenCallbacks callbacks={.damage=damage,.movecursor=cursor,.settermprop=property,.sb_pushline=pushline,.sb_clear=clear_history};
    vterm_screen_set_callbacks(t->screen,&callbacks,t);
    vterm_screen_enable_altscreen(t->screen,1);vterm_screen_set_damage_merge(t->screen,VTERM_DAMAGE_ROW);
    vterm_screen_reset(t->screen,1);memset(t->drawn,255,sizeof(t->drawn));all_dirty(t);t->visible=1;
    ssh_print("\033f\033w\033b\007\033c\017\033E");
    return t;
}
void ssh_terminal_free(SshTerminal *t)
{
    if(!t)return;
    vterm_free(t->vt);ssh_wipe(t,sizeof(*t));free(t);
    static const char reset[]="\033b\017\033c\000\033q\033v\033Y= \033K\033e";
    ssh_output(reset,sizeof(reset)-1);
}
void ssh_terminal_feed(SshTerminal *t,const char *p,size_t n)
{vterm_input_write(t->vt,p,n);vterm_screen_flush_damage(t->screen);}
void ssh_terminal_scroll(SshTerminal *t,int rows)
{
    if(t->alt)return;
    int offset=(int)t->offset+rows;if(offset<0)offset=0;if(offset>(int)t->history_count)offset=t->history_count;
    t->offset=offset;all_dirty(t);
}
void ssh_terminal_render(SshTerminal *t)
{
    char buf[2048];size_t n=0;int fg=-1,bg=-1;
    ssh_print("\033f");
    for(int y=0;y<SSH_ROWS;y++) {
        if(!t->dirty[y])continue;t->dirty[y]=0;
        int positioned=0;
        for(int x=0;x<SSH_COLS;x++) {
            SshCell c;
            if((unsigned)y<t->offset) {
                unsigned back=t->offset-y;
                c=t->history[(t->history_next+SSH_HISTORY-back)%SSH_HISTORY][x];
            } else {
                VTermScreenCell v;
                vterm_screen_get_cell(t->screen,(VTermPos){y-(int)t->offset,x},&v);c=cell(t,&v);
            }
            if(!memcmp(&c,&t->drawn[y][x],sizeof(c))){positioned=0;continue;}
            t->drawn[y][x]=c;
            if(n+16>=sizeof(buf)){ssh_output(buf,n);n=0;}
            if(!positioned){buf[n++]=27;buf[n++]='Y';buf[n++]=y+32;buf[n++]=x+32;positioned=1;}
            if(fg!=c.fg){buf[n++]=27;buf[n++]='b';buf[n++]=c.fg;fg=c.fg;}
            if(bg!=c.bg){buf[n++]=27;buf[n++]='c';buf[n++]=c.bg;bg=c.bg;}
            buf[n++]=c.ch;
        }
    }
    if(n)ssh_output(buf,n);
    if(!t->offset&&t->visible) {
        char pos[]={27,'Y',(char)(t->cursor.row+32),(char)(t->cursor.col+32),27,'e'};ssh_output(pos,sizeof(pos));
    }
}
void ssh_terminal_consume(SshTerminal *t,size_t n)
{if(n>t->output_size)n=t->output_size;memmove(t->output,t->output+n,t->output_size-n);t->output_size-=n;}
void ssh_terminal_key(SshTerminal *t,int code,unsigned mods)
{
    unsigned scan=(code>>16)&255,c=code&255;VTermKey key=VTERM_KEY_NONE;
    if(t->offset){t->offset=0;all_dirty(t);}
    switch(scan) {
    case 0x48:key=VTERM_KEY_UP;break;case 0x50:key=VTERM_KEY_DOWN;break;
    case 0x4b:key=VTERM_KEY_LEFT;break;case 0x4d:key=VTERM_KEY_RIGHT;break;
    case 0x47:key=VTERM_KEY_HOME;break;case 0x4f:key=VTERM_KEY_END;break;
    case 0x52:key=VTERM_KEY_INS;break;case 0x53:key=VTERM_KEY_DEL;break;
    case 0x49:key=VTERM_KEY_PAGEUP;break;case 0x51:key=VTERM_KEY_PAGEDOWN;break;
    default:if(scan>=0x3b&&scan<=0x44)key=VTERM_KEY_FUNCTION(scan-0x3a);
        else if(scan==0x57||scan==0x58)key=VTERM_KEY_FUNCTION(scan-0x4c);break;
    }
    if(key==VTERM_KEY_NONE) {
        if(c==13)key=VTERM_KEY_ENTER;else if(c==9)key=VTERM_KEY_TAB;
        else if(c==8||c==127)key=VTERM_KEY_BACKSPACE;else if(c==27)key=VTERM_KEY_ESCAPE;
    }
    if(key!=VTERM_KEY_NONE)vterm_keyboard_key(t->vt,key,(VTermModifier)mods);
    else if(c) {
        /* GEMDOS has already applied Shift/Ctrl to printable input. */
        mods&=~VTERM_MOD_SHIFT;
        if(c<32)mods&=~VTERM_MOD_CTRL;
        vterm_keyboard_unichar(t->vt,c,(VTermModifier)mods);
    }
}
