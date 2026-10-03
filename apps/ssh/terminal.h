#ifndef DC_SSH_TERMINAL_H
#define DC_SSH_TERMINAL_H
#include <stddef.h>
#include <stdint.h>
#include <vterm.h>
#define SSH_COLS 80
#define SSH_ROWS 30
#define SSH_HISTORY 256
typedef struct {unsigned char ch,fg,bg;} SshCell;
typedef struct {
    VTerm *vt;VTermScreen *screen;
    SshCell drawn[SSH_ROWS][SSH_COLS],history[SSH_HISTORY][SSH_COLS];
    unsigned history_count,history_next,offset;
    unsigned char dirty[SSH_ROWS];
    char output[8192];size_t output_size;
    VTermPos cursor;int visible,failed,alt;
} SshTerminal;
SshTerminal *ssh_terminal_new(void);
void ssh_terminal_free(SshTerminal *);
void ssh_terminal_feed(SshTerminal *,const char *,size_t);
void ssh_terminal_render(SshTerminal *);
void ssh_terminal_key(SshTerminal *,int,unsigned mods);
void ssh_terminal_scroll(SshTerminal *,int rows);
void ssh_terminal_consume(SshTerminal *,size_t);
#endif
