/* Editor behavior and failure recovery with the real model/frontend under ASan.
 * File faults are injected at the I/O boundary, allocation faults at malloc. */
#define APP_HOST_TEST
#include "app.h"
#include "window.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int allocation_fail = -1;
static void *test_malloc(size_t n) { if (allocation_fail == 0) return NULL; if (allocation_fail > 0) --allocation_fail; return malloc(n); }
static void *test_realloc(void *p, size_t n) { if (allocation_fail == 0) return NULL; if (allocation_fail > 0) --allocation_fail; return realloc(p, n); }
#define malloc test_malloc
#define realloc test_realloc
#include "../apps/ports/editor_core.c"
#undef malloc
#undef realloc
static int rename_calls, fail_rename, fail_restore, fail_write;
static int test_rename(const char *a, const char *b)
{
    ++rename_calls;
    if (rename_calls == fail_rename || (fail_restore && rename_calls == 3)) { errno = EIO; return -1; }
    return rename(a, b);
}
static ssize_t test_write(int fd, const void *p, size_t n)
{
    if (fail_write) { errno = ENOSPC; return -1; }
    return write(fd, p, n);
}
#define ED_RENAME test_rename
#define ED_WRITE test_write
#include "../apps/ports/editor_file.c"
static int alert_choice = 3, picker_accept, prompt_result;
static const char *pick_name = "SAVE.TXT";
static void test_aes(int op, int ni, int no, int na)
{
    aes_call(op, ni, no, na);
    if (op == 52) ao[0] = alert_choice;
    if (op == 91) { ao[0] = 1; ao[1] = picker_accept; strcpy((char *)aa[1], pick_name); }
    if (op == 50) ao[0] = prompt_result;
}
#define aes_call test_aes
#define app_main guest_app_main
#include "../apps/ports/editor.c"
#undef aes_call
/* Windows are rendered by the separate GEM integration test. */
int app_begin_windowed(void) { return 1; }
int app_window_open_kind(AppWindow *w, const char *t, int a, int b, int c, int d, int k)
{ (void)w; (void)t; (void)a; (void)b; (void)c; (void)d; (void)k; return 1; }
void app_window_close(AppWindow *w) { w->handle = -1; }
int app_window_message(AppWindow *w, const int16_t *m) { (void)w; (void)m; return WINDOW_IGNORE; }
void app_window_redraw(AppWindow *w, AppRect r, void (*f)(void)) { (void)w; (void)r; (void)f; }
int app_window_contains(AppWindow *w, int x, int y) { return x >= w->work.x && y >= w->work.y && x < w->work.x + w->work.w && y < w->work.y + w->work.h; }
void app_window_event(AppEvent *e, int ms, int b) { (void)e; (void)ms; (void)b; }
static long fake_info(void *p, uint32_t n)
{
    struct dc_system_info *i = p; assert(n == sizeof(*i)); memset(i, 0, n);
    i->version = DC_SYSTEM_INFO_VERSION; i->drive_mask = 4 | 8 | 16; i->readonly_mask = 8; i->volatile_mask = 4; return n;
}
static struct dc_native_api api = {.version = 1, .size = sizeof(api), .system_info = fake_info};
static void content(const char *s) { assert(doc.len == strlen(s)); assert(!memcmp(doc.text, s, doc.len + 1)); }
static void load(const char *s) { assert(ed_load(&doc, s, strlen(s))); set_syntax(); layout(); }
static void disk(const char *path, const char *s)
{ FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(s, 1, strlen(s), f) == strlen(s)); assert(!fclose(f)); }
static void disk_content(const char *path, const char *s)
{ char *p, error[128]; size_t n; assert(editor_read_file(path, &p, &n, error, sizeof(error))); assert(n == strlen(s) && !memcmp(p, s, n)); free(p); }
static void test_edits(void)
{
    load(""); keypress('a', 0); keypress('b', 0); keypress('c', 0); content("abc");
    keypress(26, 4); content(""); keypress(25, 4); content("abc");
    ed_mark_saved(&doc); assert(!ed_dirty(&doc));
    keypress('d', 0); keypress(26, 4); content("abc"); assert(!ed_dirty(&doc));
    keypress(25, 4); content("abcd"); assert(ed_dirty(&doc));
    keypress(KEY_HOME, 0); keypress(KEY_RIGHT, 1); keypress(KEY_RIGHT, 1);
    assert(doc.anchor == 0 && doc.cursor == 2); keypress('X', 0); content("Xcd");
    keypress(26, 4); content("abcd"); assert(doc.anchor == 0 && doc.cursor == 2);
    keypress(1, 4); keypress(KEY_DELETE, 0); content(""); keypress(26, 4); content("abcd");
    load("one\r\ntwo"); assert(doc.crlf); assert(ed_end(&doc, 0) == 3);
    ed_select(&doc, 3, 0); keypress(KEY_RIGHT, 0); assert(doc.cursor == 5);
    keypress(8, 0); content("onetwo"); keypress(26, 4); content("one\r\ntwo");
    ed_select(&doc, 3, 0); keypress(KEY_DELETE, 0); content("onetwo");
    ed_select(&doc, doc.len, 0); keypress(KEY_DELETE, 0); content("onetwo");
    load("\t  hello"); ed_select(&doc, doc.len, 0); keypress(13, 0); content("\t  hello\n\t  ");
    keypress(26, 4); content("\t  hello");
    load("a\nb\nc"); ed_select(&doc, 0, 0); ed_select(&doc, 4, 1); keypress(9, 0); content("\ta\n\tb\nc");
    keypress(9, 1); content("a\nb\nc"); keypress(26, 4); content("\ta\n\tb\nc");
    load("alpha beta gamma"); ed_select(&doc, 0, 0); keypress(KEY_RIGHT, 4); assert(doc.cursor == 6);
    keypress(KEY_END, 4); assert(doc.cursor == doc.len); keypress(KEY_LEFT, 5); assert(doc.cursor == 11 && doc.anchor == doc.len);
    keypress(KEY_HOME, 4); assert(doc.cursor == 0 && doc.anchor == 0);
    /* Unhandled shortcuts must not add control bytes. */
    keypress(2, 4); content("alpha beta gamma");
}
static void test_search(void)
{
    load("one one\nONE one");
    assert(ed_find(&doc, "one", 0, 1) && doc.anchor == 0);
    assert(ed_find(&doc, "one", 0, 1) && doc.anchor == 4);
    assert(ed_find(&doc, "one", 0, 1) && doc.anchor == 12);
    assert(ed_find(&doc, "one", 0, 1) && doc.anchor == 0);
    assert(ed_find(&doc, "one", 1, 1) && doc.anchor == 12);
    ed_select(&doc, doc.len, 0); assert(ed_find(&doc, "one", 1, 1) && doc.anchor == 12);
    ed_select(&doc, doc.len, 0); assert(ed_find(&doc, "one", 0, 1) && doc.anchor == 0);
    assert(ed_replace_all(&doc, "one", "two", 0) == 4); content("two two\ntwo two");
    assert(ed_undo(&doc, 0)); content("one one\nONE one");
    assert(ed_replace_all(&doc, "one", "", 1) == 3); content(" \nONE ");
    assert(ed_undo(&doc, 0)); assert(ed_replace_all(&doc, "", "x", 1) == 0);
    assert(!ed_find(&doc, "absent", 0, 1));
}
static void test_view(void)
{
    window.work = (AppRect){0, 20, 640, 430};
    load("\tint x = 42; /* comment\n continued */ return x;\n");
    strcpy(filename, "E:\\TEST.C"); set_syntax(); layout();
    unsigned char hl[80]; int state = 0;
    ed_highlight(&doc, 0, &state, hl); assert(state == 1 && hl[1] == 3 && hl[9] == 5 && hl[13] == 1);
    ed_highlight(&doc, 1, &state, hl); assert(!state && hl[1] == 1 && hl[14] == 2);
    ed_select(&doc, 1, 0); reveal(); assert(ed_column(&doc, doc.cursor) == 4);
    int v, x; cursor_visual(&v, &x); assert(v == 0 && x == 4);
    assert(position_at(0, 3) == 0 && position_at(0, 4) == 1);
    /* Resize + word wrap round trips byte positions, including tabs. */
    window.work.w = 320; wrap = 1;
    load("one two three four five six seven eight nine ten\n\tfoo bar baz\n");
    layout(); assert(total > (int)doc.nlines);
    for (size_t p = 0; p < doc.len; ++p) {
        ed_select(&doc, p, 0); cursor_visual(&v, &x);
        assert(position_at(v, x) == p);
    }
    ed_select(&doc, doc.len, 0); reveal(); refresh(1); assert(top <= max_top());
    wrap = 0; window.work.w = 640; layout();
    /* Dragging selects and release preserves the selection. */
    load("abcdef\nxyz"); AppEvent ev = {.x = text_x + 8, .y = text_y + 2, .buttons = 1};
    buttons = 0; pointer_event(&ev); assert(doc.cursor == 1);
    ev.x += 24; pointer_event(&ev); assert(doc.anchor == 1 && doc.cursor == 4);
    ev.buttons = 0; pointer_event(&ev); assert(!dragging && doc.anchor == 1);
    menu_build(); menu_update(); assert(menu[0].tail == 8 && menu[9].head == 10);
    assert(menu[menu[8].tail].next == 8);
}
static void test_allocations_and_limits(void)
{
    for (int fault = 0; fault < 5; ++fault) {
        load("keep"); ed_select(&doc, 2, 0); ed_mark_saved(&doc);
        char large[600]; memset(large, '\n', sizeof(large));
        allocation_fail = fault;
        int ok = ed_insert(&doc, large, sizeof(large), 0);
        allocation_fail = -1;
        if (!ok) { content("keep"); assert(doc.cursor == 2 && !doc.undo && !ed_dirty(&doc)); }
        else { assert(ed_undo(&doc, 0)); content("keep"); }
    }
    load("keep"); allocation_fail = 0; assert(!ed_load(&doc, "new", 3)); allocation_fail = -1; content("keep");
    assert(!ed_load(&doc, "a\0b", 3)); content("keep");
    char *big = malloc(ED_MAX_BYTES + 1); assert(big); memset(big, 'x', ED_MAX_BYTES + 1);
    assert(ed_load(&doc, big, ED_MAX_BYTES)); ed_select(&doc, doc.len, 0);
    assert(!ed_insert(&doc, "x", 1, 0)); assert(doc.len == ED_MAX_BYTES);
    assert(!ed_load(&doc, big, ED_MAX_BYTES + 1)); assert(doc.len == ED_MAX_BYTES);
    assert(ed_delete(&doc, 1)); assert(ed_undo(&doc, 0)); assert(doc.len == ED_MAX_BYTES);
    memset(big, '\n', ED_MAX_LINES); assert(!ed_load(&doc, big, ED_MAX_LINES));
    assert(ed_load(&doc, big, ED_MAX_LINES - 1)); assert(doc.nlines == ED_MAX_LINES);
    ed_select(&doc, doc.len, 0); assert(!ed_newline(&doc)); free(big);
    load("");
    for (int i = 0; i < 200; ++i) { ed_break_group(&doc); assert(ed_insert(&doc, "x", 1, 0)); }
    assert(doc.count == ED_HISTORY_COUNT && doc.history_bytes <= ED_HISTORY_BYTES);
    for (int i = 0; i < ED_HISTORY_COUNT; ++i) assert(ed_undo(&doc, 0));
    assert(!ed_undo(&doc, 0) && doc.len == 200 - ED_HISTORY_COUNT);
}
static void test_random_edits(void)
{
    char reference[2048] = ""; size_t n = 0; load(""); srand(41);
    for (int i = 0; i < 2500; ++i) {
        size_t a = rand() % (n + 1), b = a + rand() % (n - a + 1), len = rand() % 12;
        char added[12]; for (size_t j = 0; j < len; ++j) added[j] = rand() % 5 ? 'a' + rand() % 26 : '\n';
        char before[2048]; memcpy(before, reference, n + 1); size_t oldlen = n;
        unsigned long revision = doc.revision;
        assert(ed_replace(&doc, a, b, added, len, 0));
        memmove(reference + a + len, reference + b, n - b); memcpy(reference + a, added, len);
        n += len; n -= b - a; reference[n] = 0; content(reference);
        size_t line = 0; assert(doc.lines[0] == 0);
        for (size_t p = 0; p < n; ++p) if (reference[p] == '\n') assert(doc.lines[++line] == p + 1);
        assert(doc.nlines == line + 1);
        if (doc.revision != revision) {
            assert(ed_undo(&doc, 0)); assert(doc.len == oldlen); content(before);
            assert(ed_undo(&doc, 1)); content(reference);
        }
    }
}
static void test_files(void)
{
    dc_os = &api; char path[128]; default_path(path, sizeof(path)); assert(!strcmp(path, "E:\\NOTES.TXT"));
    disk("E:\\SOURCE.TXT", "one\r\ntwo"); assert(load_document("E:\\SOURCE.TXT")); content("one\r\ntwo");
    assert(save_document(0)); disk_content("E:\\SOURCE.TXT", "one\r\ntwo");
    load("edited"); strcpy(filename, "E:\\SOURCE.TXT"); ed_select(&doc, doc.len, 0); ed_insert(&doc, "!", 1, 0);
    fail_write = 1; assert(!save_document(0)); fail_write = 0; assert(ed_dirty(&doc)); disk_content(filename, "one\r\ntwo");
    rename_calls = 0; fail_rename = 2; assert(!save_document(0)); assert(strstr(status, "New copy"));
    disk_content(filename, "one\r\ntwo"); disk_content("E:\\ED000000.TMP", "edited!"); remove("E:\\ED000000.TMP");
    rename_calls = 0; fail_restore = 1; assert(!save_document(0)); assert(strstr(status, "Recovery files"));
    disk_content("E:\\ED000000.BAK", "one\r\ntwo"); disk_content("E:\\ED000000.TMP", "edited!");
    assert(!rename("E:\\ED000000.BAK", filename)); remove("E:\\ED000000.TMP");
    fail_rename = fail_restore = 0; assert(save_document(0)); assert(!ed_dirty(&doc)); disk_content(filename, "edited!");
    /* Cancellation, failed opens and save-before-close leave the buffer available. */
    ed_insert(&doc, "more", 4, 0); alert_choice = 3; assert(!may_discard());
    assert(!load_document("E:\\ABSENT.TXT")); content("edited!more");
    strcpy(filename, "D:\\READONLY.TXT"); alert_choice = 1; picker_accept = 0; assert(!may_discard()); content("edited!more");
    picker_accept = 1; assert(may_discard()); assert(!ed_dirty(&doc)); assert(!strcmp(filename, "E:\\SAVE.TXT"));
    remove("E:\\SAVE.TXT"); remove("E:\\SOURCE.TXT");
}
int main(void)
{
    assert(ed_init(&doc)); window.work = (AppRect){0, 20, 640, 430}; window.handle = 1;
    test_edits(); test_search(); test_view(); test_allocations_and_limits(); test_random_edits(); test_files();
    ed_free(&doc); free(clipboard);
    puts("Editor: undo, selection, CRLF, search, wrap, syntax, limits, allocation faults and save recovery PASS");
    return 0;
}
