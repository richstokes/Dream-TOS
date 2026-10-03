/* OpenSSH private keys, including bcrypt + aes256-ctr encryption.
 * Key cryptography and parsing use wolfSSH/wolfCrypt and OpenBSD bcrypt.
 * GPL-3.0-or-later. */
#include "keys.h"
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wolfssl/wolfcrypt/coding.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssh/ssh.h>
int _libssh2_bcrypt_pbkdf(const char *,size_t,const uint8_t *,size_t,uint8_t *,size_t,unsigned);
typedef struct {const unsigned char *p;size_t n;} Span;
static int number(Span *s,unsigned *n)
{
    if(s->n<4)return -1;
    *n=((unsigned)s->p[0]<<24)|((unsigned)s->p[1]<<16)|((unsigned)s->p[2]<<8)|s->p[3];s->p+=4;s->n-=4;return 0;
}
static int field(Span *s,Span *f)
{
    unsigned n;if(number(s,&n)||n>s->n)return -1;
    f->p=s->p;f->n=n;s->p+=n;s->n-=n;return 0;
}
static int equals(Span s,const char *p){return s.n==strlen(p)&&!memcmp(s.p,p,s.n);}
static void put32(unsigned char **p,unsigned n)
{*(*p)++=n>>24;*(*p)++=n>>16;*(*p)++=n>>8;*(*p)++=n;}
static void putfield(unsigned char **p,Span s)
{put32(p,(unsigned)s.n);memcpy(*p,s.p,s.n);*p+=s.n;}
void ssh_key_free(SshKey *k)
{
    if(k->private_key){ssh_wipe(k->private_key,k->private_size);free(k->private_key);}
    free(k->public_key);memset(k,0,sizeof(*k));
}
int ssh_key_load(const char *path,SshKey *key)
{
    const char begin[]="-----BEGIN OPENSSH PRIVATE KEY-----",end[]="-----END OPENSSH PRIVATE KEY-----";
    char *text=NULL,*start,*finish;unsigned char *raw=NULL,*plain=NULL,*wrapped=NULL;
    size_t size=0;word32 rawsize=32768;unsigned count,rounds;int ok=0;
    char password[256]={0};unsigned char derived[48]={0};
    FILE *f=fopen(path,"rb");memset(key,0,sizeof(*key));
    if(!f){ssh_print("Cannot read identity file.\r\n");return -1;}
    text=malloc(32769);raw=malloc(32768);
    if(!text||!raw)goto done;
    size=fread(text,1,32768,f);text[size]=0;
    if(ferror(f)||size==32768||strncmp(text,begin,strlen(begin)))goto done;
    start=text+strlen(begin);finish=strstr(start,end);
    if(!finish||Base64_Decode((byte*)start,(word32)(finish-start),raw,&rawsize))goto done;
    Span s={raw,rawsize},cipher,kdf,options,pub,priv;
    if(s.n<15||memcmp(s.p,"openssh-key-v1\0",15))goto done;
    s.p+=15;s.n-=15;
    if(field(&s,&cipher)||field(&s,&kdf)||field(&s,&options)||number(&s,&count)||count!=1||
       field(&s,&pub)||field(&s,&priv)||s.n||pub.n>8192||priv.n<8||priv.n>16384)goto done;
    plain=malloc(priv.n);if(!plain)goto done;memcpy(plain,priv.p,priv.n);
    if(equals(cipher,"none")) {if(!equals(kdf,"none")||options.n)goto done;}
    else {
        Span salt;
        if(!equals(cipher,"aes256-ctr")||!equals(kdf,"bcrypt")||
           field(&options,&salt)||number(&options,&rounds)||options.n||salt.n<16||salt.n>64||
           !rounds||rounds>1024||priv.n%16) {
            ssh_print("Identity encryption must be bcrypt/aes256-ctr (1..1024 rounds).\r\n");goto done;
        }
        if(ssh_prompt("Key passphrase: ",password,sizeof(password),0))goto done;
        ssh_print("Unlocking identity...\r\n");ssh_idle();
        if(_libssh2_bcrypt_pbkdf(password,strlen(password),salt.p,salt.n,derived,sizeof(derived),rounds))goto done;
        Aes aes;
        int rc=wc_AesInit(&aes,NULL,INVALID_DEVID);
        if(!rc){rc=wc_AesSetKey(&aes,derived,32,derived+32,AES_ENCRYPTION);if(!rc)rc=wc_AesCtrEncrypt(&aes,plain,plain,(word32)priv.n);wc_AesFree(&aes);}
        ssh_wipe(password,sizeof(password));ssh_wipe(derived,sizeof(derived));
        if(rc)goto done;
    }
    if(memcmp(plain,plain+4,4)){ssh_print("Wrong passphrase or damaged identity.\r\n");goto done;}
    Span public_fields=pub,type,private_fields={plain+8,priv.n-8},private_type;
    if(field(&public_fields,&type)||field(&private_fields,&private_type)||type.n>=sizeof(key->type)||
       type.n!=private_type.n||memcmp(type.p,private_type.p,type.n))goto done;
    if(!equals(type,"ssh-ed25519")&&!equals(type,"ssh-rsa")&&!equals(type,"ecdsa-sha2-nistp256")&&
       !equals(type,"ecdsa-sha2-nistp384")&&!equals(type,"ecdsa-sha2-nistp521"))goto done;
    /* Rebuild an unencrypted OpenSSH wrapper for the upstream signing code. */
    key->private_size=(unsigned)(15+8+8+4+4+4+pub.n+4+priv.n);
    wrapped=malloc(key->private_size);key->public_key=malloc(pub.n);
    if(!wrapped||!key->public_key)goto done;
    unsigned char *p=wrapped;memcpy(p,"openssh-key-v1\0",15);p+=15;
    putfield(&p,(Span){(const byte*)"none",4});putfield(&p,(Span){(const byte*)"none",4});
    putfield(&p,(Span){(const byte*)"",0});put32(&p,1);putfield(&p,pub);putfield(&p,(Span){plain,priv.n});
    if((size_t)(p-wrapped)!=key->private_size)goto done;
    memcpy(key->type,type.p,type.n);key->type[type.n]=0;
    memcpy(key->public_key,pub.p,pub.n);key->public_size=(unsigned)pub.n;
    key->private_key=wrapped;wrapped=NULL;ok=1;
done:
    if(fclose(f))ok=0;
    ssh_wipe(password,sizeof(password));ssh_wipe(derived,sizeof(derived));
    if(text){ssh_wipe(text,32769);free(text);}if(raw){ssh_wipe(raw,32768);free(raw);}
    if(plain){ssh_wipe(plain,priv.n);free(plain);}if(wrapped){ssh_wipe(wrapped,key->private_size);free(wrapped);}
    if(!ok){ssh_key_free(key);ssh_print("Could not load OpenSSH identity.\r\n");}
    return ok?0:-1;
}
