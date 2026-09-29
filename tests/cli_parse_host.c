/* Parser boundary/quoting regressions in the actual upstream shell source. */
#define STANDALONE_CONSOLE
#include "../upstream/emutos/cli/cmdparse.c"
#include <assert.h>
WORD nflops_copy;
void messagenl(const char *s) { (void)s; }
WORD strequal(const char *a, const char *b) { return !strcmp(a,b); }
int main(void)
{
    char text[256], redirect[MAXPATHLEN];
    char *args[MAX_ARGS];
    strcpy(text, "grep -n \"two words\" FILE.TXT > C:\\OUT.TXT");
    assert(parse_line(text,args,redirect) == 4);
    assert(!strcmp(args[2], "\"two words\""));
    assert(!strcmp(redirect, "C:\\OUT.TXT"));
    strcpy(text, "echo \"unfinished");
    assert(parse_line(text,args,redirect) < 0);
    strcpy(text, "echo missing >");
    assert(parse_line(text,args,redirect) < 0);
    strcpy(text, "echo a >b >c");
    assert(parse_line(text,args,redirect) < 0);
    strcpy(text, "pwd");
    assert(parse_line(text,args,redirect) == 1 && !redirect[0]);
    for (int n = MAX_ARGS; n <= MAX_ARGS+1; n++) {
        for (int i = 0; i < n; i++) { text[i*2] = 'a'; text[i*2+1] = ' '; }
        text[n*2] = 0;
        int result = parse_line(text,args,redirect);
        assert(n == MAX_ARGS ? result == MAX_ARGS : result < 0);
    }
    return 0;
}
