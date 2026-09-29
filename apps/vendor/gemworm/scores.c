#include <stdio.h>
#include "drives.h"

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

/* Dreamcast port: the application disc is read-only. Scores go on the SD
 * card when one is mounted, otherwise on the RAM disk. */
char *get_hi_score_filepath(const char *fullpath)
{
    (void)fullpath;
    char *path = strdup("C:\\WORM.HI");
    if (path)
        path[0] = dc_storage_drive();
    return path;
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
            if (!fgets(linebuf, sizeof(linebuf), fp)) strcpy(linebuf, "--- 0");
            memcpy(scores[i].initials, linebuf, 3);
            scores[i].initials[3] = '\0';
            scores[i].score = atoi(&linebuf[3]);
        }
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
    if(fp == NULL) { free(filename); return; }
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
