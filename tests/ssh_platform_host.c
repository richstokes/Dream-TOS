/* Host integration adapter: real shared TCP code, raw local PTY. */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "dreamcast/tcp.h"
#include <sys/time.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <stdlib.h>
uint32_t ssh_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);}
void ssh_idle(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
int ssh_key(void){fd_set f;FD_ZERO(&f);FD_SET(0,&f);struct timeval t={0,0};unsigned char c;if(select(1,&f,NULL,NULL,&t)>0&&read(0,&c,1)==1)return c;return 0;}
unsigned ssh_modifiers(void){return 0;}
void ssh_output(const char*p,size_t n){while(n){ssize_t r=write(1,p,n);if(r<=0)return;p+=r;n-=r;}}
int ssh_network_ready(void){return 1;}
int ssh_resolve(const char*h,uint8_t ip[4]){struct addrinfo hint={0},*r;hint.ai_family=AF_INET;hint.ai_socktype=SOCK_STREAM;if(getaddrinfo(h,NULL,&hint,&r))return -1;memcpy(ip,&((struct sockaddr_in*)r->ai_addr)->sin_addr,4);freeaddrinfo(r);return 0;}
long ssh_tcp_connect(const uint8_t ip[4],unsigned p){return dc_tcp_connect(ip,p);}
long ssh_tcp_connected(int h){return dc_tcp_connected(h);}
long ssh_tcp_read(int h,void*p,unsigned n){return dc_tcp_recv(h,p,n);}
long ssh_tcp_write(int h,const void*p,unsigned n){return dc_tcp_send(h,p,n);}
void ssh_tcp_close(int h){dc_tcp_close(h);}
char ssh_default_drive(void){return 'C';}
int main(int argc,char**argv){struct termios old,raw;int tty=!tcgetattr(0,&old);if(tty){raw=old;raw.c_lflag&=~(ICANON|ECHO|ISIG);raw.c_iflag&=~(ICRNL|IXON);raw.c_oflag&=~OPOST;tcsetattr(0,TCSANOW,&raw);}int rc=ssh_main(argc,argv);if(tty)tcsetattr(0,TCSANOW,&old);return rc;}
