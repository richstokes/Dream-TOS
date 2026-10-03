/* Dreamcast interactive SSH client. GPL-3.0-or-later. */
#include "platform.h"
#include "seed.h"
#include "keys.h"
#include "known_hosts.h"
#include "terminal.h"
#include "dreamcast/tcp.h"
#include <wolfssh/ssh.h>
#include <wolfssh/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char host[254],user[128],seed[260],hosts[260];
    const char *key_path;
    unsigned port,only_auth,key_attempts,password_attempts,interactive_attempts;
    int socket,cancelled,host_verified;
    uint32_t activity;
    SshKey key;
    char answers[16][256]; byte *responses[16]; word32 lengths[16];
} Client;
static void usage(void)
{
    ssh_print("SSH - secure remote terminal\r\n"
      "Usage: ssh [-p port] [-l user] [-i key] [-s seed] [-K hosts]\r\n"
      "           [-a key|password|interactive] [user@]host\r\n"
      "Defaults: first writable drive's \\SSH\\SEED.BIN and HOSTS.TXT.\r\n"
      "A prepared seed is recommended; without one, accept the risk prompt.\r\n"
      "Keys: OpenSSH Ed25519, RSA or ECDSA; optional AES-CTR passphrase.\r\n"
      "In session: Ctrl+] then . disconnects; p/n scrolls; r rekeys.\r\n"
      "Ctrl+] twice sends Ctrl+]. Ctrl+C goes to the remote terminal.\r\n");
}
static int copy_arg(char *out,size_t size,const char *in)
{if(strlen(in)>=size)return -1;strcpy(out,in);return 0;}
static int options(Client *c,int argc,char **argv)
{
    const char *target=NULL;c->port=22;c->socket=-1;
    snprintf(c->seed,sizeof(c->seed),"%c:\\SSH\\SEED.BIN",ssh_default_drive());
    snprintf(c->hosts,sizeof(c->hosts),"%c:\\SSH\\HOSTS.TXT",ssh_default_drive());
    for(int i=1;i<argc;i++) {
        char *a=argv[i];
        if(!strcmp(a,"-h")||!strcmp(a,"--help"))return 1;
        if(a[0]=='-') {
            if(!a[1]||a[2]||i+1==argc)return -1;
            const char *v=argv[++i];
            switch(a[1]) {
            case 'p': {char *end;unsigned long p=strtoul(v,&end,10);if(!*v||*end||p<1||p>65535)return -1;c->port=p;break;}
            case 'l':if(copy_arg(c->user,sizeof(c->user),v))return -1;break;
            case 'i':c->key_path=v;break;
            case 's':if(copy_arg(c->seed,sizeof(c->seed),v))return -1;break;
            case 'K':if(copy_arg(c->hosts,sizeof(c->hosts),v))return -1;break;
            case 'a':
                if(!strcmp(v,"key"))c->only_auth=WOLFSSH_USERAUTH_PUBLICKEY;
                else if(!strcmp(v,"password"))c->only_auth=WOLFSSH_USERAUTH_PASSWORD;
                else if(!strcmp(v,"interactive"))c->only_auth=WOLFSSH_USERAUTH_KEYBOARD;
                else return -1;
                break;
            default:return -1;
            }
        } else {if(target)return -1;target=a;}
    }
    if(!target)return 1;
    const char *at=strrchr(target,'@');
    if(at) {
        size_t n=at-target;if(!n||n>=sizeof(c->user)||c->user[0])return -1;
        memcpy(c->user,target,n);c->user[n]=0;target=at+1;
    }
    size_t n=strlen(target);if(!n||n>=sizeof(c->host))return -1;
    for(size_t i=0;i<n;i++) {
        unsigned char ch=target[i];
        if(!isalnum(ch)&&ch!='-'&&ch!='.')return -1;
        c->host[i]=(char)tolower(ch);
    }
    if(c->host[n-1]=='.')c->host[--n]=0;
    if(!n||c->host[0]=='-'||c->host[0]=='.')return -1;
    for(size_t i=0;c->user[i];i++)if((unsigned char)c->user[i]<33||(unsigned char)c->user[i]>126)return -1;
    if(c->only_auth==WOLFSSH_USERAUTH_PUBLICKEY&&!c->key_path)return -1;
    return 0;
}
static int receive(WOLFSSH *ssh,void *buf,word32 size,void *ctx)
{
    (void)ssh;Client *c=ctx;long n=ssh_tcp_read(c->socket,buf,size>65536?65536:size);
    if(n>0){c->activity=ssh_now();return (int)n;}
    if(n==DC_TCP_AGAIN)return WS_CBIO_ERR_WANT_READ;
    return n==0?WS_CBIO_ERR_CONN_CLOSE:WS_CBIO_ERR_GENERAL;
}
static int send_data(WOLFSSH *ssh,void *buf,word32 size,void *ctx)
{
    (void)ssh;Client *c=ctx;long n=ssh_tcp_write(c->socket,buf,size>65536?65536:size);
    if(n>0){c->activity=ssh_now();return (int)n;}
    if(n==DC_TCP_AGAIN||n==0)return WS_CBIO_ERR_WANT_WRITE;
    return WS_CBIO_ERR_GENERAL;
}
static int host_key(const byte *key,word32 size,void *ctx)
{
    Client *c=ctx;int rc=ssh_host_check(c->hosts,c->host,c->port,key,size);
    c->activity=ssh_now();c->host_verified=!rc;return rc;
}
unsigned int ssh_choose_auth(void *session,unsigned int offered,int partial)
{
    Client *c=wolfSSH_GetUserAuthCtx(session);
    if(c->cancelled)return 0;
    if(partial)ssh_print("Additional authentication required.\r\n");
    if(c->only_auth)offered&=c->only_auth;
    if((offered&WOLFSSH_USERAUTH_PUBLICKEY)&&c->key.private_key&&!c->key_attempts++)return WOLFSSH_USERAUTH_PUBLICKEY;
    if((offered&WOLFSSH_USERAUTH_KEYBOARD)&&c->interactive_attempts<3){c->interactive_attempts++;return WOLFSSH_USERAUTH_KEYBOARD;}
    if((offered&WOLFSSH_USERAUTH_PASSWORD)&&c->password_attempts<3){c->password_attempts++;return WOLFSSH_USERAUTH_PASSWORD;}
    ssh_print("No remaining supported authentication method.\r\n");return 0;
}
static int prompt(Client *c,const char *label,unsigned i,int echo)
{
    int rc=ssh_prompt(label,c->answers[i],sizeof(c->answers[i]),echo);
    c->activity=ssh_now();if(rc<0)c->cancelled=1;
    return rc;
}
static int authenticate(byte type,WS_UserAuthData *auth,void *ctx)
{
    Client *c=ctx;
    if(!c->host_verified||c->cancelled)return WOLFSSH_USERAUTH_FAILURE;
    ssh_wipe(c->answers,sizeof(c->answers));
    if(type==WOLFSSH_USERAUTH_PUBLICKEY&&c->key.private_key) {
        auth->sf.publicKey.publicKeyType=(const byte*)c->key.type;
        auth->sf.publicKey.publicKeyTypeSz=strlen(c->key.type);
        auth->sf.publicKey.publicKey=c->key.public_key;
        auth->sf.publicKey.publicKeySz=c->key.public_size;
        auth->sf.publicKey.privateKey=c->key.private_key;
        auth->sf.publicKey.privateKeySz=c->key.private_size;
    } else if(type==WOLFSSH_USERAUTH_PASSWORD) {
        if(prompt(c,"Password: ",0,0)<0)return WOLFSSH_USERAUTH_FAILURE;
        auth->sf.password.password=(const byte*)c->answers[0];
        auth->sf.password.passwordSz=strlen(c->answers[0]);
    } else if(type==WOLFSSH_USERAUTH_KEYBOARD) {
        WS_UserAuthData_Keyboard *k=&auth->sf.keyboard;
        if(k->promptCount>16)return WOLFSSH_USERAUTH_FAILURE;
        if(k->promptNameSz){ssh_safe_text(k->promptName,k->promptNameSz);ssh_print("\r\n");}
        if(k->promptInstructionSz){ssh_safe_text(k->promptInstruction,k->promptInstructionSz);ssh_print("\r\n");}
        for(unsigned i=0;i<k->promptCount;i++) {
            ssh_safe_text(k->prompts[i],strnlen((char*)k->prompts[i],1024));
            if(prompt(c,"",i,k->promptEcho[i])<0)return WOLFSSH_USERAUTH_FAILURE;
            c->responses[i]=(byte*)c->answers[i];c->lengths[i]=strlen(c->answers[i]);
        }
        k->responseCount=k->promptCount;k->responses=c->responses;k->responseLengths=c->lengths;
    } else return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
    return WOLFSSH_USERAUTH_SUCCESS;
}
static int error_code(WOLFSSH *ssh,int rc){return rc==WS_ERROR?wolfSSH_get_error(ssh):rc;}
static int pending(int rc)
{return rc==WS_SUCCESS||rc==WS_WANT_READ||rc==WS_WANT_WRITE||rc==WS_REKEYING||rc==WS_CHAN_RXD||rc==WS_WINDOW_FULL;}
static int ended(int rc){return rc==WS_CHANNEL_CLOSED||rc==WS_DISCONNECT||rc==WS_EOF;}
static void report_error(const char *where,int rc)
{char line[200];snprintf(line,sizeof(line),"%s: %s (%d).\r\n",where,wolfSSH_ErrorToName(rc),rc);ssh_print(line);}
static int session(WOLFSSH *ssh)
{
    SshTerminal *t=ssh_terminal_new();if(!t)return 1;
    byte buf[4096];uint32_t rendered=ssh_now(),last_rekey=rendered,eof_at=0;
    int prefix=0,result=0,fail=0,done=0;
    while(!done) {
        /* Bound receive work so sustained output cannot starve the keyboard. */
        for(int i=0;i<4;i++) {
            int n=wolfSSH_stream_read(ssh,buf,sizeof(buf));
            if(n>0){ssh_terminal_feed(t,(char*)buf,n);continue;}
            int err=error_code(ssh,n);
            if(err==WS_EXTDATA) {
                while((n=wolfSSH_extended_data_read(ssh,buf,sizeof(buf)))>0)ssh_terminal_feed(t,(char*)buf,n);
            } else if(err==WS_EOF) {if(!eof_at)eof_at=ssh_now();}
            else if(ended(err)){done=1;}
            else if(!pending(err)){fail=err;done=1;}
            break;
        }
        /* worker flushes pending ciphertext and processes rekeys/window updates. */
        if(!done) {
            int err=error_code(ssh,wolfSSH_worker(ssh,NULL));
            if(err==WS_EXTDATA){int n;while((n=wolfSSH_extended_data_read(ssh,buf,sizeof(buf)))>0)ssh_terminal_feed(t,(char*)buf,n);}
            else if(ended(err)){if(err!=WS_EOF)done=1;else if(!eof_at)eof_at=ssh_now();}
            else if(!pending(err)){fail=err;done=1;}
        }
        for(int i=0;i<32&&!done&&t->output_size<sizeof(t->output)-128;i++) {
            int key=ssh_key();if(!key)break;
            if(prefix) {
                prefix=0;
                switch(key&255) {
                case '.':done=1;break;
                case 'p':ssh_terminal_scroll(t,SSH_ROWS-1);break;
                case 'n':ssh_terminal_scroll(t,1-SSH_ROWS);break;
                case 'r': {int rc=wolfSSH_TriggerKeyExchange(ssh);if(!pending(error_code(ssh,rc))){fail=error_code(ssh,rc);done=1;}last_rekey=ssh_now();break;}
                case 29:ssh_terminal_key(t,29,0);break;
                default:break;
                }
            } else if((key&255)==29)prefix=1;
            else ssh_terminal_key(t,key,ssh_modifiers());
        }
        if(t->output_size&&!done&&!eof_at) {
            int n=wolfSSH_stream_send(ssh,(byte*)t->output,t->output_size);
            if(n>0)ssh_terminal_consume(t,n);
            else {int err=error_code(ssh,n);if(!pending(err)){fail=err;done=1;}}
        }
        uint32_t now=ssh_now();
        if((uint32_t)(now-rendered)>=33||done){ssh_terminal_render(t);rendered=now;}
        if((uint32_t)(now-last_rekey)>=3600000u) {
            int rc=error_code(ssh,wolfSSH_TriggerKeyExchange(ssh));
            if(!pending(rc)){fail=rc;done=1;}last_rekey=now;
        }
        if(eof_at&&(uint32_t)(now-eof_at)>3000)done=1;
        if(t->failed){fail=WS_BUFFER_E;done=1;}
        ssh_idle();
    }
    result=fail?1:wolfSSH_GetExitStatus(ssh);
    ssh_terminal_render(t);ssh_terminal_free(t);ssh_wipe(buf,sizeof(buf));
    if(fail)report_error("Connection ended",fail);
    else ssh_print("Connection closed.\r\n");
    return result;
}
int ssh_main(int argc,char **argv)
{
    Client *c=calloc(1,sizeof(*c));WOLFSSH_CTX *ctx=NULL;WOLFSSH *ssh=NULL;
    int rc=1,initialized=0;
    if(!c)return 1;
    int parsed=options(c,argc,argv);if(parsed){usage();rc=parsed<0?2:0;goto done;}
    if(!ssh_network_ready()){ssh_print("Network is unavailable; check IFCONFIG and DHCP.\r\n");goto done;}
    if(!c->user[0]&&ssh_prompt("Login as: ",c->user,sizeof(c->user),1)<0)goto done;
    if(!c->user[0])goto done;
    if(ssh_seed_open(c->seed)) {
        ssh_print("Cannot load and rotate private seed: ");ssh_print(c->seed);
        ssh_print("\r\nWARNING: Continuing uses weak clock/timing randomness.\r\n"
                  "An attacker may predict encryption keys and expose passwords,\r\n"
                  "session traffic or private keys. A prepared seed is recommended.\r\n"
                  "Run tools/prepare_ssh.py on your computer for secure seed setup.\r\n");
        char answer[16];uint32_t started=ssh_now();
        int accepted=!ssh_prompt("Type risk to continue for this connection (Enter cancels): ",
                                 answer,sizeof(answer),1)&&!strcmp(answer,"risk");
        ssh_wipe(answer,sizeof(answer));
        if(!accepted){ssh_print("Connection cancelled.\r\n");goto done;}
        if(ssh_seed_insecure(started)){ssh_print("Could not initialize random generator.\r\n");goto done;}
        ssh_print("Continuing with weak randomness for this connection.\r\n");
    }
    if(c->key_path&&ssh_key_load(c->key_path,&c->key))goto done;
    uint8_t ip[4];ssh_print("Resolving ");ssh_print(c->host);ssh_print("...\r\n");
    if(ssh_resolve_target(c->host,ip)<0){ssh_print("Invalid IPv4 address or DNS lookup failed.\r\n");goto done;}
    c->socket=ssh_tcp_connect(ip,c->port);
    if(c->socket<0){ssh_print("TCP connection failed.\r\n");goto done;}
    c->activity=ssh_now();
    for(;;) {
        long status=ssh_tcp_connected(c->socket);if(!status)break;
        if(status!=DC_TCP_AGAIN){ssh_print("TCP connection refused or failed.\r\n");goto done;}
        int k=ssh_key()&255;
        if(k==3||k==27||(uint32_t)(ssh_now()-c->activity)>15000){ssh_print("Connection cancelled or timed out.\r\n");goto done;}
        ssh_idle();
    }
    if(wolfSSH_Init()!=WS_SUCCESS)goto done;initialized=1;
    ctx=wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT,NULL);if(!ctx)goto done;
    wolfSSH_SetIORecv(ctx,receive);wolfSSH_SetIOSend(ctx,send_data);
    wolfSSH_SetUserAuth(ctx,authenticate);wolfSSH_CTX_SetPublicKeyCheck(ctx,host_key);
    /* Exclude SHA-1 and old finite-field DH algorithms, including ssh-rsa signatures. */
    if(wolfSSH_CTX_SetAlgoListKex(ctx,"curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nistp256,ecdh-sha2-nistp384,ecdh-sha2-nistp521")||
       wolfSSH_CTX_SetAlgoListKey(ctx,"ssh-ed25519,ecdsa-sha2-nistp256,ecdsa-sha2-nistp384,ecdsa-sha2-nistp521,rsa-sha2-512,rsa-sha2-256")||
       wolfSSH_CTX_SetAlgoListCipher(ctx,"aes128-gcm@openssh.com,aes256-gcm@openssh.com,aes128-ctr,aes256-ctr")||
       wolfSSH_CTX_SetAlgoListMac(ctx,"hmac-sha2-256,hmac-sha2-512"))goto done;
    ssh=wolfSSH_new(ctx);if(!ssh)goto done;
    wolfSSH_SetIOReadCtx(ssh,c);wolfSSH_SetIOWriteCtx(ssh,c);
    wolfSSH_SetUserAuthCtx(ssh,c);wolfSSH_SetPublicKeyCheckCtx(ssh,c);
    wolfSSH_SetHighwater(ssh,1u<<28);
    if(wolfSSH_SetUsername(ssh,c->user)||wolfSSH_SetChannelType(ssh,WOLFSSH_SESSION_TERMINAL,NULL,0))goto done;
    ssh_print("Connecting (Ctrl+C cancels)...\r\n");
    for(;;) {
        int status=wolfSSH_connect(ssh);if(status==WS_SUCCESS)break;
        int err=error_code(ssh,status);
        if(!pending(err)){report_error("SSH login failed",err);goto done;}
        int k=ssh_key()&255;
        if(k==3||k==27||c->cancelled||(uint32_t)(ssh_now()-c->activity)>60000){ssh_print("SSH login cancelled or timed out.\r\n");goto done;}
        ssh_idle();
    }
    ssh_wipe(c->answers,sizeof(c->answers));ssh_key_free(&c->key);
    rc=session(ssh);
    wolfSSH_SendDisconnect(ssh,WOLFSSH_DISCONNECT_BY_APPLICATION);
 done:
    if(ssh)wolfSSH_free(ssh);if(ctx)wolfSSH_CTX_free(ctx);if(initialized)wolfSSH_Cleanup();
    if(c->socket>0)ssh_tcp_close(c->socket);
    ssh_key_free(&c->key);ssh_seed_close();ssh_wipe(c,sizeof(*c));free(c);return rc;
}
