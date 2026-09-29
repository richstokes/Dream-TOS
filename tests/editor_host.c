#define APP_HOST_TEST
#define app_main guest_app_main
#include "../apps/ports/editor.c"
#include <assert.h>
static long fake_info(void *buffer, uint32_t bytes)
{
    struct dc_system_info *i = buffer;
    if (bytes != sizeof(*i)) return -64;
    memset(i, 0, sizeof(*i));
    i->version = DC_SYSTEM_INFO_VERSION;
    i->drive_mask = 4 | 8 | 16;      /* C: RAM, D: disc, E: SD */
    i->readonly_mask = 8;
    i->volatile_mask = 4;
    return sizeof(*i);
}
static struct dc_native_api fake_api = {.version = 1, .size = sizeof(fake_api), .system_info = fake_info};
int main(void)
{
    /* Older OS without system_info: only the RAM disk is writable. */
    assert(drive_state('C') == 2 && drive_state('c') == 2 && !drive_state('D') && !drive_state('E'));
    dc_os = &fake_api;
    assert(drive_state('C') == 2 && !drive_state('D') && drive_state('E') == 1 && drive_state('e') == 1);
    assert(!drive_state('F') && !drive_state('1') && !drive_state('['));
    assert(has_drive("E:\\A.TXT") && !has_drive("A.TXT") && !has_drive("1:X"));
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
    /* Saving to a persistent SD drive succeeds and is not called temporary. */
    editorInsertChar('x');
    free(E.filename);
    E.filename = strdup("E:\\SD.TXT");
    has_path = 1;
    assert(!editorSave() && !E.dirty && !strstr(E.statusmsg, "lost at reset") && strstr(E.statusmsg, "on E:"));
    free(E.filename);
    E.filename = strdup("C:\\SAVE.TXT");
    editorInsertChar('y');
    assert(!editorSave() && strstr(E.statusmsg, "lost at reset"));
    clear_buffer();
    remove("CRLF.TXT");
    remove("C:\\SAVE.TXT");
    remove("E:\\SD.TXT");
    return 0;
}
