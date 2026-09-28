#ifndef SCORES_HEADERS
#define SCORES_HEADERS

void load_scores(const char *fullpath);
void save_scores(const char *fullpath);
char *get_score_at(int pos, int *score);

int is_high_score(int score);
void add_high_score(const char *initials, int score);

#endif /* SCORES_HEADERS */
