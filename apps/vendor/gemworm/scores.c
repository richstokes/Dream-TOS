#include <stdio.h>

#ifdef __GNUC__

#include <gem.h>

#define HIWORD(x)  ((int16_t)((uint32_t)x >> 16))
#define LOWORD(x)  ((int16_t)((uint32_t)x & 0xFFFF)) 

#else

#include <portab.h>
#include <aes.h>
#include <vdi.h>

#endif /* __GNUC__ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCOREFILE   "worm.hi"

struct score {
    char initials[4];
    int score;
};

char *defaults[10] = {
    "JBA",
    "GEM",
    "TOS",
    "DOS",
    "68K",
    "X86",
    "AES",
    "VDI",
    "WRM",
    "MGX" };

static struct score scores[10];

char *get_hi_score_filepath(const char *fullpath)
{
char *scorepath;
char *tmp;
size_t fpl;

    if(fullpath != NULL)
        fpl = strlen(fullpath) + strlen(SCOREFILE); /* fine... */
    else
        fpl = strlen(SCOREFILE);
    scorepath = (char *)malloc(fpl);
    
    if(fullpath != NULL) {
        strcpy(scorepath, fullpath);
        tmp = strrchr(scorepath, '\\');
        if(tmp == NULL)
            tmp = strrchr(scorepath, '/');
        if(tmp == NULL)
            tmp = scorepath;
        else
            tmp++;    
        tmp[0] = '\0';
        
    } else
        scorepath[0] = '\0';
        
    strcat(scorepath, SCOREFILE);
    
    return scorepath;
}

void load_scores(const char *fullpath)
{
char *filename;
FILE *fp;
int i;
char linebuf[16];

    filename = get_hi_score_filepath(fullpath);
    fp = fopen(filename, "r");
    if(fp != NULL) {
        for(i=0;i<10;i++) {
            fgets(linebuf, 16, fp);
            memcpy(scores[i].initials, linebuf, 3);
            scores[i].initials[3] = '\0';
            scores[i].score = atoi(&linebuf[3]);
        }
        scores[i].initials[3] = '\0';
        fclose(fp);
    } else {
        for(i=0;i<10;i++) {
            memcpy(scores[i].initials, defaults[i], 4);
            scores[9-i].score = i == 0 ? 10 : (i * 500 + 100);
        }
    }
    free(filename);
}

void save_scores(const char *fullpath)
{
char *filename;
FILE *fp;
int i;

    filename = get_hi_score_filepath(fullpath);
    fp = fopen(filename, "w");
    if(fp == NULL) return;
    for(i=0;i<10;i++) {
        fwrite(scores[i].initials, 1, 3, fp);
        fprintf(fp, " %6d\n", scores[i].score);
    }
    fclose(fp);
    free(filename);
}

char *get_score_at(int pos, int *score)
{
    *score = scores[pos].score;
    return scores[pos].initials;
}

int is_high_score(int score)
{
    return (score > scores[9].score);
}

void add_high_score(const char *initials, int score)
{
int i,j;

    for(i=0;i<10;i++) {
        if(score > scores[i].score) {
            for(j=8;j>=i;j--) {
                scores[j+1].score = scores[j].score;
                memcpy(scores[j+1].initials, scores[j].initials, 3);
            }
            scores[i].score = score;
            memcpy(scores[i].initials, initials, 3);
            break;
        }
    }
}
