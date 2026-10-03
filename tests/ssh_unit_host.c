/* Terminal, trust store and seed failures under ASan/UBSan. */
#include "platform.h"
#include "terminal.h"
#include "known_hosts.h"
#include "seed.h"
#include <wolfssl/wolfcrypt/hash.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static const char *input;
static int allow_sampling;
static uint32_t ticks;
static char output[32768];static size_t written;
uint32_t ssh_now(void){return ticks;}
uint32_t ssh_wallclock(void){return 0x5d410123;}
void ssh_make_parent(const char*p){char dir[260];const char*last=strrchr(p,'/');if(!last||last==p||(size_t)(last-p)>=sizeof(dir))return;memcpy(dir,p,last-p);dir[last-p]=0;mkdir(dir,0700);}
void ssh_idle(void){assert(allow_sampling);ticks++;}
int ssh_key(void){return input&&*input?(unsigned char)*input++:0;}
unsigned ssh_modifiers(void){return 0;}
void ssh_output(const char*p,size_t n){if(written+n<sizeof(output)){memcpy(output+written,p,n);written+=n;output[written]=0;}}
int ssh_network_ready(void){return 0;}
int ssh_resolve(const char*h,uint8_t ip[4]){(void)h;(void)ip;return -1;}
long ssh_tcp_connect(const uint8_t ip[4],unsigned p){(void)ip;(void)p;return -1;}
long ssh_tcp_connected(int h){(void)h;return -1;}
long ssh_tcp_read(int h,void*p,unsigned n){(void)h;(void)p;(void)n;return -1;}
long ssh_tcp_write(int h,const void*p,unsigned n){(void)h;(void)p;(void)n;return -1;}
void ssh_tcp_close(int h){(void)h;}
char ssh_default_drive(void){return 'C';}
static uint32_t at(SshTerminal*t,int y,int x){VTermScreenCell c;assert(vterm_screen_get_cell(t->screen,(VTermPos){y,x},&c));return c.chars[0];}
static void feed(SshTerminal*t,const char*s){ssh_terminal_feed(t,s,strlen(s));}
static void terminal(void)
{
    SshTerminal*t=ssh_terminal_new();assert(t);
    feed(t,"hello\033[2;3H\033[31;44mX");assert(at(t,0,0)=='h');assert(at(t,1,2)=='X');
    VTermScreenCell c;vterm_screen_get_cell(t->screen,(VTermPos){1,2},&c);
    assert(c.fg.indexed.idx==1&&c.bg.indexed.idx==4);
    feed(t,"\033[?1049h\033[Halternate");assert(t->alt&&at(t,0,0)=='a');
    feed(t,"\033[?1049l");assert(!t->alt&&at(t,0,0)=='h');
    feed(t,"\033[3;1H\342\224\200\303\251");assert(at(t,2,0)==0x2500&&at(t,2,1)==0xe9);
    feed(t,"\033[6n");assert(t->output_size&&!memcmp(t->output,"\033[3;3R",6));ssh_terminal_consume(t,t->output_size);
    feed(t,"\033[?1h");ssh_terminal_key(t,0x480000,0);assert(t->output_size==3&&!memcmp(t->output,"\033OA",3));ssh_terminal_consume(t,3);
    feed(t,"\033[?1l");ssh_terminal_key(t,0x3b0000,0);assert(t->output_size==3&&!memcmp(t->output,"\033OP",3));ssh_terminal_consume(t,3);
    ssh_terminal_key(t,3,0);assert(t->output_size==1&&t->output[0]==3);ssh_terminal_consume(t,1);
    feed(t,"\033[0m\033[H\033[2J");for(int i=0;i<400;i++)feed(t,"scroll\r\n");assert(t->history_count==SSH_HISTORY);
    ssh_terminal_scroll(t,10000);assert(t->offset==SSH_HISTORY);ssh_terminal_render(t);
    ssh_terminal_scroll(t,-10000);assert(!t->offset);
    feed(t,"\033[0m\033[H\033[2J");ssh_terminal_render(t);
    assert(t->drawn[0][0].fg==7&&t->drawn[0][0].bg==15);
    feed(t,"\033[2;4r\033[4;1Hone\r\ntwo\033[r");assert(at(t,2,0)=='o'&&at(t,3,0)=='t');
    /* Split UTF-8/CSI and oversized OSC strings remain bounded. */
    for(int i=0;i<20000;i++)feed(t,"\033]0;untrusted title");feed(t,"\a\033[Hsafe");
    ssh_terminal_render(t);ssh_terminal_free(t);
}
static void trust(void)
{
    const unsigned char a[]={1,2,3},b[]={1,2,4};
    input="no\r";assert(ssh_host_check("hosts","host",22,a,sizeof(a))<0);
    input="yes\r";assert(!ssh_host_check("hosts","host",22,a,sizeof(a)));assert(!*input);
    input=NULL;assert(!ssh_host_check("hosts","host",22,a,sizeof(a)));
    assert(ssh_host_check("hosts","host",22,b,sizeof(b))<0);
    input="yes\r";assert(!ssh_host_check("hosts","host",2222,b,sizeof(b)));
    FILE*f=fopen("hosts","ab");assert(f);fputs("broken",f);fclose(f);
    input=NULL;assert(ssh_host_check("hosts","host",22,a,sizeof(a))<0);
    input="yes\r";assert(ssh_host_check("missing/child/hosts","other",22,a,sizeof(a))<0);
    input="yes\r";assert(!ssh_host_check("newdir/hosts","other",22,a,sizeof(a)));
}
static void seed(void)
{
    unsigned char old[104],next[104],rnd[32];memcpy(old,"DCSSH001",8);for(unsigned i=0;i<64;i++)old[i+8]=i;assert(!wc_Sha256Hash(old,72,old+72));
    assert(ssh_seed_open("absent")<0);
    FILE*f=fopen("seed","wb");assert(f);assert(fwrite(old,1,104,f)==104);fclose(f);
    assert(!ssh_seed_open("seed"));assert(!ssh_entropy(rnd,sizeof(rnd)));ssh_seed_close();assert(ssh_entropy(rnd,sizeof(rnd))<0);
    f=fopen("seed","rb");assert(fread(next,1,104,f)==104);fclose(f);assert(memcmp(old,next,104));
    next[12]^=1;f=fopen("seed","wb");fwrite(next,1,104,f);fclose(f);assert(ssh_seed_open("seed")<0);
    allow_sampling=1;assert(!ssh_seed_insecure(10));assert(!ssh_entropy(rnd,sizeof(rnd)));
    unsigned char first[32];memcpy(first,rnd,32);ssh_seed_close();
    assert(ssh_entropy(rnd,sizeof(rnd))<0);
    assert(!ssh_seed_insecure(20));assert(!ssh_entropy(rnd,sizeof(rnd)));
    assert(memcmp(first,rnd,32));ssh_seed_close();allow_sampling=0;
    f=fopen("seed","rb");assert(fread(old,1,104,f)==104);fclose(f);assert(!memcmp(old,next,104));
    assert(!fopen("SEED.BIN","rb"));
}
int main(void){
    uint8_t ip[4];assert(!ssh_resolve_target("192.168.1.248",ip));assert(ip[0]==192&&ip[3]==248);
    const char*bad[]={"256.1.1.1","1.2.3","01.2.3.4","1.2.3.4.5","1..2.3","1.2.3.","",NULL};
    for(int i=0;bad[i];i++)assert(ssh_resolve_target(bad[i],ip)<0);
    terminal();trust();seed();puts("SSH unit checks passed");return 0;}
