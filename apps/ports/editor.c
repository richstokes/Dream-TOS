/* DreamEdit - native GEM text editor. GPL-2.0-or-later.
 * The original terminal engine has been replaced by a checked document model.
 * The vendored BSD Kilo source remains unmodified for provenance. */
#include "app.h"
#include "window.h"
#include "drives.h"
#include "editor_core.h"
#include "editor_file.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int16_t next, head, tail;
    uint16_t type, flags, state;
    intptr_t spec;
    int16_t x, y, w, h;
} Object;
typedef struct {
    char *text, *template, *valid;
    int16_t font, junk1, just, colour, junk2, thickness, textlen, templatelen;
} TextInfo;
static Editor doc;
static AppWindow window = {.handle = -1};
static char filename[128], status[256], title[180], last_title[180];
static char query[64], replacement[64];
static char *clipboard;
static size_t clipboard_len;
static int clipboard_local;
static int wrap, numbers = 1, syntax, match_case = 1;
static int rows, cols, gutter, text_x, text_y, top, left, total, widest;
static int buttons, dragging, done, force_redraw = 1, preferred_col = -1;
static int cache_top = -1, cache_left = -1, cache_cols = -1, cache_rows = -1;
static unsigned long indexed_revision = ~0ul, last_type_time;
static int indexed_cols = -1, indexed_tab = -1, indexed_wrap = -1, indexed_syntax = -1;
static uint32_t visual[ED_MAX_LINES + 1], widths[ED_MAX_LINES];
static unsigned char states[ED_MAX_LINES];
#define MAX_ROWS 30
#define MAX_COLS 80
typedef struct {
    char text[MAX_COLS + 1], number[8];
    unsigned char colour[MAX_COLS], selected[MAX_COLS];
    int caret;
} ScreenRow;
static ScreenRow screen[MAX_ROWS], previous[MAX_ROWS];
static struct { size_t line, start, end, column; } visible_segments[MAX_ROWS];
static int caret_row, caret_column;
static char status_line[160], old_status[160];
static unsigned redraw_rows;
static int redraw_status;
static int draw_background;
static Object menu[100];
static int menu_action[100], menu_count;
enum {
    C_ABOUT = 1, C_NEW, C_OPEN, C_SAVE, C_SAVE_AS, C_QUIT,
    C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_SELECT_ALL, C_INDENT, C_OUTDENT,
    C_FIND, C_NEXT, C_PREVIOUS, C_REPLACE, C_REPLACE_ALL, C_GOTO, C_CASE,
    C_WRAP, C_NUMBERS, C_AUTOINDENT, C_TABS, C_SPACES
};
static void message(const char *fmt, ...)
{
    va_list args; va_start(args, fmt); vsnprintf(status, sizeof(status), fmt, args); va_end(args);
}
static int has_drive(const char *p)
{ return p[0] && isalpha((unsigned char)p[0]) && p[1] == ':'; }
static void default_path(char *path, size_t cap)
{ snprintf(path, cap, "%c:\\NOTES.TXT", dc_storage_drive()); }
static void set_syntax(void)
{
    const char *ext = strrchr(filename, '.'); char lower[12] = {0};
    if (ext) for (size_t i = 0; ext[i] && i + 1 < sizeof(lower); ++i) lower[i] = tolower((unsigned char)ext[i]);
    syntax = !strcmp(lower, ".c") || !strcmp(lower, ".h") || !strcmp(lower, ".cpp") ||
        !strcmp(lower, ".hpp") || !strcmp(lower, ".cc");
    indexed_revision = ~0ul;
}
static int confirm(const char *s, int default_button)
{
    ai[0] = default_button; aa[0] = (intptr_t)s; aes_call(52, 1, 1, 1); force_redraw = 1; return ao[0];
}
static Object object(int next, int type, int flags, intptr_t spec, int x, int y, int w, int h)
{ return (Object){next, -1, -1, type, flags, 0, spec, x, y, w, h}; }
/* Modal AES form: GEM saves/restores ownership and dispatches desktop events. */
static int prompt(const char *label, char *buffer, size_t cap)
{
    char text[64], mask[64], valid[] = "X";
    size_t length = cap < sizeof(text) ? cap : sizeof(text);
    snprintf(text, length, "%s", buffer);
    memset(mask, '_', length - 1); mask[length - 1] = 0;
    TextInfo ted = {text, mask, valid, 3, 0, 0, 0x1100, 0, -1, length, length};
    Object tree[5];
    tree[0] = object(-1, 20, 0, 0x21100, 0, 0, 544, 128); tree[0].head = 1; tree[0].tail = 4;
    tree[1] = object(2, 28, 0, (intptr_t)label, 16, 16, 512, 16);
    tree[2] = object(3, 30, 8, (intptr_t)&ted, 16, 48, (length - 1) * 8, 18);
    tree[3] = object(4, 26, 1 | 2 | 4, (intptr_t)"OK", 128, 88, 96, 24);
    tree[4] = object(0, 26, 1 | 4 | 32, (intptr_t)"Cancel", 304, 88, 96, 24);
    aa[0] = (intptr_t)tree; aes_call(54, 0, 5, 1);
    int16_t rect[4] = {ao[1], ao[2], ao[3], ao[4]};
    memset(ai, 0, sizeof(ai)); memcpy(ai + 5, rect, sizeof(rect)); aes_call(51, 9, 1, 0);
    ai[0] = 0; ai[1] = 8; memcpy(ai + 2, rect, sizeof(rect)); aa[0] = (intptr_t)tree; aes_call(42, 6, 1, 1);
    ai[0] = 2; aa[0] = (intptr_t)tree; aes_call(50, 1, 1, 1); int accepted = (ao[0] & 0x7fff) == 3;
    memset(ai, 0, sizeof(ai)); ai[0] = 3; memcpy(ai + 5, rect, sizeof(rect)); aes_call(51, 9, 1, 0);
    if (accepted) snprintf(buffer, cap, "%s", text);
    force_redraw = 1; return accepted;
}
static int choose_file(const char *label, char *path, size_t cap)
{
    char directory[256], name[16];
    const char *base = strrchr(path, '\\');
    if (base) {
        size_t prefix = (size_t)(base - path + 1);
        if (prefix + 4 >= sizeof(directory)) { message("Path too long"); return 0; }
        memcpy(directory, path, prefix); strcpy(directory + prefix, "*.*");
        snprintf(name, sizeof(name), "%.12s", base + 1);
    } else { snprintf(directory, sizeof(directory), "%c:\\*.*", dc_storage_drive()); name[0] = 0; }
    aa[0] = (intptr_t)directory; aa[1] = (intptr_t)name; aa[2] = (intptr_t)label;
    aes_call(91, 0, 2, 3); force_redraw = 1;
    if (!ao[0] || !ao[1] || !name[0]) return 0;
    char *slash = strrchr(directory, '\\');
    if (!slash || !has_drive(directory) || strchr(name, '\\') || strchr(name, ':') || strchr(name, '*') || strchr(name, '?')) {
        message("Choose a directory and a DOS 8.3 filename"); return 0;
    }
    slash[1] = 0;
    if (strlen(directory) + strlen(name) >= cap) { message("Path too long"); return 0; }
    snprintf(path, cap, "%s%s", directory, name); return 1;
}
static int save_document(int save_as)
{
    char path[128]; snprintf(path, sizeof(path), "%s", filename);
    if (!has_drive(path) || !dc_drive_state(path[0])) {
        const char *base = strrchr(filename, '\\');
        if (base && base[1]) snprintf(path, sizeof(path), "%c:\\%s", dc_storage_drive(), base + 1);
        else default_path(path, sizeof(path));
        save_as = 1;
    }
    if (!filename[0]) save_as = 1;
    if (save_as) {
        if (!choose_file("Save text file", path, sizeof(path))) return 0;
        if (!dc_drive_state(path[0])) { message("Drive is read-only or not mounted"); return 0; }
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); if (confirm("[2][Replace the selected file?][Replace|Cancel]", 2) != 1) return 0; }
    }
    if (!editor_write_file(path, doc.text, doc.len, status, sizeof(status))) {
        /* Wrap recovery paths so the alert fits the 640-pixel screen. */
        char alert[320] = "[1]["; size_t used = 4, n = strlen(status);
        for (size_t i = 0; i < n && i < 240; ++i) {
            if (i && i % 48 == 0) alert[used++] = '|';
            alert[used++] = status[i];
        }
        strcpy(alert + used, "][OK]"); confirm(alert, 1); return 0;
    }
    snprintf(filename, sizeof(filename), "%s", path); set_syntax(); ed_mark_saved(&doc);
    if (dc_drive_state(path[0]) == DC_DRIVE_VOLATILE) {
        size_t n = strlen(status);
        snprintf(status + n, sizeof(status) - n, " (C: lost at reset)");
    }
    return 1;
}
static int may_discard(void)
{
    if (!ed_dirty(&doc)) return 1;
    int choice = confirm("[2][Save changes before continuing?][Save|Discard|Cancel]", 1);
    return choice == 2 || (choice == 1 && save_document(0));
}
static int load_document(const char *path)
{
    if (strlen(path) >= sizeof(filename)) { message("Path too long; document unchanged"); return 0; }
    char *text; size_t length;
    if (!editor_read_file(path, &text, &length, status, sizeof(status))) return 0;
    int ok = ed_load(&doc, text, length); free(text);
    if (!ok) { message("%s", doc.error); return 0; }
    snprintf(filename, sizeof(filename), "%s", path); set_syntax(); top = left = 0; preferred_col = -1;
    force_redraw = 1; message("Opened %s", path); return 1;
}
static int scrap_path(char *path, size_t cap)
{
    char directory[128] = {0};
    aa[0] = (intptr_t)directory; aes_call(80, 0, 1, 1);
    if (!directory[0]) {
        /* A private directory avoids colliding with unrelated C: files. */
        if (!dc_os || !dc_os->gemdos) return 0;
        strcpy(directory, "C:\\CLIPBRD\\");
        aa[0] = (intptr_t)directory; aes_call(81, 0, 1, 1);
    }
    /* AES can advertise its default scrap directory before it exists. */
    if (dc_os && dc_os->gemdos && (!strcmp(directory, "C:\\CLIPBRD\\") || !strcmp(directory, "C:\\CLIPBRD")))
        dc_os->gemdos(0x39, "C:\\CLIPBRD");
    size_t n = strlen(directory);
    if (n + 11 >= cap) return 0;
    snprintf(path, cap, "%s%sSCRAP.TXT", directory, n && directory[n - 1] == '\\' ? "" : "\\");
    return 1;
}
static int copy_selection(int cut)
{
    size_t a, b; ed_selection(&doc, &a, &b);
    if (a == b) { message("Select text first"); return 0; }
    char *s = malloc(b - a + 1);
    if (!s) { message("Not enough memory for clipboard"); return 0; }
    memcpy(s, doc.text + a, b - a); s[b - a] = 0;
    free(clipboard); clipboard = s; clipboard_len = b - a;
    char path[144], info[256]; int shared = scrap_path(path, sizeof(path)) &&
        editor_write_file(path, s, clipboard_len, info, sizeof(info));
    clipboard_local = !shared;
    if (cut && !ed_delete(&doc, 0)) { message("%s", doc.error); return 0; }
    message("%s %lu bytes%s", cut ? "Cut" : "Copied", (unsigned long)clipboard_len, shared ? "" : " (editor clipboard)");
    return 1;
}
static void paste(void)
{
    char path[144], info[128], *s = NULL; size_t n = 0;
    if (!clipboard_local && scrap_path(path, sizeof(path)) && editor_read_file(path, &s, &n, info, sizeof(info))) {
        if (memchr(s, 0, n)) { free(s); message("Clipboard contains binary data"); return; }
        free(clipboard); clipboard = s; clipboard_len = n;
    }
    if (!clipboard) { message("Clipboard is empty"); return; }
    /* Convert pasted newlines to this document's convention; loaded bytes stay intact. */
    size_t need = clipboard_len;
    if (doc.crlf) for (size_t i = 0; i < clipboard_len; ++i) if (clipboard[i] == '\n' && (!i || clipboard[i - 1] != '\r')) ++need;
    if (need > ED_MAX_BYTES) { message("Clipboard exceeds document limit"); return; }
    char *normal = malloc(need + 1);
    if (!normal) { message("Not enough memory for paste"); return; }
    n = 0;
    for (size_t i = 0; i < clipboard_len; ++i) {
        if (clipboard[i] == '\r' && i + 1 < clipboard_len && clipboard[i + 1] == '\n') continue;
        if (clipboard[i] == '\n' && doc.crlf) normal[n++] = '\r';
        normal[n++] = clipboard[i];
    }
    ed_break_group(&doc); if (!ed_insert(&doc, normal, n, 0)) message("%s", doc.error);
    free(normal);
}
/* A visual row wraps at the last whitespace that fits, falling back to a
 * character boundary for long words. Tabs use document-wide tab stops. */
static size_t segment_end(size_t p, size_t end, size_t column, size_t *next_column)
{
    size_t start = p, startcol = column, space = p, spacecol = column;
    while (p < end) {
        size_t next = column + (doc.text[p] == '\t' ? doc.tabstop - column % doc.tabstop : 1);
        /* Keep one cell for the caret at an exact-fit end of line. */
        if (next - startcol > (size_t)(cols > 1 ? cols - 1 : 1) && p > start) break;
        column = next; ++p;
        if (doc.text[p - 1] == ' ' || doc.text[p - 1] == '\t') { space = p; spacecol = column; }
    }
    if (p < end && space > start) { p = space; column = spacecol; }
    *next_column = column; return p;
}
static void layout(void)
{
    gutter = numbers ? 56 : 0;
    text_x = window.work.x + 4 + gutter; text_y = window.work.y + 2;
    cols = (window.work.w - 8 - gutter) / 8; rows = (window.work.h - 42) / 16;
    if (cols < 1) cols = 1; if (cols > MAX_COLS) cols = MAX_COLS;
    if (rows < 1) rows = 1; if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (indexed_revision == doc.revision && indexed_cols == cols && indexed_tab == doc.tabstop &&
        indexed_wrap == wrap && indexed_syntax == syntax) return;
    indexed_revision = doc.revision; indexed_cols = cols; indexed_tab = doc.tabstop;
    indexed_wrap = wrap; indexed_syntax = syntax;
    total = widest = 0; int state = 0;
    for (size_t l = 0; l < doc.nlines; ++l) {
        visual[l] = total; size_t end = ed_end(&doc, l), width = ed_column(&doc, end);
        widths[l] = width; if ((int)width > widest) widest = width;
        states[l] = state; if (syntax) ed_highlight(&doc, l, &state, NULL);
        if (!wrap) ++total;
        else {
            size_t p = doc.lines[l], col = 0;
            do { p = segment_end(p, end, col, &col); ++total; } while (p < end);
        }
    }
    visual[doc.nlines] = total;
    if (wrap) left = 0;
}
static size_t line_at_visual(int v)
{
    size_t lo = 0, hi = doc.nlines;
    while (lo + 1 < hi) { size_t mid = (lo + hi) / 2; if ((int)visual[mid] <= v) lo = mid; else hi = mid; }
    return lo;
}
static void segment(int v, size_t *line, size_t *start, size_t *end, size_t *column)
{
    *line = line_at_visual(v); *start = doc.lines[*line]; *end = ed_end(&doc, *line); *column = 0;
    if (wrap) {
        int part = v - visual[*line]; size_t nextcol;
        while (part-- > 0) *start = segment_end(*start, *end, *column, column);
        *end = segment_end(*start, *end, *column, &nextcol);
    }
}
static void cursor_visual(int *v, int *x)
{
    size_t l = ed_line(&doc, doc.cursor), col = ed_column(&doc, doc.cursor);
    *v = visual[l]; *x = (int)col - left;
    if (wrap) {
        size_t start = doc.lines[l], end = ed_end(&doc, l), startcol = 0, nextcol;
        while (start < end) {
            size_t next = segment_end(start, end, startcol, &nextcol);
            if (doc.cursor < next || next == end) break;
            start = next; startcol = nextcol; ++*v;
        }
        *x = (int)(col - startcol);
        if (*x >= cols) *x = cols - 1;
    }
}
static int max_top(void) { return total > rows ? total - rows : 0; }
static int max_left(void) { return !wrap && widest >= cols ? widest - cols + 1 : 0; }
static void clamp_view(void)
{
    if (top < 0) top = 0; if (top > max_top()) top = max_top();
    if (left < 0) left = 0; if (left > max_left()) left = max_left();
}
static void reveal(void)
{
    layout(); int v, x; cursor_visual(&v, &x);
    if (v < top) top = v; if (v >= top + rows) top = v - rows + 1;
    if (!wrap) { if (x < 0) left += x; if (x >= cols) left += x - cols + 1; }
    clamp_view();
}
static size_t position_at(int v, int x)
{
    if (v < 0) return 0; if (v >= total) return doc.len;
    size_t line, start, end, column; segment(v, &line, &start, &end, &column);
    size_t goal = column + (x < 0 ? 0 : x) + (wrap ? 0 : left);
    size_t p = start;
    while (p < end) {
        size_t next = column + (doc.text[p] == '\t' ? doc.tabstop - column % doc.tabstop : 1);
        if (next > goal) break;
        column = next; ++p;
    }
    return p;
}
static void set_field(int field, int value)
{
    ai[0] = window.handle; ai[1] = field; ai[2] = value; ai[3] = ai[4] = ai[5] = 0;
    aes_call(105, 6, 1, 0);
}
static void sliders(void)
{
    static int values[4] = {-1, -1, -1, -1};
    int next[4] = {max_left() ? (uint64_t)left * 1000 / max_left() : 0,
        max_top() ? top * 1000 / max_top() : 0,
        wrap || widest < cols ? 1000 : cols * 1000 / (widest + 1),
        total <= rows ? 1000 : rows * 1000 / total};
    int fields[] = {8, 9, 15, 16};
    for (int i = 0; i < 4; ++i) if (values[i] != next[i]) { set_field(fields[i], next[i]); values[i] = next[i]; }
}
static void build_row(int y)
{
    ScreenRow *r = &screen[y]; memset(r, 0, sizeof(*r)); r->caret = -1;
    memset(r->text, ' ', cols); memset(r->colour, 1, cols);
    int v = top + y;
    if (v >= total) return;
    size_t line = visible_segments[y].line, start = visible_segments[y].start,
        end = visible_segments[y].end, column = visible_segments[y].column;
    if (v == (int)visual[line]) snprintf(r->number, sizeof(r->number), "%5lu", (unsigned long)line + 1);
    size_t first = start, firstcol = column;
    size_t fromcol = wrap ? column : (size_t)left, tocol = fromcol + cols;
    while (first < end) {
        size_t next = firstcol + (doc.text[first] == '\t' ? doc.tabstop - firstcol % doc.tabstop : 1);
        if (next > fromcol) break;
        firstcol = next; ++first;
    }
    size_t stop = first, stopcol = firstcol;
    while (stop < end && stopcol < tocol) { stopcol += doc.text[stop] == '\t' ? doc.tabstop - stopcol % doc.tabstop : 1; ++stop; }
    unsigned char colours[MAX_COLS + 1] = {0};
    int state = states[line];
    if (syntax && stop > first) ed_highlight_range(&doc, line, &state, colours, first, stop);
    /* GEM's green/cyan are very light on white; use legible default colours. */
    static const unsigned char palette[] = {1, 9, 4, 4, 2, 7, 4};
    size_t a, b; ed_selection(&doc, &a, &b);
    for (size_t p = first, col = firstcol; p < stop; ++p) {
        size_t next = col + (doc.text[p] == '\t' ? doc.tabstop - col % doc.tabstop : 1);
        for (; col < next && col < tocol; ++col) if (col >= fromcol) {
            int x = col - fromcol;
            unsigned char ch = doc.text[p];
            r->text[x] = ch == '\t' ? ' ' : ch < 32 || ch == 127 ? '?' : ch;
            r->colour[x] = palette[colours[p - first]]; r->selected[x] = p >= a && p < b;
        }
    }
    /* Make a selected newline visible, including empty selected lines. */
    if (end == ed_end(&doc, line) && end >= a && end < b && widths[line] >= fromcol && widths[line] < tocol)
        r->selected[widths[line] - fromcol] = 1;
    if (caret_row == v && caret_column >= 0 && caret_column < cols) r->caret = caret_column;
}
static void draw(void)
{
    if (draw_background) app_box(window.work.x, window.work.y, window.work.w, window.work.h, 0);
    for (int y = 0; y < rows; ++y) if (redraw_rows & (1u << y)) {
        ScreenRow *r = &screen[y]; int py = text_y + y * 16;
        app_box(window.work.x, py, window.work.w, 16, 0);
        if (numbers) { app_box(window.work.x, py, gutter, 16, 8); app_text(window.work.x + 4, py + 13, r->number, 9); }
        for (int x = 0; x < cols; ) {
            int end = x + 1, bg = r->selected[x], fg = bg ? 0 : r->colour[x];
            while (end < cols && r->selected[end] == bg && (bg || r->colour[end] == fg)) ++end;
            if (bg) app_box(text_x + x * 8, py, (end - x) * 8, 16, 4);
            char run[MAX_COLS + 1]; memcpy(run, r->text + x, end - x); run[end - x] = 0;
            app_text(text_x + x * 8, py + 13, run, fg); x = end;
        }
        if (r->caret >= 0) app_line(text_x + r->caret * 8, py + 14, text_x + r->caret * 8 + 7, py + 14,
            r->selected[r->caret] ? 0 : 1);
    }
    if (redraw_status) {
        int y = text_y + rows * 16;
        app_box(window.work.x, y, window.work.w, window.work.y + window.work.h - y, 8);
        app_line(window.work.x, y, window.work.x + window.work.w - 1, y, 9);
        char line[81]; int n = (window.work.w - 8) / 8; if (n > 80) n = 80;
        snprintf(line, sizeof(line), "%.*s", n, status_line); app_text(window.work.x + 4, y + 15, line, 1);
        snprintf(line, sizeof(line), "%.*s", n, status); app_text(window.work.x + 4, y + 31, line, 1);
    }
}
static void refresh(int all)
{
    layout(); clamp_view();
    snprintf(title, sizeof(title), "DreamEdit - %s%s", filename[0] ? filename : "Untitled", ed_dirty(&doc) ? " *" : "");
    if (strcmp(title, last_title)) {
        snprintf(last_title, sizeof(last_title), "%s", title);
        uint32_t p = (uint32_t)(uintptr_t)title;
        ai[0] = window.handle; ai[1] = 2; memcpy(ai + 2, &p, sizeof(p)); ai[4] = ai[5] = 0;
        aes_call(105, 6, 1, 0);
    }
    if (cache_top != top || cache_left != left || cache_cols != cols || cache_rows != rows) all = 1;
    cache_top = top; cache_left = left; cache_cols = cols; cache_rows = rows;
    redraw_rows = 0;
    cursor_visual(&caret_row, &caret_column);
    size_t line, start, end, column;
    segment(top, &line, &start, &end, &column);
    for (int y = 0; y < rows; ++y) {
        visible_segments[y].line = line; visible_segments[y].start = start;
        visible_segments[y].end = end; visible_segments[y].column = column;
        build_row(y);
        if (all || memcmp(&screen[y], &previous[y], sizeof(screen[y]))) redraw_rows |= 1u << y;
        previous[y] = screen[y];
        if (wrap && end < ed_end(&doc, line)) {
            segment_end(start, ed_end(&doc, line), column, &column);
            start = end; size_t next_column;
            end = segment_end(start, ed_end(&doc, line), column, &next_column);
        } else if (line + 1 < doc.nlines) {
            ++line; start = doc.lines[line]; end = ed_end(&doc, line); column = 0;
            if (wrap) { size_t next_column; end = segment_end(start, end, column, &next_column); }
        }
    }
    size_t a, b; ed_selection(&doc, &a, &b);
    snprintf(status_line, sizeof(status_line), "Ln %lu/%lu Col %lu  %s  %s:%d%s%s%s",
        (unsigned long)ed_line(&doc, doc.cursor) + 1, (unsigned long)doc.nlines,
        (unsigned long)ed_column(&doc, doc.cursor) + 1, doc.crlf ? "CRLF" : "LF",
        doc.spaces ? "Spaces" : "Tabs", doc.tabstop, wrap ? " Wrap" : "",
        a != b ? " Selected" : "", ed_dirty(&doc) ? " Modified" : "");
    /* Status text can change independently of the cursor. */
    static char last_message[256];
    redraw_status = all || strcmp(old_status, status_line) || strcmp(last_message, status);
    snprintf(old_status, sizeof(old_status), "%s", status_line); snprintf(last_message, sizeof(last_message), "%s", status);
    draw_background = all;
    if (all) { redraw_rows = (1u << rows) - 1; redraw_status = 1; app_window_redraw(&window, window.work, draw); }
    else {
        /* Each damage rectangle is clipped again to GEM's visible rectangles. */
        for (int y = 0; y < rows; ) {
            if (!(redraw_rows & (1u << y))) { ++y; continue; }
            int start = y; while (y < rows && (redraw_rows & (1u << y))) ++y;
            app_window_redraw(&window, (AppRect){window.work.x, text_y + start * 16, window.work.w, (y - start) * 16}, draw);
        }
        if (redraw_status) {
            int y = text_y + rows * 16;
            app_window_redraw(&window, (AppRect){window.work.x, y, window.work.w, window.work.y + window.work.h - y}, draw);
        }
    }
    sliders(); force_redraw = 0;
}
static int selected_query(void)
{
    size_t a, b; ed_selection(&doc, &a, &b); size_t n = strlen(query);
    if (!n || b - a != n) return 0;
    for (size_t i = 0; i < n; ++i)
        if (match_case ? doc.text[a + i] != query[i] : tolower((unsigned char)doc.text[a + i]) != tolower((unsigned char)query[i])) return 0;
    return 1;
}
static int ask_query(void)
{
    size_t a, b; ed_selection(&doc, &a, &b);
    if (b > a && b - a < sizeof(query) && !memchr(doc.text + a, '\n', b - a)) {
        memcpy(query, doc.text + a, b - a); query[b - a] = 0;
    }
    return prompt("Find text:", query, sizeof(query)) && query[0];
}
static int find_next(int backwards)
{
    if (!query[0] && !ask_query()) return 0;
    int found = ed_find(&doc, query, backwards, match_case);
    message(found ? "Match: %s" : "No match: %s", query); return found;
}
static void command(int c)
{
    ed_break_group(&doc); doc.error = NULL; preferred_col = -1;
    switch (c) {
    case C_ABOUT:
        app_alert("DreamEdit text editor|1 MiB / 32768 lines|Undo, clipboard, search and GEM windows.|F3 next match; Shift+F3 previous.|Originally based on Kilo (BSD-2-Clause)."); force_redraw = 1; break;
    case C_NEW:
        if (may_discard() && ed_load(&doc, "", 0)) {
            filename[0] = 0; top = left = 0; set_syntax(); force_redraw = 1;
            message("New document. Save defaults to %c:%s", dc_storage_drive(), dc_drive_note(dc_storage_drive()));
        }
        break;
    case C_OPEN: {
        char path[128]; snprintf(path, sizeof(path), "%s", filename);
        if (!path[0]) snprintf(path, sizeof(path), "D:\\APPS.TXT");
        if (choose_file("Open text file", path, sizeof(path)) && may_discard()) load_document(path);
        break;
    }
    case C_SAVE: save_document(0); break;
    case C_SAVE_AS: save_document(1); break;
    case C_QUIT: if (may_discard()) done = 1; break;
    case C_UNDO: if (!ed_undo(&doc, 0)) message("Nothing to undo"); break;
    case C_REDO: if (!ed_undo(&doc, 1)) message("Nothing to redo"); break;
    case C_CUT: copy_selection(1); break;
    case C_COPY: copy_selection(0); break;
    case C_PASTE: paste(); break;
    case C_SELECT_ALL: doc.anchor = 0; doc.cursor = doc.len; break;
    case C_INDENT: ed_indent(&doc, 0); break;
    case C_OUTDENT: ed_indent(&doc, 1); break;
    case C_FIND:
        if (ask_query()) { size_t a, b; ed_selection(&doc, &a, &b); ed_select(&doc, a, 0); find_next(0); }
        break;
    case C_NEXT: find_next(0); break;
    case C_PREVIOUS: find_next(1); break;
    case C_REPLACE:
        if (ask_query() && prompt("Replace with (empty deletes):", replacement, sizeof(replacement))) {
            if (selected_query() || find_next(0)) {
                reveal(); refresh(1);
                int choice = confirm("[2][Replace this match?][Replace|Next|Cancel]", 1);
                if (choice == 1) { ed_insert(&doc, replacement, strlen(replacement), 0); message("Replaced one match; F3 finds the next"); }
                else if (choice == 2) find_next(0);
            }
        }
        break;
    case C_REPLACE_ALL:
        if (ask_query() && prompt("Replace all with (empty deletes):", replacement, sizeof(replacement))) {
            int n = ed_replace_all(&doc, query, replacement, match_case);
            if (n >= 0) message("Replaced %d matches; Ctrl+Z undoes all", n);
        }
        break;
    case C_GOTO: {
        char number[16] = "";
        if (prompt("Go to line:", number, sizeof(number))) {
            char *end; long line = strtol(number, &end, 10);
            if (!number[0] || *end || line < 1 || (size_t)line > doc.nlines) message("Enter a line from 1 to %lu", (unsigned long)doc.nlines);
            else ed_select(&doc, doc.lines[line - 1], 0);
        }
        break;
    }
    case C_CASE: match_case = !match_case; message("Search is %s", match_case ? "case sensitive" : "case insensitive"); break;
    case C_WRAP: wrap = !wrap; force_redraw = 1; break;
    case C_NUMBERS: numbers = !numbers; force_redraw = 1; break;
    case C_AUTOINDENT: doc.autoindent = !doc.autoindent; message("Auto-indent %s", doc.autoindent ? "on" : "off"); break;
    case C_SPACES: doc.spaces = !doc.spaces; message("Tab inserts %s", doc.spaces ? "spaces" : "a tab character"); break;
    case C_TABS: {
        char value[4]; snprintf(value, sizeof(value), "%d", doc.tabstop);
        if (prompt("Tab width (1-8):", value, sizeof(value))) {
            char *end; long n = strtol(value, &end, 10);
            if (!*value || *end || n < 1 || n > 8) message("Tab width must be 1-8");
            else { doc.tabstop = n; force_redraw = 1; }
        }
        break;
    }
    }
    if (doc.error) message("%s", doc.error);
    reveal();
}
static void menu_build(void)
{
    memset(menu, 0, sizeof(menu));
    menu[0] = object(-1, 25, 0, 0, 0, 0, 640, 480); menu[0].head = 1; menu[0].tail = 8;
    menu[1] = object(8, 20, 0, 0x1100, 0, 0, 640, 20); menu[1].head = menu[1].tail = 2;
    menu[2] = object(1, 25, 0, 0, 0, 0, 320, 20); menu[2].head = 3; menu[2].tail = 7;
    const char *titles[] = {" Desk ", " File ", " Edit ", " Search ", " View "};
    int x = 0;
    for (int i = 0; i < 5; ++i) {
        int width = strlen(titles[i]) * 8;
        menu[3 + i] = object(i == 4 ? 2 : 4 + i, 32, 0, (intptr_t)titles[i], x, 0, width, 20); x += width;
    }
    menu[2].w = x;
    menu[8] = object(0, 25, 0, 0, 0, 20, 640, 460); menu[8].head = 9;
    /* AES rewrites the first dropdown with desk accessories. Reserve six slots. */
    menu[9] = object(18, 20, 0, 0xff1100, 0, 0, 264, 16); menu[9].head = menu[9].tail = 10;
    for (int i = 10; i < 18; ++i) menu[i] = object(9, 28, 0, (intptr_t)"-------------------------------", 0, (i - 10) * 16, 264, 16);
    menu[10].spec = (intptr_t)"  About DreamEdit..."; menu_action[10] = C_ABOUT; menu[11].state = 8;
    struct Item { const char *label; int command; };
    static const struct Item file[] = {{"  New                 ^N", C_NEW}, {"  Open...             ^O", C_OPEN},
        {"  Save                ^S", C_SAVE}, {"  Save as...     Shift^S", C_SAVE_AS}, {"  Quit                ^Q", C_QUIT}};
    static const struct Item edit[] = {{"  Undo                ^Z", C_UNDO}, {"  Redo                ^Y", C_REDO},
        {"  Cut                 ^X", C_CUT}, {"  Copy                ^C", C_COPY}, {"  Paste               ^V", C_PASTE},
        {"  Select all          ^A", C_SELECT_ALL}, {"  Indent             Tab", C_INDENT}, {"  Outdent      Shift+Tab", C_OUTDENT}};
    static const struct Item search[] = {{"  Find...             ^F", C_FIND}, {"  Find next           F3", C_NEXT},
        {"  Find previous Shift+F3", C_PREVIOUS}, {"  Replace...          ^H", C_REPLACE},
        {"  Replace all... Shift^H", C_REPLACE_ALL}, {"  Go to line...       ^G", C_GOTO}, {"  Match case", C_CASE}};
    static const struct Item view[] = {{"  Word wrap           ^W", C_WRAP}, {"  Line numbers        ^L", C_NUMBERS},
        {"  Auto-indent", C_AUTOINDENT}, {"  Tab width...", C_TABS}, {"  Insert spaces", C_SPACES}};
    const struct Item *items[] = {file, edit, search, view};
    int counts[] = {5, 8, 7, 5}; menu_count = 18;
    for (int m = 0; m < 4; ++m) {
        int box = menu_count++, count = counts[m];
        menu[box] = object(box + count + 1, 20, 0, 0xff1100, menu[4 + m].x, 0, 208, count * 16);
        menu[box].head = box + 1; menu[box].tail = box + count;
        for (int i = 0; i < count; ++i) {
            int idx = menu_count++;
            menu[idx] = object(i == count - 1 ? box : idx + 1, 28, 0, (intptr_t)items[m][i].label, 0, i * 16, 208, 16);
            menu_action[idx] = items[m][i].command;
        }
        if (m == 3) { menu[box].next = 8; menu[8].tail = box; }
    }
    menu[menu_count - 1].flags |= 32;
    ai[0] = 1; aa[0] = (intptr_t)menu; aes_call(30, 1, 1, 1);
}
static void menu_update(void)
{
    size_t a, b; ed_selection(&doc, &a, &b);
    for (int i = 18; i < menu_count; ++i) {
        int c = menu_action[i], checked = c == C_CASE ? match_case : c == C_WRAP ? wrap :
            c == C_NUMBERS ? numbers : c == C_AUTOINDENT ? doc.autoindent : c == C_SPACES ? doc.spaces : 0;
        int disabled = c == C_UNDO ? !doc.undo : c == C_REDO ? doc.undo == doc.count :
            c == C_CUT || c == C_COPY ? a == b : 0;
        menu[i].state = (checked ? 4 : 0) | (disabled ? 8 : 0);
    }
}
static void keypress(int key, int modifiers)
{
    int ch = key & 255, scan = (key >> 8) & 255, shift = modifiers & 3, ctrl = modifiers & 4;
    if (!key) return;
    doc.error = NULL;
    if (scan == 0x3d) { command(shift ? C_PREVIOUS : C_NEXT); return; }
    if (ctrl || (ch > 0 && ch < 27 && ch != 8 && ch != 9 && ch != 13)) {
        int code = ch;
        if (code >= 'a' && code <= 'z') code -= 'a' - 1;
        if (code >= 'A' && code <= 'Z') code -= 'A' - 1;
        int action = code == 1 ? C_SELECT_ALL : code == 3 ? C_COPY : code == 6 ? C_FIND : code == 7 ? C_GOTO :
            code == 8 ? (shift ? C_REPLACE_ALL : C_REPLACE) : code == 12 ? C_NUMBERS : code == 14 ? C_NEW :
            code == 15 ? C_OPEN : code == 17 ? C_QUIT : code == 19 ? (shift ? C_SAVE_AS : C_SAVE) :
            code == 22 ? C_PASTE : code == 23 ? C_WRAP : code == 24 ? C_CUT : code == 25 ? C_REDO :
            code == 26 ? (shift ? C_REDO : C_UNDO) : 0;
        if (action) { command(action); return; }
    }
    layout(); size_t pos = doc.cursor, a, b; ed_selection(&doc, &a, &b); int move = 1, vertical = 0;
    switch (scan) {
    case 0x73: ctrl = 1; /* Ctrl+Left scan on Atari keyboards */
        /* fall through */
    case 0x4b: pos = !shift && a != b ? a : ctrl ? ed_word(&doc, pos, -1) : ed_prev(&doc, pos); break;
    case 0x74: ctrl = 1;
        /* fall through */
    case 0x4d: pos = !shift && a != b ? b : ctrl ? ed_word(&doc, pos, 1) : ed_next(&doc, pos); break;
    case 0x77: ctrl = 1;
        /* fall through */
    case 0x47: pos = ctrl ? 0 : doc.lines[ed_line(&doc, pos)]; break;
    case 0x75: ctrl = 1;
        /* fall through */
    case 0x4f: pos = ctrl ? doc.len : ed_end(&doc, ed_line(&doc, pos)); break;
    case 0x48: case 0x50: case 0x49: case 0x51: {
        int v, x; cursor_visual(&v, &x); vertical = 1;
        if (preferred_col < 0) preferred_col = x + (wrap ? 0 : left);
        int delta = scan == 0x48 ? -1 : scan == 0x50 ? 1 : scan == 0x49 ? -rows : rows;
        pos = position_at(v + delta, preferred_col - (wrap ? 0 : left)); break;
    }
    default: move = 0;
    }
    if (move) { ed_select(&doc, pos, shift); if (!vertical) preferred_col = -1; }
    else {
        preferred_col = -1;
        if (scan == 0x53) ed_delete(&doc, 0);
        else if (ctrl || (modifiers & 8)) return; /* Never insert unhandled control codes. */
        else if (ch == 8 || ch == 127) ed_delete(&doc, 1);
        else if (ch == 13) ed_newline(&doc);
        else if (ch == 9 || scan == 0x0f) ed_indent(&doc, shift != 0);
        else if (ch == 27) ed_select(&doc, doc.cursor, 0);
        else if (ch >= 32) {
            unsigned long now = app_millis();
            if (now - last_type_time > 1000) ed_break_group(&doc);
            char c = ch; ed_insert(&doc, &c, 1, 1); last_type_time = now;
        }
    }
    if (doc.error) message("%s", doc.error);
    else if (doc.revision != indexed_revision) message("Ctrl+Z undo   Ctrl+S save   Ctrl+F find");
    reveal();
}
static void pointer_event(const AppEvent *event)
{
    int down = event->buttons & 1;
    if (down && !(buttons & 1) && app_window_contains(&window, event->x, event->y) &&
        event->y >= text_y && event->y < text_y + rows * 16) {
        dragging = 1; preferred_col = -1;
        int x = (event->x - text_x) / 8, y = (event->y - text_y) / 16;
        ed_select(&doc, position_at(top + y, x), event->modifiers & 3);
    } else if (down && dragging) {
        if (event->y < text_y) --top;
        if (event->y >= text_y + rows * 16) ++top;
        if (!wrap && event->x < text_x) --left;
        if (!wrap && event->x >= text_x + cols * 8) ++left;
        clamp_view();
        int x = (event->x - text_x) / 8, y = (event->y - text_y) / 16;
        if (x < 0) x = 0; if (x >= cols) x = cols - 1;
        if (y < 0) y = 0; if (y >= rows) y = rows - 1;
        ed_select(&doc, position_at(top + y, x), 1);
    }
    if (!down) dragging = 0;
    buttons = event->buttons;
}
static void window_message(const int16_t *m)
{
    if (m[0] == 10) { /* MN_SELECTED */
        int action = m[4] >= 0 && m[4] < menu_count ? menu_action[m[4]] : 0;
        ai[0] = m[3]; ai[1] = 1; aa[0] = (intptr_t)menu; aes_call(33, 2, 1, 1);
        if (action) command(action);
        return;
    }
    if (m[3] != window.handle) return;
    if (m[0] == 24) { /* WM_ARROWED */
        switch (m[4]) {
        case 0: top -= rows; break; case 1: top += rows; break; case 2: --top; break; case 3: ++top; break;
        case 4: left -= cols; break; case 5: left += cols; break; case 6: --left; break; case 7: ++left; break;
        }
        clamp_view();
    } else if (m[0] == 25) { left = (int64_t)max_left() * m[4] / 1000; clamp_view(); }
    else if (m[0] == 26) { top = (int64_t)max_top() * m[4] / 1000; clamp_view(); }
    else {
        int result = app_window_message(&window, m);
        if (result == WINDOW_CLOSE) command(C_QUIT);
        else if (result == WINDOW_CHANGED) { dragging = 0; force_redraw = 1; layout(); reveal(); }
        else if (result == WINDOW_REDRAW) force_redraw = 1;
    }
}
static void finish(void)
{
    ai[0] = 0; aa[0] = (intptr_t)menu; aes_call(30, 1, 1, 1);
    app_window_close(&window); app_end(); ed_free(&doc); free(clipboard); clipboard = NULL;
}
int app_main(int argc, char **argv)
{
    if (!ed_init(&doc)) return 1;
    if (!app_begin_windowed()) { ed_free(&doc); return 1; }
    menu_build();
    if (!app_window_open_kind(&window, "DreamEdit text editor", 608, 414, 320, 180,
        1 | 2 | 4 | 8 | 32 | 64 | 128 | 256 | 512 | 1024 | 2048)) {
        app_alert("Unable to open the editor window"); finish(); return 1;
    }
    atexit(finish);
    message("Ctrl+O open   Ctrl+S save   Ctrl+Z undo");
    if (argc > 1) load_document(argv[1]);
    reveal();
    while (!done) {
        menu_update(); refresh(force_redraw);
        AppEvent event; app_window_event(&event, dragging ? 30 : 150, buttons);
        if (event.flags & APP_MESSAGE) window_message(event.message);
        if (event.flags & APP_KEY) keypress(event.key, event.modifiers);
        pointer_event(&event);
    }
    return 0;
}
