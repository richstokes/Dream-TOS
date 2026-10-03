/* Native text editor model. GPL-2.0-or-later. */
#include "editor_core.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
struct EdChange {
    size_t start, removed, inserted, before_cursor, before_anchor, after_cursor, after_anchor;
    unsigned long before_revision, after_revision, group;
    int typing;
    char data[]; /* removed bytes, then inserted bytes */
};
static int fail(Editor *e, const char *s) { e->error = s; return 0; }
static void history_clear(Editor *e)
{
    for (size_t i = 0; i < e->count; ++i) free(e->history[i]);
    e->undo = e->count = e->history_bytes = 0;
}
void ed_free(Editor *e)
{
    history_clear(e); free(e->text); free(e->lines); memset(e, 0, sizeof(*e));
}
static int reserve(Editor *e, size_t bytes, size_t lines)
{
    if (bytes > ED_MAX_BYTES || lines > ED_MAX_LINES)
        return fail(e, "Document limit: 1 MiB / 32768 lines");
    if (bytes + 1 > e->cap) {
        size_t cap = e->cap ? e->cap : 256;
        while (cap < bytes + 1) cap *= 2;
        if (cap > ED_MAX_BYTES + 1) cap = ED_MAX_BYTES + 1;
        char *p = realloc(e->text, cap);
        if (!p) return fail(e, "Not enough memory; document unchanged");
        e->text = p; e->cap = cap;
    }
    if (lines > e->linecap) {
        size_t cap = e->linecap ? e->linecap : 32;
        while (cap < lines) cap *= 2;
        uint32_t *p = realloc(e->lines, cap * sizeof(*p));
        if (!p) return fail(e, "Not enough memory; document unchanged");
        e->lines = p; e->linecap = cap;
    }
    return 1;
}
static size_t line_count(const char *s, size_t n)
{
    size_t count = 0;
    for (size_t i = 0; i < n; ++i) count += s[i] == '\n';
    return count;
}
static void reindex(Editor *e)
{
    e->nlines = 1; e->lines[0] = 0;
    for (size_t i = 0; i < e->len; ++i)
        if (e->text[i] == '\n') e->lines[e->nlines++] = (uint32_t)(i + 1);
    e->text[e->len] = 0;
}
int ed_init(Editor *e)
{
    memset(e, 0, sizeof(*e)); e->tabstop = 4; e->autoindent = 1;
    if (!reserve(e, 0, 1)) { ed_free(e); return 0; }
    reindex(e); return 1;
}
int ed_load(Editor *e, const char *s, size_t n)
{
    if (n > ED_MAX_BYTES) return fail(e, "Document limit: 1 MiB");
    if (memchr(s, 0, n)) return fail(e, "Binary file: embedded NUL bytes");
    /* Stage the entire document before releasing the old one. */
    Editor next;
    if (!ed_init(&next)) return fail(e, "Not enough memory; document unchanged");
    if (!reserve(&next, n, line_count(s, n) + 1)) {
        const char *error = next.error; ed_free(&next); return fail(e, error);
    }
    memcpy(next.text, s, n); next.len = n; reindex(&next);
    const char *lf = memchr(s, '\n', n);
    next.crlf = lf && lf > s && lf[-1] == '\r';
    next.tabstop = e->tabstop; next.autoindent = e->autoindent; next.spaces = e->spaces;
    ed_free(e); *e = next; return 1;
}
void ed_break_group(Editor *e) { ++e->group; }
void ed_mark_saved(Editor *e) { e->saved = e->revision; ed_break_group(e); }
int ed_dirty(const Editor *e) { return e->saved != e->revision; }
size_t ed_line(const Editor *e, size_t pos)
{
    size_t lo = 0, hi = e->nlines;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (e->lines[mid] <= pos) lo = mid; else hi = mid;
    }
    return lo;
}
size_t ed_end(const Editor *e, size_t line)
{
    if (line + 1 >= e->nlines) return e->len;
    size_t end = e->lines[line + 1] - 1;
    if (end > e->lines[line] && e->text[end - 1] == '\r') --end;
    return end;
}
size_t ed_prev(const Editor *e, size_t p)
{
    if (p) { --p; if (p && e->text[p] == '\n' && e->text[p - 1] == '\r') --p; }
    return p;
}
size_t ed_next(const Editor *e, size_t p)
{
    if (p < e->len) { if (e->text[p] == '\r' && p + 1 < e->len && e->text[p + 1] == '\n') ++p; ++p; }
    return p;
}
static int wordchar(unsigned char c) { return isalnum(c) || c == '_'; }
size_t ed_word(const Editor *e, size_t p, int direction)
{
    if (direction < 0) {
        while (p && !wordchar(e->text[ed_prev(e, p)])) p = ed_prev(e, p);
        while (p && wordchar(e->text[ed_prev(e, p)])) p = ed_prev(e, p);
    } else {
        while (p < e->len && wordchar(e->text[p])) p = ed_next(e, p);
        while (p < e->len && !wordchar(e->text[p])) p = ed_next(e, p);
    }
    return p;
}
size_t ed_column(const Editor *e, size_t pos)
{
    size_t col = 0;
    for (size_t p = e->lines[ed_line(e, pos)]; p < pos; ++p)
        col += e->text[p] == '\t' ? e->tabstop - col % e->tabstop : 1;
    return col;
}
size_t ed_at_column(const Editor *e, size_t line, size_t goal)
{
    size_t p = e->lines[line], end = ed_end(e, line), col = 0;
    while (p < end && col < goal) {
        size_t next = col + (e->text[p] == '\t' ? e->tabstop - col % e->tabstop : 1);
        if (next > goal) break;
        col = next; ++p;
    }
    return p;
}
void ed_select(Editor *e, size_t pos, int extend)
{
    if (pos > e->len) pos = e->len;
    if (pos && pos < e->len && e->text[pos] == '\n' && e->text[pos - 1] == '\r') --pos;
    e->cursor = pos; if (!extend) e->anchor = pos; ed_break_group(e);
}
void ed_selection(const Editor *e, size_t *start, size_t *end)
{
    *start = e->cursor < e->anchor ? e->cursor : e->anchor;
    *end = e->cursor > e->anchor ? e->cursor : e->anchor;
}
static void apply(Editor *e, size_t a, size_t b, const char *s, size_t n)
{
    memmove(e->text + a + n, e->text + b, e->len - b);
    if (n) memcpy(e->text + a, s, n);
    e->len = e->len - (b - a) + n; reindex(e);
}
static size_t cost(const EdChange *c) { return sizeof(*c) + c->removed + c->inserted; }
int ed_replace(Editor *e, size_t a, size_t b, const char *s, size_t n, int typing)
{
    e->error = NULL;
    if (a > b || b > e->len || n > ED_MAX_BYTES || e->len - (b - a) > ED_MAX_BYTES - n)
        return fail(e, "Document limit: 1 MiB");
    if (n && memchr(s, 0, n)) return fail(e, "Cannot insert binary data");
    if (a == b && !n) return 1;
    if (b - a == n && (!n || !memcmp(e->text + a, s, n))) { e->cursor = e->anchor = a + n; return 1; }
    size_t lines = e->nlines - line_count(e->text + a, b - a) + line_count(s, n);
    if (lines > ED_MAX_LINES) return fail(e, "Document limit: 32768 lines");
    EdChange *previous = e->undo ? e->history[e->undo - 1] : NULL;
    int merge = typing && previous && previous->typing && previous->group == e->group &&
        e->undo == e->count && e->revision != e->saved && a == b && !previous->removed &&
        previous->start + previous->inserted == a;
    size_t prefix = merge ? previous->inserted : 0;
    EdChange *c = malloc(sizeof(*c) + b - a + n + prefix);
    if (!c) return fail(e, "Not enough memory for undo; document unchanged");
    *c = (EdChange){.start = merge ? previous->start : a, .removed = b - a,
        .inserted = n + prefix, .before_cursor = e->cursor, .before_anchor = e->anchor,
        .after_cursor = a + n, .after_anchor = a + n, .before_revision = e->revision,
        .after_revision = e->serial + 1, .group = e->group, .typing = typing};
    if (merge) {
        c->before_cursor = previous->before_cursor; c->before_anchor = previous->before_anchor;
        c->before_revision = previous->before_revision;
        memcpy(c->data, previous->data, prefix);
    } else if (b > a) memcpy(c->data, e->text + a, b - a);
    if (n) memcpy(c->data + b - a + prefix, s, n);
    /* Copy the insertion before reserve: s is allowed to alias e->text. */
    if (!reserve(e, e->len - (b - a) + n, lines)) { free(c); return 0; }
    while (e->count > e->undo) {
        EdChange *old = e->history[--e->count]; e->history_bytes -= cost(old); free(old);
    }
    if (merge) { --e->count; --e->undo; e->history_bytes -= cost(previous); free(previous); }
    while (e->count && (e->count == ED_HISTORY_COUNT || e->history_bytes + cost(c) > ED_HISTORY_BYTES)) {
        EdChange *old = e->history[0]; e->history_bytes -= cost(old); free(old);
        --e->count; --e->undo; memmove(e->history, e->history + 1, e->count * sizeof(*e->history));
    }
    apply(e, a, b, c->data + c->removed + prefix, n);
    e->history[e->count++] = c; e->undo = e->count; e->history_bytes += cost(c);
    e->cursor = e->anchor = a + n; e->revision = ++e->serial;
    return 1;
}
int ed_insert(Editor *e, const char *s, size_t n, int typing)
{
    size_t a, b; ed_selection(e, &a, &b); return ed_replace(e, a, b, s, n, typing);
}
int ed_delete(Editor *e, int backward)
{
    size_t a, b; ed_selection(e, &a, &b); ed_break_group(e);
    if (a == b) { if (backward) a = ed_prev(e, a); else b = ed_next(e, b); }
    return ed_replace(e, a, b, NULL, 0, 0);
}
int ed_newline(Editor *e)
{
    size_t a, b; ed_selection(e, &a, &b);
    size_t start = e->lines[ed_line(e, a)], indent = 0;
    if (e->autoindent) while (start + indent < a && (e->text[start + indent] == ' ' || e->text[start + indent] == '\t')) ++indent;
    size_t nl = e->crlf ? 2 : 1;
    char *s = malloc(nl + indent);
    if (!s) return fail(e, "Not enough memory; document unchanged");
    memcpy(s, e->crlf ? "\r\n" : "\n", nl); memcpy(s + nl, e->text + start, indent);
    ed_break_group(e); int ok = ed_replace(e, a, b, s, nl + indent, 0); free(s); return ok;
}
int ed_indent(Editor *e, int outdent)
{
    size_t a, b; ed_selection(e, &a, &b); ed_break_group(e);
    if (a == b && !outdent) {
        char s[8]; memset(s, ' ', sizeof(s));
        return ed_insert(e, e->spaces ? s : "\t", e->spaces ? e->tabstop - ed_column(e, a) % e->tabstop : 1, 0);
    }
    size_t first = ed_line(e, a), last = ed_line(e, b);
    if (b > a && last > first && b == e->lines[last]) --last;
    size_t start = e->lines[first], end = last + 1 < e->nlines ? e->lines[last + 1] : e->len;
    size_t cap = end - start + (last - first + 1) * e->tabstop;
    char *s = malloc(cap + 1);
    if (!s) return fail(e, "Not enough memory; document unchanged");
    size_t n = 0;
    for (size_t line = first; line <= last; ++line) {
        size_t p = e->lines[line], stop = line + 1 < e->nlines ? e->lines[line + 1] : e->len;
        if (outdent) {
            if (p < stop && e->text[p] == '\t') ++p;
            else for (int i = 0; i < e->tabstop && p < stop && e->text[p] == ' '; ++i) ++p;
        } else if (e->spaces) { memset(s + n, ' ', e->tabstop); n += e->tabstop; }
        else s[n++] = '\t';
        memcpy(s + n, e->text + p, stop - p); n += stop - p;
    }
    int selected = a != b;
    unsigned long revision = e->revision;
    int ok = ed_replace(e, start, end, s, n, 0);
    if (ok) {
        if (selected) { e->anchor = start; e->cursor = start + n; }
        else e->cursor = e->anchor = ed_at_column(e, first, 0);
        if (e->revision != revision && e->undo) { e->history[e->undo - 1]->after_cursor = e->cursor; e->history[e->undo - 1]->after_anchor = e->anchor; }
    }
    free(s); return ok;
}
int ed_undo(Editor *e, int redo)
{
    ed_break_group(e); e->error = NULL;
    if (redo ? e->undo == e->count : !e->undo) return 0;
    EdChange *c = e->history[redo ? e->undo : e->undo - 1];
    size_t remove = redo ? c->removed : c->inserted, n = redo ? c->inserted : c->removed;
    const char *s = c->data + (redo ? c->removed : 0);
    size_t lines = e->nlines - line_count(e->text + c->start, remove) + line_count(s, n);
    if (!reserve(e, e->len - remove + n, lines)) return 0;
    apply(e, c->start, c->start + remove, s, n);
    e->cursor = redo ? c->after_cursor : c->before_cursor; e->anchor = redo ? c->after_anchor : c->before_anchor;
    e->revision = redo ? c->after_revision : c->before_revision;
    if (redo) ++e->undo; else --e->undo; return 1;
}
static int equal(const char *a, const char *b, size_t n, int match_case)
{
    for (size_t i = 0; i < n; ++i)
        if (match_case ? a[i] != b[i] : tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}
int ed_find(Editor *e, const char *query, int backward, int match_case)
{
    size_t n = strlen(query), a, b; ed_selection(e, &a, &b); ed_break_group(e);
    if (!n || n > e->len) return 0;
    size_t limit = e->len - n + 1, start = backward ? a : b;
    if (start > limit) start = backward ? limit : 0;
    for (size_t step = 0; step < limit; ++step) {
        size_t p = backward ? (start + limit - 1 - step) % limit : (start + step) % limit;
        if (equal(e->text + p, query, n, match_case)) { e->anchor = p; e->cursor = p + n; return 1; }
    }
    return 0;
}
int ed_replace_all(Editor *e, const char *query, const char *replacement, int match_case)
{
    size_t q = strlen(query), r = strlen(replacement), matches = 0;
    if (!q) return 0;
    for (size_t p = 0; p + q <= e->len; ) {
        if (equal(e->text + p, query, q, match_case)) { ++matches; p += q; } else ++p;
    }
    if (!matches) return 0;
    if (r > q && matches > (ED_MAX_BYTES - e->len) / (r - q)) { fail(e, "Document limit: 1 MiB"); return -1; }
    size_t len = e->len - matches * q + matches * r;
    char *s = malloc(len + 1);
    if (!s) { fail(e, "Not enough memory; document unchanged"); return -1; }
    size_t n = 0;
    for (size_t p = 0; p < e->len; ) {
        if (p + q <= e->len && equal(e->text + p, query, q, match_case)) {
            memcpy(s + n, replacement, r); n += r; p += q;
        } else s[n++] = e->text[p++];
    }
    ed_break_group(e); int ok = ed_replace(e, 0, e->len, s, n, 0); free(s);
    return ok ? (int)matches : -1;
}
static int keyword(const char *p, size_t n, const char *words)
{
    while (*words) {
        const char *end = strchr(words, ' '); size_t len = end ? (size_t)(end - words) : strlen(words);
        if (len == n && !memcmp(p, words, n)) return 1;
        words += len; if (*words) ++words;
    }
    return 0;
}
void ed_highlight_range(const Editor *e, size_t line, int *state,
                        unsigned char *colours, size_t from, size_t to)
{
    size_t a = e->lines[line], end = ed_end(e, line), i = a;
    /* State 1 is a block comment; 2/3 are continued string/character literals. */
    int mode = *state, first = 1;
    while (i < end && (!colours || i < to)) {
        size_t start = i; int colour = 0;
        if (mode == 1) {
            colour = 1;
            while (i < end) { if (i + 1 < end && e->text[i] == '*' && e->text[i + 1] == '/') { i += 2; mode = 0; break; } ++i; }
        } else if (mode == 2 || mode == 3 || e->text[i] == '"' || e->text[i] == '\'') {
            int quote = mode ? (mode == 2 ? '"' : '\'') : e->text[i++]; colour = 4; mode = 0;
            while (i < end) {
                if (e->text[i] == '\\') { if (i + 1 == end) { mode = quote == '"' ? 2 : 3; ++i; break; } i += 2; }
                else if (e->text[i++] == quote) break;
            }
        } else if (i + 1 < end && e->text[i] == '/' && e->text[i + 1] == '/') { colour = 1; i = end; }
        else if (i + 1 < end && e->text[i] == '/' && e->text[i + 1] == '*') { colour = 1; i += 2; mode = 1; }
        else if (first && e->text[i] == '#') { colour = 6; ++i; while (i < end && (isalpha((unsigned char)e->text[i]) || e->text[i] == ' ')) ++i; }
        else if (isdigit((unsigned char)e->text[i])) { colour = 5; ++i; while (i < end && (isalnum((unsigned char)e->text[i]) || e->text[i] == '.')) ++i; }
        else if (isalpha((unsigned char)e->text[i]) || e->text[i] == '_') {
            ++i; while (i < end && wordchar(e->text[i])) ++i;
            if (keyword(e->text + start, i - start, "if else while for do switch case default return break continue goto sizeof typedef struct enum union static const extern volatile register auto inline restrict class public private protected namespace using new delete try catch throw template virtual override nullptr true false")) colour = 2;
            else if (keyword(e->text + start, i - start, "void char short int long float double signed unsigned bool size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t")) colour = 3;
        } else ++i;
        if (colours) {
            size_t lo = start > from ? start : from, hi = i < to ? i : to;
            if (hi > lo) memset(colours + lo - from, colour, hi - lo);
        }
        for (size_t p = start; p < i; ++p) if (!isspace((unsigned char)e->text[p])) first = 0;
    }
    *state = mode;
}
void ed_highlight(const Editor *e, size_t line, int *state, unsigned char *colours)
{
    ed_highlight_range(e, line, state, colours, e->lines[line], ed_end(e, line));
}
