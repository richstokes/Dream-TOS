/* wolfCrypt Hash_DRBG seeded from a user-provisioned, rotating secret file.
 * Explicit opt-in permits a weak timing/RTC seed for one connection.
 * GPL-3.0-or-later. */
#include "platform.h"
#include "seed.h"
#include <stdio.h>
#include <string.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
static WC_RNG master;
static unsigned char bootstrap[64];
static unsigned available;
static int ready, busy, initialized;
int ssh_entropy(unsigned char *out,unsigned int size)
{
    if(available) {
        if(size>available)return RNG_FAILURE_E;
        memcpy(out,bootstrap+64-available,size);available-=size;return 0;
    }
    if(!ready || busy)return RNG_FAILURE_E;
    busy=1;
    int rc=wc_RNG_GenerateBlock(&master,out,size);
    busy=0;return rc;
}
void ssh_seed_close(void)
{
    ready=available=busy=0;
    if(initialized)wc_FreeRng(&master);
    initialized=0;ssh_wipe(bootstrap,sizeof(bootstrap));ssh_wipe(&master,sizeof(master));
}
int ssh_seed_open(const char *path)
{
    unsigned char record[104],next[104],check[105],digest[32];
    FILE *f=NULL; int ok=0;
    ssh_seed_close();
    f=fopen(path,"r+b");
    if(!f)goto done;
    if(fread(record,1,sizeof(record),f)!=sizeof(record)||fgetc(f)!=EOF||ferror(f)||
       memcmp(record,"DCSSH001",8))goto done;
    if(wc_Sha256Hash(record,72,digest)||memcmp(record+72,digest,32))goto done;
    memcpy(bootstrap,record+8,64);available=64;
    if(wc_InitRng(&master))goto done;
    initialized=1;available=0;ready=1;ssh_wipe(bootstrap,sizeof(bootstrap));
    memcpy(next,"DCSSH001",8);
    /* Reserve the next boot's state before releasing any randomness to SSH. */
    if(wc_RNG_GenerateBlock(&master,next+8,64)||wc_Sha256Hash(next,72,next+72))goto done;
    if(fseek(f,0,SEEK_SET)||fwrite(next,1,sizeof(next),f)!=sizeof(next)||fflush(f))goto done;
    if(fclose(f)){f=NULL;goto done;}f=NULL;
    f=fopen(path,"rb");
    if(!f||fread(check,1,sizeof(check),f)!=sizeof(next)||ferror(f)||memcmp(check,next,sizeof(next)))goto done;
    ok=1;
done:
    if(f&&fclose(f))ok=0;
    ssh_wipe(record,sizeof(record));ssh_wipe(next,sizeof(next));ssh_wipe(check,sizeof(check));ssh_wipe(digest,sizeof(digest));
    if(!ok)ssh_seed_close();
    return ok?0:-1;
}

int ssh_seed_insecure(uint32_t prompt_started)
{
    /* Hashing only conditions the inputs; it does not make clocks or human
     * response timing into a secure random source. Never persist this output
     * in SEED.BIN, where a later launch could mistake it for a provisioned seed. */
    wc_Sha512 hash;
    uint32_t sample[4]={prompt_started,0,0,0};
    int rc;
    ssh_seed_close();
    rc=wc_InitSha512(&hash);
    if(rc)return -1;
    for(unsigned i=0;i<32&&!rc;i++) {
        sample[1]=ssh_now();sample[2]=ssh_wallclock();sample[3]=i;
        rc=wc_Sha512Update(&hash,(const unsigned char *)sample,sizeof(sample));
        ssh_idle();
    }
    if(!rc)rc=wc_Sha512Final(&hash,bootstrap);
    wc_Sha512Free(&hash);ssh_wipe(&hash,sizeof(hash));ssh_wipe(sample,sizeof(sample));
    if(!rc) {
        available=sizeof(bootstrap);
        rc=wc_InitRng(&master);
        if(!rc){initialized=1;ready=1;}
    }
    available=0;ssh_wipe(bootstrap,sizeof(bootstrap));
    if(rc)ssh_seed_close();
    return rc?-1:0;
}
