/* Bounded local prompts; server-supplied prompt text is never a control stream. */
#include "platform.h"
#include <string.h>
void ssh_wipe(void *p,size_t n) { volatile unsigned char *v=p;while(n--)*v++=0; }
void ssh_print(const char *p) { ssh_output(p,strlen(p)); }
void ssh_safe_text(const unsigned char *p,size_t n)
{
    if(n>1024)n=1024;
    while(n--) { char c=*p++; if(c=='\n')ssh_print("\r\n");else if(c>=32&&c<=126)ssh_output(&c,1);else ssh_print("?"); }
}
int ssh_prompt(const char *label,char *out,size_t cap,int echo)
{
    size_t n=0;
    ssh_print(label);out[0]=0;
    for(;;) {
        int k=ssh_key(); unsigned char c=k;
        if(!k){ssh_idle();continue;}
        if(c==3 || c==27){ssh_wipe(out,cap);ssh_print("\r\nCancelled.\r\n");return -1;}
        if(c=='\r'||c=='\n'){out[n]=0;ssh_print("\r\n");return 0;}
        if(c==8||c==127){if(n){out[--n]=0;if(echo)ssh_print("\b \b");}continue;}
        if(c==21){while(n){out[--n]=0;if(echo)ssh_print("\b \b");}continue;}
        if(c>=32 && c<127 && n+1<cap){out[n++]=c;out[n]=0;if(echo)ssh_output((char*)&c,1);}
    }
}

/* The native DNS API accepts names, not numeric addresses. */
int ssh_resolve_target(const char *host,uint8_t ip[4])
{
    const char *s=host;
    for(;*s;s++)if((*s<'0'||*s>'9')&&*s!='.')return ssh_resolve(host,ip);
    s=host;
    for(int i=0;i<4;i++) {
        const char *start=s;unsigned value=0,digits=0;
        while(*s>='0'&&*s<='9') {value=value*10+(*s++-'0');if(++digits>3)return -1;}
        if(!digits||value>255||(digits>1&&*start=='0'))return -1;
        ip[i]=(uint8_t)value;
        if(i<3){if(*s!='.')return -1;s++;}
    }
    return *s?-1:0;
}
