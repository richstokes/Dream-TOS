/* SHA-256 pins for exact host + port. GPL-3.0-or-later. */
#include "known_hosts.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/coding.h>
int ssh_host_check(const char *path,const char *host,unsigned port,const unsigned char *key,size_t size)
{
    unsigned char hash[32],encoded[48];word32 len=sizeof(encoded);
    char line[384],name[254],fingerprint[64],extra,answer[12];unsigned saved_port;
    int found=0,lines=0;
    if(!size||size>16384||wc_Sha256Hash(key,(word32)size,hash)||Base64_Encode_NoNl(hash,32,encoded,&len))return -1;
    while(len&&encoded[len-1]=='=')len--;
    encoded[len]=0;
    char actual[64];snprintf(actual,sizeof(actual),"SHA256:%s",encoded);
    FILE *f=fopen(path,"rb");
    if(!f&&errno!=ENOENT){ssh_print("Cannot read host fingerprint file.\r\n");return -1;}
    if(f) {
        int invalid=0;
        while(fgets(line,sizeof(line),f)) {
            if(++lines>1024||!strchr(line,'\n')){invalid=1;break;}
            if(line[0]=='#'||line[0]=='\n')continue;
            if(sscanf(line,"%253s %u %63s %c",name,&saved_port,fingerprint,&extra)!=3){invalid=1;break;}
            if(!strcmp(name,host)&&saved_port==port) {
                if(strcmp(fingerprint,actual)) {
                    fclose(f);ssh_print("HOST KEY CHANGED. Connection refused.\r\nReceived: ");ssh_print(actual);ssh_print("\r\nVerify with the server owner before editing HOSTS.TXT.\r\n");return -1;
                }
                found=1;
            }
        }
        if(ferror(f))invalid=1;
        if(fclose(f))invalid=1;
        if(invalid){ssh_print("Invalid host fingerprint file.\r\n");return -1;}
    }
    if(found)return 0;
    ssh_print("New server ");ssh_print(host);snprintf(line,sizeof(line),":%u\r\n",port);ssh_print(line);
    ssh_print(actual);ssh_print("\r\nVerify this fingerprint through a trusted source.\r\n");
    if(ssh_prompt("Trust and save this server? Type yes: ",answer,sizeof(answer),1)||strcmp(answer,"yes"))return -1;
    if(lines>=1024)return -1;
    ssh_make_parent(path);
    f=fopen(path,"ab");
    if(!f){ssh_print("Cannot save server fingerprint; refusing login.\r\n");return -1;}
    int failed=fprintf(f,"%s %u %s\n",host,port,actual)<0;
    if(fclose(f))failed=1;
    return failed?-1:0;
}
