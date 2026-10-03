/* Checked, byte-preserving text editing for the native GEM editor.
 * GPL-2.0-or-later. Offsets are byte positions; CRLF is one cursor step. */
#ifndef EDITOR_CORE_H
#define EDITOR_CORE_H
#include <stddef.h>
#include <stdint.h>
#define ED_MAX_BYTES (1024u * 1024u)
#define ED_MAX_LINES 32768u
#define ED_HISTORY_BYTES (2u * 1024u * 1024u)
#define ED_HISTORY_COUNT 128

typedef struct EdChange EdChange;
typedef struct {
    char *text;
    size_t len, cap, cursor, anchor;
    uint32_t *lines;
    size_t nlines, linecap;
    int crlf, tabstop, autoindent, spaces;
    unsigned long revision, saved, serial, group;
    EdChange *history[ED_HISTORY_COUNT];
    size_t undo, count, history_bytes;
    const char *error;
} Editor;
int ed_init(Editor *e);
void ed_free(Editor *e);
int ed_load(Editor *e, const char *text, size_t len);
int ed_replace(Editor *e, size_t start, size_t end, const char *s, size_t n, int typing);
int ed_insert(Editor *e, const char *s, size_t n, int typing);
int ed_delete(Editor *e, int backward);
int ed_newline(Editor *e);
int ed_indent(Editor *e, int outdent);
int ed_undo(Editor *e, int redo);
void ed_break_group(Editor *e);
void ed_mark_saved(Editor *e);
int ed_dirty(const Editor *e);
size_t ed_line(const Editor *e, size_t pos);
size_t ed_end(const Editor *e, size_t line);
size_t ed_prev(const Editor *e, size_t pos);
size_t ed_next(const Editor *e, size_t pos);
size_t ed_word(const Editor *e, size_t pos, int direction);
size_t ed_column(const Editor *e, size_t pos);
size_t ed_at_column(const Editor *e, size_t line, size_t column);
void ed_select(Editor *e, size_t pos, int extend);
void ed_selection(const Editor *e, size_t *start, size_t *end);
int ed_find(Editor *e, const char *query, int backward, int match_case);
int ed_replace_all(Editor *e, const char *query, const char *replacement, int match_case);
/* Colours: normal, comment, keyword, type, string, number, preprocessor. */
void ed_highlight(const Editor *e, size_t line, int *state, unsigned char *colours);
void ed_highlight_range(const Editor *e, size_t line, int *state,
                        unsigned char *colours, size_t from, size_t to);
#endif
