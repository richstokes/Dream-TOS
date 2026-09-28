#define APP_HOST_TEST
#define app_main guest_app_main
#include "../apps/ports/editor.c"
#include <assert.h>
int main(void)
{
    clear_buffer();
    editorInsertChar('a');
    editorInsertChar('b');
    editorInsertNewline();
    editorInsertChar('c');
    int n;
    char *s = editorRowsToString(&n);
    assert(n == 5 && !strcmp(s, "ab\nc\n"));
    free(s);
    editorDelChar();
    editorDelChar();
    s = editorRowsToString(&n);
    assert(!strcmp(s, "ab\n"));
    free(s);
    FILE *f = fopen("CRLF.TXT", "wb");
    assert(f);
    fputs("one\r\ntwo\r\n", f);
    assert(!fclose(f));
    load_file("CRLF.TXT");
    assert(E.numrows == 2 && E.row[0].size == 3 && E.row[1].size == 3 && !E.dirty);
    free(E.filename);
    E.filename = strdup("C:\\SAVE.TXT");
    assert(!editorSave());
    clear_buffer();
    load_file("C:\\SAVE.TXT");
    assert(E.numrows == 2 && !strcmp(E.row[1].chars, "two"));
    clear_buffer();
    remove("CRLF.TXT");
    remove("C:\\SAVE.TXT");
    return 0;
}
